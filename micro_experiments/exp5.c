/**
 * This experiment shims the open C library call.
 * It requires that LD_PRELOAD=./exp5_shim.so environment variable be set before execution.
 */

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

int main()
{
    int fd = open("./test.txt", O_RDONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Could not open test.txt, errno = %d\n", errno);
        return 1;
    }

    printf("fd = %d\n");
    close(fd);
    return 0;
}
