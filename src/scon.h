#ifndef SCON_H
#define SCON_H

/**
 * The handle of a subcontext.
 */
typedef struct scon_t 
{
    void *libhandle;
} scon_t;

/**
 * Initializes a subcontext given the path to the library.
 */
void scon_init(scon_t *self, const char *libpath);

/**
 * Loads a function pointer from the subcontext with the given name.
 */
void *scon_loadf(scon_t *self, const char *funcname);

/**
 * Frees resources held by the subcontext.
 */
void scon_free(scon_t *self);

#endif
