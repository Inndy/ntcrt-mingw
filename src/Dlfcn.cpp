/*
 * dlopen, dlsym, dlclose, dlerror and dladdr of <dlfcn.h> by the loader of ntdll, as dlfcn-win32 does them on kernel32
 * but for these:
 *   - dlerror is of the thread, and only dlerror clears it (POSIX): a dlopen, dlsym or dlclose that succeeds keeps the
 *     error of an earlier call that dlerror did not return.
 *   - A module that dlopen loaded with RTLD_LOCAL stays local until each of its dlopen has a dlclose, not until the
 *     first dlclose.
 *   - The messages are those of the common errors in English, there is no FormatMessage.
 */

#include "NtCRT.h"

#include <dlfcn.h>
#include <string.h>

/* Win32 SEM_FAILCRITICALERRORS of the thread: no hard error box for a DLL that cannot be loaded */
#define NTCRT_ERRORMODE_FAILCRITICALERRORS 0x10
EXTERN_C NTSYSAPI NTSTATUS NTAPI RtlSetThreadErrorMode(_In_ ULONG NewMode, _Out_opt_ PULONG OldMode);
EXTERN_C NTSYSAPI ULONG NTAPI RtlGetThreadErrorMode(VOID);

EXTERN_C HMODULE WINAPI _K32_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

EXTERN_C NTSYSAPI NTSTATUS NTAPI LdrAddRefDll(_In_ ULONG Flags, _In_ PVOID DllHandle);

/*
 * The modules that dlopen opened and dlclose did not close as often: the opens, and whether the module is local
 * (_NtCRT_DlOpened). The lock is never held across a call of the loader, and is taken under the loader lock (a
 * DllMain that calls dlopen), never the reverse.
 */
typedef struct _NTCRT_DLOPEN
{
    PVOID Base;
    ULONG Opens;
    BOOLEAN Local;
} NTCRT_DLOPEN;

static RTL_SRWLOCK g_OpenLock = RTL_SRWLOCK_INIT;
static NTCRT_DLOPEN* g_Opens;
static ULONG g_OpenCount, g_OpenCapacity;

/* The index of Base in g_Opens, g_OpenCount if none, under g_OpenLock */
static
ULONG
_NtCRT_DlFind(
    _In_ PVOID Base)
{
    ULONG i;

    for (i = 0; i < g_OpenCount && g_Opens[i].Base != Base; i++)
    {
    }
    return i;
}

static
BOOL
_NtCRT_IsLocal(
    _In_ PVOID Base)
{
    BOOL Local;
    ULONG i;

    RtlAcquireSRWLockShared(&g_OpenLock);
    i = _NtCRT_DlFind(Base);
    Local = i < g_OpenCount && g_Opens[i].Local;
    RtlReleaseSRWLockShared(&g_OpenLock);
    return Local;
}

/*
 * An open of dlopen, FALSE when out of memory. A module is local while it is opened only with RTLD_LOCAL and the
 * first open loaded it: a global open makes it global, whichever comes first.
 */
static
BOOL
_NtCRT_DlOpened(
    _In_ PVOID Base,
    _In_ BOOL Local,
    _In_ BOOL LoadedBefore)
{
    ULONG i;

    RtlAcquireSRWLockExclusive(&g_OpenLock);
    i = _NtCRT_DlFind(Base);
    if (i == g_OpenCount)
    {
        if (g_OpenCount == g_OpenCapacity)
        {
            ULONG Capacity = g_OpenCapacity * 2 + 8;
            NTCRT_DLOPEN* Opens =
                (NTCRT_DLOPEN*)(g_Opens == NULL
                                    ? RtlAllocateHeap(NTCRT_HEAP, 0, Capacity * sizeof(NTCRT_DLOPEN))
                                    : RtlReAllocateHeap(NTCRT_HEAP, 0, g_Opens, Capacity * sizeof(NTCRT_DLOPEN)));
            if (Opens == NULL)
            {
                RtlReleaseSRWLockExclusive(&g_OpenLock);
                return FALSE;
            }
            g_Opens = Opens;
            g_OpenCapacity = Capacity;
        }
        g_Opens[g_OpenCount].Base = Base;
        g_Opens[g_OpenCount].Opens = 0;
        g_Opens[g_OpenCount++].Local = (BOOLEAN)(Local && !LoadedBefore);
    }
    g_Opens[i].Opens++;
    g_Opens[i].Local &= (BOOLEAN)Local;
    RtlReleaseSRWLockExclusive(&g_OpenLock);
    return TRUE;
}

