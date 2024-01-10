
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
#include <dlfcn.h>

/* we *MUST* include these files *BEFORE* any of the "russ" functions because
 * they provide overrides which are critical to make a subcontext work
 * properly.  If you don't have these, then the wrappers will do the wrong
 * thing, and break subcons.
 */
#include "subcontexts.h"
#include "subcontexts_control.h"

#include "russ_common_code/c/russ_todo.h"
#include "russ_common_code/c/russ_int_typedefs_linux.h"
#include "russ_common_code/c/russ_locks.h"
#include "russ_common_code/c/russ_flushfile.h"
#include "russ_common_code/c/russ_dumplocalmaps.h"
#include "russ_common_code/c/russ_dumplocalfilelist.h"
#include "russ_common_code/c/russ_parseprocmaps.h"



struct mapScanCallback_priv
{
	sc *targSc;

	bool rw_p__only;   // true  = only import rw-p ranges (second pass)

	struct stat ignoreFile1, ignoreFile2;

	int importCount;

	/* this is set by the callback if a terminal error occurs; main()
	 * must check it and die if it is set.
	 */
	bool kill;
};
static bool mapScanCallback(void *priv_void,
                            u64 start, u64 end,
                            bool isR, bool isW, bool isX, bool isP,
                            u64 fileOffset,
                            u16 devno, u64 inode,
                            char *desc);



