
#ifndef _SUBCONTEXTS_V1_OVERRIDES_H__INCLUDED_
#define _SUBCONTEXTS_V1_OVERRIDES_H__INCLUDED_


/* VERSION 1 OVERRIDES
 *
 * Version 1 of subcontexts is "cooperative" - meaning that we have to trust
 * the user programs to not do things which are dangerous.  Unfortunately,
 * libc (quite reasonably and innocently) has code which does mmap()/munmap()
 * and such.  For instance, when you fopen() a file, it mmap()s a page
 * (presumably, for the buffering, though I don't know for sure).
 *
 * In general, I figure that the user is responsible for knowing when he is
 * doing dangerous things - and debugging the result.  Learn your lesson, and
 * don't do it again.  But the f*() functions are just *too* useful to give up
 * entirely.
 *
 * Fortunately, I've found a (partial) solution: fopencookie() allows you to
 * create a new pseudo-file which has its own read/write/seek/close
 * implementation.  I'm just going to wrap an ordering file descriptor with
 * that function - so we can use ordinary calls for everything, except for the
 * functions which open new files.  I'll overload fopen(), fdopen(), freopen(),
 * and tmpfile.
 *
 * If you have some code (such as some of the russ*() functions or system())
 * which opens its own files, then those will use standard fopen(), and will
 * mmap() buffers.  But hopefully, those will be rare, and will get closed
 * promptly, and thus not be a problem in practice.
 *
 * In some cases here, I've simply redirected the function to one which gives
 * an explicit warning.  In other cases, I have written (or hope to write in
 * the future) an emulation layer.
 */


#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <libio.h>
#include <sys/mman.h>

/* Russ says:
 *
 * I have developed a coding style of using TODO() widely as a placeholder for
 * code I'm going to write later.  The problem is, it breaks when called
 * *INSIDE* a subcontext, because 
 */
#include "russ_common_code/c/russ_int_typedefs_linux.h"



static inline void *sc_emulation_mmap(void *addr, size_t length,
                                      int prot, int flags,
                                      int fd, off_t offset)
{
	return sc_mmap(getCurSc(),
	               addr, length, prot, flags, fd, offset);
}
#undef mmap
#define mmap(a,b,c,d,e,f) sc_emulation_mmap(a,b,c,d,e,f)


static inline void sc_emulation_munmap(void *addr, size_t length)
{
	sc_munmap(getCurSc(),
	          addr, length);
}
#undef munmap
#define munmap(a,b) sc_emulation_munmap(a,b)


#undef  mremap
  // TODO: write mremap()
#define mremap(a,b,c,d) sc_emulation_mremap(a,b,c,d)


#undef  mprotect
  // TODO: write mprotect()
#define mprotect(a,b,c) sc_emulation_mprotect(a,b,c)


static inline FILE *sc_emulation_fopen(const char *path, const char *mode)
{
	FILE *retval = fopen(path,mode);
	if (retval == NULL)
		return NULL;

	setvbuf(retval, NULL, _IONBF, 0);

	return retval;
}
#undef  fopen
#define fopen(a,b) sc_emulation_fopen(a,b)


static inline FILE *sc_emulation_tmpfile()
{
	FILE *retval = tmpfile();
	if (retval == NULL)
		return NULL;

	setvbuf(retval, NULL, _IONBF, 0);

	return retval;
}
#undef  tmpfile
#define tmpfile sc_emulation_tmpfile


#undef freopen
    // TODO: implement freopen(), if we ever actuall care about it!
#define freopen(a,b) sc_emulation_freopen(a,b)


/* I don't understand why, but fflush(NULL) causes crashes when it is called
 * from inside a subcontext call.  But since (in theory) all of our subcontext
 * processes are supposed to use FILE-buffering disabled throughout (see
 * our overrides above, plus the code at the head of main() in
 * subcontext_init.c), disabling fflush() should be harmless.  Knock on wood.
 */
#define fflush(a) do {;} while(0)


#endif


