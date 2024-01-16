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

Table *table;
int pagesize;
int temp_fd;

void sigsegv_handler(int signal, siginfo_t *info, void *ucontext);

int main()
{
    // register the SIGSEGV signal handler
    struct sigaction sa_sigsegv;
    bzero(&sa_sigsegv, sizeof(sa_sigsegv));
    sa_sigsegv.sa_sigaction = sigsegv_handler;
    sa_sigsegv.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa_sigsegv, NULL);    

    pagesize = sysconf(_SC_PAGE_SIZE);
    
    // open the temp file
    char *fname = "tmpmem";
    temp_fd = open(fname, O_RDWR);
    if (temp_fd == -1)
    {
        fprintf(stderr, "failed to open temp file. errno = %d\n", errno);
        return 1;
    }

    // map the metadata into memory
    void* mmap_ret = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, temp_fd, 0);
    if (mmap_ret == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed! errno = %d\n", errno);
        return 1;
    }
    
    uint8_t *bytes = (uint8_t *)mmap_ret;
    table = (Table *)bytes;
    table->data = (TableEntry *)(bytes + sizeof(Table));
     
    for (int i = 0; i < table->length; i++)
    {
        printf("page %d:\n", i);
        printf("%s\n\n", table->data[i].page); 
    }

    close(temp_fd);
    return 0;
}


void sigsegv_handler(int signal, siginfo_t *info, void *ucontext)
{
    if (info->si_errno != 0)
    {
        fprintf(stderr, "SIGSEGV failed! si_errno = %d\n", info->si_errno);
        exit(1);
    }

    char *addr = (char *)info->si_addr;
    char success = 0;
    for (int i = 0; i < table->length; i++)
    {
        TableEntry *curr = &table->data[i];
        if (addr >= curr->page && addr < curr->page + pagesize) // seg fault here
        {
            // the address maps to this page
            if (mmap(curr->page, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, temp_fd, pagesize * (i + 1)) == MAP_FAILED)
            {
                fprintf(stderr, "mmap in SIGSEGV handler failed! errno = %d\n", errno);
                exit(1);
            }

            success = 1;
            break;
        }
    }

    if (success == 0)
    {
        fprintf(stderr, "addr: %p was not a valid virtual address\n", addr);
        exit(1);
    }
}
