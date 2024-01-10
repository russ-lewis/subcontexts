
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <memory.h>
#include <sys/mman.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ucontext.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "subcontexts.h"
#include "subcontexts_control.h"
#include "subcontexts_local.h"
#include "subcontexts_registry.h"

#include "russ_common_code/c/russ_todo.h"
#include "russ_common_code/c/russ_int_typedefs_linux.h"
#include "russ_common_code/c/russ_locks.h"
#include "russ_common_code/c/russ_flushfile.h"
#include "russ_common_code/c/russ_dumplocalmaps.h"
#include "russ_common_code/c/russ_dumpbacktrace.h"
#include "russ_common_code/c/russ_futexwrappers.h"

#undef mmap
#undef munmap



/* these variables are declared in subcontexts_local.h */
russ_lock        sc_local_list__lock = RUSS_LOCK_INIT;
struct sc_local *sc_local_list       = NULL;

/* this variable is set by the sc_init().  Since it is in the subcontexts
 * shared library, there is a different copy of this variable for every
 * subcontext, and for every process that loads the library.  (Obviously, the
 * subcontext_init process shares its process-copy with the subcontext that it
 * creates, since the whole point is that the subcontext is just an image of
 * that process's memory.)
 *
 * Thus, in ordinary processes (which use but do not create subcontexts), this
 * variable will be NULL in the *PROCESS* context, but non-NULL (and different)
 * in each of the subcontext-contexts which the process imports.  The getCurSc()
 * function simply returns this value.
 *
 * Strange.
 *
 * But kinda cool.
 *
 * POSTSCRIPT: Note the difference between the sc_local struct (which is a
 *             linked list, process specific, which documents metadata about
 *             the subcontexts mapped into a specific process), and this
 *             pointer (which is a single pointer per subcontext, giving a
 *             pointer to that subcontext's global information).
 */
struct sc *curSc = NULL;



/* TODO: add support for page-fault-chaining, where the subcontexts
 *       page fault handler would call *another* handler when it
 *       doesn't know how to handle certain page faults.
 */



static int  sc_init_register_signal_handlers(void);

static int scInit_setup_procListEntry(sc*);
static int scInit_setup_backgroundThread(sc_local*);

static void sc_signal_handler_SEGV(int, siginfo_t*, void*);
static void sc_signal_handler_INT (int, siginfo_t*, void*);

static sc_region *sc_getFreeRegion(sc*);

static  int sc_backgroundThread(void*);
static void sc_handleCleanupLogStep(sc_local*, sc_cleanupLogStep *);



static void library_init() __attribute__((constructor));
static void library_init()
{
	/* register the signal handlers */
	if (sc_init_register_signal_handlers() != 0)
		exit(-1);
}



sc *sc_init(int fd)
{
	int rc;
	int i;


	/* confirm that the input file is empty.  If you don't do this, then
	 * it's possible that newly-mmap()ed files might start out with
	 * non-zero contents.
	 */
	struct stat statbuf;
	if (fstat(fd, &statbuf) != 0)
	{
		perror("Could not stat() the backing file passed to sc_init()");
		return NULL;
	}
	if (statbuf.st_size != 0)
	{
		fprintf(stderr, "The backing file passed to sc_init() was not empty.\n");
		return NULL;
	}


	/* the 'sc' struct (the opaque handle returned to the user) must be
	 * malloc()ed from *PRIVATE* memory.  But all of the pointers that are
	 * stored inside it are pointers into shared subcontext memory.
	 */
	sc_local *scLocal = malloc(sizeof(sc_local));
	if (scLocal == NULL)
		return NULL;


	/* save away the file descriptor, for later use */
	scLocal->shm_file = fd;


	/* we expect that the starting file is empty.  We resize the file to
	 * allocate the first page.
	 */
	rc = ftruncate(fd, getpagesize());
	if (rc != 0)
	{
		perror("Could not resize the backing file");
		return NULL;
	}


	/* this struct represents the first page we'll init.  We'll
	 * use this only during the initial mmap(); after that, we'll
	 * switch to using better pointers.  This is just to simplify
	 * the pointer arithmetic.
	 */
	sc__firstPage *firstPage;
	  assert(sizeof(*firstPage) <= getpagesize());


	/* map that first page into memory.  Note that this map, since it's the
	 * first one ever (for this subcontext), doesn't have any expected
	 * vaddr; we just take whatever Linux gives us, and that becomes the
	 * final location for this page, for all time.
	 */
	firstPage = mmap(NULL, getpagesize(),
	                 PROT_READ | PROT_WRITE,
	                 MAP_SHARED,
	                 fd, 0);
	if (firstPage == MAP_FAILED)
	{
		perror("Could not mmap() the root struct");
		return NULL;
	}


	/* fill in the various pointers
	 *
	 * the page *MUST* contain at least two regions (for the
	 * first active and first reserved regions), but normally it
	 * will have more than that.
	 */
	scLocal->sc = &firstPage->root;

	sc_region *regions = firstPage->firstRegions;
	u64 regions_count = (((ssize_t)firstPage) + getpagesize() -
	                      (ssize_t)regions) / sizeof(regions[0]);
	  assert(regions_count >= 2);


	/* save the 'sc' for later use by getCurSc().  See the long discussion
	 * where this variable is declared, above in this file.
	 */
	curSc = scLocal->sc;


	/* declare that we are not part of any ongoin reservation */
	scLocal->recentReserve.vaddr = 0;
	scLocal->recentReserve.len   = 0;


	/* We start by initializing the magic value (so that other
	 * processes will know that this is a subcontexts root struct),
	 * and then the lock (in exclusive mode, so that they won't
	 * examine the contents beyond the magic).
	 *
	 * Once we have done these basic things, we will report that
	 * init has completed (even though it hasn't really) so that
	 * the user can start running other processes.  They will map
	 * the page, see the proper magic value, and then try to lock
	 * the struct - and block until init is complete.
	 */
	scLocal->sc->magic = SUBCONTEXTS_ROOT_MAGIC;
	russ_lock_init     (&scLocal->sc->lock);
	russ_lock_exclusive(&scLocal->sc->lock);

	printf("Subcontexts ready.  Join the group using pid %d, fileno %d.\n", getpid(), scLocal->shm_file);
	fflush(NULL);


	/* some fields are simple; we fill them in first; we'll do the
	 * more complex ones later.
	 */
	scLocal->sc->unmapped_regions = NULL;
	scLocal->sc->tmpfile_size     = getpagesize();
	scLocal->sc->proc_count       = 0;  // we'll fix this later
	scLocal->sc->cleanupLog_lastSeqNum = 1;
	scLocal->sc->cleanupLog            = NULL;
	scLocal->sc->cleanupLog_tail       = NULL;
	scLocal->local_cleanupLog_flushPoint = scLocal->sc->cleanupLog_lastSeqNum;


	/* initialize the process list */
	scLocal->sc->proc_list                  = NULL;
	scLocal->sc->proc_list__deadFlagPending = false;


	/* region0 contains the root control struct, and the two
	 * metadata structs we're filling in now.
	 *
	 * Note that we don't need to grab locks on any of the regions
	 * that we're initializing because we hold an exclusive lock
	 * on the root.  Nobody can even *find* these structs.
	 */
	russ_lock_init(&regions[0].lock);
	scLocal->sc->active_regions = &regions[0];
	regions[0].next           = NULL;
	regions[0].vaddr          = (u64)scLocal->sc;
	regions[0].len            = getpagesize();
	regions[0].prot           = PROT_READ | PROT_WRITE;
	regions[0].tmpfile_offset = 0;
	regions[0].permaPin       = true;

	scLocal->sc->active_regionsTail = &regions[0];


	/* mark out a large range of virtual space for the first
	 * reserved region.
	 */
	u64   reserved_len  = (1ULL << 20);
	void *reserved_area = mmap(NULL, reserved_len,
	                           PROT_NONE,
	                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
	                           -1, 0);
	  assert(reserved_area != MAP_FAILED);


	scLocal->sc->reserved_regions = &regions[1];
	russ_lock_init(&regions[1].lock);
	regions[1].next           = NULL;
	regions[1].vaddr          = (u64)reserved_area;
	regions[1].len            =      reserved_len;
	regions[1].prot           = PROT_NONE;
	regions[1].tmpfile_offset = -1;
	regions[1].permaPin       = false;


	/* if the page had space for any more region structs, then
	 * put them on the free list.
	 */
	if (regions_count == 2)
		scLocal->sc->free_structs = NULL;
	else
	{
		for (i=2; i<regions_count; i++)
			russ_lock_init(&regions[i].lock);

		scLocal->sc->free_structs = &regions[2];
		for (i=3; i<regions_count; i++)
			regions[i-1].next = &regions[i];
		regions[regions_count-1].next = NULL;
	}


	scLocal->sc->malloc_md = &firstPage->malloc_md;

	russ_lock_init(&scLocal->sc->malloc_md->lock);
	scLocal->sc->malloc_md->freeList = NULL;


	/* the last step in init is to allocate the first process
	 * struct.  We have to do this with malloc() because we will
	 * free() it later - which is why it can't be part of the first
	 * page.  But since we're going to malloc(), we have to release
	 * the lock on the root struct, since this first malloc() will
	 * mmap() new memory.  Plus, we'll need register the signal
	 * handler, since mmap() will make use of it.
	 *
	 * This all means, of course, that there is a window of time
	 * where we (theoretically) have ZERO subcontexts processes
	 * running.  Oh, well.
	 */
	russ_unlock_exclusive(&scLocal->sc->lock);


	/* add this struct to the linked list of subcontexts
	 *
	 * UPDATE: Do this *BEFORE* we call sc_malloc(), since it will
	 *         need to map the sc to the sc_local.
	 */
	russ_lock_exclusive(&sc_local_list__lock);
	  scLocal->next = sc_local_list;
	  sc_local_list = scLocal;
	russ_unlock_exclusive(&sc_local_list__lock);


	/* allocate the first process struct and initialize it */
	sc_process *firstProc;
	firstProc = sc_malloc(scLocal->sc, sizeof(*firstProc));
	  assert(firstProc != NULL);

	russ_lock_init(&firstProc->lock);
	firstProc->next  = NULL;
	firstProc->state = SUBCONTEXTS_PROC_STATE_ALIVE;
	firstProc->pid   = getpid();


	/* post it into the root struct */
	russ_lock_exclusive(&scLocal->sc->lock);
	  scLocal->sc->proc_count = 1;
	  scLocal->sc->proc_list  = firstProc;
	russ_unlock_exclusive(&scLocal->sc->lock);


	/* the user_root is a throwaway pointer...we will replace it
	 * with dlopen() functionality soon.  So we don't worry about
	 * being race-safe.
	 */
	scLocal->sc->descriptive_name = NULL;
	scLocal->sc->user_root = NULL;


	/* post the local process to the process list */
	if (scInit_setup_procListEntry(scLocal->sc) != 0)
		return NULL;


	/* kick off the background thread, which will monitor the cleanupLog
	 * and handle any other communication tasks.
	 */
	if (scInit_setup_backgroundThread(scLocal) != 0)
		return NULL;


	return scLocal->sc;
}



