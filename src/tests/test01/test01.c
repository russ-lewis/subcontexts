#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <scon.h>

int main()
{
    scon_t scon = scon_create("test01_scon.so"); 
    scon_callf(scon, "test", NULL);
    scon_close(scon);
    return 0;
}
