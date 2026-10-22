/*
 * dlopen, dlsym, dlclose, dlerror and dladdr of <dlfcn.h> by the loader of ntdll, as dlfcn-win32 does them on kernel32
 * but for these:
 *   - dlerror is of the thread, and only dlerror clears it (POSIX): a dlopen, dlsym or dlclose that succeeds keeps the
 *     error of an earlier call that dlerror did not return.
 *   - A module that dlopen loaded with RTLD_LOCAL leaves the local list when it is unloaded, not at its first dlclose.
 *   - The messages are those of the common errors in English, there is no FormatMessage.
 */

#include "NtCRT.h"

#include <dlfcn.h>
#include <stdio.h>

/* Win32 SEM_FAILCRITICALERRORS of the thread: no hard error box for a DLL that cannot be loaded */
#define NTCRT_ERRORMODE_FAILCRITICALERRORS 0x10
EXTERN_C NTSYSAPI NTSTATUS NTAPI RtlSetThreadErrorMode(_In_ ULONG NewMode, _Out_opt_ PULONG OldMode);
EXTERN_C NTSYSAPI ULONG NTAPI RtlGetThreadErrorMode(VOID);

EXTERN_C HMODULE WINAPI _K32_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

/* The modules dlopen loaded with RTLD_LOCAL */
static RTL_SRWLOCK g_LocalLock = RTL_SRWLOCK_INIT;
static PVOID* g_Locals;
static ULONG g_LocalCount, g_LocalCapacity;

static
BOOL
_NtCRT_IsLocal(
    _In_ PVOID Base)
{
    BOOL Found = FALSE;

    RtlAcquireSRWLockShared(&g_LocalLock);
    for (ULONG i = 0; i < g_LocalCount && !Found; i++)
    {
        Found = g_Locals[i] == Base;
    }
    RtlReleaseSRWLockShared(&g_LocalLock);
    return Found;
}