sc *sc_join(int fd)
{
	int i;


if (0)
{
FILE *fp = fopen("/proc/self/maps", "r");
russ_flushfile(fileno(fp), 1);
fclose(fp);
}


	/* the 'sc' struct (the opaque handle returned to the user) must be
	 * malloc()ed from *PRIVATE* memory.  But all of the pointers that are
	 * stored inside it are pointers into shared subcontext memory.
	 */
	sc_local *scLocal = malloc(sizeof(sc_local));
	if (scLocal == NULL)
		return NULL;


	/* save away the file descriptor, for later use */
	scLocal->shm_file = fd;


	/* mmap() the root control struct anywhere.  We don't care
	 * where it lands, for now.
	 */
	scLocal->sc = mmap(NULL, getpagesize(),
	                   PROT_READ | PROT_WRITE,
	                   MAP_SHARED,
	                   scLocal->shm_file, 0);
	if (scLocal->sc == MAP_FAILED)
	{
		perror("Could not mmap() the root struct");
		return NULL;
	}


	/* if the magic isn't what we expect, then we can't use it -
	 * we either have a bad struct, or maybe a code version
	 * mismatch.
	 *
	 * Note that we do this *BEFORE* we take the lock, because we
	 * don't want to accidentally corrupt data which we don't
	 * understand!
	 */
	if (scLocal->sc->magic != SUBCONTEXTS_ROOT_MAGIC)
	{
		fprintf(stderr, "Failed to initialize subcontexts: the root struct has a bad magic value.  Expected=0x%08x Actual=0x%08x\n", SUBCONTEXTS_ROOT_MAGIC, scLocal->sc->magic);

		munmap(scLocal->sc, getpagesize());
		free(scLocal);
		return NULL;
	}


	/* lock the root control struct.  Note that we haven't
	 * relocated it yet, but we aren't following any pointers yet,
	 * so it's OK to just use it in its current location.
	 *
	 * Note that we're going to move this struct in just a moment,
	 * and that changes our virtual perception of it.  But that
	 * doesn't change the struct as it is seen by any other
	 * process.  Therefore, a shared lock is appropriate.
	 */
	russ_lock_shared(&scLocal->sc->lock);


	/* now that we own the lock, it is reasonable for us to
	 * access the struct.  In most cases, we could get away with
	 * accessing it without a lock, since its contents never
	 * change; however, it's possible that we might be racing with
	 * init, and so the lock is necessary in general.  Plus, it's
	 * just good practice to be careful.  Right now, the focus is
	 * on correctness, not performance.
	 */


	/* our first step is to see if the subcontext in question has
	 * already been mapped into the local process.  We could
	 * detect this implicitly by simply checking to see if the
	 * mmap()-to-relocate below fails, but I like having an
	 * explicit message, with an explicit error code.
	 */
	u64 root_dest = ((u64)scLocal->sc->active_regions) & ~(getpagesize()-1);

	sc_local *curMappedSc = sc_local_list;   // could be NULL
	if (lookupScLocal((sc*)root_dest) != NULL)
	{
		fprintf(stderr, "%s(): ERROR: The subcontext is already mapped into the local process!\n", __func__);

		munmap(scLocal->sc, getpagesize());
		free(scLocal);
		return NULL;
	}


	/* Note that it's possible that this might already be in the
	 * correct location; after all, if the virtual addresses are
	 * not randomized, Linux might place this first 4K mmap() in
	 * the same location for every process.  But in general, we
	 * have to handle it.
	 *
	 * To calculate the correct address for the root struct, we
	 * take the address of the first active region struct (which
	 * must be in the same page) and round down.  Note that we used
	 * to do this with a pointer subtraction, but that was
	 * error-prone as I periodically was updating the format of the
	 * first page.
	 */

	if ((void*)root_dest != scLocal->sc)
	{
		/* Note that we specify an address, but we do *NOT*
		 * pass MAP_FIXED!!!  If we passed MAP_FIXED, then
		 * this mmap() might overwrite something else, and
		 * thus cause corruption.  Instead, we just pass a
		 * "hint" to Linux, which it is allowed to ignore if
		 * it wants.
		 */
		void *new_map = mmap((void*)root_dest, getpagesize(),
	                        PROT_READ | PROT_WRITE,
	                        MAP_SHARED,
	                        scLocal->shm_file, 0);

		if (new_map != (void*)root_dest)
		{
			fprintf(stderr, "Failed to initialize subcontexts: could not relocate the root struct to the proper virtual address");

			if (new_map == MAP_FAILED)
				perror("");
			else
			{
				fprintf(stderr, "there appears to be a conflicting map already in place.  Target range: addr=0x%016llx len=0x%016llx; actual range: addr=0x%016llx\n", (u64)root_dest, (u64)getpagesize(), (u64)new_map);

				char cmd[256];
				sprintf(cmd, "cat /proc/%d/maps", getpid());
				system(cmd);
			}

			russ_unlock_shared(&scLocal->sc->lock);

			munmap(scLocal->sc, getpagesize());
			free(scLocal);
			return NULL;
		}

		/* we moved it successfully.  Free the original map,
		 * then we can move forward using the new location.
		 */
		munmap(scLocal->sc, getpagesize());
		scLocal->sc = (sc*)root_dest;
	}


	/* we need to store our process metadata in a shared struct -
	 * but we can't allocate memory until we have initialized
	 * everything.  Therefore, we rely on an operational process
	 * allocating memory on our behalf, and storing a pointer to
	 * it in the root struct.  We can extract this poitner from
	 * the root struct even if we do not yet have any other pages
	 * mapped; however, we must be careful to not touch it until
	 * the entire page fault handling mechanism is ready for use.
	 *
	 * In this step, we extract that pointer from the root struct,
	 * and nullify the pointer, so that if another process is
	 * racing to complete init, we won't fight over it.
	 */
	sc_process *myProc = NULL;

	russ_unlock_shared (&scLocal->sc->lock);
	russ_lock_exclusive(&scLocal->sc->lock);

	if (scLocal->sc->prealloc_proc == NULL)
	{
		russ_unlock_exclusive(&scLocal->sc->lock);

		fprintf(stderr, "Subcontexts init failed: prealloc_proc was NULL.  This is probably a transient condition caused by two processes racing to init at the same time.\n");
		return NULL;
	}

	myProc = scLocal->sc->prealloc_proc;
	scLocal->sc->prealloc_proc = NULL;

	russ_downgrade_lock(&scLocal->sc->lock);


	/* now we enter a very tricky phase of init.  At this point,
	 * our program has only mmap()ed a single page of the
	 * subcontext - the "first page," which contains the root
	 * control struct.
	 *
	 * Before we are fully operational, we need to hold page maps
	 * at all of the virtual addresses which the rest of the
	 * processes are already using - this means both the pages in
	 * the active list, and also all of the pages in the reserved
	 * list.  There are two basic ways we might do this: with
	 * PERM_NONE pages (relying on the page fault handler to
	 * replace them with the proper maps on demand), or with the
	 * actual maps.  (For reserved pages, PERM_NONE is of course
	 * the only valid option.)  I like using PERM_NONE in theory,
	 * but I'm not convinced that it is actually better; we have to
	 * walk the active_regions list either way, and do an mmap()
	 * at every step, and if I don't actually touch the virtual
	 * memory, I'm not convinced that PERM_NONE is a lot better
	 * than the real maps.
	 *
	 * So, how do we get these maps in place?  We could try to do
	 * it implicitly, using the page fault handler - but that would
	 * only work for pages which contain metadata that we touch.
	 * We would still have to walk the two lists and perform all of
	 * the mmap()s required by each struct.  And so I choose the
	 * simple (though perhaps inelegant) solution: I will manually
	 * walk both lists, and manually map the pages there.  I will
	 * set up the page fault handler only *AFTER* I have
	 * established the mappings.
	 *
	 * (Note that it's safe to walk the active regions list even
	 * while we're still mapping pages because we guarantee that
	 * the maps will be in order - that is, the metadata for a page
	 * will always show up before the page is used in the list.)
	 */


	/* save the pointer to the first element in each list; lock
	 * both; then unlock the root struct.
	 *
	 * UPDATE: Skip over the first element of the active_regions
	 *         list, since it is already mapped!
	 */
	sc_region *lists[2];

	russ_lock_shared(&scLocal->sc->active_regions->lock);

	lists[0] = scLocal->sc->active_regions->next;
	lists[1] = scLocal->sc->reserved_regions;

	russ_lock_shared(&lists[0]->lock);
	russ_lock_shared(&lists[1]->lock);

	russ_unlock_shared(&scLocal->sc->active_regions->lock);
	russ_unlock_shared(&scLocal->sc->lock);

	for (i=0; i<2; i++)
	{
		sc_region *cur = lists[i];
		  assert(cur != NULL);

		while (cur != NULL)
		{
			/* perform the map.  If there is a conflicting
			 * map in place, then we must terminate this
			 * process.
			 */

			int flags;
			int file;
			u64 offset;
			if (i == 0 && cur->prot != PROT_NONE)
			{
				flags = MAP_SHARED;
				file  = scLocal->shm_file;
				offset = cur->tmpfile_offset;

				assert(offset + cur->len <=
				       scLocal->sc->tmpfile_size);

				assert(offset % getpagesize() == 0);
			}
			else
			{
				/* this handles reserved regions *AND*
				 * active regions with PROT_NONE.
				 */

				flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
				file  = -1;
				offset = 0;

				assert(cur->tmpfile_offset == -1);
			}

			void *map = mmap((void*)cur->vaddr, cur->len,
			                 cur->prot, flags,
			                 file, offset);
			if (map == MAP_FAILED)
			{
				char msg[256];
				sprintf(msg, "Could not mmap() an existing range: addr=0x%016llx len=0x%016llx : flags=%d file=%d offset=0x%llx", cur->vaddr, cur->len, flags,file,offset);
				perror(msg);
				return NULL;
			}

			if (map != (void*)cur->vaddr)
			{
				fprintf(stderr, "subcontext init failure: Could not perform the initial mmap() of a range.  vaddr=0x%016llx len=0x%016llx prot=0x%x flags=0x%x file=%d offset=0x%llx : mmap() returned %p\n", cur->vaddr, cur->len, cur->prot, flags, file, offset, map);

				FILE *fp = fopen("/proc/self/maps", "r");
				russ_flushfile(fileno(fp), 1);
				return NULL;
			}


			/* advance to the next in the list, if it
			 * exists
			 */
			sc_region *next = cur->next;

			if (next != NULL)
				russ_lock_shared(&next->lock);
			russ_unlock_shared(&cur->lock);

			cur = next;
		}
	}


	/* record this process in proc_count and the process list
	 *
	 * while we hold the lock, we must also record that we have
	 * no work to do in the cleanupLog.
	 */

	myProc->state = SUBCONTEXTS_PROC_STATE_ALIVE;
	myProc->pid   = getpid();

	russ_lock_exclusive(&scLocal->sc->lock);
	  scLocal->sc->proc_count++;



// TODO: add support for this condition.  We can't handle a reservation
//       which is pending while we init - we need to flush it out before
//       we do our reservation-scan, and then block additional ones from
//       running until we're done.  We can accomplish the latter by writing
//       a dummy pointer value to this field while we init.
if (scLocal->sc->pending_reservation != NULL)
{
	russ_unlock_exclusive(&scLocal->sc->lock);

TODO();   // before we release the lock above, we need to re-populate prealloc_proc, and free the one that we had already.

	TODO();
}



	  scLocal->local_cleanupLog_flushPoint = scLocal->sc->cleanupLog_lastSeqNum;

	  myProc->next = scLocal->sc->proc_list;
	  scLocal->sc->proc_list = myProc;
	russ_unlock_exclusive(&scLocal->sc->lock);


	/* finally, register the signal handler.  Any future accesses
	 * to pages which page fault will be handled cleanly.
	 */
	if (sc_init_register_signal_handlers() != 0)
	{
		russ_unlock_shared(&scLocal->sc->lock);
		return NULL;
	}


	/* post the local process to the process list */
	if (scInit_setup_procListEntry(scLocal->sc) != 0)
		return NULL;


	/* kick off the background thread, which will monitor the cleanupLog
	 * and handle any other communication tasks.
	 */
	if (scInit_setup_backgroundThread(scLocal) != 0)
		return NULL;


	/* add this struct to the linked list of subcontexts */
	russ_lock_exclusive(&sc_local_list__lock);
	  scLocal->next = sc_local_list;
	  sc_local_list = scLocal;
	russ_unlock_exclusive(&sc_local_list__lock);


	return scLocal->sc;
}



