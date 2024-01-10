
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

#include "subcontexts.h"

#include "russ_common_code/c/russ_todo.h"
#include "russ_common_code/c/russ_int_typedefs_linux.h"
#include "russ_common_code/c/russ_locks.h"



// TODO: remove this
#include "subcontexts_control.h"



int subcontext_main(sc *mySc, int argc, char **argv)
{
	int rc;
	int i;


#if 0
TODO();   // cool demo ideas:
          //   - IPC using linked lists and futex-wakeup.  Private memory in
          //     one process talks (indirectly) to private memory in another.
          //   - process that loads into memory and posts its *ENTIRE MAP*
          //     into subcontexts memory - effective a subroutine load.
          //     process can then die, after registering a callback into this
          //     code
          //   - thread emulation using fork - rewrite "subcontexts_demo" as
          //     "subcontexts_join", a general-purpose library

TODO();   // make a subcontexts-loader app (where processes to run under
          // subcontexts would be shared libraries - pass the name of the
          // library as a command-line option.  command might be
          // "subcontext_init"
          //
          // Thinking about symbols: we can use dlopen() to load a shared
          // library (with RTLD_LOCAL) to map in the tool - and then we'll
          // scan /proc/self/maps to find what pages have been mapped - and
          // import all the new pages (compare to the old map) into the
          // subcontext.

TODO();   // rename everything from "subcontexts" to "sc"

TODO();   // make the active_regions a hash table instead of a linked list,
          // but we have to first prove that we can avoid deadlock due to
          // recursive faults

TODO();   // next thing to test: cleanupLog/broadcast.  Use mmap(FIXED, RO)
          // as a testcase - we have to synchronously clear the writable flag
          // in all processes.
          //
          // Then test reserve-region-extension as another - which uses async
          // operations.

TODO();   // long-term direction: move subcontexts vars into a struct, to
          // support multiple subcons in a process.  Include dynamic mapping of
          // subcons into each others' processes.  Need a private-memory table,
          // in each process, of loaded subcons in order to prevent circular
          // imports.

TODO();   // eventually: support dynamic page permissions as we call between
          // subcons

TODO();   // eventually: support rapid context switching by having multiple
          // copies of each process - simply communicate stack pointer (and
          // probably a few other registers) across IPC and the "wakeup"
#endif



	printf("Entering the infinite spin loop...\n");
	while (1)
	{
		sleep(2);

		printf("\n");

		printf("Locking the struct...");
		russ_lock_shared(&mySc->lock);
		printf("ok, lock.val=0x%08x\n", mySc->lock.val);

		printf("  magic=0x%08x : tmpfile_size=%lld proc_count=%d cleanupLog_lastSeqNum=%lld\n",
		       mySc->magic,
		       mySc->tmpfile_size,
		       mySc->proc_count,
		       mySc->cleanupLog_lastSeqNum);

		printf("  Unlocking the struct...");
		russ_unlock_shared(&mySc->lock);
		printf("ok, lock.val=0x%08x\n", mySc->lock.val);

		if (mySc->lock.val != 0)
		{
			printf("    -- WARNING -- Nonzero lock value!\n");
		}
	}


	return 0;
}


