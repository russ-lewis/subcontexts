
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
#include "subcontexts_control.h"

#include "russ_common_code/c/russ_todo.h"
#include "russ_common_code/c/russ_int_typedefs_linux.h"
#include "russ_common_code/c/russ_locks.h"


int main(int argc, char **argv)
{
	int rc;
	int i;

	sc *mySc;


	if (argc != 3)
	{
		fprintf(stderr, "SYNTAX: %s [pid] [fileno]\n", argv[0]);
		return 1;
	}


	/* first, open the tmpfile.  Note that there is no entry in
	 * the filesystem for this file, since it's a tmpfile - but
	 * we can access it through the /proc filesystem for any file
	 * in the group.
	 *
	 * It's very strange to use atoi() here, and then immediately
	 * convert back to decimal...but it's a trivial way to ensure
	 * that a malicious input doesn't try to do anything nasty with
	 * our filename here.
	 */
	char  tmp_name[256];
	sprintf(tmp_name, "/proc/%d/fd/%d", atoi(argv[1]), atoi(argv[2]));
	
	FILE *tmp = fopen(tmp_name, "r+");
	if (tmp == NULL)
	{
		perror("Could not open the tmpfile to join the group");
		return 2;
	}

	mySc = sc_join(fileno(tmp));


	/* did we succeed in initialization?  If not, then the called function
	 * should have already printed an error to stderr.
	 */
	if (mySc == NULL)
		return 2;


	printf("ROOT CONTROL STRUCT (%p):\n", mySc);
	printf("-------------------\n");
	printf("  magic=0x%08x (ROOT_MAGIC=0x%08x)\n", mySc->magic, SUBCONTEXTS_ROOT_MAGIC);
	printf("  lock=0x%08x\n", mySc->lock.val);
	printf("  active_regions=%p\n", mySc->active_regions);
	printf("  active_regionsTail=%p\n", mySc->active_regionsTail);
	printf("  reserved_regions=%p\n", mySc->reserved_regions);
	printf("  unmapped_regions=%p\n", mySc->unmapped_regions);
	printf("  free_structs=%p\n", mySc->free_structs);
	printf("  tmpfile_size=0x016llx\n", mySc->tmpfile_size);
	printf("  proc_count=%d\n", mySc->proc_count);
	printf("  proc_list=%p\n", mySc->proc_list);
	printf("  proc_list__deadFlagPending=%d\n", mySc->proc_list__deadFlagPending);
	printf("  prealloc_proc=%p\n", mySc->prealloc_proc);
	printf("  cleanupLog_lastSeqNum=%lld\n", mySc->cleanupLog_lastSeqNum);
	printf("  cleanupLog=%p\n", mySc->cleanupLog);
	printf("  cleanupLog_tail=%p\n", mySc->cleanupLog_tail);
	printf("  malloc_metadata=%p\n", mySc->malloc_md);
	printf("  user_mySc=%p\n", mySc->user_root);
	printf("\n");

	sc_region *curRegion;
	for (curRegion  = mySc->active_regions;
	     curRegion != NULL;
	     curRegion  = curRegion->next)
	{
		printf("ACTIVE REGION (%p):\n", curRegion);
		printf("-------------\n");
		printf("  lock=0x%08x\n", curRegion->lock.val);
		printf("  next=%p\n", curRegion->next);
		printf("  vaddr=0x%016llx\n", curRegion->vaddr);
		printf("  len=0x%016llx\n", curRegion->len);
		printf("  prot=0x%x (READ=0x%x WRITE=0x%x)\n", curRegion->prot, PROT_READ,PROT_WRITE);
		printf("  permaPin=%d\n", curRegion->permaPin);
		printf("  tmpfile_offset=0x%016llx\n", curRegion->tmpfile_offset);
		printf("\n");
	}

	for (curRegion  = mySc->reserved_regions;
	     curRegion != NULL;
	     curRegion  = curRegion->next)
	{
		printf("RESERVED REGION (%p):\n", curRegion);
		printf("---------------\n");
		printf("  lock=0x%08x\n", curRegion->lock.val);
		printf("  next=%p\n", curRegion->next);
		printf("  vaddr=0x%016llx\n", curRegion->vaddr);
		printf("  len=0x%016llx\n", curRegion->len);
		printf("  prot=0x%x (READ=0x%x WRITE=0x%x)\n", curRegion->prot, PROT_READ,PROT_WRITE);
		printf("  permaPin=%d\n", curRegion->permaPin);
		printf("  tmpfile_offset=0x%016llx\n", curRegion->tmpfile_offset);
		printf("\n");
	}

	for (curRegion  = mySc->unmapped_regions;
	     curRegion != NULL;
	     curRegion  = curRegion->next)
	{
		printf("UNMAPPED REGION (%p):\n", curRegion);
		printf("---------------\n");
		printf("  lock=0x%08x\n", curRegion->lock.val);
		printf("  next=%p\n", curRegion->next);
		printf("  vaddr=0x%016llx\n", curRegion->vaddr);
		printf("  len=0x%016llx\n", curRegion->len);
		printf("  prot=0x%x (READ=0x%x WRITE=0x%x)\n", curRegion->prot, PROT_READ,PROT_WRITE);
		printf("  permaPin=%d\n", curRegion->permaPin);
		printf("  tmpfile_offset=0x%016llx\n", curRegion->tmpfile_offset);
		printf("\n");
	}

	for (curRegion  = mySc->free_structs;
	     curRegion != NULL;
	     curRegion  = curRegion->next)
	{
		printf("FREE STRUCT (%p):\n", curRegion);
		printf("-----------\n");
		printf("  lock=0x%08x\n", curRegion->lock.val);
		printf("  next=%p\n", curRegion->next);
		printf("  vaddr=0x%016llx\n", curRegion->vaddr);
		printf("  len=0x%016llx\n", curRegion->len);
		printf("  prot=0x%x (READ=0x%x WRITE=0x%x)\n", curRegion->prot, PROT_READ,PROT_WRITE);
		printf("  permaPin=%d\n", curRegion->permaPin);
		printf("  tmpfile_offset=0x%016llx\n", curRegion->tmpfile_offset);
		printf("\n");
	}

	return 0;
}