int scInit_setup_procListEntry(sc *mySc)
{
	/* we need to allocate memory for a new process struct, and save it
	 * into the prealloc_proc pointer.
	 */
	sc_process *nextProc = sc_malloc(mySc, sizeof(*nextProc));
	if (nextProc == NULL)
	{
		fprintf(stderr, "Could not allocate memory for the next process's metadata.  This process will die, and all future init attempts will fail!\n");

TODO();   // clean up my own metadata!

		return 3;
	}

	russ_lock_init(&nextProc->lock);
	nextProc->next  = NULL;
	nextProc->state = SUBCONTEXTS_PROC_STATE_PREALLOC;
	nextProc->pid   = 0;

	russ_lock_exclusive(&mySc->lock);
	  assert(mySc->prealloc_proc == NULL);
	  mySc->prealloc_proc = nextProc;
	russ_unlock_exclusive(&mySc->lock);

	return 0;
}



int sc_init_register_signal_handlers()
{

if (0)
printf("%s()\n", __func__);

	/* define an alternate stack for the page fault handler.  In general,
	 * this is imporant because we need to be able to handle a page fault
	 * which occurs on the stack itself.  I'm not sure that it is
	 * important for this demo, but I'm going to run with it for now.
	 */
	const int ALT_STACK_SIZE = 1024*1024;
	void *altstack = mmap(NULL, ALT_STACK_SIZE,
	                      PROT_READ | PROT_WRITE,
	                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
	                      -1, 0);
	if (altstack == MAP_FAILED)
	{
		perror("Could not map pages for the sigaltstack");
		return -1;
	}

	stack_t          st;
	struct sigaction sa_SEGV, sa_INT;
	
	bzero(&st, sizeof(st));
	bzero(&sa_SEGV, sizeof(sa_SEGV));
	bzero(&sa_INT , sizeof(sa_INT ));

	st.ss_sp    = altstack;
	st.ss_flags = 0;
	st.ss_size  = ALT_STACK_SIZE;

	sa_SEGV.sa_sigaction = sc_signal_handler_SEGV;
	sigemptyset(&sa_SEGV.sa_mask);
	sa_SEGV.sa_flags = SA_SIGINFO | // use sa_sigaction instead of
	                                // sa_handler
	                   SA_ONSTACK | // use the sigaltstack() stack
	                   SA_RESTART | // restart library calls like
	                                // open() after we fix the page fault
	                   SA_NODEFER;  // allow recursive SEGV

	sa_INT.sa_sigaction = sc_signal_handler_INT;
	sigemptyset(&sa_INT.sa_mask);
	sa_INT.sa_flags = SA_SIGINFO | // use sa_sigaction instead of sa_handler
	                  SA_ONSTACK;  // use the sigaltstack() stack

	if (sigaltstack(&st, NULL) != 0)
	{
		perror("Could not establish sigaltstack");
		munmap(altstack, ALT_STACK_SIZE);
		return -1;
	}

	if (sigaction(SIGSEGV, &sa_SEGV, NULL) != 0)
	{
		perror("Could not register the SEGV signal handler");
		munmap(altstack, ALT_STACK_SIZE);
		return -1;
	}

	if (sigaction(SIGINT, &sa_INT, NULL) != 0)
	{
		perror("Could not register the INT signal handler");
		munmap(altstack, ALT_STACK_SIZE);
		return -1;
	}

	return 0;
}

void sc_signal_handler_SEGV(int signum, siginfo_t *info, void *context_void)
{

// TODO: according to
//           http://stackoverflow.com/questions/2663456/write-a-signal-handler-to-catch-sigsegv
//       this whole function is very problematic, as there might be subtle
//       race conditions.
//
//       I can see how the calls to printf() are particularly problematic -
//       though I would be *VERY* surprised if the rest of it was.  But they
//       may be right - maybe there are subtle race conditions I'm missing.
//
//       I should investigate using the library 'libsigsegv' .


	ucontext_t *context = context_void;

	assert(signum         == SIGSEGV);
	assert(info->si_signo == SIGSEGV);

	assert(info->si_code ==   SI_KERNEL ||
	       info->si_code == SEGV_MAPERR ||
	       info->si_code == SEGV_ACCERR);

	u64 vaddr = (u64)info->si_addr;


	/* NULL pointer dereferences are auatomatically failures.  Note that
	 * this is necessary to make the recursion-detection code below work;
	 * it is also necessary in order to hadle TODO() calls elegantly.
	 */
	if (vaddr == 0)
	{
		fflush(NULL);

		fprintf(stderr, "%s(): Aborting local process because of an attempt to touch NULL.\n", __func__);

		fprintf(stderr, "\n");

		fprintf(stderr, " --- BACKTRACE BEGIN ---\n");
		russ_dumpBacktrace(stdout);
		fprintf(stderr, " --- BACKTRACE END ---\n");

		fprintf(stderr, "\n");

		fprintf(stderr, " --- MAP DUMP BEGIN ---\n");
		russ_dumpLocalMaps(NULL);
		fprintf(stderr, " --- MAP DUMP END ---\n");

		fprintf(stderr, "\n");

		sc_killLocalProc();
	}


	/* According to sys/ucontext.h, REG_ERR is register 19 of gregs ...
	 * even though (for reasons I can't fathom), I can't get REG_ERR to
	 * work as a symbol.
	 *
	 * According to the Linux kernel, function setup_sigcontext() in
	 * arch/x86/kernel/signal.c, this field is set from the error_code
	 * field; and arch/x86/mm/fault.c tells us how to interpret the bits.
	 *
	 * The key bits we expect are:
	 *      - USER  (always)
	 *      - WRITE (if a write)
	 *      - INSTR (if a code fetch)
	 * Any other bits are unexpected, and we will refuse to attempt to
	 * try to handle the error.
	 */
	unsigned long int err = context->uc_mcontext.gregs[19];
	bool isWrite = ((err &  2) != 0);
	bool isUser  = ((err &  4) != 0);
	bool isInstr = ((err & 16) != 0);
	unsigned long int  neverSetBits = err & ~(2|4|16);

	if (info->si_code == SEGV_MAPERR)
	{
		printf("%s(): Killing the local process because the range is not mapped to anything; this probably indicates a wild pointer.  vaddr=0x%016llx isWrite=%d isUser=%d isInstr=%d info->si_code=%d (SEGV_MAPERR=%d SI_KERNEL=%d)\n", __func__, vaddr, isWrite,isUser,isInstr, info->si_code,SEGV_MAPERR,SI_KERNEL);

		russ_dumpLocalMaps(NULL);
		sc_killLocalProc();
	}

	if (info->si_code == SI_KERNEL)
	{
		printf("%s(): Killing the local process because the si_code is SI_KERNEL (not sure exactly what that means).  vaddr=0x%016llx isWrite=%d isUser=%d isInstr=%d info->si_code=%d (SEGV_MAPERR=%d SI_KERNEL=%d)\n", __func__, vaddr, isWrite,isUser,isInstr, info->si_code,SEGV_MAPERR,SI_KERNEL);

		russ_dumpLocalMaps(NULL);
		sc_killLocalProc();
	}

	if (neverSetBits != 0 || isUser == 0 ||
	    isWrite && isInstr)
	{
		printf("%s(): Killing the local process because the SEGV context is insane!  vaddr=0x%016llx isWrite=%d isUser=%d isInstr=%d neverSetBits=0x%lx info->si_code=%d (SEGV_MAPERR=%d SI_KERNEL=%d)\n", __func__, vaddr, isWrite,isUser,isInstr, neverSetBits, info->si_code,SEGV_MAPERR,SI_KERNEL);

		russ_dumpLocalMaps(NULL);
		sc_killLocalProc();
	}


	fprintf(stderr, "%s(): vaddr=0x%llx isWrite=%d isInstr=%d : BEGIN\n", __func__, vaddr, isWrite,isInstr);


	/* search for 'vaddr' in the list of active regions.  Note that this
	 * might cause recursive SEGVs; this happens if there have been new
	 * pages allocated since this process initialized, and those pages
	 * contain region structs which we'll touch.
	 *
	 * Since we set SA_NODEFER in the signal flags (see the call to
	 * sigaction() above), recursive SEGV is possible.
	 *
	 * While recursion is normal and part of the subcontexts design - and
	 * infinite recursion is designed to be impossible - I want to be able
	 * to debug.  So I use a static variable here to store the vaddr that
	 * we're searching for - if we end up with simple recursion, then this
	 * will detect it.  Note that if we end up with something more than
	 * simple recursion (like alternating between two addresses), then this
	 * won't detect the problem.
	 */
	static u64 prev_vaddr = 0;
	if (vaddr == prev_vaddr)
	{
		printf("%s(): Write to readonly page (or recursive page fault) detected!  Aborting local process!\n", __func__);
		exit(3);
	}
	prev_vaddr = vaddr;


	russ_lock_shared(&sc_local_list__lock);
	  sc_local *curScLocal = sc_local_list;
	    assert(curScLocal->next == NULL);   // handle multiple entries on the list
	russ_unlock_shared(&sc_local_list__lock);


	russ_lock_sharedRecursive(&curScLocal->sc->lock);

	russ_lock *prev_lock = &curScLocal->sc->lock;
	sc_region *curReg    =  curScLocal->sc->active_regions;

	while (curReg != NULL)
	{
		/* leapfrog the lock */
		russ_lock_sharedRecursive  (&curReg->lock);
		russ_unlock_sharedRecursive( prev_lock);
		prev_lock = &curReg->lock;

		/* is this region what we wanted? */
		if (curReg->vaddr            <= vaddr &&
		    curReg->vaddr + curReg->len >  vaddr)
		{
			/* the region matches the address which the user tried
			 * to access.  But was it a type of access not
			 * supported by the region?
			 */
			assert(curReg->prot == PROT_READ              ||
			       curReg->prot == PROT_READ | PROT_WRITE ||
			       curReg->prot == PROT_READ | PROT_EXEC);
			assert(isWrite == false || isInstr == false);

			if (isWrite && curReg->prot != (PROT_READ | PROT_WRITE) ||
			    isInstr && curReg->prot != (PROT_READ | PROT_EXEC ))
			{
				fprintf(stderr, "%s(): Invalid access.  isWrite=%d isInstr=%d region->prot=0x%x (READ=0x%x WRITE=0x%x EXEC=0x%x)\n", __func__, isWrite,isInstr, curReg->prot, PROT_READ, PROT_WRITE, PROT_EXEC);

				sc_killLocalProc();
			}



			/* mmap() these pages in.  In theory, we could mmap()
			 * only one page, but why bother?  After all, I don't
			 * know that Linux is smart enough to connect adjacent
			 * vma_struct's together.
			 */
			void *map = mmap((void*)curReg->vaddr, curReg->len,
			                 curReg->prot, MAP_SHARED | MAP_FIXED,
			                 curScLocal->shm_file, curReg->tmpfile_offset);
			if (map != (void*)curReg->vaddr)
			{
				printf("%s(): Could not map in pages to fulfill the page fault!\n", __func__);
				exit(3);
			}


			/* else, break out of the loop, with cur != NULL,
			 * so that the code outside the loop won't think
			 * that it needs to crash the program.
			 */
			break;
		}

		/* go look at the next */
		curReg = curReg->next;
	}

	russ_unlock_sharedRecursive(prev_lock);


	/* if we get here, then we have scanned the active_regions list,
	 * and cleaned up all of the locks that we used.  If we found a
	 * region which matched the user's vaddr and the permissions allowed
	 * the access, then we used mmap() to set it up.  If we did not find
	 * a matching region - or if the permissions didn't match - then
	 * 'cur' is NULL here and we need to crash.
	 *
	 * We kill ourselves by escalating to a SIGINT - since SIGINT has
	 * code which automatically does process cleanup.  However, we also
	 * have a static variable to do recursion detection.  We also have
	 * a fallback case, which should never be triggered unless SIGINT
	 * is blocked somehow.
	 */
	if (curReg == NULL)
	{
		fprintf(stderr, "%s(): Killing the local process because of an attempt to access invalid memory at address 0x%016llx\n", __func__, vaddr);

		russ_dumpLocalMaps(NULL);
		sc_killLocalProc();
	}


	/* we're done.  The address should be working now.
	 *
	 * REMEMBER: we have to zero out prev_vaddr, so that we don't later
	 *           get confused about what is going on!
	 */
	prev_vaddr = 0;

	fprintf(stderr, "%s(): vaddr=0x%llx isWrite=%d isInstr=%d : COMPLETE OK\n", __func__, vaddr, isWrite,isInstr);
	return;
}