int main(int argc, char **argv)
{
	int rc;
	int i;

	sc *mySc;


	/* file buffering does *BAD* things to our processes, because it
	 * fundamenally is a link between memory (where the buffer is) and
	 * process-state (where the data gets printed).  Code which uses
	 * stdin, stdout, or stderr get these variables (or macros) from
	 * the standard C library - which, as we know, is a subcontext-private
	 * entity.  So, in a given process, there are *MULTIPLE* of those
	 * variables - one representing stdin,stdout,stderr for *EACH INIT
	 * PROCESS*.  Therefore, if you call into a subcontext function and
	 * then it attempts to use any of these three streams (including,
	 * critically, indirectly through TODO() and the like), then you will
	 * be using the *SUBCONTEXT* version of the stream (with its buffer in
	 * private mmap()ed memory or heap) instead of the *PROCESS*'s version.
	 *
	 * The solution is to disable buffering on all three, so that all
	 * attempts to access the data will get turned directly into syscalls.
	 * That is, the reference to stderr (for instance) inside the
	 * subcontext will still use the subcontext-private structure, but it
	 * will see that buffering is disabled, and thus will directly make
	 * the syscall - and the syscall will use the process-appropriate
	 * version of file descriptor 2.  Turning off buffering should hurt
	 * performance (if a process is chatty), but make programs work
	 * correctly.
	 *
	 * In truth, this is a massive, terrible hack - based on the fact that
	 * (in this version of subcontexts) we're unwilling to modify the
	 * kernel.  Someday, we'll have process-local file handles (or maybe
	 * something better that we'll invent later), and this worry will go
	 * away.
	 */
	setvbuf(stdin,  NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);


	/* sanity-check the arguments */
	char *filename = NULL;
	if (argc >= 4 && strcmp(argv[1], "--file") == 0)
	{
		filename = argv[2];

		argc -= 2;
		for (i=1; i<argc; i++)
			argv[i] = argv[i+2];
	}

	if (argc < 2)
	{
		fprintf(stderr, "SYNTAX: %s [--file <filename>] <appAsSharedLib> [app_args...]\n", argv[0]);
		return 1;
	}


	dlerror();   // clear the error status
	void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
	if (handle == NULL)
	{
		fprintf(stderr, "%s: Could not open the app-as-shared-library: %s\n", argv[0], dlerror());
		return 2;
	}

	dlerror();   // clear the erorr status
	void *app_main_void = dlsym(handle, "subcontext_main");
	if (app_main_void == NULL)
	{
		char *err = dlerror();

		if (err == NULL)
			err = "symbol was defined, but was NULL";

		fprintf(stderr, "%s: Could not find the symbol 'subcontext_main' in the app-as-shared-library: %s\n", argv[0], err);
		return 2;
	}

	/* normally, dlclose() would clean up the library.  But since we
	 * passed RTLD_NODELETE, this won't happen.
	 */
	dlclose(handle);


	/* if we get here, then we have loaded the shared library and
	 * extracted a main function from it.  We are now ready to create
	 * an empty subcontext and import the pages of the library into
	 * that subcontext.
	 */


	/* BUGFIX: Open (but don't parse!) the 'maps' file *BEFORE* we create
	 *         the subcontext.  The original version of this code tried
	 *         to skip over regions allocated by the subcontext, but that
	 *         failed because the 'reserved' regions were mmap()ed with
	 *         MAP_ANONYMOUS and thus don't show up.  This caused us to
	 *         have conflicting regions: reserved regions, and active
	 *         regions with PROT_NONE!
	 *
	 * UPDATE: It seems that opening the file early is *NOT* enough...we
	 *         also have to read it immediately.  Probably, the right way
	 *         to do this is with a simple read...but the code below
	 *         already uses fgets.  So here's a hack!
	 */
	FILE *maps   = fopen("/proc/self/maps", "r");
	FILE *bounce = tmpfile();
	russ_flushfile(fileno(maps), fileno(bounce));
	fclose(maps);
	rewind(bounce);
	maps = bounce;
	


	/* currently, we only support the use of temp files.  Later, we'll
	 * probably add support for the user to create a named file, so that
	 * this tool can terminate and we'll still have something runnable.
	 *
	 * I know it's odd, but it's perfectly plausible to use a named file
	 * (which you might save forever on disk) as the backing store for
	 * a subcontext - so long, of course, as we don't have any dynamic
	 * implicit state to track (such as open secondary files).
	 */
	FILE *backing_file;
	if (filename == NULL)
		backing_file = tmpfile();
	else
		backing_file = fopen(filename, "w+x");   // 'x' means "fail if it exists"

	if (backing_file == NULL)
	{
		perror("Could not open the backing file");
		return 2;
	}

	mySc = sc_init(fileno(backing_file));
	if (mySc == NULL)
	{
		fprintf(stderr, "%s: Could not create the subcontext.\n", argv[0]);
russ_dumpLocalMaps(NULL);
russ_dumpLocalFilelist(NULL);
		return 2;
	}


	/* we must now walk the list of mapped memory.  We need to convert
	 * every map into a page in this subcontexts - with two exceptions:
	 * (1) pages that are mmap()s of the loader tool (since they are
	 * almost certainly fixed-location, and thus we could not load multiple
	 * subcontexts which might share the same space), and (2) pages that
	 * are mmap()s of the subcontext temporary file (since they are already
	 * in the subcontext!)
	 *
	 * We detect both by checking the devno/inode for each file, which we
	 * collect using stat().
	 */
	struct mapScanCallback_priv callback_priv;

	callback_priv.targSc = mySc;

	if ( stat("/proc/self/exe",     &callback_priv.ignoreFile1) != 0 ||
	    fstat(fileno(backing_file), &callback_priv.ignoreFile2) != 0)
	{
		perror("Could not find stat() on either the loader executable, or the temporary file used for the subcontext");
		return 3;
	}

	if ( ! S_ISREG(callback_priv.ignoreFile1.st_mode) ||
	     ! S_ISREG(callback_priv.ignoreFile2.st_mode))
	{
		fprintf(stderr, "Impossible error: Either the loader executable or the temporary file used for the subcontext are not files.  exe.st_mode=0x%x tmp.st_mode=0x%x : S_IFMT=0x%x S_IFREG=0x%x\n", callback_priv.ignoreFile1.st_mode, callback_priv.ignoreFile2.st_mode, S_IFMT, S_IFREG);
		return 3;
	}


	/* this function parses the 'maps' file and gives us a callback for
	 * each line; since we have set ignoreMode=true, we will import every
	 * line *EXCEPT* for the ones which match our two files we found
	 * above.
	 */
	callback_priv.rw_p__only  = false;
	callback_priv.importCount = 0;
	callback_priv.kill        = false;

	int cb_count = russ_parseProcMaps(maps, &callback_priv, &mapScanCallback);

	fclose(maps);

	if (callback_priv.kill)
	{
		fprintf(stderr, "subcontexts_init: Import of pages failed, the subcontext will now die!\n");
		return 1;
	}

	printf("%s: Import first pass: %d ranges were imported, out of %d maps found.\n", argv[0], callback_priv.importCount, cb_count);
	fflush(NULL);


	/* we do a second pass because sometimes glibc will allocate pages for
	 * temporary variables during this init process (such for file buffers
	 * and such) - and then we'll later find that we touch (from a remote
	 * process) these pages, and thus segfault.
	 *
	 * The solution is this second pass (which might repeat arbitrarily
	 * many times), where we hunt for newly-mmap()ed ranges.
	 */
	do
	{
		callback_priv.rw_p__only  = true;
		callback_priv.importCount = 0;

		cb_count = russ_parseProcMaps(NULL, &callback_priv, &mapScanCallback);

		if (callback_priv.kill)
		{
			fprintf(stderr, "subcontexts_init: Import of pages failed, the subcontext will now die!\n");
			return 1;
		}

		printf("%s: Import second pass: %d ranges were imported, out of %d maps found.\n", argv[0], callback_priv.importCount, cb_count);
		fflush(NULL);
	} while (callback_priv.importCount > 0);


	/* we have fully initialized the subcontext.  It is now time to call
	 * the main function.  We re-use our own argv[] to pass on to the
	 * app.
	 */
	for (i=1; i<argc; i++)
		argv[i-1] = argv[i];
	argc--;

	int (*app_main)(sc*,int,char**) = (void*)app_main_void;
	return app_main(mySc, argc, argv);
}