/* Add (Local) or remove a module, FALSE when out of memory */
static
BOOL
_NtCRT_SetLocal(
    _In_ PVOID Base,
    _In_ BOOL Local)
{
    BOOL Done = TRUE;
    ULONG i;

    RtlAcquireSRWLockExclusive(&g_LocalLock);
    for (i = 0; i < g_LocalCount && g_Locals[i] != Base; i++)
    {
    }
    if (!Local && i < g_LocalCount)
    {
        g_Locals[i] = g_Locals[--g_LocalCount];
    } else if (Local && i == g_LocalCount)
    {
        if (g_LocalCount == g_LocalCapacity)
        {
            ULONG Capacity = g_LocalCapacity * 2 + 8;
            PVOID* Locals = (PVOID*)(g_Locals == NULL
                                         ? RtlAllocateHeap(NTCRT_HEAP, 0, Capacity * sizeof(PVOID))
                                         : RtlReAllocateHeap(NTCRT_HEAP, 0, g_Locals, Capacity * sizeof(PVOID)));
            if (Locals != NULL)
            {
                g_Locals = Locals;
                g_LocalCapacity = Capacity;
            }
        }
        Done = g_LocalCount < g_LocalCapacity;
        if (Done)
        {
            g_Locals[g_LocalCount++] = Base;
        }
    }
    RtlReleaseSRWLockExclusive(&g_LocalLock);
    return Done;
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
    char Number[32];
    size_t Size;

    for (size_t i = 0; i < sizeof(Texts) / sizeof(Texts[0]) && Text == NULL; i++)
    {
        Text = Texts[i].Error == Error ? Texts[i].Text : NULL;
    }
    if (Text == NULL)
    {
        snprintf(Number, sizeof(Number), "Error %lu.", Error);
        Text = Number;
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Ptd->DlerrorBuffer);
    Size = strlen(Argument) + strlen(Text) + 5;
    Ptd->DlerrorBuffer = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, Size);
    if (Ptd->DlerrorBuffer != NULL)
    {
        snprintf(Ptd->DlerrorBuffer, Size, "\"%s\": %s", Argument, Text);
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
    /* A module loaded now is local with RTLD_LOCAL, one loaded before becomes global without it */
    if (Loaded != Base && (mode & RTLD_LOCAL) != 0 && !_NtCRT_SetLocal(Base, TRUE))
    {
        LdrUnloadDll(Base);
        _NtCRT_DlError(file, ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (Loaded == Base && (mode & RTLD_LOCAL) == 0)
    {
        _NtCRT_SetLocal(Base, FALSE);
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
    _In_ const char* Name)
{
    PLIST_ENTRY Head = &NtCurrentPeb()->Ldr->InLoadOrderModuleList;
    PVOID Cookie, Address = NULL;
    BOOL Searching = After == NULL;
    NTSTATUS Status;

    if (After == NULL && (Address = _NtCRT_DlExport(_NtCRT_ProgramBase(), Name, &Status)) != NULL)
    {
        return Address;
    }
    LdrLockLoaderLock(LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, NULL, &Cookie);
    for (PLIST_ENTRY Entry = Head->Flink; Entry != Head && Address == NULL; Entry = Entry->Flink)
    {
        PVOID Base = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks)->DllBase;

        if (Searching && Base != _NtCRT_ProgramBase() && !_NtCRT_IsLocal(Base))
        {
            Address = _NtCRT_DlExport(Base, Name, &Status);
        }
        Searching |= Base == After;
    }
    LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, Cookie);
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
        Address = _NtCRT_DlSearch(Caller, name);
    } else if (handle == RTLD_DEFAULT || handle == _NtCRT_ProgramBase())
    {
        Address = _NtCRT_DlSearch(NULL, name);
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
    PVOID Base;

    if (handle == _NtCRT_ProgramBase())
    {
        return 0;
    }
    Status = LdrUnloadDll(handle);
    if (!NT_SUCCESS(Status))
    {
        char Argument[2 + 2 * sizeof(void*) + 1];

        snprintf(Argument,
                 sizeof(Argument),
                 "0x%0*llX",
                 (int)(2 * sizeof(void*)),
                 (unsigned long long)(ULONG_PTR)handle);
        _NtCRT_DlError(Argument, RtlNtStatusToDosError(Status));
        return 1;
    }
    /* A local module that is gone, whose address another module may take */
    if (RtlPcToFileHeader(handle, &Base) == NULL)
    {
        _NtCRT_SetLocal(handle, FALSE);
    }
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
    PVOID Base, Cookie;
    ULONG_PTR Offset;

    if (RtlPcToFileHeader((PVOID)addr, &Base) == NULL)
    {
        return 0;
    }
    info->dli_fbase = Base;
    info->dli_sname = NULL;
    info->dli_saddr = NULL;

    /* The path of the module */
    RtlFreeHeap(NTCRT_HEAP, 0, Ptd->DladdrName);
    Ptd->DladdrName = NULL;
    LdrLockLoaderLock(LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, NULL, &Cookie);
    for (PLIST_ENTRY Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY Module = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        if (Module->DllBase == Base)
        {
            PWSTR Path = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, Module->FullDllName.Length + sizeof(WCHAR));

            if (Path != NULL)
            {
                memcpy(Path, Module->FullDllName.Buffer, Module->FullDllName.Length);
                Path[Module->FullDllName.Length / sizeof(WCHAR)] = L'\0';
                Ptd->DladdrName = _NtCRT_WideToFile(Path);
                RtlFreeHeap(NTCRT_HEAP, 0, Path);
            }
            break;
        }
    }
    LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, Cookie);
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
    return 1;
}

NTCRT_IMPORT_ONLY(dlopen, dlopen);
NTCRT_IMPORT_ONLY(dlclose, dlclose);
NTCRT_IMPORT_ONLY(dlsym, dlsym);
NTCRT_IMPORT_ONLY(dlerror, dlerror);
NTCRT_IMPORT_ONLY(dladdr, dladdr);
