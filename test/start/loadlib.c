/*
 * LoadLibrary of NtCRT with the PEB of Windows 7, whose LdrLoadDll takes a search path or NULL: the flags in place of
 * the path (LDR_PATH_IS_FLAGS) came with Windows 8, Windows 7 reads them as a string and faults.
 */

#include <KNSoft/NDK/NDK.h>
#include <stdio.h>
#include <wchar.h>

static int Failed;

#define CHECK(Cond) \
    do \
    { \
        if (!(Cond)) \
        { \
            printf("FAIL loadlib line %d: %s\n", __LINE__, #Cond); \
            Failed = 1; \
        } \
    } while (0)

typedef NTSTATUS(NTAPI* LOAD_DLL)(PCWSTR, PULONG, PCUNICODE_STRING, PVOID*);

/* The slot of LdrLoadDll in the import table, of a stdcall function on x86 */
#ifdef _WIN64
extern LOAD_DLL __imp_LdrLoadDll;
#else
extern LOAD_DLL __imp_LdrLoadDll __asm__("__imp__LdrLoadDll@16");
#endif

static LOAD_DLL g_LoadDll;
static WCHAR g_Path[MAX_PATH * 2];
static BOOL g_Flags;

/* The search path of the last load, the flags of Windows 8 recorded and not passed on */
static NTSTATUS NTAPI LoadDll(PCWSTR DllPath, PULONG Flags, PCUNICODE_STRING DllName, PVOID* DllBase)
{
    g_Flags = ((ULONG_PTR)DllPath & 1) != 0;
    g_Path[0] = L'\0';
    if (g_Flags)
    {
        return STATUS_ACCESS_VIOLATION;
    }
    if (DllPath != NULL)
    {
        wcsncpy(g_Path, DllPath, ARRAYSIZE(g_Path) - 1);
    }
    return g_LoadDll(DllPath, Flags, DllName, DllBase);
}

int main(void)
{
    PPEB Peb = NtCurrentPeb();
    ULONG Major = Peb->OSMajorVersion, Minor = Peb->OSMinorVersion;
    PCUNICODE_STRING DefaultPath = &Peb->ProcessParameters->DllPath;
    WCHAR Name[MAX_PATH];
    DWORD Protect;
    size_t Directory;

    swprintf(Name, ARRAYSIZE(Name), L"%ls\\System32\\", SharedUserData->NtSystemRoot);
    Directory = wcslen(Name);
    wcscat(Name, L"winmm.dll");

    if (!VirtualProtect(&__imp_LdrLoadDll, sizeof(PVOID), PAGE_READWRITE, &Protect))
    {
        printf("FAIL loadlib: VirtualProtect\n");
        return 1;
    }
    g_LoadDll = __imp_LdrLoadDll;
    __imp_LdrLoadDll = LoadDll;

    /* The flags of Windows 8 and later */
    if (Major > 6 || (Major == 6 && Minor >= 2))
    {
        CHECK(LoadLibraryW(L"version.dll") == NULL && g_Flags);
    }

    Peb->OSMajorVersion = 6;
    Peb->OSMinorVersion = 1;

    /* The default search path */
    CHECK(LoadLibraryA("version.dll") != NULL && !g_Flags && g_Path[0] == L'\0');
    SetLastError(0);
    CHECK(LoadLibraryW(L"ntcrt_no_such.dll") == NULL && !g_Flags && GetLastError() == ERROR_MOD_NOT_FOUND);

    /* The directory of the name before the default search path */
    CHECK(LoadLibraryExW(Name, NULL, LOAD_WITH_ALTERED_SEARCH_PATH) != NULL && !g_Flags &&
          wcsncmp(g_Path, Name, Directory) == 0 && g_Path[Directory] == L';' &&
          wcslen(g_Path + Directory + 1) == DefaultPath->Length / sizeof(WCHAR) &&
          wcsncmp(g_Path + Directory + 1, DefaultPath->Buffer, DefaultPath->Length / sizeof(WCHAR)) == 0);
    CHECK(LoadLibraryExW(L"winmm.dll", NULL, LOAD_WITH_ALTERED_SEARCH_PATH) != NULL && !g_Flags &&
          g_Path[0] == L'\0');

    /* The flags of the search without a path of Windows 8 are not supported */
    SetLastError(0);
    CHECK(LoadLibraryExW(L"version.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32) == NULL &&
          GetLastError() == ERROR_NOT_SUPPORTED);

    Peb->OSMajorVersion = Major;
    Peb->OSMinorVersion = Minor;
    __imp_LdrLoadDll = g_LoadDll;

    if (!Failed)
    {
        printf("loadlib: ok\n");
    }
    return Failed;
}
