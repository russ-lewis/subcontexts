#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static int (*real_open)(const char *, int);

// got the idea to shim this way from https://lucasteske.medium.com/linux-shim-for-patching-executable-in-run-time-9cdcd773ed98
void __attribute__((constructor)) initialize()
{
    real_open = dlsym(RTLD_NEXT, "open");
    if (real_open == NULL)
    {
        fprintf(stderr, "Could not load open syscall\n");
        exit(1);
    }
}

int open(const char *path, int oflag) 
{
    printf("Opening %s\n", path);
    return real_open(path, oflag);    
}