void sc_signal_handler_INT(int signum, siginfo_t *info, void *context)
{
	assert(signum         == SIGINT);
	assert(info->si_signo == SIGINT);

	fprintf(stderr, "\n"
	                "--- SIGINT detected, process will now shut down ---\n"
	                "--- STACK BACKTRACE BEGIN ---\n");

	russ_dumpBacktrace(stderr);

	fprintf(stderr, "--- STACK BACKTRACE END ---\n"
	                "\n");


	printf("%s(): TODO: Add support for lock cleanup!\n", __func__);

	sc_killLocalProc();
}



void sc_killLocalProc()
{
	/* we lock this, but we'll never free it.  We hold it in shared mode
	 * right up to the death of the process.  If the background thread
	 * needs this lock, it will need to grab it in shared mode as well.
	 *
	 * Note that, as we go through the shutdown process, we will grab
	 * *EXCLUSIVE* locks on each mapped subcontext - but that is separate
	 * from this (local) lock on the list.
	 */
	russ_lock_shared(&sc_local_list__lock);


	/* iterate over every mapped subcontext.  Flush out any pending
	 * cleanupLog entries, and then remove ourselves from the process
	 * list.
	 */
	sc_local *curScLocal = sc_local_list;
	while (curScLocal != NULL)
	{
		printf("%s(): Performing cleanup.  local=%p shared=%p name='%s'\n", __func__, curScLocal, curScLocal->sc, curScLocal->sc->descriptive_name);

		russ_lock_exclusive(&curScLocal->sc->lock);

		/* If this process has not processed all of the entries in the
		 * cleanupLog, then pause and wait for our background thread
		 * to process them.
		 */
		while (curScLocal->local_cleanupLog_flushPoint != curScLocal->sc->cleanupLog_lastSeqNum)
		{
			assert(curScLocal->local_cleanupLog_flushPoint < curScLocal->sc->cleanupLog_lastSeqNum);

			printf("%s(): Sleeping to allow the background thread to wake up and process more cleanup log entries.  local_flushPoint=%lld root->lastSeqNum=%lld\n", __func__, curScLocal->local_cleanupLog_flushPoint, curScLocal->sc->cleanupLog_lastSeqNum);

			russ_unlock_exclusive(&curScLocal->sc->lock);
			  sched_yield();
			russ_lock_exclusive(&curScLocal->sc->lock);
		}

		/* find this process's entry in the process list */
		int myPid = getpid();

		curScLocal->sc->proc_count--;

		russ_lock   *prevLock = &curScLocal->sc->lock;
		sc_process **prevProc = &curScLocal->sc->proc_list;
		sc_process  * curProc =  curScLocal->sc->proc_list;

		while (curProc != NULL)
		{
			russ_lock_exclusive  (&curProc->lock);
			russ_unlock_exclusive( prevLock);

			if (curProc->pid == myPid)
			{
				*prevProc =  curProc->next;
				 prevLock = &curProc->lock;
				break;
			}

			prevLock = &curProc->lock;
			prevProc = &curProc->next;
			curProc  =  curProc->next;
		}

		russ_unlock_exclusive(prevLock);

		if (curProc == NULL)
			printf("WARNING: Could not find my process ID in the process list.\n");
		else
			sc_free(curScLocal->sc, curProc);

		/* advance to the next subcontext in the *LOCAL* list of
		 * mapped subcontexts.
	 	 */
		curScLocal = curScLocal->next;
	}


	exit(-1);
}



/* this function performs malloc(), but allocates memory from shared memory.
 * This is not a very smart algorithm, as I'm not a malloc() expert - but it
 * is sufficient for now.
 *
 * Allocated buffers are not put on any global list, so there is no way to
 * walk the list of all of the allocated buffers.  However, each allocation
 * is always large enough to contain at least a 'subcontexts_malloc_listNode'
 * struct; thus, when the user free()s the struct, we can re-use the buffer
 * as a member in the free list (this prevents deadlocks at free() time).
 *
 * It is perfectly valid for the free list to be empty - that simply means that
 * there are no unused buffers at this time.  A future malloc() will need to
 * first mmap() a page to get free memory.
 *
 * For small allocations, we allocate a single page at a time - however, for
 * big allocations we obviously must allocate several virtual pages at once.
 * Both types of allocations are documented by a special listNode struct -
 * they have nonzero values in the 'pages' field.  See the struct declaration
 * for more info.
 *
 * During malloc() we will use the first list node which we find which is
 * large enough for our allocation.  If there is sufficient space, then we'll
 * split the buffer into two pieces (allocating a new listNode buffer to
 * document what's left over).  If not, then we'll just remove it from the
 * list.
 *
 * Each allocated buffer has a header and a footer; each has a magic value to
 * detect overflow.  The header additionally has a field to indicate the length
 * of the allocation (including the header, and footer).  In cases where the
 * buffer we deliver is a little longer than the user's requested size (because
 * of alignment issues, or because a buffer is not quite large enough), the
 * length listed will be the *BUFFER* length, not the request length.  The
 * footer is always at the end of the *BUFFER* - and thus, there may be a few
 * dead bytes between the end of the user's perception of the buffer and the
 * footer.
 */
