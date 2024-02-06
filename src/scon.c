#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include <mem/map.h>
#include <scon.h>

void scon_init(scon_t *self, const char *libpath)
{
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
    self->libhandle = dlopen(abspath, RTLD_NOW);
    if (self->libhandle == NULL)
    {
        fprintf(stderr, dlerror());
        exit(1);
    }
    
    Map *after = Map_parse(-1);
    Map *diff = Map_diff(before, after);
    Map_free(before); 
    Map_free(after);

    if (diff->head != NULL)
    {
        for (MapEntry *curr = diff->head; curr != NULL; curr = curr->next)
        {
            // TODO need to copy the pages?
        }
    }    
    
    Map_free(diff);
}

void *scon_loadf(scon_t *self, const char *funcname)
{
    void *func = dlsym(self->libhandle, funcname);
    if (func == NULL)
    {
        fprintf(stderr, dlerror());
    }

    return func;
}

void scon_free(scon_t *self)
{
    dlclose(self->libhandle);
}

