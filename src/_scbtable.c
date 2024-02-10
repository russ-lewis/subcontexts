#include <stdlib.h>
#include <memory.h>
#include <assert.h>
#include <signal.h>
#include <sys/mman.h>

#include <stdio.h>
#include <mem/map.h>

#include <scon.h>
#include "_scbtable.h"

static unsigned int length = 0;
static unsigned int capacity = 10;
static scb *scb_table = NULL;

scon_t allocate_scb()
{
    scon_t scon = length;
    length++;
    if (length >= capacity / 2)
    {
        capacity *= 2;
        scb_table = realloc(scb_table, sizeof(scb) * capacity);
    }

    bzero(&scb_table[scon], sizeof(scb));
    return scon;
}

scb *get_scb(const scon_t scon)
{
    assert(scon < length);
    return &scb_table[scon];
}

static void sigsegv_handler(int signal, siginfo_t *info, void *ucontext)
{
    if (info->si_errno != 0)
    {
        fprintf(stderr, "SIGSEGV failed! si_errno = %d\n", info->si_errno);
        exit(1);
    }

    // try to find the address in any of the subcontext's maps
    void *addr = info->si_addr;
    for (int i = 0; i < length; i++)
    {
        scb *scon = &scb_table[i];
        for (MapEntry *curr = scon->memmap->head; curr != NULL; curr = curr->next)
        {
            if (addr >= curr->start_addr && addr < curr->end_addr)
            {
                // the seg fault is for this page, enable execution.

                Map_print(scon->memmap);
                printf("addr = %p, start = %p, end = %p\n", addr, curr->start_addr, curr->end_addr);
                int maplength = curr->end_addr - curr->start_addr;
                mprotect(curr->start_addr, maplength, PROT_READ | PROT_WRITE | PROT_EXEC);
                return;
            }
        }
    }

    fprintf(stderr, "scon segmentation fault");
    exit(1);
}

__attribute__((constructor))
static void init() 
{
    scb_table = malloc(sizeof(scb) * capacity);
    
    // register the SIGSEGV signal handler
    struct sigaction sa_sigsegv;
    bzero(&sa_sigsegv, sizeof(sa_sigsegv));
    sa_sigsegv.sa_sigaction = sigsegv_handler;
    sa_sigsegv.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa_sigsegv, NULL);
}

__attribute__((destructor))
static void destroy() 
{
    free(scb_table);
}

