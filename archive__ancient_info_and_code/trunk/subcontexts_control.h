
#ifndef _SUBCONTEXTS_CONTROL_H__INCLUDED_
#define _SUBCONTEXTS_CONTROL_H__INCLUDED_


#include <sys/mman.h>

#include "russ_common_code/c/russ_int_typedefs_linux.h"
#include "russ_common_code/c/russ_locks.h"

struct sc_local;  // declared here so that we don't have cyclic includes.
                  // it is defined in subcontexts_local.h


struct sc_region;
struct sc_cleanupLogStep;
struct sc_process;
struct sc_malloc_metadata;


/* this struct is always stored at page 0 of the temporary file; it contains
 * all of the information necessary to bootstrap the rest of the subcontexts
 * information.
 *
 * When a process is starting up, it mmap()s this struct to a private
 * location, and then reads the 'active_regions' pointer to find the virtual
 * address of the first region.  The first region is *ALWAYS* the public
 * virtual location of this struct.  Thus, the process can re-map the buffer
 * to the new virtual address, and from then on reference it from there.
 *
 * Pages in this list contain *all* of the control structs for this list -
 * that is, subcontext pages contain their own metadata.  The way this works
 * is fairly simple: metadata is allocated first, and data second; thus, the
 * metadata for any given virtual page always resides on a virtual page
 * earlier in the list.  (The obvious exception is the first region, already
 * described.)  Thus, to handle a page fault, we walk the list of regions;
 * if we recursively fault in that walk, then we simply walk again, on the
 * expectation that the metadata for the metadata resides on an earlier
 * page.
 *
 * Note that this means, effectively, that a page fault on a virtual page
 * requires that we map in the metadata pages for *all* addresses before
 * it on the list - however, we still view this as demand paging, since
 * we only map in pages as they are touched.  Data pages that are never
 * used - or metadata pages which are both far down the list, and also their
 * data pages are never used - will never get mapped.
 */
typedef struct sc
{
	/* the magic value for subcontexts is a 32-bit integer: the first
	 * two characters are always 'S','C' (subcons); the next indicates
	 * the type of struct (in this case, 'R' for root), and the last is
	 * a version number for this struct (0, so far).
	 */
#define SUBCONTEXTS_MAGIC(type, vers)   ( (((u32) 'S'  ) << 24) | \
                                          (((u32) 'C'  ) << 16) | \
                                          (((u32)(type)) << 8 ) | \
                                          ( (u32)(vers)       ) )
#define SUBCONTEXTS_MAGIC_GET_TYPE(val)   (((val) >> 8) & 0x0ff)
#define SUBCONTEXTS_MAGIC_GET_VERS(val)   ( (val)       & 0x0ff)

