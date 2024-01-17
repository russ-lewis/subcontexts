#ifndef SCON_MAPS_H
#define SCON_MAPS_H

#include <string.h>

/*
 * An entry in the /proc/[pid]/maps file.
 */
typedef struct MapEntry
{
    unsigned int start_addr;
    unsigned int end_addr;

    char r;
    char w;
    char e;
    char p;
    
    unsigned int offset;
    unsigned int dev_major;
    unsigned int dev_minor;
    int inode;
    char pathname[128];

    struct MapEntry *next;
} MapEntry;

typedef struct Maps
{
    MapEntry *first;
    size_t size;
} Maps;

MapEntry *MapEntry_parse(char *);

Maps *Maps_parse(int);

Maps *Maps_new();

void Maps_free(Maps *);

#endif

