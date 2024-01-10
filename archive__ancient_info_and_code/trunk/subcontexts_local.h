
#ifndef _SUBCONTEXTS_LOCAL_H__INCLUDED_
#define _SUBCONTEXTS_LOCAL_H__INCLUDED_


#include "subcontexts.h"
#include "subcontexts_control.h"

#include "russ_common_code/c/russ_int_typedefs_linux.h"
#include "russ_common_code/c/russ_locks.h"



/* this struct is always store in *LOCAL* process memory - it is part of
 * the subcontexts core library, but that library is in the local memory
 * of a running process.  (Note that when we create a new subcontxt with
 * the 'subcontexts_init' tool - which calls sc_import() - we will import
 * all of the process's local memory - except for its heap and stack - and
 * thus the "local" memory of that process will reside in the shared memory
 * of the subcontext.  But I'm hoping to fix that later - hopefully, we'll
 * be able to change the algo to be smarter.)
 *
 * 
 */

/* the primary thread (the user thread) and the background thread
 * need to be able to serialize access to this metadata.  But since
 * we only need to have two threads interacting and one will be idle
 * 99% of the time, we can get away with using a single lock.
 *
 * Remember, this is a static lock - so it's shared across all of the
 * copies of this struct.  But that only includes copies which are
 * known to this library - thus only the *LOCAL* structs.  Every
 * process has its own copy of this "static" variable.
 */
extern russ_lock sc_local_list__lock;   /* defined in subcontexts_core.c */
typedef struct sc_local
{
	/* linked list of subcontexts */
	struct sc_local *next;


	/* these are the local-process "global variables" */
	struct sc                 *sc;
	int                        shm_file;
	u64                        local_cleanupLog_flushPoint;
	pid_t                      backgroundThread_tid;

	/* see the long discussion of SC_CLS_RESERVE_MEMORY in subcontexts_control.h */
	struct { u64 vaddr,len; } recentReserve;
} sc_local;

extern struct sc_local *sc_local_list;   /* defined in subcontexts_core.c */



#endif