#define SUBCONTEXTS_ROOT_MAGIC   SUBCONTEXTS_MAGIC('R',0)
	u32 magic;

	/* this locks the root struct, including the pointers within
	 * it.  It does *NOT* lock the members of any of the lists.
	 * To traverse a list, you must ladder-climb:
	 *      lock root
	 *      read next pointer
	 *      lock next
	 *      if (delete next)
	 *           change root to point to next->next
	 *           unlock root
	 *           free next
	 *      else
	 *           unlock root
	 *           do stuff
	 */
	russ_lock lock;

	/* this is the list of regions which are currently in use.
	 * If we hit a page fault, we will walk this list to see
	 * if we can satisfy the page fault with a 
	 */
	struct sc_region *active_regions;
	struct sc_region *active_regionsTail;

	/* these are regions that have not currently been mmap()ed for active
	 * use, but which all processes agree are not in use for private
	 * addresses, either.  They can be used for quick no-communication
	 * allocation in the future.
	 *
	 * The addresses in here do not currently consume any space in the
	 * temporary file, and thus can be discarded as needed; during init(),
	 * each process should scan this list and crop it down as needed;
	 * it should also mmap() inaccessible pages into these regions in
	 * order to prevent accidental uses of these ranges for
	 * non-subcontexts memory.
	 *
	 * Extending this list requires a broadcast operation, which is very
	 * expensive - so we try to do it as rarely as possible.
	 */
	struct sc_region *reserved_regions;

	/* see the long comment about SC_CLS_RESERVE_MEMORY below.  Only one
	 * in-progress reservation is allowed at any time, and if one such
	 * op is ongoing, then the pointer to its struct is here.  This
	 * pointer will be NULL 99.9% of the time.
	 */
	struct sc_region *pending_reservation;

	/* these are regions which were used in the past but which the user
	 * deallocated with subcontexts_munmap().  These regions consume
	 * space in the temporary file, but are not in use.  This means that
	 * they have fixed virtual addresses *AND* fixed locations in the
	 * temporary file, but they are *NOT* in use.  Future attempts to
	 * mmap() should re-use these before they eat into reserved_regions
	 * above.
	 *
	 * TODO: shouldn't we return the virtual addresses to the
	 *       reserved_regions pool?
	 */
	struct sc_region *unmapped_regions;

	/* this is the pool of unused region structs */
	struct sc_region *free_structs;

	/* this is how much space we've consumed in the tmpfile.  Currently,
	 * we never free space in this file, we just append to it.  When
	 * pages are freed, they are posted to the 'unmapped_regions' list
	 * above; we reuse these regions later.
	 */
	u64 tmpfile_size;

	/* how many processes currently are part of the group?  (This is
	 * critically important for the cleanup log!)
	 */
	u32 proc_count;

	/* this is a linked list, containing metadata for all of the
	 * active processes.
	 *
	 * To interpret this flag, see the 'dead' flag in the process
	 * struct description.
	 */
	struct sc_process *proc_list;
	bool proc_list__deadFlagPending;

	/* new init-ing processes don't want to allocate memory.  This is
	 * a pointer to a struct already on the process list which is in
	 * the PREALLOC state and thus ready to be claimed.  If this
	 * pointer is NULL, then the init-ing process should spin and wait
	 * for an operational one to allocate a struct and hang it here.
	 */
	struct sc_process *prealloc_proc;

	/* the 'cleanup log' is used to communicate critical operations
	 * between processes; they are things which must be done promptly,
	 * not waiting for demand paging to kick in.  Generally this
	 * involves unmapping some sort of memory.
	 *
	 * New operations are posted to this log by any process, and then
	 * executed by every other process; a reference counter is used
	 * to keep track of how many processes still need to perform the
	 * step.  Entries are freed from the log when they have been
	 * completed by every process.
	 *
	 * Most entries in this log are synchronous, meaning that the
	 * process which posts them will use a broadcast to wake up all
	 * other processes, and block until they complete it.  However,
	 * a few are async, meaning that they can wait until the next
	 * time that we enter subcontext code.
	 *
	 * In either case, a process will always, at the beginning of
	 * any subcontexts operation, scan this log for new things to
	 * do, and perform them before doing anything else.
	 *
	 * To keep track of whether or not there is new work to be done,
	 * we use a 64-bit sequence number; each new operation gets a
	 * new number, and each process stores a *PRIVATE* value indicating
	 * how far in the log it has already progressed.  This allows a
	 * process to instantly skip over old log entries (if the entire
	 * log has already been processed by this process).  If, however,
	 * one or more entries are new, then a full log scan is required;
	 * this is painful but survivable.
	 *
	 * TODO: handle wrapping of the sequence number.  Idea: can we
	 *       find a func along the lines of "subtract, mask high
	 *       bits, then compare against a large constant" to
	 *       implement "greater-than" using a wrapping value???
	 */
	u64 cleanupLog_lastSeqNum;
	struct sc_cleanupLogStep *cleanupLog;
	struct sc_cleanupLogStep *cleanupLog_tail;

	/* this *pointer* is protected by the lock for this struct, but
	 * the struct that it points to is not.  Also, we make the guarantee
	 * that the struct it points to, once initialized, will never move.
	 * Therefore, user processes should copy this value into their
	 * private memory during init; after init, they can follow that
	 * pointer out of private memory, and not content to lock the root
	 * control struct.
	 */
	struct sc_malloc_metadata *malloc_md;



	/* ---- USER POINTER ----
	 *
	 * This pointer will go away, eventually, when we replace it with
	 * dynamically loaded symbols using dlopen().  But for now, until
	 * that functionality is in place, this will store a pointer to the
	 * root of the "user data."  It is obviously a pointer into shared
	 * memory - but to shared memory which was malloc()ed or mmap()ed
	 * by the user, not by internal subcontexts-control code.
	 */
	char *descriptive_name;
	void *user_root;
} sc;



/* every publicly mapped region in every subcontext is documented by
 * one of these structs.  Each struct is on one of several singly-linked
 * lists, rooted at the root control struct.
 */
