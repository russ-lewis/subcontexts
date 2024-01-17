#include <stdio.h>
#include "maps.h"

int main()
{
    Maps *maps = Maps_parse(-1);
    MapEntry *curr = maps->first;
    for (int i = 0; curr != NULL; i++)
    {
        printf("Map Entry %d:\n", i + 1);
        printf("\tstart_addr = %x\n", curr->start_addr);
        printf("\tend_addr = %x\n", curr->end_addr);
        
        curr = curr->next;
    }

    return 0;
}