void *sc_malloc(sc *curSc, ssize_t size)
{
	/* is ssize_t an unsigned value?  I expect so, but this confirms it */
	assert(size >= 0);


	/* the user passes us a handle to the subcontext root control struct;
	 * we follow the 'malloc_md' field without using the lock.  This is
	 * because malloc has its own independent locking system, and we
	 * make a promise to never change the malloc_md pointer after
	 * subcontexts init.
	 */


	/* NOTE: the malloc metadata has its own locking system.  While the
	 *       malloc metadata is in shared memory and we get the pointer
	 *       from the root struct, we are allowed to save that pointer
	 *       away and then use it again (bypassing the root struct) from
	 *       then on.
	 */
	russ_lock_exclusive(&curSc->malloc_md->lock);


	/* the actual size we must allocate includes the header & footer, plus
	 * any alignment required for the footer.  We produce two versions of
	 * this value: one for the buffer itself, and one which includes space
	 * for a trailing listNode (so that we can split a large buffer).
	 */
	u64 alloc_size  = sizeof(sc_malloc_header) + size;
	    alloc_size += 7;     /* a moderately good compiler will turn... */
	    alloc_size /= 8;     /* ...these three lines...                 */
	    alloc_size *= 8;     /* ...into some simple bitwise ops         */
	    alloc_size += sizeof(sc_malloc_footer);
	  assert(alloc_size % 8 == 0);

	u64 split_size  = alloc_size + sizeof(sc_malloc_listNode);


	/* scan the free list for any element which is large enough for this
	 * allocation.  If it is large enough for splitting, then split it
	 * (paying attention to whether or not the field has 'pages' set); if
	 * it is not big enough to split but it *IS* big enough for the
	 * allocation (*AND* the node does not have 'pages' set), then we'll
	 * just use the struct without splitting it.
	 */
	sc_malloc_listNode  *cur   =  curSc->malloc_md->freeList;
	sc_malloc_listNode **pPrev = &curSc->malloc_md->freeList;

	while (cur != NULL)
	{
		/* if the current node is large enough to be split - or if it
		 * is large enough to contain a simple allocation *AND* it has
		 * pages==0, then our search is over!
		 */
		if (cur->len >= split_size ||
		    cur->len >= alloc_size && cur->pages == 0)
		{
			break;
		}


		/* move on to the next buffer */
		pPrev = &cur->next;
		cur   =  cur->next;
	}


	/* when we get here, we either have a buffer with enough space, or
	 * we fell off of the end of the list and we need to mmap() more space.
	 */
	if (cur == NULL)
	{
		/* we need to allocate a page.  We'll just place it on the list
		 * as if it was already there...and then fall down into the
		 * block-splitting code below.
		 *
		 * Note that we use 'split_size' as the baseline size, since we
		 * definitely will need to split this block!
		 */

		u64 page_size = getpagesize();

		u64 mmap_size  = split_size + page_size-1;
		    mmap_size /= page_size;
		    mmap_size *= page_size;


		/* attempt to mmap() new page()s */
		void *buf = sc_mmap(curSc,
		                    NULL, mmap_size,
		                    PROT_READ | PROT_WRITE,
		                    MAP_PRIVATE | MAP_ANONYMOUS,
		                    -1, 0);
		if (buf == MAP_FAILED)
		{
			russ_unlock_exclusive(&curSc->malloc_md->lock);
			return NULL;
		}


		/* fill out the listNode struct for the new allocation;
		 * add it to the front of the free list (since it's a new set
		 * of pages, it's a good candidate for future malloc()s), and
		 * then update the various pointers to pretend that we just
		 * found it there.
		 */
		cur = buf;

		cur->len   = mmap_size;
		cur->pages = mmap_size / page_size;

		pPrev      = &curSc->malloc_md->freeList;
		cur->next  =  curSc->malloc_md->freeList;
		curSc->malloc_md->freeList = cur;
	}


	/* when we get here, we must have valid 'cur' and 'pPrev' values,
	 * and 'cur' must be large enough for allocation
	 */
	assert( pPrev != NULL);
	assert(*pPrev == cur);
	assert(cur->len >= alloc_size);


	/* if pages is nonzero, then split the buffer just at the end of
	 * 'cur'.  Note that we could simplify the algorithm a little bit
	 * by allocating from the *TAIL* of the buffer instead of the front;
	 * however, it makes for prettier allocation patterns to allocate
	 * from the front.
	 */
	if (cur->pages != 0)
	{
		assert(cur->len >= split_size);

		sc_malloc_listNode *next = cur+1;

		next->len = cur->len - sizeof(*cur);
		cur ->len =            sizeof(*cur);

		next->next = cur->next;
		cur ->next = next;

		next->pages = 0;

		pPrev = &cur->next;
		cur   =  next;

		/* note that we fall through into the next check.  It is
		 * perfectly possible that we'll have to split the buffer a
		 * second time.
		 */
	}
	assert( pPrev != NULL);
	assert(*pPrev == cur);
	assert(cur->pages == 0);
	assert(cur->len   >= alloc_size);


	/* if the length of this buffer is long enough, then we split it.
	 * As noted just above, this might be the second split.  In a crazy
	 * corner case, this might even be the second split of newly-allocated
	 * pages!
	 */
	if (cur->len >= split_size)
	{
		sc_malloc_listNode *next = ((void*)cur) + alloc_size;

		next->len = cur->len - alloc_size;
		cur ->len =            alloc_size;

		next->next = cur->next;
		cur ->next = next;

		next->pages = 0;
	}
	assert( pPrev != NULL);
	assert(*pPrev == cur);
	assert(cur->pages == 0);
	assert(cur->len   >= alloc_size);
	assert(cur->len   <  split_size);


	/* if we get here, then 'cur' is perfect: it is *NOT* a 'pages' struct,
	 * it is at least long enough for the allocation, but not long enough
	 * to split.  It's time to just use this, and to remove it from the
	 * linked list.
	 *
	 * Remember that we have to round the alloc_size up to the buffer size,
	 * since the buffer might be a little larger.
	 */

	alloc_size = cur->len;
	  assert(alloc_size % 8 == 0);

	*pPrev = cur->next;

	sc_malloc_header *head =  (void*)cur;
	sc_malloc_footer *foot = ((void*)cur) + alloc_size;
	  foot--;
	void *retval = head+1;

	head->len   = alloc_size;
	head->magic = SUBCONTEXTS_MALLOC_HEAD_MAGIC;
	foot->magic = SUBCONTEXTS_MALLOC_FOOT_MAGIC;

	russ_unlock_exclusive(&curSc->malloc_md->lock);
	return retval;
}

void sc_free(sc *curSc, void *buf)
{
	assert(((u64)buf) % 8 == 0);


	/* the user passes us a handle to the subcontext root control struct;
	 * we follow the 'malloc_md' field without using the lock.  This is
	 * because malloc has its own independent locking system, and we
	 * make a promise to never change the malloc_md pointer after
	 * subcontexts init.
	 */


	/* check that the magic values are correct.  Note that we use the 'len'
	 * field in the header before we actually confirm that the magic is
	 * valid; however, we won't actually use the footer pointer which
	 * results until we've confirmed the header magic.
	 */
	sc_malloc_header *head = buf;
	  head--;
	sc_malloc_footer *foot = ((void*)head) + head->len;
	  foot--;

	if (head->magic != SUBCONTEXTS_MALLOC_HEAD_MAGIC ||
	    foot->magic != SUBCONTEXTS_MALLOC_FOOT_MAGIC)
	{
		printf("ERROR: Corrupted buffer detected in subcontexts_free(): Invalid magic values.  ");

		if (head->magic != SUBCONTEXTS_MALLOC_HEAD_MAGIC)
		{
			printf("head: expected=0x%08x actual=0x%08llx\n",
			       SUBCONTEXTS_MALLOC_HEAD_MAGIC, head->magic);
		}
		else
		{
			printf("foot: expected=0x%08x actual=0x%08llx\n",
			       SUBCONTEXTS_MALLOC_FOOT_MAGIC, foot->magic);
		}

		exit(3);
	}


	/* fill in the struct which will be put on the free list */

	u64 len = head->len;   // saving this off into a temporary is not
	                       // necessary in the current struct layout,
	                       // since 'len' is in the same place in both
	                       // the header and the list entry.  But it's more
	                       // correct and safer to do this.  The compiler
	                       // should eliminate the temporary.

	sc_malloc_listNode *listNode = (void*)head;
	listNode->len   = len;
	  // fill in listNode->next below
	listNode->pages = 0;


	/* return the buffer to the free list.
	 *
	 * TODO: sort the free list, then join buffers together and free pages.
	 */
	russ_lock_exclusive(&curSc->malloc_md->lock);
	  listNode->next = curSc->malloc_md->freeList;
	  curSc->malloc_md->freeList = listNode;
	russ_unlock_exclusive(&curSc->malloc_md->lock);

	return;
}



void *sc_mmap(sc *curSc,
              void *addr, size_t length, int prot, int flags,
              int fd, int offset)
{
	/* we only support certain limited modes so far! */
	assert(addr   == NULL);
	assert(length >  0);
	assert(length % getpagesize() == 0);
	assert(prot  == PROT_READ | PROT_WRITE || prot == PROT_READ);
	assert(flags == MAP_PRIVATE | MAP_ANONYMOUS);
	assert(fd == -1);
	assert(offset == 0);


	/* we don't need the local metadata for much - but we need it in order
	 * to access the open file (so that we can change the file size).
	 * Maybe we'll eventually sync all of the file IDs between the various
	 * processes, so that we can avoid use of local memory?
	 */
	sc_local *curScLocal = lookupScLocal(curSc);
	if (curScLocal == NULL)
	{
		fprintf(stderr, "%s(): ERROR: the 'sc' did not match any currently-mapped subcontext.\n", __func__);
		return MAP_FAILED;
	}


	russ_lock_exclusive(&curSc->lock);

	if (curSc->cleanupLog != NULL)
	{
TODO();   /* implement this */
	}


	/* try to extend the tmpfile before we do anything else.  If it fails,
	 * then abort immediately (maybe it will work later?).  If it doesn't
	 * fail but something else fails later, then we'll just live with
	 * having a hole in the file.  Hopefully the OS will store it
	 * sparsely.
	 */
	curSc->tmpfile_size += length;
	if (ftruncate(curScLocal->shm_file, curSc->tmpfile_size) != 0)
	{
		russ_unlock_exclusive(&curSc->lock);
		return MAP_FAILED;
	}


	/* get a region struct from the free list.  Note that this function
	 * may allocate additional pages (in order to create additional free
	 * list entries).
	 *
	 * If it returns NULL (meaning that we have run out of free structs
	 * and we can't allocate more, then return failure to the user!)
	 */
	sc_region *region = sc_getFreeRegion(curSc);
	if (region == NULL)
	{
		russ_unlock_exclusive(&curSc->lock);
		return MAP_FAILED;
	}


	/* pull pages off of the first reserved region in the list.
	 *
	 * TODO: Add code to repopulate this list (which is an async process,
	 *       and therefore we have to start it way ahead of time).
	 *
	 * TODO: Add code to scan through the reserved-regions list, looking
	 *       for a space which is big enough.
	 */
	assert(curSc->reserved_regions != NULL);
	assert(curSc->reserved_regions->len >= length);

	u64 retval = curSc->reserved_regions->vaddr;

	russ_lock_init(&region->lock);
	region->next           = NULL;
	region->vaddr          = retval;
	region->len            = length;
	region->prot           = prot;
	region->tmpfile_offset = curSc->tmpfile_size - length;
	region->permaPin       = false;

	curSc->reserved_regions->vaddr += length;
	curSc->reserved_regions->len   -= length;

	if (curSc->reserved_regions->len == 0)
	{
TODO();   // handle cleanup of the reserved region struct!
	}


	/* add the struct to the end of the active list */

	sc_region *oldTail = curSc->active_regionsTail;
	  assert(oldTail != NULL);

	russ_lock_exclusive(&oldTail->lock);
	  oldTail->next                        = region;
	  curSc->active_regionsTail = region;
	russ_unlock_exclusive(&oldTail->lock);


// TODO: add code to *JOIN* active regions together when possible.  But be
//       very careful about preserving allocation-order assumptions so that
//       we don't create deadlock loops!


	/* we're done */
	russ_unlock_exclusive(&curSc->lock);
	return (void*)retval;
}



