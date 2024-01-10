
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
#include "russ_common_code/c/russ_futexwrappers.h"



int main(int argc, char **argv)
{
	int rc;
	int i;

	u64 cleanupLog_seqNum;

	sc *mySc;


	if (argc >= 3)
	{
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
	}


	/* figure out the local struct for this (shared) sc struct */
	struct sc_local *myScLocal = lookupScLocal(mySc);
	if (myScLocal == NULL)
	{
		fprintf(stderr, "%s: Could not map the sc to the sc_local.\n", __func__);
		return 2;
	}


	/* what command is running??? */
	if (argc == 5 && strcmp(argv[3], "--addReserved") == 0)
	{
		int pages = atoi(argv[4]);
		  assert(pages > 0);

		printf("%s: Posting a request to reserve %d new pages.\n", argv[0], pages);

		russ_lock_exclusive(&mySc->lock);

		while (mySc->pending_reservation != NULL)
		{
			printf("%s: pending_reservation is already NULL...spinning until it changes.  pending_reservation=%p={ vaddr=0x%016llx len=0x%016llx }\n", argv[0], mySc->pending_reservation, mySc->pending_reservation->vaddr, mySc->pending_reservation->len);

			sc_region *pending = mySc->pending_reservation;

			russ_unlock_exclusive(&mySc->lock);
			  russ_futex_untilNotEq64((u64*)&mySc->pending_reservation, (u64)pending);
			russ_lock_exclusive(&mySc->lock);
		}

		int rc = sc_postNewReservation(myScLocal, pages, &cleanupLog_seqNum);
		if (rc != 0)
			fprintf(stderr, "%s: Could not create a new reservation!\n", argv[0]);

		russ_unlock_exclusive(&mySc->lock);
	}

	else
	{
		fprintf(stderr, "SYNTAX: %s [pid] [fileno] (--addReserved [pages])\n", argv[0]);
		return 1;
	}


TODO();   // wait for the entry 'cleanupLog_seqNum' to flush

	return 0;
}


