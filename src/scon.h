/**
 * Includes functions to initialize and uses subcontexts.
 */

#ifndef SCON_H
#define SCON_H

#include <stdio.h>
#include <mem/map.h>

typedef unsigned int scon_t;

/**
 * Initializes a subcontext given the path to the library.
 */
scon_t scon_create(const char *);

/**
 * Calls a function in the library.
 */
void *scon_callf(const scon_t, const char *, void *);

/**
 * Frees resources held by the subcontext.
 */
void scon_close(const scon_t);

#endif
