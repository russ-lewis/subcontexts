#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>

#include <mem/map.h>
#include <scon.h>
#include "_scbtable.h"

scon_t scon_create(const char *libpath)
{
    scon_t sconhandle = allocate_scb();
    scb *scon = get_scb(sconhandle);
    
    // dlopen does not follow relative paths, so find the absolute path of libpath
    char abspath[PATH_MAX];
    if (libpath[0] == '/')
    {
        memcpy(abspath, libpath, strlen(libpath));
    }
    else
    {
        getcwd(abspath, sizeof(abspath));
        strcat(abspath, "/");
        strcat(abspath, libpath);
    }

    Map *before = Map_parse(-1);

    // open the library
    scon->libhandle = dlopen(abspath, RTLD_NOW);
    if (scon->libhandle == NULL)
    {
        fprintf(stderr, dlerror());
        exit(1);
    }
    
    Map *after = Map_parse(-1);
    scon->memmap = Map_diff(before, after);
    Map_free(before); 
    Map_free(after);
    
    //char *tfname = "XXXXXX";
    //scon->memhandle = mkstemp(tfname);

    for (MapEntry *curr = scon->memmap->head; curr != NULL; curr = curr->next)
    {
        // change memory permissions of mapped memory to none
        int length = curr->end_addr - curr->start_addr;
        mprotect(curr->start_addr, length, PROT_NONE);
        // TODO map the memory to a temp file?
    }

    return sconhandle;
}

void *scon_callf(const scon_t sconhandle, const char *funcname, void *arg)
{
    scb *scon = get_scb(sconhandle);
    void *(*func)(void *) = (void *(*)(void *))dlsym(scon->libhandle, funcname);
    if (func == NULL)
    {
        fprintf(stderr, dlerror());
    }

    void *ret = func(arg);
    
    // after function call is made, turn off memory permission
    for (MapEntry *curr = scon->memmap->head; curr != NULL; curr = curr->next)
    {
        int length = curr->end_addr - curr->start_addr;
        mprotect(curr->start_addr, length, PROT_NONE);
    }

    return ret;
}

void scon_close(const scon_t sconhandle)
{
    scb *scon = get_scb(sconhandle);
    for (MapEntry *curr = scon->memmap->head; curr != NULL; curr = curr->next)
    {
        // change memory permissions of mapped memory to r/w
        int length = curr->end_addr - curr->start_addr;
        mprotect(curr->start_addr, length, PROT_READ | PROT_WRITE | PROT_EXEC);
    }

    dlclose(scon->libhandle); 
    Map_free(scon->memmap);

    // TODO free scon from table?
}