/* A dlclose that unloaded Base once: the module leaves the list at the last, though another module may keep it */
static
void
_NtCRT_DlClosed(
    _In_ PVOID Base)
{
    ULONG i;

    RtlAcquireSRWLockExclusive(&g_OpenLock);
    i = _NtCRT_DlFind(Base);
    if (i < g_OpenCount && --g_Opens[i].Opens == 0)
    {
        g_Opens[i] = g_Opens[--g_OpenCount];
    }
    RtlReleaseSRWLockExclusive(&g_OpenLock);
}

/* The hexadecimal digits of Value into Buffer, Digits of them, no snprintf that would link the printf engine */
static
char*
_NtCRT_DlHex(
    _Out_writes_(Digits) char* Buffer,
    _In_ ULONG_PTR Value,
    _In_ int Digits)
{
    for (int i = Digits - 1; i >= 0; i--, Value >>= 4)
    {
        Buffer[i] = "0123456789ABCDEF"[Value & 15];
    }
    return Buffer + Digits;
}

/* The error of the thread for dlerror: "\"Argument\": text", as dlfcn-win32 writes it with FormatMessage */
static
void
_NtCRT_DlError(
    _In_ const char* Argument,
    _In_ ULONG Error)
{
    static const struct
    {
        ULONG Error;
        const char* Text;
    } Texts[] = {
        { ERROR_INVALID_HANDLE, "The handle is invalid." },
        { ERROR_NOT_ENOUGH_MEMORY, "Not enough memory resources are available to process this command." },
        { ERROR_INVALID_PARAMETER, "The parameter is incorrect." },
        { ERROR_MOD_NOT_FOUND, "The specified module could not be found." },
        { ERROR_PROC_NOT_FOUND, "The specified procedure could not be found." },
        { ERROR_BAD_EXE_FORMAT, "The module is not a valid Win32 application." },
        { ERROR_DLL_INIT_FAILED, "A dynamic link library (DLL) initialization routine failed." },
        { ERROR_NO_UNICODE_TRANSLATION,
          "No mapping for the Unicode character exists in the target multi-byte code page." },
    };
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();
    const char* Text = NULL;
    char Number[32], *p;
    size_t ArgumentLength, TextLength;

    for (size_t i = 0; i < sizeof(Texts) / sizeof(Texts[0]) && Text == NULL; i++)
    {
        Text = Texts[i].Error == Error ? Texts[i].Text : NULL;
    }
    if (Text == NULL)
    {
        /* "Error <decimal>." */
        p = Number + sizeof(Number);
        *--p = '\0';
        *--p = '.';
        do
        {
            *--p = (char)('0' + Error % 10);
        } while ((Error /= 10) != 0);
        memcpy(p - 6, "Error ", 6);
        Text = p - 6;
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Ptd->DlerrorBuffer);
    ArgumentLength = strlen(Argument);
    TextLength = strlen(Text);
    Ptd->DlerrorBuffer = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, ArgumentLength + TextLength + 5);
    if ((p = Ptd->DlerrorBuffer) != NULL)
    {
        *p++ = '"';
        memcpy(p, Argument, ArgumentLength);
        p += ArgumentLength;
        memcpy(p, "\": ", 3);
        memcpy(p + 3, Text, TextLength + 1);
    }
    Ptd->DlerrorPending = TRUE;
}

static inline PVOID _NtCRT_ProgramBase(void) { return NtCurrentPeb()->ImageBaseAddress; }

