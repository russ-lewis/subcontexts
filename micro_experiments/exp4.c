/**
 * This experiment attempts to load a library dynamically and determine how the maps file has changed.
 */

#include <stdio.h>
#include <dlfcn.h>
#include "../src/mem/map.h"

int main()
{
    Map *before = Map_parse(-1);

    void *libhandle = dlopen("./exp4_lib.so", RTLD_NOW);
    void (*lib_func)() = (void (*)())dlsym(libhandle, "lib_func");
    lib_func();
    
    Map *after = Map_parse(-1);

    Map *diff = Map_diff(before, after);
    printf("BEFORE:\n");   
    Map_print(before); 
    printf("\n\nAFTER:\n");   
    Map_print(after);    
    printf("\n\nDIFF:\n");   
    Map_print(diff);    

    dlclose(libhandle);
    Map_free(before);
    Map_free(after);
    Map_free(diff); 
    return 0;
}
