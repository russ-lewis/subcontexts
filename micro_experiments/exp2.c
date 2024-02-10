#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/mman.h>

uint8_t *buffer_A;
uint8_t *buffer_B;
int length;

void sigsegv_handler(int signal, siginfo_t *info, void *ucontext);

int main()
{
    // register the SIGSEGV signal handler
    struct sigaction sa_sigsegv;
    bzero(&sa_sigsegv, sizeof(sa_sigsegv));
    sa_sigsegv.sa_sigaction = sigsegv_handler;
    sa_sigsegv.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa_sigsegv, NULL);
   
    // get the page size, then create buffer A with a backing file and r/w permission
    // also create buffer B with map anonymous and r/w permission
    length = sysconf(_SC_PAGE_SIZE);
    
    int fd = open("test.txt", O_RDWR);
    if (fd == -1)
    {
        fprintf(stderr, "failed to open test.txt\n");
        return 1;
    }

    void* mmap_ret = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (mmap_ret == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed! errno = %d\n", errno);
        return 1;
    }

    buffer_A = (uint8_t *)mmap_ret;
    
    mmap_ret = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mmap_ret == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed! errno = %d\n", errno);
        return 1;
    }

    buffer_B = (uint8_t *)mmap_ret;
    
    // change memory permissions of one of the buffers to none
    mprotect(buffer_B, length, PROT_NONE);
    
    // copy 200 bytes from buffer A to buffer B
    memcpy(buffer_A, buffer_B, 200);

    return 0;
}

/*
 * This function handles the SIGSEGV signal. 
 * It checks if the address of the page fault is in one of the two buffers;
 * if it is then it turns on permissions for the buffer it is in and off for the other, 
 * otherwise it throws an error and terminates.
 */
void sigsegv_handler(int signal, siginfo_t *info, void *ucontext)
{
    if (info->si_errno != 0)
    {
        fprintf(stderr, "SIGSEGV failed! si_errno = %d\n", info->si_errno);
        exit(1);
    }
    
    // determine what buffer the address is in and switch permissions
    uint8_t *addr = (uint8_t *)info->si_addr;
    if (addr >= buffer_A && addr < buffer_A + length)
    {
        printf("%s: turning on access to buffer A and turning off access to buffer B\n", __func__);
        mprotect(buffer_A, length, PROT_READ | PROT_WRITE);
        mprotect(buffer_B, length, PROT_NONE);
    }
    else if (addr >= buffer_B && addr < buffer_B + length)
    {
        printf("%s: turning on access to buffer B and turning off access to buffer A\n", __func__);
        mprotect(buffer_B, length, PROT_READ | PROT_WRITE);
        mprotect(buffer_A, length, PROT_NONE);
    }
    else
    {
        // TODO report a SEGV as a "real" error and kill the program
        fprintf(stderr, "%s: addr = %p was not in buffer A: %p - %p or buffer B: %p - %p\n", __func__, addr, buffer_A, buffer_A + length, buffer_B, buffer_B + length);
        exit(1);
    }
}
