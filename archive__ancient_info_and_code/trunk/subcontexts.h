
#ifndef _SUBCONTEXTS_H__INCLUDED_
#define _SUBCONTEXTS_H__INCLUDED_


/* This is the core subcontexts header - *ALL* programs which use subcontexts
 * must include it.  It includes a variety of new subroutines which implement
 * basic services (mmap, munmap, fork, etc.) in a subcontexts-aware fashion;
 * it also has #define's which disable to original versions of those
 * functions.  Do *NOT* attempt to disable or subvert those #define's in user
 * code unless you are *very* sure what you are doing - if you do, you will
 * likely break subcontexts!
 */


/* this anonymous struct is visible only to the control code, and is an opaque
 * handle for user code.  Each such handle represents a single subcontext which
 * is mapped into the local process.
 *
 * The subcontexts library keeps a list of the currently-mapped subcontexts in
 * its own local variables (which are in private memory).
 */

struct sc;
typedef struct sc sc;



/* this function gets the pointer to the local subcontext.  This is *VERY* odd,
 * but it works: since the function is implemented inside the subcontexts
 * library, it is process-specific (and thus also specific to every subcontext
 * that is loaded with subcontexts_init).
 *
 * If a program calls this function outside of subcontexts code (that is, in
 * its private code), then this function scans the local process's list of
 * mapped subcontexts, doesn't find this page in any of them, and thus returns
 * NULL.  But if code inside a subcontext calls this, then it will find the
 * function's code page inside the sc of a certain subcontext - the one which
 * called it!
 *
 * Shared libraries and subcontexts are a *WEIRD* combination.  I haven't
 * decided if I think they are weird-good or weird-awful yet.
 *
 *      Maybe "a bit of both"   - SkyLord, Guardians of the Galaxy
 */
sc *getCurSc(void);



/* these two functions map new subcontexts into the local process.  sc_init()
 * creates a new subcontext, using an empty file opened by the caller.
 * sc_join() joins an existing subcontext.
 */
sc *sc_init(int fd);
sc *sc_join(int fd);



/* this is a simple wrapper for fork(), which includes updating the
 * process list of all mapped subcontexts
 */
int sc_fork(void);

/* every time that you exec(), you *MUST* call this to detach from the
 * currently-mapped subcontexts.
 *
 * NOTE: system() and popen() are (probably) safe functions, even though
 *       neither are subcontexts aware - since they won't touch any of
 *       the memory associated with any currently-mapped subcontext, the
 *       fact that there is a process which (temporarily) has extra maps
 *       of the pages is harmless.
 */
void sc_preExecDetach(void);



/* these are versions of the standard C library functions which, instead
 * of allocating and freeing process local memory, instead allocate and
 * free pages in a locally-mapped subcontext.
 */
void *sc_mmap(sc*,
              void *addr, size_t length, int prot, int flags,
              int fd, int offset);

void  sc_munmap(sc*,
                void *addr, size_t length);

/* this is the same idea, but for malloc()/free().  The buffers returned
 * by malloc() are in shared memory inside the selected subcontext.
 *
 * WARNING: Never pass an sc_malloc() buffer to C's ordinary free(), nor
 *          an ordinary malloc() buffer to sc_free().  Likewise, when calling
 *          sc_free, *ALWAYS* pass it the same sc pointer as you did for the
 *          matching sc_malloc().
 */
void *sc_malloc(sc*, ssize_t);
void  sc_free  (sc*, void*);



#include "subcontexts_v1_overrides.h"



#endif