typedef struct sc_region {
	/* this is the lock for this struct.  It protects nothing but
	 * the contents of this struct; however, that *IMPLICITLY*
	 * also protects the next struct in the list.
	 */
	russ_lock lock;

	/* every use of this struct includes a singly-linked list */
	struct sc_region *next;

	u64 vaddr;
	u64 len;

	/* these use the PROT_READ, PROT_WRITE, and PROT_EXEC defines
	 * from sys/mman.h.
	 *
	 * This *MUST* be PROT_NONE for reserved regions.
	 */
	u8 prot;

	/* this is set on regions which are used to contain other
	 * subcontext_region structs; these pages can *NEVER* be
	 * freed, in case that freeing and re-using those virtual
	 * addresses might lead to a circular metadata dependency.
	 *
	 * In other words, a region with this set must be in the
	 * active_regions list, and it will never leave.
	 *
	 * Note that I'm not saying that I know for sure that a
	 * problem exists; I may come up with a smarter algo in the
	 * future.  But right now, I'm using a very simplistic one,
	 * just to make sure that we can bootstrap the entire linked
	 * list of regions.
	 */
	bool permaPin;

	/* TODO: add fields to document the state and purpose of this
	 *       page.  Is it a ZERO page?  It is COW of something?
	 *       Is it a map of a file on disk?  Etc.
	 */

	/* this is -1 for all regions in the reserved_regions struct;
	 * it is also -1 for pages which haven't yet been initialized.
	 */
	u64 tmpfile_offset;

} sc_region;



typedef struct sc_process
{
	russ_lock lock;
	struct sc_process *next;

	/* this variable tracks the state of this struct: in the PREALLOC
	 * state, it has been preallocated by a currently-operational
	 * process, for use by a future process (so that the process doesn't
	 * have to do memory allocation while it is running).  In the INIT
	 * state, it has been claimed by such a process but is not yet
	 * operational, and the lower fields in this struct have undefined
	 * contents (and the process is not listed in proc_count).  In the
	 * ALIVE state, the process is operational and this struct is valid.
	 * In the DEAD state, the process has died and some remaining
	 * operational process needs to clean up this struct, decrement
	 * proc_count, and free the memory.
	 */
#define SUBCONTEXTS_PROC_STATE_PREALLOC 1
#define SUBCONTEXTS_PROC_STATE_INIT     2
#define SUBCONTEXTS_PROC_STATE_ALIVE    3
#define SUBCONTEXTS_PROC_STATE_DEAD     4
	int state;

	/* the true PID of the process, according to Linux */
	int pid;
} sc_process;



typedef struct sc_cleanupLogStep
{
	russ_lock lock;

	u64 seqNum;
	struct sc_cleanupLogStep *next;

	/* ack_targ is set to proc_count when the entry is created;
	 * it is the count of the number of processes which must look
	 * at this entry before it is cleaned up.  ack_count is the
	 * number which have done so.
	 */
	int ack_targ;
	int ack_count;

#define SC_CLS_RESERVE_MEMORY       1
#define SC_CLS_RESERVE_MEMORY_ABORT 2
	int op;

	/* each operation has its own private fields in this union */
	union {
		struct {
			/* this op is used to create new reserved regions.
			 * This requires that some process suggest a range
			 * of memory, and then all of the processes mmap()
			 * a PROT_NONE range to cover it (to prevent
			 * future range conflicts).  However, any process
			 * might fail, because of a previously-mmap()ed
			 * range.  (We could try to prevent this by atomically
			 * determining the current ranges used by all
			 * processes, but that would almost certainly require
			 * a global freeze - which I'm not willing to do.)
			 *
			 * Thus, a reservation is first *PROPOSED* using this
			 * operation.  Each process in turn attempts to mmap()
			 * the range, and if it succeeds, then it increments
			 * ack_count and it's done.  When the last process
			 * does its allocation, that process can malloc() a
			 * reserved region struct and add it to the list; at
			 * any moment after that, any process can start using
			 * that reserved region for new sc_mmap()s.
			 *
			 * The problem, of course, is that one or more
			 * processes might fail (because of conflicting
			 * maps already in place).  In that case, we need to
			 * abort this operation and clean it up.  To do this,
			 * the failing process needs to mark this op (to
			 * prevent more processes from executing it) and then
			 * add a new (abort) entry to the cleanupLog.  This
			 * will wake up the processes which have already
			 * performed this map, and tell them to free it.
			 *
			 * But still we have a problem: how do the processes
			 * know what to do when they process an abort?  Shall
			 * they free memory (because they allocated it) or
			 * not (because they knew to skip it)?  We can't use
			 * the naive strategy of "always mmap, then always
			 * munmap on abort", since if two or more processes
			 * fail, we need to keep track of *which* ones failed.
			 * Likewise, I considered requiring that reserve
			 * operations block all future ops, so that a process
			 * won't move on until it completes (which is unlikely
			 * to scale well), or even the use of a linked list of
			 * "mapped so far" processes (which is ugly).
			 *
			 * The solution is a tiny hack, but survivable: we
			 * simply only allow a single ongoing reservation
			 * operation at a time - and each process keeps, in
			 * its private memory, the record of what it did with
			 * the most recent reservation.  We keep the
			 * "allocation is ongoing" flag in the root control
			 * struct of the sc - since it's global to the
			 * subcontext.
			 *
			 * To mark an op as aborted, simply set its length
			 * to zero (and, of course, add the abort op to the
			 * end!).
			 */
			u64  vaddr,len;
		} reserve_memory;
	};
} sc_cleanupLogStep;



