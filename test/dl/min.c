/*
 * Only the functions of <dlfcn.h>: make test-dl checks by the map that they link no printf or stdio of NtCRT, and
 * the size against an empty program.
 */

#include <dlfcn.h>
#include <stddef.h>

int main(void)
{
    Dl_info Info;
    void* Lib = dlopen("dllib.dll", RTLD_NOW);

    return Lib == NULL || dlsym(Lib, "dl_add") == NULL || dlsym(Lib, "dl_nope") != NULL || dlerror() == NULL ||
           dladdr(dlsym(Lib, "dl_add"), &Info) == 0 || dlclose(Lib) != 0 || dlclose(Lib) == 0 || dlerror() == NULL;
}
