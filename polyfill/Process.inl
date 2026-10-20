#pragma once

#include <KNSoft/NDK/NDK.inl>

FORCEINLINE
BOOL
WINAPI
_CRT_InitializeCriticalSectionEx(
    _Out_ LPCRITICAL_SECTION lpCriticalSection,
    _In_ DWORD dwSpinCount,
    _In_ DWORD Flags)
{
#if _CRT_NTDDI_MIN >= NTDDI_VISTA
    return _Inline_InitializeCriticalSectionEx(lpCriticalSection, dwSpinCount, Flags);
#else
    /* So far, none of the calls from CRT use Flags */
    ASSERT(Flags == 0);
    return _CRT_InitializeCriticalSectionAndSpinCount(lpCriticalSection, dwSpinCount);
#endif
}

FORCEINLINE
BOOL
WINAPI
_CRT_FreeLibrary(
    _In_ HMODULE hLibModule)
{
    ASSERT(!LDR_IS_RESOURCE(hLibModule));
    return NT_SUCCESS(LdrUnloadDll(hLibModule));
}

/*
 * LoadLibraryEx before Windows 8, whose LdrLoadDll takes a search path and no flags: the default path (NULL), or for
 * LOAD_WITH_ALTERED_SEARCH_PATH and a name with a directory, that directory before the default path of the process
 */
static
__attribute__((noinline))
HMODULE
WINAPI
_CRT_LoadLibraryPath(
    _In_ PUNICODE_STRING DllName,
    _In_ DWORD dwFlags)
{
    PCUNICODE_STRING DefaultPath = &NtCurrentPeb()->ProcessParameters->DllPath;
    USHORT Directory = 0;
    PWSTR Path = NULL;
    ULONG LoadFlags = 0;
    PVOID DllBase;
    NTSTATUS Status;

    if (dwFlags & LOAD_WITH_ALTERED_SEARCH_PATH)
    {
        Directory = DllName->Length / sizeof(WCHAR);
        while (Directory != 0 && DllName->Buffer[Directory - 1] != L'\\' && DllName->Buffer[Directory - 1] != L'/')
        {
            Directory--;
        }
    }
    if (Directory != 0)
    {
        Path = (PWSTR)RtlAllocateHeap(RtlProcessHeap(),
                                      0,
                                      (Directory + 2) * sizeof(WCHAR) + DefaultPath->Length);
        if (Path == NULL)
        {
            _Inline_BaseSetLastNTError(STATUS_NO_MEMORY);
            return NULL;
        }
        RtlCopyMemory(Path, DllName->Buffer, Directory * sizeof(WCHAR));
        Path[Directory] = L';';
        RtlCopyMemory(Path + Directory + 1, DefaultPath->Buffer, DefaultPath->Length);
        Path[Directory + 1 + DefaultPath->Length / sizeof(WCHAR)] = UNICODE_NULL;
    }

    Status = LdrLoadDll(Path, &LoadFlags, DllName, &DllBase);
    if (Path != NULL)
    {
        RtlFreeHeap(RtlProcessHeap(), 0, Path);
    }
    if (NT_SUCCESS(Status))
    {
        return (HMODULE)DllBase;
    }
    _Inline_BaseSetLastNTError(Status);
    return NULL;
}

FORCEINLINE
HMODULE
WINAPI
_CRT_LoadLibraryExW(
    _In_ LPCWSTR lpLibFileName,
    _Reserved_ HANDLE hFile,
    _In_ DWORD dwFlags)
{
    NTSTATUS Status;
    UNICODE_STRING DllName;
    ULONG LoadFlags = 0;
    PVOID DllBase;

    if (lpLibFileName == NULL || hFile != NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto _Fail;
    }

    if ((dwFlags & ~(LOAD_WITH_ALTERED_SEARCH_PATH |
                     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                     LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                     LOAD_LIBRARY_SEARCH_SYSTEM32)) != 0)
    {
        _Inline_SetLastError(ERROR_NOT_SUPPORTED);
        return NULL;
    }

    Status = _Inline_RtlInitUnicodeStringEx(&DllName, lpLibFileName);
    if (!NT_SUCCESS(Status))
    {
        goto _Fail;
    }
    while (DllName.Length != 0 && DllName.Buffer[DllName.Length / sizeof(WCHAR) - 1] == L' ')
    {
        DllName.Length -= sizeof(WCHAR);
    }
    if (DllName.Length == 0)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto _Fail;
    }

    if (!IS_NT_VERSION_GE(NT_VERSION_WIN8))
    {
        /* LdrLoadDll before Windows 8 takes a search path only, NULL for the default */
        if ((dwFlags & ~LOAD_WITH_ALTERED_SEARCH_PATH) != 0)
        {
            _Inline_SetLastError(ERROR_NOT_SUPPORTED);
            return NULL;
        }
        return _CRT_LoadLibraryPath(&DllName, dwFlags);
    }

    /* The supported Win32 flags have identical values to their LDR_PATH_* counterparts. */
    Status = LdrLoadDll((PCWSTR)(ULONG_PTR)(LDR_PATH_IS_FLAGS | dwFlags),
                        &LoadFlags,
                        &DllName,
                        &DllBase);
    if (NT_SUCCESS(Status))
    {
        return (HMODULE)DllBase;
    }

