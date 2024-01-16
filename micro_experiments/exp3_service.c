#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <assert.h>
#include <sys/mman.h>

#include "table.h"

const int length = 3;

int main()
{
    int pagesize = sysconf(_SC_PAGE_SIZE);
    
    // ensure that the size of the table is less than the page size
    assert(length * sizeof(TableEntry) < pagesize);

    // create temp file
    char *fname = "tmpmem";
    int fd = open(fname, O_CREAT | O_RDWR, 0666);
    if (fd == -1)
    {
        fprintf(stderr, "failed to create temp file\n");
        return 1;
    }
    
    // allocate space in temp file for metadata and pages
    ftruncate(fd, pagesize * (length + 1));

    // map the metadata into memory
    void* mmap_ret = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mmap_ret == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed! errno = %d\n", errno);
        return 1;
    }

    uint8_t *bytes = (uint8_t *)mmap_ret;
    Table *table = (Table *)bytes;
    table->data = (TableEntry *)(bytes + sizeof(Table));
    table->length = length;

    // map each page into memory, store the address into the table, fill with random data
    for (int i = 0; i < length; i++)
    {
        mmap_ret = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, pagesize * (i + 1));
        if (mmap_ret == MAP_FAILED)
        {
            fprintf(stderr, "mmap failed! errno = %d\n", errno);
            return 1;
        }

        table->data[i].page = (char *)mmap_ret;
        if (i > 0)
        {
            table->data[i-1].next = &table->data[i];
        }
    
        // fill the page with random data
        for (int j = 0; j < pagesize - 1; j++)
        {
            table->data[i].page[j] = (char)(rand() % 26 + 97);
        }
        table->data[i].page[pagesize-1] = 0; // null terminate string
    }
    
    // close the temp file
    close(fd);

    // sleep until eof is sent
    printf("sleeping forever...\n");
    char buffer[32];
    while (fgets(buffer, 32, stdin)) { }
    
    // remove the temp file
    remove(fname);
    return 0;
}

