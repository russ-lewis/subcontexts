#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>

#include "maps.h"

Maps *Maps_parse(int pid)
{
    char *path;
    if (pid == -1)
    {
        path = "/proc/self/maps";
    }
    else
    {
        char buffer[32];
        sprintf(buffer, "/proc/%d/maps", pid);
        path = buffer;
    }

    FILE *mapsfile = fopen(path, "r");
    if (mapsfile == NULL)
    {
        fprintf(stderr, "unable to open %s\n", path);
        return NULL;
    }
    
    Maps *maps = Maps_new();
    MapEntry *curr = NULL;
    char line[256];
    while (fgets(line, sizeof(line), mapsfile) != NULL)
    {
        MapEntry *entry = MapEntry_parse(line); 
        if (curr == NULL)
        {
            maps->first = entry;
            curr = entry;
        }
        else
        {
            curr->next = entry;
            curr = curr->next;
        }

        maps->size++;
    }

    fclose(mapsfile);
    return maps;
}

MapEntry *MapEntry_parse(char *buffer)
{
    MapEntry *entry = calloc(sizeof(MapEntry), 1);
    char flags[4];
    sscanf(buffer, 
           "%x-%x %4c %x %x:%x %d %s", 
           &entry->start_addr, 
           &entry->end_addr, 
           flags, 
           &entry->offset,
           &entry->dev_major,
           &entry->dev_minor,
           &entry->inode,
           entry->pathname);

    entry->r = flags[0] == 'r';
    entry->w = flags[1] == 'w';
    entry->e = flags[2] == 'e';
    entry->p = flags[3] == 'p';

    return entry;
}

Maps *Maps_new()
{
    return calloc(sizeof(Maps), 1);
}

void Maps_free(Maps* self)
{
    MapEntry *curr = self->first;
    while (curr != NULL)
    {
        MapEntry *next = curr->next;
        free(curr);
        curr = next;
    }

    free(self);
}

