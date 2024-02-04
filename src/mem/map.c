#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>

#include "map.h"

Map *Map_parse(const int pid)
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
    
    Map *maps = Map_new();
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

Map *Map_diff(const Map *before, const Map *after)
{
    Map *diff = Map_new();
    MapEntry *diff_curr;
    for (MapEntry *acurr = after->first; acurr != NULL; acurr = acurr->next)
    {
        char in_both = 0;
        for (MapEntry *bcurr = before->first; bcurr != NULL; bcurr = bcurr->next)
        {
            if (MapEntry_equal(bcurr, acurr))
            {
                in_both = 1;
                break;
            }
        }

        if (!in_both)
        {
            MapEntry *entry = MapEntry_copy(acurr);
            if (diff->first == NULL)
            {
                diff->first = entry;
                diff_curr = entry;
            }
            else
            {
                diff_curr->next = entry;
                diff_curr = entry;
            }           
        }
    }

    return diff;
}

void Map_print(const Map *self)
{
    MapEntry *curr = self->first;
    if (curr == NULL)
    {
        printf("No maps\n");
        return;
    }

    printf("   start       end  rwxp    offset  device        inode  pathname\n");
    printf("-----------------------------------------------------------------\n");
    for (int i = 0; curr != NULL; i++, curr = curr->next)
    {
        MapEntry_print(curr);
    } 
}

Map *Map_new()
{
    return calloc(sizeof(Map), 1);
}

void Map_free(Map *self)
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

MapEntry *MapEntry_parse(const char *buffer)
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

MapEntry *MapEntry_copy(const MapEntry *m)
{
    MapEntry *entry = malloc(sizeof(MapEntry));
    memcpy(entry, m, sizeof(MapEntry));
    entry->next = NULL;
    return entry;
}

char MapEntry_equal(const MapEntry *a, const MapEntry *b)
{
    return a->start_addr == b->start_addr && a->end_addr == b->end_addr;
}

void MapEntry_print(const MapEntry *self)
{ 
    printf(
        "%8x  %8x  %c%c%c%c  %8x  %4x:%-4x  %8d  %s\n", 
        self->start_addr, self->end_addr, 
        self->r ? 'r' : '-', self->w ? 'w' : '-', self->e ? 'x' : '-', self->p ? 'p' : 's',
        self->offset,
        self->dev_major, self->dev_minor,
        self->inode,
        self->pathname);
}