static sc_region *sc_getFreeRegion(sc *curSc)
{
	/* caller must already hold the exclusive lock */
	assert(russ_isLock_heldExclusive_byMe(&curSc->lock));

	/* list can never be empty */
	assert(curSc->free_structs != NULL);


	/* if list has only one element, then allocate a new page so that we
	 * have lots more.  If this fails, then return NULL.
	 */
	if (curSc->free_structs->next == NULL)
	{
TODO();



#if 0
	/* the first thing that we need to do is to ensure that the
	 * free_structs list never empties.  So if there is only one
	 * element on that list, then we allocate a new page and use
	 * the one entry we have to document it; we then use the new
	 * page to create new list entries.
	 *
	 * Note that the elements on the free list will never be accessed
	 * except through this path (and munmap), and the entire list
	 * is protected by the root's lock.  We don't need to grab
	 * the locks for the individual structs...until we move them into
	 * one of the other lists.
	 */


		/* consume the first page from the first reserved region's
		 * area.
		 */
		struct subcontexts_region *rsvd1 = subcontexts_root->reserved_regions;
		  assert(rsvd1 != NULL);

		russ_lock_exclusive(&rsvd1->lock);

		u64 newPage = rsvd1->vaddr;
		rsvd1->vaddr += getpagesize();
		rsvd1->len   -= getpagesize();

		if (rsvd1->len == 0)
		{
			/* this reserved area has been completely consumed.
			 * We remove it from the reserved_regions list and
			 * add it to the free list.  This means, of course,
			 * that we now have *2* entries in that list, but we'll
			 * continue forward and still allocate a whole new
			 * page and use it for *even more* free list entries.
			 */
			subcontexts_root->reserved_regions = rsvd1->next;

			if (subcontexts_root->reserved_regions == NULL)
			{
TODO();   // we are probably hosed here.  We needed to preallocate some
          // more reserved memory *BEFORE* things got this tight!
          //
          // TODO: implement low-reserved-memory logic to prevent this, as well
          //       as mmap-failure logic while you are looking for more
          //       reserved memory.
			}

			/* reset rsvd1.  Note that we unlock the lock first.
			 * implementation (since unlock simply writes zero),
			 * but it's good practice for the future, when I might
			 * have locks which have metadata about blocking
			 * state and such.
			 */
			russ_unlock_exclusive(&rsvd1->lock);
			bzero(rsvd1, sizeof(*rsvd1));
			russ_lock_init(&rsvd1->lock);

			rsvd1->next = subcontexts_root->free_structs;
			subcontexts_root->free_structs = rsvd1;
		}


		/* pull the first entry off of the free list, and initialize
		 * it to represent a page at 'newAddr'
		 *
		 * Note that we never grab the lock on this region - we simply
		 * fill it in, and then later will post it to the tail end of
		 * the active list.
		 */
		subcontexts_region *newRegion = subcontexts_root->free_structs;
		subcontexts_root->free_structs = newRegion->next;

		russ_lock_init(&newRegion->lock);
		newRegion->vaddr          = newPage;
		newRegion->len            = getpagesize();
		newRegion->perm           = PROT_READ | PROT_WRITE;

		newRegion->tmpfile_offset       = subcontexts_root->tmpfile_size;
		subcontexts_root->tmpfile_size += getpagesize();

		if (ftruncate(subcontexts_shm_file, subcontexts_root->tmpfile_size) != 0)
		{
			perror("Could not expand the temporary file to store more pages");
			exit(3);
		}

		newRegion->permaPin = true;


		/* post that struct to the tail end of the active list */
		subcontexts_region *tail = subcontexts_root->active_regionsTail;
		  assert(tail != NULL);

		russ_lock_exclusive(&tail->lock);

		assert(tail->next == NULL);
		tail->next = newRegion;


		/* advance the tail pointer; then unlock everything */
		russ_unlock_exclusive(&tail->lock);

		subcontexts_root->active_regionsTail = newRegion;
		russ_unlock_exclusive(&subcontexts_root->lock);


		/* now, initialize the elements in the new page.  We have not
		 * yet chained them into the primary free list, but we want to
		 * initialize the page here - while we hold no locks - because
		 * the first touch of this page will of course cause a page
		 * fault and page in that new page we just added to the active
		 * list.
		 */
		subcontexts_region *newFree = (void*)newPage;
		u64 numNew = getpagesize() / sizeof(*newFree);

		u64 i;
		for (i=0; i<numNew-1; i++)
			newFree[i].next = &newFree[i+1];


		/* when we get here, the page is initialized (and has been
		 * faulted in), so now it's time to link it to any remaining
		 * bits of the free list.
		 */
		russ_lock_exclusive(&subcontexts_root->lock);

		newFree[numNew-1].next = subcontexts_root->free_structs;
		subcontexts_root->free_structs = newFree;

		/* leave the lock locked in exclusive mode, as the outer
		 * code expects!
		 */
#endif


	}
	assert(curSc->free_structs->next != NULL);


	sc_region *retval = curSc->free_structs;
	curSc->free_structs = retval->next;

	retval->next = NULL;
	return retval;
}


int sc_import(sc *mySc, void *addr, u64 len, int prot)
{
if (0)
	printf("%s(): mySc=%p : addr=%p len=0x%016llx prot=0x%x\n", __func__, mySc, addr,len,prot);

if (0)
{
	printf("PRE MAPS:\n");
	fflush(NULL);
	russ_dumpLocalMaps(NULL);
}


/* TODO: it's a shame that we have to have multiple copies of the
 *       same code pages in each process.  It's reasonable that
 *       we need separate (logical) copies of glibc and the subcontext
 *       library and everything else - but why not share the physical
 *       storage?  The current strategy - of copying the pages into
 *       the temp file - is simple but inefficient.  Ideally, I'd like
 *       to record, in the active_regions list, pointers to the library
 *       files themselves.  It would save I/O at sc_import() time, and
 *       likewise reduce the size of the backing file - as well as
 *       reduce the amount of physical memory we consume while running.
 *       (Oddly, though, other than saving pages in RAM, it actually
 *       wouldn't save any runtime CPU cost, since the various processes
 *       will all be sharing the same library code pages - whether those
 *       library code pages are backed by glibc or by our temporary file.
 */


	assert(((u64)addr) % getpagesize() == 0);
	assert(      len   % getpagesize() == 0);

	assert(prot ==  PROT_READ               ||
	       prot == (PROT_READ | PROT_WRITE) ||
	       prot == (PROT_READ | PROT_EXEC) );


	/* this algorithm is pretty hard to get right if there are multiple
	 * processes running.  Thankfully, though, this is generally something
	 * which would only run very early in init of a subcontext.  So I'll
	 * grab an exclusive lock and hold it forever - and also double-check
	 * the process count.   If there is more than 1 process, we'll just
	 * abort.
	 */
	russ_lock_exclusive(&mySc->lock);

	if (mySc->proc_count != 1)
	{
		fprintf(stderr, "%s(): The code is currently not written to handle a multi-process scenario.\n", __func__);

		russ_unlock_exclusive(&mySc->lock);
		return 1;
	}


	/* confirm that the virtual address - which is claimed to be part of
	 * the process, but not in the subcontext - is actually not yet part
	 * of the subcontext.  We'll 
	 */
	russ_lock_shared(&mySc->active_regions->lock);
	  sc_region *cur;
	  for (cur  = mySc->active_regions;
	       cur != NULL;
	       cur  = cur->next)
	  {
		if ( (u64)addr      < cur->vaddr+cur->len &&
		    ((u64)addr)+len > cur->vaddr)
		{
			russ_unlock_shared(&mySc->active_regions->lock);

			fprintf(stderr, "%s(): ERROR: Duplicate virtual address!  New range addr=0x%016llx len=0x%llx - old range addr=0x%016llx len=0x%llx\n", __func__, (u64)addr,len, cur->vaddr, cur->len);
			return 1;
		}
	  }
	russ_unlock_shared(&mySc->active_regions->lock);


	/* we know, since the map exists (or so the caller claims!), that we
	 * do *NOT* have an active or reserved region in the virtual space.
	 * So there's no point in searching for overlapping regions - we just
	 * allocate a new active region, fill it in, and post it!
	 *
	 * TODO: add debug sanity-checking
	 */
	sc_region *reg = sc_getFreeRegion(mySc);
	if (reg == NULL)
	{
		fprintf(stderr, "%s(): Could not allocate metadata struct to hold information about the region to import.\n", __func__);

		russ_unlock_exclusive(&mySc->lock);
		return 2;
	}

	russ_lock_init(&reg->lock);

	russ_lock_exclusive(&mySc->active_regions->lock);
	  mySc->active_regionsTail->next = reg;
	  mySc->active_regionsTail       = reg;
	  reg->next = NULL;
	russ_unlock_exclusive(&mySc->active_regions->lock);

	reg->vaddr = (u64)addr;
	reg->len   =      len;
	reg->prot  =      prot;
	reg->permaPin = false;

	reg->tmpfile_offset = mySc->tmpfile_size;
	mySc->tmpfile_size += len;


	/* we don't need the local metadata for much - but we need it in order
	 * to access the open file (so that we can change the file size).
	 * Maybe we'll eventually sync all of the file IDs between the various
	 * processes, so that we can avoid use of local memory?
	 */
	sc_local *myScLocal = lookupScLocal(mySc);
	if (myScLocal == NULL)
	{
		fprintf(stderr, "%s(): ERROR: the 'sc' did not match any currently-mapped subcontext.\n", __func__);
		return 3;
	}


	/* resize the temporary file */
	if (ftruncate(myScLocal->shm_file, mySc->tmpfile_size) != 0)
	{
		fprintf(stderr, "%s(): Could not resize the subcontext's backing file.\n", __func__);

		russ_unlock_exclusive(&mySc->lock);
		return 3;
	}


	/* copy the data from the original buffer to the temporary file; then
	 * mmap the temporary file to the original location.  Note that we
	 * could do the copy with seek/write, but I like mmap/memcpy/munmap
	 * better.
	 */
	void *buf = mmap(NULL, len,
	                 PROT_READ | PROT_WRITE, MAP_SHARED,
	                 myScLocal->shm_file, reg->tmpfile_offset);
	if (buf == MAP_FAILED)
	{
		perror("Could not mmap() the page in the temporary file for the preliminary copy");
		return 4;
	}
	memcpy(buf, addr, len);
	munmap(buf, len);

	void *replaced = mmap(addr, len,
	                      prot, MAP_SHARED | MAP_FIXED,
	                      myScLocal->shm_file, reg->tmpfile_offset);
	if (replaced != addr)
	{
		perror("Could not replace the original pages");
		return 5;
	}


	/* we're done! */
	russ_unlock_exclusive(&mySc->lock);


if (0)
{
	printf("%s(): mySc=%p : addr=%p len=0x%016llx prot=0x%x\n", __func__, mySc, addr,len,prot);
	printf("POST MAPS:\n");
	fflush(NULL);
	russ_dumpLocalMaps(NULL);
}

	return 0;
}

int sc_import_protNone(sc *mySc, void *addr, u64 len)
{
	/* see the comments in sc_import() above.  This is the same basic
	 * algorithm, except that it's simpler because it will just be
	 * mmap()ing PROT_NONE pages.
	 *
	 * Don't expect any other comments in this function.
	 */

	assert(((u64)addr) % getpagesize() == 0);
	assert(      len   % getpagesize() == 0);


	russ_lock_exclusive(&mySc->lock);

	if (mySc->proc_count != 1)
	{
		fprintf(stderr, "%s(): The code is currently not written to handle a multi-process scenario.\n", __func__);

		russ_unlock_exclusive(&mySc->lock);
		return 1;
	}


	sc_region *reg = sc_getFreeRegion(mySc);
	if (reg == NULL)
	{
		fprintf(stderr, "%s(): Could not allocate metadata struct to hold information about the region to import.\n", __func__);

		russ_unlock_exclusive(&mySc->lock);
		return 2;
	}

	russ_lock_init(&reg->lock);

	russ_lock_exclusive(&mySc->active_regions->lock);
	  reg->next = mySc->active_regions->next;
	  mySc->active_regions->next = reg;
	russ_unlock_exclusive(&mySc->active_regions->lock);

	reg->vaddr = (u64)addr;
	reg->len   =      len;
	reg->prot  =      PROT_NONE;
	reg->permaPin = false;
	reg->tmpfile_offset = -1;


	void *replaced = mmap(addr, len,
	                      PROT_NONE,
	                      MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
	                      -1, 0);
	if (replaced != addr)
	{
		perror("Could not replace the original pages");
		return 5;
	}


	/* we're done! */
	russ_unlock_exclusive(&mySc->lock);
	return 0;
}


