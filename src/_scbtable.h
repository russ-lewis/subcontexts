/**
 * This is a private library to the scon library and should not be included by any user code. 
 * It manages the subcontext table.
 */

#ifndef SCB_TABLE_H
#define SCB_TABLE_H

#include <mem/map.h>
#include <scon.h>

/**
 * The subcontext control block. Stores necessary information to manage a subcontext.
 */
typedef struct scb // subcontext control block
{
    void *libhandle;
    int memhandle;
    Map *memmap;
    void *start_addr;
    void *end_addr;
} scb;

/**
 * Allocates space in the scb table and returns the scon handle.
 */
scon_t allocate_scb();

/**
 * Gets an scb from the table given the scon handle.
 */
scb *get_scb(const scon_t scon);

#endif
