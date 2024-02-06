#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <scon.h>

int main()
{
    scon_t scon;
    scon_init(&scon, "test01_scon.so"); 
    void (*test)() = scon_loadf(&scon, "test");
    test(); 
    scon_free(&scon);
    return 0;
}
