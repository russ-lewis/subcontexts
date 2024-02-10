#ifndef SCON_MEMMAP_H
#define SCON_MEMMAP_H

#include <string.h>

/**
 * An entry in the /proc/[pid]/maps file.
 */
typedef struct MapEntry
{
    void *start_addr;
    void *end_addr;

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

/**
 * The collection of maps in the /proc/[pid]/maps file.
 */
typedef struct Map
{
    MapEntry *head;
    size_t size;
} Map;

/**
 * Parses a maps file.
 */
Map *Map_parse(const int);

/**
 * Gets the diff between two maps.
 */
Map *Map_diff(const Map *, const Map *);

/**
 * Prints a map.
 */
void Map_print(const Map *);

/**
 * Creates the memory for a new map on the heap.
 */
Map *Map_new();

/**
 * Frees the memory for a map struct.
 */
void Map_free(Map *);

/**
 * Parses a map entry from a line in the maps file.
 */
MapEntry *MapEntry_parse(const char *);

/**
 * Copies the data in a map entry to a new map entry.
 */
MapEntry *MapEntry_copy(const MapEntry *);

/**
 * Compares to map entries to determine equality.
 */
char MapEntry_equal(const MapEntry *, const MapEntry *);

/**
 * Prints a map entry.
 */
void MapEntry_print(const MapEntry *self);

#endif

