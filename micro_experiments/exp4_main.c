#include <stdio.h>
#include <dlfcn.h>
#include "proc/memmap.h"

int main()
{
    MemMap *before = MemMap_parse(-1);

    void *libhandle = dlopen("./exp4_lib.so", RTLD_NOW);
    void (*lib_func)() = (void (*)())dlsym(libhandle, "lib_func");
    lib_func();
    
    MemMap *after = MemMap_parse(-1);

    MemMap *diff = MemMap_diff(before, after);
    printf("BEFORE:\n");   
    MemMap_print(before); 
    printf("\n\nAFTER:\n");   
    MemMap_print(after);    
    printf("\n\nDIFF:\n");   
    MemMap_print(diff);    

    dlclose(libhandle);
    MemMap_free(before);
    MemMap_free(after);
    MemMap_free(diff); 
    return 0;
}
