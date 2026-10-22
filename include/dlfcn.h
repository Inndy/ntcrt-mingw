/*
 * <dlfcn.h> of NtCRT (src/Dlfcn.cpp): dlopen, dlsym, dlclose, dlerror and dladdr on the loader of ntdll, with the
 * values of dlfcn-win32. -ldl links an empty library.
 */

#ifndef _NTCRT_DLFCN_H
#define _NTCRT_DLFCN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Windows binds the imports of a DLL when it loads it, RTLD_LAZY is RTLD_NOW */
#define RTLD_NOW 0
#define RTLD_LAZY RTLD_NOW
/* The default: dlsym of RTLD_DEFAULT, RTLD_NEXT and the program finds the symbols of the module */
#define RTLD_GLOBAL (1 << 1)
/* For a module that dlopen loads: only dlsym of its own handle finds its symbols */
#define RTLD_LOCAL (1 << 2)

/* The program, then the modules in the order they were loaded */
#define RTLD_DEFAULT ((void*)0)
/* The modules loaded after the one that calls dlsym */
#define RTLD_NEXT ((void*)-1)

typedef struct dl_info
{
    const char* dli_fname; /* The path of the module, in a buffer of the thread that the next dladdr reuses */
    void* dli_fbase;       /* The base of the module */
    const char* dli_sname; /* The name of the nearest export at or below the address, or NULL */
    void* dli_saddr;       /* Its address, or NULL */
} Dl_info;

void* dlopen(const char* file, int mode);
int dlclose(void* handle);
void* dlsym(void* handle, const char* name);
char* dlerror(void);
int dladdr(const void* addr, Dl_info* info);

#ifdef __cplusplus
}
#endif

#endif