static int scInit_setup_backgroundThread(sc_local *myScLocal)
{
	/* allocate some (private!) memory for this stack.  Note that it's a
	 * fairly small stack - but that should be OK, since I can be careful
	 * to control what goes onto it.
	 */
	u64 size = 1024*1024;

	void *newStack = mmap(NULL, size,
	                      PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE,
	                      -1, 0);
	if (newStack == MAP_FAILED)
	{
		perror("Could not mmap() the stack for the background thread");
		return 1;
	}

	/* make a dead page at the front end (stack grows down, from the back to the front) */
	void *dead = mmap(newStack, getpagesize(),
	                  PROT_NONE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE,
	                  -1, 0);
	if (dead != newStack)
	{
		perror("Could not mmap() the dead page at the end of the stack for the background thread");
		return 2;
	}


if (0)
{
	printf("%s(): Creating the background process with newStack=%p size=0x%llx (including a single dead page at the front)\n", __func__, newStack, size);
	fflush(NULL);
}


	myScLocal->backgroundThread_tid = 0;


	/* now create the thread */
	int rc;
	rc = clone(sc_backgroundThread, newStack+size,
	           SIGCHLD |
	           CLONE_THREAD |
	           CLONE_VM | CLONE_FILES | CLONE_FS | CLONE_IO | CLONE_SIGHAND |
	           CLONE_PTRACE |
	           CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID,
	           myScLocal,
	           NULL,NULL, &myScLocal->backgroundThread_tid);
	if (rc == -1)
	{
		perror("Could not clone() the background thread");
		return 3;
	}


	/* clone() above does *NOT* synchronously set the TID (despite the
	 * CLONE_CHILD_SETTID flag).  It appears to do it later, when the
	 * thread actually runs - perhaps libc puts a wrapper around my
	 * function, and sets the value in the wrapper???
	 *
	 * Anyhow, I have to wait for it to be set here, in order to make
	 * the operation synchronous.  Note that I don't expect that
	 * libc will do FUTEX_WAKEUP on this address (though I suppose it
	 * might) - so I'll do wakeup inside of my own code, just to make
	 * sure that this line doesn't block forever.
	 */
	assert(sizeof(pid_t) == sizeof(int));   // if this fails, then be smarter in the next line!
	russ_futex_untilNotEq((int*)&myScLocal->backgroundThread_tid, 0);


	/* there is a (harmless) race here: if the background thread dies
	 * extremely quickly, then (by the CLONE_CHILD_CLEARTID flag above),
	 * this field will be cleared.  But if the background thread dies
	 * so quickly, then it's probably *GOOD* that we fail this assert!
	 */
	assert(myScLocal->backgroundThread_tid == rc);

	return 0;
}


static int sc_backgroundThread(void *void_myScLocal)
{
	sc_local *myScLocal = void_myScLocal;

	u64 i;

if (0)
{
	printf("%s(): Background thread started with myScLocal=%p (stack at %p).\n", __func__, myScLocal, &myScLocal);
	fflush(NULL);
}

	/* see the comment above about the russ_futex_untilNotEq() call */
	russ_futex_wakeup((int*)&myScLocal->backgroundThread_tid, 1);

	/* this infinite loop simply watches for new entries to be posted
	 * to the cleanupLog - and when a new one is found, this code
	 * processes it.
	 */
	while (1)
	{
		russ_futex_untilNotEq64(&myScLocal->sc->cleanupLog_lastSeqNum,
		                         myScLocal->local_cleanupLog_flushPoint);

if (0)
{
  printf("%s(): after futex: lastSeqNum=%lld flushPoint=%lld\n", __func__, myScLocal->sc->cleanupLog_lastSeqNum, myScLocal->local_cleanupLog_flushPoint);
  fflush(NULL);
}


		/* BUGFIX: It is safe to use futex() to watch a memory
		 *         location without holding a lock, since the worst
		 *         that can happen (barring possibility of a SEGV)
		 *         is a spurious wakeup.  However, it is *NOT* safe
		 *         to actually draw any conclusions without the
		 *         lock - without the lock, we must worry that the
		 *         root struct might be in an inconsistent state,
		 *         or maybe just that we don't have the right barriers
		 *         to ensure memory consistency!
		 */


		/* we use shared locks on the root, and on all entries in this
		 * list, until we reach the one we need to access.  Then we'll
		 * use an exclusive lock.
		 *
		 * Ideally, I'd allow for more concurrency by using shared
		 * locks, but one of the things we'll do here is to increment
		 * ack_count - and that would need to be atomic; also, we'd
		 * need to be careful to *read* it atomically everywhere.
		 * Probably we can do that safely (maybe it's only read here?)
		 * but I'll leave the formal proofs for later.  For now, I'm
		 * using exclusive locks for safety.  Correctness first,
		 * optimization later.
		 */
		russ_lock_shared(&myScLocal->sc->lock);


		/* sometimes, 'lastSeqNum' and 'flushPoint' are both
		 * incremented, such as when we locally post a new cleanupLog
		 * entry.  That will cause a futex wakeup - but we have
		 * nothing to do.  Check for that special condition.
		 */
		if (myScLocal->sc->cleanupLog_lastSeqNum == myScLocal->local_cleanupLog_flushPoint)
		{
			russ_unlock_shared(&myScLocal->sc->lock);
			continue;
		}


		/* simple sanity checks! */
		assert(myScLocal->local_cleanupLog_flushPoint < myScLocal->sc->cleanupLog_lastSeqNum);
		assert(myScLocal->sc->cleanupLog != NULL);


		/* the common case is that the first entry in the cleanupLog
		 * is the one we need to look at - so we'll start with an
		 * exclusive lock here first.  But if our guess is wrong,
		 * then we'll downgrade the lock and move forward.
		 *
		 * The exclusive-then-downgrade strategy is often bad for
		 * concurrency, since later lockers (which also try exclusive
		 * first) will block.  But it works here, because we intend
		 * to release the lock momentarily anyhow - as soon as we've
		 * leapfrogged forward.
		 *
		 * We could, of course, do a try-exclusive here (since a
		 * shared lock is sufficient to inspect the seqNum), but that
		 * seems excessive, at least for now.  Again, we can always
		 * improve the performance later.
		 */
		sc_cleanupLogStep *curStep = myScLocal->sc->cleanupLog;
		russ_lock_exclusive(&curStep->lock);
		russ_unlock_shared(&myScLocal->sc->lock);


		/* save this for later - if we process the first entry in the
		 * list, and if we also are the last process to do so, then
		 * we will do list cleanup at the end.
		 */
		sc_cleanupLogStep *firstStep = curStep;


		/* in the common case, curStep is what we wanted.  But
		 * sometimes it won't be - in which case, we'll need to
		 * leapfrog through the list.
		 */
		assert(curStep->seqNum <= myScLocal->local_cleanupLog_flushPoint+1);
		if (curStep->seqNum < myScLocal->local_cleanupLog_flushPoint+1)
		{
			russ_downgrade_lock(&curStep->lock);

			/* 'hops' is the number of *SHARED* lock hops we'll
			 * do - it could be zero.  Our last hop will be an
			 * exclusive lock hop, on the step we need to process.
			 */
			u64 hops = myScLocal->local_cleanupLog_flushPoint - curStep->seqNum;
			sc_cleanupLogStep *next;
			for (i=0; i<hops; i++)
			{
				next = curStep->next;
				  assert(next != NULL);
				  assert(next->seqNum == curStep->seqNum+1);

				russ_lock_shared(&next->lock);
				russ_unlock_shared(&curStep->lock);

				curStep = next;
			}

			assert(curStep->seqNum == myScLocal->local_cleanupLog_flushPoint);

			next = curStep->next;
			  assert(next != NULL);
			  assert(next->seqNum == curStep->seqNum+1);

			russ_lock_exclusive(&next->lock);
			russ_unlock_shared(&curStep->lock);

			curStep = next;
		}
		assert(russ_isLock_heldExclusive_byMe(&curStep->lock));
		assert(curStep->seqNum == myScLocal->local_cleanupLog_flushPoint+1);


		/* we've found the first new entry in the cleanupLog which we
		 * need to process.  (There might be many.)
		 *
		 * For clarity, we've moved the per-entry dispatch process into
		 * a separate function.
		 */

		bool needListCleanup = false;
		while (1)
		{
			printf("%s(): Handling cleanupLog entry seqNum=%lld op=%d\n", __func__, curStep->seqNum, curStep->op);

			sc_handleCleanupLogStep(myScLocal, curStep);


			/* ACK this operation.  If we are the last process to
			 * do so, *AND* this is the first step in the list,
			 * then set a reminder to do list cleanup when we're
			 * done processing the operations in the list.
			 */
			assert(curStep->ack_count < curStep->ack_targ);
			curStep->ack_count++;
			if (curStep == firstStep &&
			    curStep->ack_count == curStep->ack_targ)
			{
				needListCleanup = true;
			}


			/* record that we've flushed the op */
			assert(curStep->seqNum == myScLocal->local_cleanupLog_flushPoint+1);
			myScLocal->local_cleanupLog_flushPoint++;


			/* advance to the next operation in the list, if there
			 * is one.  Otherwise, we're done!
			 */
			if (curStep->next == NULL)
			{
				russ_unlock_exclusive(&curStep->lock);
				break;
			}

			sc_cleanupLogStep *next = curStep->next;

			russ_lock_exclusive(&next->lock);
			russ_unlock_exclusive(&curStep->lock);

			curStep = next;
		}


		/* do list cleanup, if required */
		if (needListCleanup)
		{
			russ_lock_exclusive(&myScLocal->sc->lock);

			/* it is impossible for two different threads to
			 * both be doing cleanup at the same time: a thread
			 * only has this set if, on the very first element
			 * in the list, it was the last process to complete.
			 * No other thread can have this flag set until
			 * *AFTER* this thread clears (at least) that first
			 * element from the list.  And since it will be
			 * leapfrogging down the list with exclusive locks,
			 * this thread is a barrier dividing all other
			 * threads: each other thread is either entirely
			 * *BEFORE* or entirely *AFTER* this cleanup scan.
			 *
			 * Note that his ordering only applies to this scan,
			 * not to all of the activity by this process; for
			 * instance, thread X might run *AFTER* this thread
			 * in the flush-operations scan, and yet run *BEFORE*
			 * the cleanup scan.  In that case, X might be the
			 * last thread to complete *SOME* operations - but
			 * those operations would *NOT* be the first in the
			 * list, and thus there will be no race for the
			 * needListCleanup flag.
			 */

			assert(myScLocal->sc->cleanupLog      != NULL);
			assert(myScLocal->sc->cleanupLog_tail != NULL);

			sc_cleanupLogStep *curStep = myScLocal->sc->cleanupLog;
			russ_lock_exclusive(&curStep->lock);
			  assert(curStep->ack_count == curStep->ack_targ);

			/* NOTE: We do *NOT* release the lock on the root
			 *       control struct, since we're going to update
			 *       it as we clean up.
			 */

			while (1)
			{
				sc_cleanupLogStep *first = myScLocal->sc->cleanupLog;
				sc_cleanupLogStep *next  = first->next;

				printf("%s(): Freeing cleanupLog entry seqNum=%lld op=%d\n", __func__, first->seqNum, first->op);


				/* some entries have additional actions which
				 * must be performed when all of the ACKs have
				 * arrived.
				 */
				switch (first->op)
				{
				case SC_CLS_RESERVE_MEMORY:
				{
					sc_region *pending = myScLocal->sc->pending_reservation;
					  myScLocal->sc->pending_reservation = NULL;

					assert(pending->vaddr == first->reserve_memory.vaddr);

					/* did we succeed in reserving this memory? */
					if (pending->len != 0)
					{
						assert(pending->len == first->reserve_memory.len);

						printf("%s(): New reservation is now available.  start=0x%016llx len=0x%016llx\n", __func__, pending->vaddr, pending->len);

						pending->next = myScLocal->sc->reserved_regions;
						myScLocal->sc->reserved_regions = pending;
					}
					else
					{
						/* we failed! */
						TODO();
					}
				}
					break;

				case SC_CLS_RESERVE_MEMORY_ABORT:
					break;   // NOP

				default:
					assert(false);   // unrecognized op
				}


				if (next == NULL)
				{
					/* we've flushed the entire list! */
					myScLocal->sc->cleanupLog      = NULL;
					myScLocal->sc->cleanupLog_tail = NULL;

					russ_unlock_exclusive(&curStep->lock);
					sc_free(myScLocal->sc, curStep);

					break;
				}

				/* if we get here, then we have a non-NULL
				 * 'next' pointer.  But we haven't locked it,
				 * and we don't know yet if that entry has
				 * completed.
				 *
				 * First, we'll free the 'curStep' and we'll
				 * update the root pointer.  After that, we'll
				 * check to see if we want to continue, or to
				 * be done.
				 */
				russ_lock_exclusive(&next->lock);

				myScLocal->sc->cleanupLog = next;

				russ_unlock_exclusive(&curStep->lock);
				sc_free(myScLocal->sc, curStep);

				curStep = next;

				assert(curStep->ack_count <= curStep->ack_targ);
				if (curStep->ack_count < curStep->ack_targ)
				{
					russ_unlock_exclusive(&curStep->lock);
					break;
				}
			}

			russ_unlock_exclusive(&myScLocal->sc->lock);
		}


		/* go back and do the futex wait until the lastLogSeqNum
		 * changes again.
		 */
	}

	assert(false);   // we never get here
}

