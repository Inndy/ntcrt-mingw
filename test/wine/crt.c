/*
 * The CRT DLL of the Wine tests is the program itself: NtCRT is linked statically. GetModuleHandle and LoadLibrary
 * of msvcrt.dll or ucrtbase.dll return the image of the program, and GetProcAddress of it finds the functions and
 * variables of msvcrt.dll that NtCRT defines (crt_exports, written by table.sh). Other modules are left to the
 * kernel32 functions of NtCRT. These definitions take the place of the import stubs (Name and __imp_Name).
 */

#include <string.h>
#include <windows.h>

struct crt_export
{
    const char *name;
    const void *addr;
    int imp; /* addr is __imp_name, the address is read from it */
};

extern const struct crt_export crt_exports[];
extern IMAGE_DOS_HEADER __ImageBase;

HMODULE WINAPI _K32_GetModuleHandleA(LPCSTR);
HMODULE WINAPI _K32_GetModuleHandleW(LPCWSTR);
HMODULE WINAPI _K32_LoadLibraryA(LPCSTR);
HMODULE WINAPI _K32_LoadLibraryW(LPCWSTR);
HMODULE WINAPI _K32_LoadLibraryExA(LPCSTR, HANDLE, DWORD);
HMODULE WINAPI _K32_LoadLibraryExW(LPCWSTR, HANDLE, DWORD);
FARPROC WINAPI _K32_GetProcAddress(HMODULE, LPCSTR);
BOOL WINAPI _K32_FreeLibrary(HMODULE);

#define OVERRIDE(Name) \
    __asm__(".globl " #Name "\n\t.set " #Name ", crt_" #Name); \
    const void *imp_##Name __asm__("__imp_" #Name) = (const void *)crt_##Name

#define SELF ((HMODULE)&__ImageBase)

/* msvcrt.dll, msvcrtd.dll or ucrtbase.dll with or without the extension and a directory */
static int is_crt(const void *name, int wide)
{
    static const char *const crts[] = { "msvcrt", "msvcrtd", "ucrtbase" };
    char base[16];
    unsigned int i, n = 0;

    if (!name) return 0;
    for (i = 0;; i++)
    {
        unsigned int c = wide ? ((const WCHAR *)name)[i] : (unsigned char)((const char *)name)[i];
        if (!c) break;
        if (c == '/' || c == '\\' || c == ':') n = 0;
        else if (n < sizeof(base) - 1) base[n++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        else n = sizeof(base);
    }
    if (n >= sizeof(base)) return 0;
    base[n] = 0;
    if (n > 4 && !strcmp(base + n - 4, ".dll")) base[n - 4] = 0;
    for (i = 0; i < ARRAYSIZE(crts); i++)
        if (!strcmp(base, crts[i])) return 1;
    return 0;
}

static HMODULE WINAPI crt_GetModuleHandleA(LPCSTR name)
{
    return is_crt(name, 0) ? SELF : _K32_GetModuleHandleA(name);
}

static HMODULE WINAPI crt_GetModuleHandleW(LPCWSTR name)
{
    return is_crt(name, 1) ? SELF : _K32_GetModuleHandleW(name);
}

static HMODULE WINAPI crt_LoadLibraryA(LPCSTR name)
{
    return is_crt(name, 0) ? SELF : _K32_LoadLibraryA(name);
}

static HMODULE WINAPI crt_LoadLibraryW(LPCWSTR name)
{
    return is_crt(name, 1) ? SELF : _K32_LoadLibraryW(name);
}

static HMODULE WINAPI crt_LoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags)
{
    return is_crt(name, 0) ? SELF : _K32_LoadLibraryExA(name, file, flags);
}

static HMODULE WINAPI crt_LoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags)
{
    return is_crt(name, 1) ? SELF : _K32_LoadLibraryExW(name, file, flags);
}

static FARPROC WINAPI crt_GetProcAddress(HMODULE module, LPCSTR name)
{
    const struct crt_export *e;

    if (module == SELF && (ULONG_PTR)name >= 0x10000)
    {
        for (e = crt_exports; e->name; e++)
        {
            if (!strcmp(e->name, name))
                return (FARPROC)(e->imp ? *(void *const *)e->addr : e->addr);
        }
        SetLastError(ERROR_PROC_NOT_FOUND);
        return NULL;
    }
    return _K32_GetProcAddress(module, name);
}

static BOOL WINAPI crt_FreeLibrary(HMODULE module)
{
    return module == SELF ? TRUE : _K32_FreeLibrary(module);
}

OVERRIDE(GetModuleHandleA);
OVERRIDE(GetModuleHandleW);
OVERRIDE(LoadLibraryA);
OVERRIDE(LoadLibraryW);
OVERRIDE(LoadLibraryExA);
OVERRIDE(LoadLibraryExW);
OVERRIDE(GetProcAddress);
OVERRIDE(FreeLibrary);