NTCRT_API void* dlopen(const char* file, int mode)
{
    UNICODE_STRING Name;
    PWSTR WideName;
    PVOID Base, Loaded = NULL;
    ULONG ErrorMode, Error;

    if (file == NULL)
    {
        return _NtCRT_ProgramBase();
    }
    WideName = _NtCRT_FileToWide(file);
    if (WideName == NULL)
    {
        _NtCRT_DlError(file, NTCRT_ERRNO == EILSEQ ? ERROR_NO_UNICODE_TRANSLATION : ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    for (PWSTR p = WideName; *p != L'\0'; p++)
    {
        *p = *p == L'/' ? L'\\' : *p;
    }

    /* Whether the module was loaded before, for RTLD_LOCAL */
    RtlInitUnicodeString(&Name, WideName);
    if (!NT_SUCCESS(LdrGetDllHandle(NULL, NULL, &Name, &Loaded)))
    {
        Loaded = NULL;
    }
    ErrorMode = RtlGetThreadErrorMode();
    RtlSetThreadErrorMode(ErrorMode | NTCRT_ERRORMODE_FAILCRITICALERRORS, NULL);
    Base = _K32_LoadLibraryExW(WideName, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    Error = RtlGetLastWin32Error();
    RtlSetThreadErrorMode(ErrorMode, NULL);
    RtlFreeHeap(NTCRT_HEAP, 0, WideName);
    if (Base == NULL)
    {
        char* Converted = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, strlen(file) + 1);

        /* The name that was loaded, with backslashes */
        if (Converted != NULL)
        {
            strcpy(Converted, file);
            for (char* p = Converted; *p != '\0'; p++)
            {
                *p = *p == '/' ? '\\' : *p;
            }
        }
        _NtCRT_DlError(Converted != NULL ? Converted : file, Error);
        RtlFreeHeap(NTCRT_HEAP, 0, Converted);
        return NULL;
    }
    /* RTLD_LOCAL hides a module that this dlopen loads, or that it opened before with RTLD_LOCAL only */
    if (!_NtCRT_DlOpened(Base, (mode & RTLD_LOCAL) != 0, Loaded == Base))
    {
        LdrUnloadDll(Base);
        _NtCRT_DlError(file, ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    return Base;
}

/* The export Name of the module at Base, NULL if none */
static
void*
_NtCRT_DlExport(
    _In_ PVOID Base,
    _In_ const char* Name,
    _Out_ NTSTATUS* Status)
{
    ANSI_STRING String;
    PVOID Address;

    RtlInitAnsiString(&String, Name);
    *Status = LdrGetProcedureAddress(Base, &String, 0, &Address);
    return NT_SUCCESS(*Status) ? Address : NULL;
}

/* The search of RTLD_DEFAULT and of the program (After NULL), and of RTLD_NEXT (After the module of the caller) */
static
void*
_NtCRT_DlSearch(
    _In_opt_ PVOID After,
    _In_ const char* Name,
    _Inout_ NTSTATUS* Error)
{
    PLIST_ENTRY Head = &NtCurrentPeb()->Ldr->InLoadOrderModuleList, Entry;
    PVOID Cookie, Address = NULL, *Bases;
    BOOL Searching = After == NULL;
    ULONG Count = 0, i;
    NTSTATUS Status;

    if (After == NULL && (Address = _NtCRT_DlExport(_NtCRT_ProgramBase(), Name, &Status)) != NULL)
    {
        return Address;
    }

    /*
     * The modules in load order, each referenced under the loader lock, are searched after it: the export of a
     * forwarder makes LdrGetProcedureAddress load its target, which under the loader lock deadlocks with another
     * thread that loads a module (the parallel loader of Windows 10). A module that is closed meanwhile stays until
     * its reference is released, outside all locks.
     */
    LdrLockLoaderLock(LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, NULL, &Cookie);
    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        Count++;
    }
    Bases = (PVOID*)RtlAllocateHeap(NTCRT_HEAP, 0, Count * sizeof(PVOID));
    Count = 0;
    for (Entry = Head->Flink; Entry != Head && Bases != NULL; Entry = Entry->Flink)
    {
        PVOID Base = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks)->DllBase;

        if (Searching && Base != _NtCRT_ProgramBase() && NT_SUCCESS(LdrAddRefDll(0, Base)))
        {
            Bases[Count++] = Base;
        }
        Searching |= Base == After;
    }
    LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, Cookie);
    if (Bases == NULL)
    {
        *Error = STATUS_NO_MEMORY;
        return NULL;
    }
    for (i = 0; i < Count && Address == NULL; i++)
    {
        if (!_NtCRT_IsLocal(Bases[i]))
        {
            Address = _NtCRT_DlExport(Bases[i], Name, &Status);
        }
    }
    for (i = 0; i < Count; i++)
    {
        LdrUnloadDll(Bases[i]);
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Bases);
    return Address;
}

NTCRT_API __attribute__((noinline)) void* dlsym(void* handle, const char* name)
{
    void* Address;
    NTSTATUS Status = STATUS_PROCEDURE_NOT_FOUND;

    if (handle == RTLD_NEXT)
    {
        PVOID Caller;

        if (RtlPcToFileHeader(__builtin_return_address(0), &Caller) == NULL)
        {
            _NtCRT_DlError(name, ERROR_INVALID_PARAMETER);
            return NULL;
        }
        Address = _NtCRT_DlSearch(Caller, name, &Status);
    } else if (handle == RTLD_DEFAULT || handle == _NtCRT_ProgramBase())
    {
        Address = _NtCRT_DlSearch(NULL, name, &Status);
    } else
    {
        Address = _NtCRT_DlExport(handle, name, &Status);
    }
    if (Address == NULL)
    {
        _NtCRT_DlError(name, RtlNtStatusToDosError(Status));
    }
    return Address;
}

NTCRT_API int dlclose(void* handle)
{
    NTSTATUS Status;

    if (handle == _NtCRT_ProgramBase())
    {
        return 0;
    }
    Status = LdrUnloadDll(handle);
    if (!NT_SUCCESS(Status))
    {
        char Argument[2 + 2 * sizeof(void*) + 1];

        Argument[0] = '0';
        Argument[1] = 'x';
        *_NtCRT_DlHex(Argument + 2, (ULONG_PTR)handle, 2 * sizeof(void*)) = '\0';
        _NtCRT_DlError(Argument, RtlNtStatusToDosError(Status));
        return 1;
    }
    _NtCRT_DlClosed(handle);
    return 0;
}

NTCRT_API char* dlerror(void)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();

    if (!Ptd->DlerrorPending)
    {
        return NULL;
    }
    Ptd->DlerrorPending = FALSE;
    return Ptd->DlerrorBuffer != NULL ? Ptd->DlerrorBuffer : (char*)"Not enough memory for the error.";
}