static bool mapScanCallback(void *priv_void,
                            u64 start, u64 end,
                            bool isR, bool isW, bool isX, bool isP,
                            u64 fileOffset,
                            u16 devno, u64 inode,
                            char *desc)
{
	struct mapScanCallback_priv *priv = priv_void;


	/* does this line represent either the exe or the tmp file?
	 * If so, then we skip it!
	 */
	if (devno == priv->ignoreFile1.st_dev && inode == priv->ignoreFile1.st_ino)
	{
		assert(isP);
		return true;
	}
	if (devno == priv->ignoreFile2.st_dev && inode == priv->ignoreFile2.st_ino)
	{
		assert( ! isP);
		return true;
	}
	assert(isP);


	/* if the descriptive text begins with [ , then we always ignore it.
	 * These ranges include [heap], [stack], [vdso], and [vscall].
	 */
	if (desc != NULL && desc[0] == '[')
		return true;


	/* are we in the second pass?  Then we have additional limitations. */
	if (priv->rw_p__only)
		if (isR == false || isW == false || isX || isP == false)
			return true;


	/* if the page doesn't have read permissions, then import
	 * doesn't make any sense.  But we should still import an
	 * unreadable page, in order to maintain the overflow
	 * protection.
	 *
	 * Question: why does it seem that each shared library has a
	 * range of ---p pages, *MAPPED TO THE LIBRARY*, immediately
	 * following the r-xp pages???
	 */
	if (!isR)
	{
		assert(!isW);
		assert(!isX);

		if (sc_import_protNone(priv->targSc,
		                       (void*)start, end-start) != 0)
		{
			fprintf(stderr, "Could not import a protection-none range 0x%016llx-0%016llx into the subcontext.\n", start,end);

			priv->kill = true;
			return false;
		}

		priv->importCount++;
		return true;
	}


	/* all the checks are done.  Import this page into the
	 * subcontext!
	 */
	int prot = (isR ? PROT_READ  : 0) |
	           (isW ? PROT_WRITE : 0) |
	           (isX ? PROT_EXEC  : 0);
	if (sc_import(priv->targSc, (void*)start, end-start, prot) != 0)
	{
		fprintf(stderr, "Could not import the range 0x%016llx-0x%016llx into the subcontext.\n", start,end);

		priv->kill = true;
		return false;
	}

	priv->importCount++;
	return true;
}


