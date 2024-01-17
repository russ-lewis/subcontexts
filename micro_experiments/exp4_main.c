#include <dlfcn.h>

int main()
{
    void *libhandle = dlopen("./exp4_lib.so", RTLD_NOW);
    void (*lib_func)() = (void (*)())dlsym(libhandle, "lib_func");
    lib_func();
    dlclose(libhandle);
    return 0;
}