NTCRT_API int dladdr(const void* addr, Dl_info* info)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();
    PLIST_ENTRY Head = &NtCurrentPeb()->Ldr->InLoadOrderModuleList;
    PIMAGE_NT_HEADERS Headers;
    PIMAGE_DATA_DIRECTORY Directory;
    PVOID Base = NULL, Cookie;
    PWSTR Path = NULL;
    ULONG_PTR Offset;

    /* The module of the address and its path, referenced so that it stays while its exports are read after the lock */
    LdrLockLoaderLock(LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, NULL, &Cookie);
    for (PLIST_ENTRY Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY Module = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        if ((ULONG_PTR)addr - (ULONG_PTR)Module->DllBase < Module->SizeOfImage)
        {
            if (Module->DllBase == _NtCRT_ProgramBase() || NT_SUCCESS(LdrAddRefDll(0, Module->DllBase)))
            {
                Base = Module->DllBase;
                Path = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, Module->FullDllName.Length + sizeof(WCHAR));
                if (Path != NULL)
                {
                    memcpy(Path, Module->FullDllName.Buffer, Module->FullDllName.Length);
                    Path[Module->FullDllName.Length / sizeof(WCHAR)] = L'\0';
                }
            }
            break;
        }
    }
    LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, Cookie);
    if (Base == NULL)
    {
        return 0;
    }
    info->dli_fbase = Base;
    info->dli_sname = NULL;
    info->dli_saddr = NULL;
    RtlFreeHeap(NTCRT_HEAP, 0, Ptd->DladdrName);
    Ptd->DladdrName = Path != NULL ? _NtCRT_WideToFile(Path) : NULL;
    RtlFreeHeap(NTCRT_HEAP, 0, Path);
    info->dli_fname = Ptd->DladdrName;

    /* The nearest named export at or below the address, not a forwarder */
    Headers = (PIMAGE_NT_HEADERS)((PUCHAR)Base + ((PIMAGE_DOS_HEADER)Base)->e_lfanew);
    Directory = &Headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    Offset = (ULONG_PTR)addr - (ULONG_PTR)Base;
    if (Headers->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXPORT && Directory->Size != 0)
    {
        PIMAGE_EXPORT_DIRECTORY Exports = (PIMAGE_EXPORT_DIRECTORY)((PUCHAR)Base + Directory->VirtualAddress);
        PULONG Functions = (PULONG)((PUCHAR)Base + Exports->AddressOfFunctions);
        PULONG Names = (PULONG)((PUCHAR)Base + Exports->AddressOfNames);
        PUSHORT Ordinals = (PUSHORT)((PUCHAR)Base + Exports->AddressOfNameOrdinals);
        ULONG Best = 0;

        for (ULONG i = 0; i < Exports->NumberOfNames; i++)
        {
            ULONG Rva = Ordinals[i] < Exports->NumberOfFunctions ? Functions[Ordinals[i]] : 0;

            if (Rva != 0 && Rva <= Offset && Rva >= Best &&
                (Rva < Directory->VirtualAddress || Rva >= Directory->VirtualAddress + Directory->Size))
            {
                Best = Rva;
                info->dli_sname = (const char*)Base + Names[i];
                info->dli_saddr = (PUCHAR)Base + Rva;
            }
        }
    }
    if (Base != _NtCRT_ProgramBase())
    {
        LdrUnloadDll(Base);
    }
    return 1;
}

NTCRT_IMPORT_ONLY(dlopen, dlopen);
NTCRT_IMPORT_ONLY(dlclose, dlclose);
NTCRT_IMPORT_ONLY(dlsym, dlsym);
NTCRT_IMPORT_ONLY(dlerror, dlerror);
NTCRT_IMPORT_ONLY(dladdr, dladdr);