/* this struct carries metadata for a *FREE* buffer */
typedef struct sc_malloc_listNode
{
	u64 len;   /* the length of this chunk, including this struct */
	struct sc_malloc_listNode *next;

	/* this is 0 for almost all instances of this struct; if it is
	 * nonzero, then it means that this struct is at the head of a
	 * set of pages allocated by malloc().  If a complete page worth
	 * of free memory arises starting at this struct (plus enough to
	 * place a struct at the head of the next page), then this page
	 * will be munmap()ed.
	 *
	 * Since this metadata must be preserved, nodes in the free list
	 * which have nonzero here cannot completely be consumed (as would
	 * happen with most other entries - if we want to allocate from
	 * this range, then we must split the entry into two pieces first.
	 */
	u64 pages;
} sc_malloc_listNode;

/* these two structs carry metadata for *ACTIVE* allocations */
typedef struct sc_malloc_header
{
	u64 len;
	u64 magic;
#define SUBCONTEXTS_MALLOC_HEAD_MAGIC   SUBCONTEXTS_MAGIC('m', 1)
} sc_malloc_header;

typedef struct sc_malloc_footer
{
	u64 magic;
#define SUBCONTEXTS_MALLOC_FOOT_MAGIC   SUBCONTEXTS_MAGIC('m', 2)
} sc_malloc_footer;

/* this carries the control information for malloc() itself */
typedef struct sc_malloc_metadata
{
	russ_lock lock;

	sc_malloc_listNode *freeList;
} sc_malloc_metadata;



/* this struct contains all of the pieces which the first process must
 * initialize.  That process uses this struct *ONLY* during that init
 * process, and then from then on uses the sc struct, and the pointers
 * which project from that, for all access.
 *
 * This is just for convenience - I kept having to update the init
 * code to do more and more painful pointer arithmetic, and also to
 * keep updating the assert() which verifies that the starting structs
 * all begin on the same page.
 *
 * Note that the firstRegions[] field is an array of size 3 - this
 * is so that we can easily assert() that the entire minimum layout
 * fits into a single page.  However, in practice we expect the page
 * to have space for many region structs - and thus we effectively
 * treat this as an array of *some* length, greater than or equal to 3.
 *
 * Why 3?  It's because the root page *MUST* include the following
 * region structs:
 *    - the first active region (which documents the first page)
 *    - the first free region struct (which will store the information
 *      on the next page(s) allocated after the first page)
 *    - the first reserved region
 * The first two are *ABSOLUTE* requirements, so that a joining process
 * can bootstrap itself; the third is a coding-ease requirement, since
 * we want the init process to be able to post a starting "reserved"
 * region (to make future page allocations easy) inside the first
 * page.  In actual implementation, [0] is the first active page, [1]
 * is the first reservation, and [2] is the first free struct.
 */
typedef struct
{
	sc                 root;
	sc_malloc_metadata malloc_md;
	sc_region          firstRegions[3];
} sc__firstPage;



/* ---- WARNING ----
 *
 * This function is *ONLY* designed for internal use by the subcontexts
 * library - maps a shared pointer (to the root control struct of some
 * subcontext) to the local struct which represents it.
 */
struct sc_local *lookupScLocal(sc*);

/* ---- WARNING ----
 *
 * This function is *ONLY* designed for internal use by the subcontexts
 * library - it kills the local process by sending SIGINT to itself.
 */
void sc_killLocalProc(void);

/* ---- WARNING ----
 *
 * This function is *ONLY* designed for internal use by the subcontexts
 * library - it imports an existing page from the local address space
 * into subcontexts space.
 *
 * This function requires that we allocate reserved space for the range
 * (which requires interaction with all of the remote processes to ensure
 * that they can allocate the space), and then the reserved pages will
 * be turned into active pages, with the existing data memcpy()'d into
 * the temporary file.
 */
int sc_import(sc*, void *addr, u64 len, int prot);

/* ---- WARNING ----
 *
 * This function is *ONLY* designed for internal use by the subcontexts
 * library - it imports an existing page from the local address space
 * into subcontexts space.
 *
 * This version imports PROT_NONE pages - so it will never read the
 * original page.  That's good, since (presumably) the page is not readable!
 */
int sc_import_protNone(sc*, void *addr, u64 len);

/* ---- WARNING ----
 *
 * This function is *ONLY* designed for internal use by the subcontexts
 * library - it posts a new request to allocate reserved pages.  It does
 * *NOT* block waiting for it to finish - but it *WILL* return the seqNum
 * of the new cleanupLog entry (if the caller passes a pointer to an
 * out parameter).
 */
int sc_postNewReservation(struct sc_local*, u64 pages, u64 *seqNum_out);


#endif