static void sc_handleCleanupLogStep(sc_local *myScLocal, sc_cleanupLogStep *step)
{
	switch (step->op)
	{
	case SC_CLS_RESERVE_MEMORY:
		/* if the length has been set to zero, then the operation
		 * has been aborted - do nothing!
		 */
		if (step->reserve_memory.len == 0)
			return;

		
		{
			/* do *NOT* use FIXED here, since we want to detect
			 * conflicts without causing harm!
			 */
			void *map = mmap((void*)step->reserve_memory.vaddr, step->reserve_memory.len,
			                 PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS,
			                 -1, 0);

			if (map == (void*)step->reserve_memory.vaddr)
			{
				// success!  Record it, so that we know to
				// clean up later if necessary.

				myScLocal->recentReserve.vaddr = step->reserve_memory.vaddr;
				myScLocal->recentReserve.len   = step->reserve_memory.len;

				return;
			}

			if (map == MAP_FAILED)
				perror("Could not mmap() the region for a new reservation");
			else
				munmap(map, step->reserve_memory.len);

TODO();
		}
		break;

	case SC_CLS_RESERVE_MEMORY_ABORT:
		TODO();
		break;

	default:
		printf("%s(): Invalid op=%d in cleanupLog entry seqNum=%lld\n", __func__, step->op, step->seqNum);
		russ_unlock_shared(&step->lock);
		sc_killLocalProc();
	}

	return;
}



int sc_postNewReservation(sc_local *myScLocal, u64 pages, u64 *seqNum_out)
{
	assert(russ_isLock_heldExclusive_byMe(&myScLocal->sc->lock));
	assert(myScLocal->sc->pending_reservation == NULL);

	assert(pages > 0);


assert(myScLocal->sc->proc_count > 1);   // handle the (much simpler) case of a single running process - no cleanupLog entry is required!


	/* if our local process has not processed all of the pending
	 * cleanupLog entries, then flush them now.
	 *
	 * TODO: might it be better to abort rather than try to flush
	 *       here?  Won't we want to release the root control
	 *       lock if we're not careful?
	 */
	if (myScLocal->local_cleanupLog_flushPoint != myScLocal->sc->cleanupLog_lastSeqNum)
	{
TODO();
	}


	/* allocate an sc_region, which we'll eveentually hang at
	 * 'pending_reservation'
	 */
	sc_region *newReg = sc_getFreeRegion(myScLocal->sc);
	if (newReg == NULL)
		return 1;


	/* allocate a new cleanupLog entry, which we'll eventually hang
	 * at the end of the list.
	 */
	sc_cleanupLogStep *newStep = sc_malloc(myScLocal->sc, sizeof(*newStep));
	if (newStep == NULL)
		return 2;


	/* reserve a region in the local process's space */
	void *range = mmap(NULL, pages * getpagesize(),
	                   PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS,
	                   -1, 0);
	if (range == MAP_FAILED)
	{
		sc_free(myScLocal->sc, newStep);

TODO();   // restore 'newReg' to the pool

		return 3;
	}


	/* fill out the sc_region struct.  Then hang it on the root struct. */
	russ_lock_init(&newReg->lock);
	newReg->next  = NULL;
	newReg->vaddr = (u64)range;
	newReg->len   = pages * getpagesize();
	newReg->prot  = PROT_NONE;
	newReg->permaPin = false;
	newReg->tmpfile_offset = -1;

	myScLocal->sc->pending_reservation = newReg;


	/* fill out the cleanupLog step, then add it to the list */
	russ_lock_init(&newStep->lock);

	myScLocal->sc->cleanupLog_lastSeqNum++;
	newStep->seqNum = myScLocal->sc->cleanupLog_lastSeqNum;

	newStep->next      = NULL;
	newStep->op        = SC_CLS_RESERVE_MEMORY;
	newStep->ack_targ  = myScLocal->sc->proc_count;
	newStep->ack_count = 1;   // represents just the local process!

	newStep->reserve_memory.vaddr = newReg->vaddr;
	newStep->reserve_memory.len   = newReg->len;

	if (myScLocal->sc->cleanupLog == NULL)
	{
		assert(myScLocal->sc->cleanupLog_tail == NULL);

		myScLocal->sc->cleanupLog      = newStep;
		myScLocal->sc->cleanupLog_tail = newStep;
	}
	else
	{
		assert(myScLocal->sc->cleanupLog_tail != NULL);

		/* we can't alter the 'next' pointer in the tail unless we
		 * hold the lock.  Remember that any other process (or even
		 * the local background thread) can be walking the list even
		 * while we hold the exclusive lock on the root.
		 */
		sc_cleanupLogStep *oldTail = myScLocal->sc->cleanupLog_tail;

		russ_lock_exclusive(&oldTail->lock);
		  oldTail->next = newStep;
		russ_unlock_exclusive(&oldTail->lock);

		myScLocal->sc->cleanupLog_tail = newStep;
	}


	/* we enforced, at the head of this function, that the cleanupLog had
	 * been fully flushed (locally).  So, since we already have an mmap()
	 * for this reservation, we need to mark this new step as flushed
	 * locally as well!
	 *
	 * (If this ever becomes hard to enforce in the future, we could
	 * munmap() the region and then allow the normal reservation-handling
	 * to map it a second time.  But this works for now, and hopefully
	 * for all time.)
	 */
	assert(myScLocal->sc->cleanupLog_lastSeqNum == newStep->seqNum);
	assert(newStep->seqNum == myScLocal->local_cleanupLog_flushPoint+1);
	myScLocal->local_cleanupLog_flushPoint++;


	/* the background threads all use futex to wait on this field - I
	 * need to make sure that they notice the change.
	 *
	 * Why do we do this here, rather than earlier, when we actually
	 * make the change?  Because there's no point in waking up those
	 * other threads while we still hold the lock - which we do throughout
	 * this function.  I have no idea how long our caller will hold this
	 * lock - but inside this function, I can delay the wakeup as much
	 * as possible.
	 *
	 * How many threads shall we wake up?  I don't have any way to make
	 * a perfect recommendation, since we might have arbitrarily many
	 * processes.  A rough guess is that 2x the number of processes should
	 * be more than enough - remember that only the background threads
	 * (not the primary threads) are waiting for this to change.
	 */
	russ_futex_wakeup64((u64*)&myScLocal->sc->cleanupLog_lastSeqNum,
	                    2 * myScLocal->sc->proc_count);


	/* if the user wanted the seqNum, then deliver it.  then
	 * we're done.
	 */
	if (seqNum_out != NULL)
		*seqNum_out = newStep->seqNum;

	return 0;
}



sc_registry_funcs *__subcontexts_registry_bootstrap(struct sc_local *local)
{
	/* only map the registry once in the lifetime of each process */
	static sc_registry_funcs *retval = NULL;
	if (retval != NULL)
		return retval;


	/* special case (only in the registry init process): the registry
	 * is already mapped, and is the first in the list.
	 */
	if (sc_local_list                       != NULL &&
	    sc_local_list->sc->descriptive_name != NULL &&
	    strcmp(sc_local_list->sc->descriptive_name, "subcontexts registry") == 0)
	{
		retval = sc_local_list->sc->user_root;
		return retval;
	}


	/* normal case: we must map the registry in */
	FILE *regBackingFile = fopen("/tmp/subcontexts_registry.sc", "r+");
	if (regBackingFile == NULL)
	{
		perror("Could not open the backing file for the registry");
		return NULL;
	}

	sc *regSc = sc_join(fileno(regBackingFile));
	  fclose(regBackingFile);

	if (regSc == NULL)
	{
		fprintf(stderr, "Could not join the registry subcontext\n");
		return NULL;
	}

	assert(strcmp(regSc->descriptive_name, "subcontexts registry") == 0);


/* TODO: automatically sc_join() to the subcontext which serves this symbol */


	retval = regSc->user_root;
	return retval;
}


sc_local *lookupScLocal(sc *searchFor)
{
	sc_local *retval;

if (0) printf("%s(): sc=%p\n", __func__, searchFor);

	russ_lock_shared(&sc_local_list__lock);
	  retval = sc_local_list;
	  while (retval != NULL)
	  {
if (0) printf("  cur=%p cur->sc=%p='%s'\n", retval, retval->sc, retval->sc->descriptive_name);

		if (retval->sc == searchFor)
			break;

		retval = retval->next;
	  }
	russ_unlock_shared(&sc_local_list__lock);

if (0) printf("  retval=%p\n", retval);

	return retval;
}


sc *getCurSc(void)
{
	return curSc;
}