_Fail:
    _Inline_BaseSetLastNTError(Status);
    return NULL;
}

FORCEINLINE
FARPROC
WINAPI
_CRT_GetProcAddress(
    _In_ HMODULE hModule,
    _In_ LPCSTR lpProcName)
{
    NTSTATUS Status;
    ANSI_STRING ProcName, *Name;
    ULONG ProcOridinal;
    PVOID Address;

    if ((UINT_PTR)lpProcName > MAXWORD)
    {
        Name = &ProcName;
        _Inline_RtlInitAnsiString(Name, lpProcName);
        ProcOridinal = 0;
    } else
    {
        Name = NULL;
        ProcOridinal = (ULONG)(ULONG_PTR)lpProcName;
    }
    Status = LdrGetProcedureAddress(hModule, Name, ProcOridinal, &Address);
    if (NT_SUCCESS(Status))
    {
        return (FARPROC)Address;
    }

    _Inline_BaseSetLastNTError(Status);
    return NULL;
}

/* Use ntdll UEH instead of kernel32 */

/* CRT did not ask for the return value of SetUnhandledExceptionFilter */
FORCEINLINE
VOID
WINAPI
_CRT_SetUnhandledExceptionFilter(
    _In_opt_ LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
    /* C28023: _Function_class_ annotation warning */
#pragma warning(disable: 28023)
    RtlSetUnhandledExceptionFilter(lpTopLevelExceptionFilter);
#pragma warning(default: 28023)
}

#define SetUnhandledExceptionFilter _CRT_SetUnhandledExceptionFilter
#define UnhandledExceptionFilter RtlUnhandledExceptionFilter

#define GetCurrentThreadId _Inline_GetCurrentThreadId
#define GetCurrentProcessId _Inline_GetCurrentProcessId
#define GetCurrentThread _Inline_GetCurrentThread
#define GetCurrentProcess _Inline_GetCurrentProcess
#define GetEnvironmentStringsW _Inline_GetEnvironmentStringsW
#define SetEnvironmentVariableW _Inline_SetEnvironmentVariableW
#define FreeEnvironmentStringsW _Inline_FreeEnvironmentStringsW
#define GetStartupInfoW _Inline_GetStartupInfoW
#define GetModuleHandleW _Inline_GetModuleHandleW
#define GetModuleFileNameW _Inline_GetModuleFileNameW
#define GetModuleHandleExW _Inline_GetModuleHandleExW
#define LoadLibraryExW _CRT_LoadLibraryExW
#define GetProcAddress _CRT_GetProcAddress
#define FreeLibrary _CRT_FreeLibrary
#define IsDebuggerPresent _Inline_IsDebuggerPresent
#define IsThreadAFiber _Inline_IsThreadAFiber
#define ExitProcess _Inline_ExitProcess
#define TerminateProcess _Inline_TerminateProcess
#define RaiseException _Inline_RaiseException

#define TlsAlloc _Inline_TlsAlloc
#define TlsFree _Inline_TlsFree

/* TODO: No FLS inline implementations, fallback to TLS */
#define FlsAlloc(callback) _Inline_TlsAlloc()
#define FlsFree _Inline_TlsFree
#define FlsGetValue _Inline_TlsGetValue
#define FlsSetValue _Inline_TlsSetValue

#define GetLastError _Inline_GetLastError
#define SetLastError _Inline_SetLastError

#define QueryPerformanceCounter _Inline_QueryPerformanceCounter

#define EncodePointer _Inline_EncodePointer
#define DecodePointer _Inline_DecodePointer
#define InitializeCriticalSectionEx _CRT_InitializeCriticalSectionEx
#define InitializeCriticalSectionAndSpinCount _Inline_InitializeCriticalSectionAndSpinCount
#define DeleteCriticalSection _Inline_DeleteCriticalSection
#define EnterCriticalSection _Inline_EnterCriticalSection
#define LeaveCriticalSection _Inline_LeaveCriticalSection
