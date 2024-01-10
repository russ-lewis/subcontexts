
#ifndef _SUBCONTEXTS_REGISTRY_H__INCLUDED_
#define _SUBCONTEXTS_REGISTRY_H__INCLUDED_


#include "subcontexts.h"
#include "subcontexts_local.h"


/* this struct contains function pointers to all of the functions exported
 * by the subcontexts registry.
 */
typedef struct {
	int   (*post)(char *name, void *sym);
	void *(*find)(char *name);
} sc_registry_funcs;


/* this function is implemented in the subcontexts core.  It takes as an
 * argument the handle for the subcontexts registry subcontext, which must
 * already have been loaded.  It returns a pointer to the struct above, or
 * NULL on error.
 *
 * UPDATE: It's actually a wrapper macro, calling an internal function; you
 *         should *NOT* call the internal function unless you are *sure* you
 *         know what you're doing.  Otherwise, you're going to have real
 *         trouble when the registry here tries to automatically sc_join()
 *         for you.
 */
#define subcontexts_registry_bootstrap()                            \
                   __subcontexts_registry_bootstrap(sc_local_list)

sc_registry_funcs *__subcontexts_registry_bootstrap(struct sc_local*);



#endif

