#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/mman.h>

uint8_t *memptr;
int length;

int pagesize;

void sigsegv_handler(int signal, siginfo_t *info, void *ucontext);

int main()
{
    // register the SIGSEGV signal handler
    struct sigaction sa_sigsegv;
    bzero(&sa_sigsegv, sizeof(sa_sigsegv));
    sa_sigsegv.sa_sigaction = sigsegv_handler;
    sa_sigsegv.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa_sigsegv, NULL);
   
    // get the page size, set the length to be 16 pages, map anonymous with r/w permissions
    pagesize = sysconf(_SC_PAGE_SIZE);
    length = pagesize * 16;
    void* mmap_ret = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mmap_ret == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed! errno = %d\n", errno);
        return 1;
    }
    
    memptr = (uint8_t *)mmap_ret;

    // change memory permissions of mapped memory to none
    mprotect(memptr, length, PROT_NONE);

    // increment all bytes by 1
    for (int i = 0; i < length; i++)
    {
        uint8_t *p = memptr + i;
        (*p)++;
    }

    return 0;
}

/*
 * This function handles the SIGSEGV signal. 
 * It checks if the address of the page fault is in a valid range;
 * if it is then it turns on permissions for the page, otherwise
 * it throws an error and terminates.
 */
void sigsegv_handler(int signal, siginfo_t *info, void *ucontext)
{
    if (info->si_errno != 0)
    {
        fprintf(stderr, "SIGSEGV failed! si_errno = %d\n", info->si_errno);
        exit(1);
    }
    
    // if the address is within the mapped buffer, turn on permissions to the memory
    uint8_t *addr = (uint8_t *)info->si_addr;
    if (addr >= memptr && addr < memptr + length)
    {
        printf("%s: addr = %p was in the range: %p - %p\n", __func__, addr, memptr, memptr + length);
        mprotect(addr, pagesize, PROT_READ | PROT_WRITE);
    }
    else
    {
        // TODO report a SEGV as a "real" error and kill the program
        fprintf(stderr, "%s: addr = %p was not in the range: %p - %p\n", __func__, addr, memptr, memptr + length);
        exit(1);
    }
}
