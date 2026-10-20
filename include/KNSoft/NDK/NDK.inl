/*
 * KNSoft.NDK inline implementations for MinGW-w64
 *
 * Ported from KNSoft.NDK (MIT, https://github.com/KNSoft/KNSoft.NDK), only the part used by KNSoft.NtCRT.
 */

#pragma once

#include "NDK.h"

#ifdef __cplusplus
#define _NDK_INLINE inline
#else
#define _NDK_INLINE static __inline__
#endif

EXTERN_C_START

#pragma region Rtl

_NDK_INLINE
ULONG
NTAPI
_Inline_RtlNtStatusToDosError(
    _In_ NTSTATUS Status)
{
    NtWriteTeb(LastStatusValue, Status);
    return RtlNtStatusToDosErrorNoTeb(Status);
}

_NDK_INLINE
NTSTATUS
NTAPI
_Inline_RtlGetLastNtStatus(VOID)
{
    return NtReadTeb(LastStatusValue);
}

_NDK_INLINE
ULONG
NTAPI
_Inline_RtlGetLastWin32Error(VOID)
{
    return NtReadTeb(LastErrorValue);
}

_NDK_INLINE
VOID
NTAPI
_Inline_RtlSetLastWin32Error(
    _In_ ULONG Win32Error)
{
    NtWriteTeb(LastErrorValue, Win32Error);
}

_NDK_INLINE
VOID
NTAPI
_Inline_RtlAcquirePebLock(VOID)
{
    RtlEnterCriticalSection(NtCurrentPeb()->FastPebLock);
}

_NDK_INLINE
VOID
NTAPI
_Inline_RtlReleasePebLock(VOID)
{
    RtlLeaveCriticalSection(NtCurrentPeb()->FastPebLock);
}

_NDK_INLINE
VOID
NTAPI
_Inline_RtlInitString(
    _Out_ PSTRING DestinationString,
    _In_opt_z_ PCSZ SourceString)
{
    if (SourceString)
    {
        SIZE_T Size = strlen(SourceString);
        if (Size > MAXUSHORT - sizeof(ANSI_NULL))
        {
            Size = MAXUSHORT - sizeof(ANSI_NULL);
        }
        DestinationString->Length = (USHORT)Size;
        DestinationString->MaximumLength = DestinationString->Length + sizeof(ANSI_NULL);
    } else
    {
        DestinationString->Length = DestinationString->MaximumLength = 0;
    }
    DestinationString->Buffer = (PCHAR)SourceString;
}

_NDK_INLINE
VOID
NTAPI
_Inline_RtlInitAnsiString(
    _Out_ PANSI_STRING DestinationString,
    _In_opt_z_ PCSZ SourceString)
{
    _Inline_RtlInitString(DestinationString, SourceString);
}

_NDK_INLINE
VOID
NTAPI
_Inline_RtlInitUnicodeString(
    _Out_ PUNICODE_STRING DestinationString,
    _In_opt_z_ PCWSTR SourceString)
{
    if (SourceString)
    {
        SIZE_T Size = wcslen(SourceString) * sizeof(WCHAR);
        if (Size > UNICODE_STRING_MAX_BYTES - sizeof(UNICODE_NULL))
        {
            Size = UNICODE_STRING_MAX_BYTES - sizeof(UNICODE_NULL);
        }
        DestinationString->Length = (USHORT)Size;
        DestinationString->MaximumLength = DestinationString->Length + sizeof(UNICODE_NULL);
    } else
    {
        DestinationString->Length = DestinationString->MaximumLength = 0;
    }
    DestinationString->Buffer = (PWCHAR)SourceString;
}

_NDK_INLINE
NTSTATUS
NTAPI
_Inline_RtlInitUnicodeStringEx(
    _Out_ PUNICODE_STRING DestinationString,
    _In_opt_z_ PCWSTR SourceString)
{
    if (SourceString)
    {
        SIZE_T Size = wcslen(SourceString) * sizeof(WCHAR);
        if (Size > (MAXUSHORT & ~1) - sizeof(WCHAR))
        {
            return STATUS_NAME_TOO_LONG;
        }
        DestinationString->Length = (USHORT)Size;
        DestinationString->MaximumLength = (USHORT)Size + sizeof(WCHAR);
    } else
    {
        DestinationString->Length = DestinationString->MaximumLength = 0;
    }
    DestinationString->Buffer = (PWCHAR)SourceString;
    return STATUS_SUCCESS;
}

_NDK_INLINE
NTSTATUS
NTAPI
_Inline_LdrGetDllHandle(
    _In_opt_ PCWSTR DllPath,
    _In_opt_ PULONG DllCharacteristics,
    _In_ PUNICODE_STRING DllName,
    _Out_ PVOID* DllHandle)
{
    return LdrGetDllHandleEx(LDR_GET_DLL_HANDLE_EX_UNCHANGED_REFCOUNT, DllPath, DllCharacteristics, DllName, DllHandle);
}

_NDK_INLINE
BOOLEAN
NTAPI
_Inline_RtlIsProcessorFeaturePresent(
    _In_ ULONG ProcessorFeature)
{
    return ProcessorFeature < PROCESSOR_FEATURE_MAX ? SharedUserData->ProcessorFeatures[ProcessorFeature] : FALSE;
}

_NDK_INLINE
ULONG64
NTAPI
_Inline_RtlGetEnabledExtendedFeatures(
    _In_ ULONG64 FeatureMask)
{
    return SharedUserData->XState.EnabledVolatileFeatures & FeatureMask;
}

#pragma endregion

#pragma region Internal functions

_NDK_INLINE
ULONG
_Inline_BaseSetLastNTError(
    _In_ NTSTATUS Status)
{
    ULONG Error = _Inline_RtlNtStatusToDosError(Status);
    _Inline_RtlSetLastWin32Error(Error);
    return Error;
}

_NDK_INLINE
PVOID
_Inline_BasepMapModuleHandle(
    _In_opt_ HMODULE ModuleHandle,
    _In_ BOOLEAN AsResourceFile)
{
    if (ModuleHandle == NULL)
    {
        return NtCurrentPeb()->ImageBaseAddress;
    }
    return !LDR_IS_RESOURCE(ModuleHandle) || AsResourceFile ? ModuleHandle : NULL;
}

_NDK_INLINE
PLARGE_INTEGER
_Inline_BaseFormatTimeOut(
    _Out_ PLARGE_INTEGER Timeout,
    _In_ ULONG Milliseconds)
{
    if (Milliseconds == INFINITE)
    {
        return NULL;
    }
    Timeout->QuadPart = Int32x32To64(Milliseconds, -10000);
    return Timeout;
}

#pragma endregion

#pragma region Process Environment

_NDK_INLINE
DWORD
WINAPI
_Inline_GetCurrentProcessId(VOID)
{
    return (DWORD)(ULONG_PTR)NtCurrentProcessId();
}

_NDK_INLINE
DWORD
WINAPI
_Inline_GetCurrentThreadId(VOID)
{
    return (DWORD)(ULONG_PTR)NtCurrentThreadId();
}

_NDK_INLINE
HANDLE
WINAPI
_Inline_GetCurrentProcess(VOID)
{
    return NtCurrentProcess();
}

_NDK_INLINE
HANDLE
WINAPI
_Inline_GetCurrentThread(VOID)
{
    return NtCurrentThread();
}

_NDK_INLINE
LPWCH
WINAPI
_Inline_GetEnvironmentStringsW(VOID)
{
    PWCHAR pEnv, p;
    SIZE_T sSize;

    _Inline_RtlAcquirePebLock();
    pEnv = (PWCHAR)NtCurrentPeb()->ProcessParameters->Environment;
    for (p = pEnv; *p != UNICODE_NULL; p += wcslen(p) + 1);
    sSize = (p - pEnv + 1) * sizeof(WCHAR);
    p = (PWCHAR)RtlAllocateHeap(RtlProcessHeap(), 0, sSize);
    if (p)
    {
        memcpy(p, pEnv, sSize);
    } else
    {
        _Inline_BaseSetLastNTError(STATUS_NO_MEMORY);
    }
    _Inline_RtlReleasePebLock();
    return p;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_FreeEnvironmentStringsW(
    _In_ LPWCH penv)
{
    return RtlFreeHeap(RtlProcessHeap(), 0, penv);
}

_NDK_INLINE
BOOL
WINAPI
_Inline_SetEnvironmentVariableW(
    _In_ LPCWSTR lpName,
    _In_opt_ LPCWSTR lpValue)
{
    NTSTATUS Status = RtlSetEnvironmentVar(NULL,
                                           lpName,
                                           wcslen(lpName),
                                           lpValue,
                                           lpValue != NULL ? wcslen(lpValue) : 0);
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

_NDK_INLINE
VOID
WINAPI
_Inline_GetStartupInfoW(
    _Out_ LPSTARTUPINFOW lpStartupInfo)
{
    PRTL_USER_PROCESS_PARAMETERS ProcParam = NtCurrentPeb()->ProcessParameters;
    ULONG WindowFlags;

    lpStartupInfo->cb = sizeof(*lpStartupInfo);
    lpStartupInfo->lpReserved = ProcParam->ShellInfo.Buffer;
    lpStartupInfo->lpDesktop = ProcParam->DesktopInfo.Buffer;
    lpStartupInfo->lpTitle = ProcParam->WindowTitle.Buffer;
    lpStartupInfo->dwX = ProcParam->StartingX;
    lpStartupInfo->dwY = ProcParam->StartingY;
    lpStartupInfo->dwXSize = ProcParam->CountX;
    lpStartupInfo->dwYSize = ProcParam->CountY;
    lpStartupInfo->dwXCountChars = ProcParam->CountCharsX;
    lpStartupInfo->dwYCountChars = ProcParam->CountCharsY;
    lpStartupInfo->dwFillAttribute = ProcParam->FillAttribute;
    WindowFlags = ProcParam->WindowFlags;
    lpStartupInfo->dwFlags = WindowFlags;
    lpStartupInfo->wShowWindow = (WORD)ProcParam->ShowWindowFlags;
    lpStartupInfo->cbReserved2 = ProcParam->RuntimeData.Length;
    lpStartupInfo->lpReserved2 = (LPBYTE)ProcParam->RuntimeData.Buffer;
    if (WindowFlags & (STARTF_USESTDHANDLES | STARTF_USEHOTKEY | STARTF_USEMONITOR))
    {
        lpStartupInfo->hStdInput = ProcParam->StandardInput;
        lpStartupInfo->hStdOutput = ProcParam->StandardOutput;
        lpStartupInfo->hStdError = ProcParam->StandardError;
    }
}

_NDK_INLINE
BOOL
WINAPI
_Inline_IsDebuggerPresent(VOID)
{
    return NtCurrentPeb()->BeingDebugged;
}

#pragma endregion

#pragma region Loader

_NDK_INLINE
HMODULE
WINAPI
_Inline_GetModuleHandleW(
    _In_opt_ LPCWSTR lpModuleName)
{
    NTSTATUS Status;
    UNICODE_STRING DllName;
    PVOID DllHandle;

    if (lpModuleName == NULL)
    {
        return (HMODULE)NtCurrentPeb()->ImageBaseAddress;
    }
    _Inline_RtlInitUnicodeString(&DllName, lpModuleName);
    Status = _Inline_LdrGetDllHandle(NULL, NULL, &DllName, &DllHandle);
    if (NT_SUCCESS(Status))
    {
        return (HMODULE)DllHandle;
    }
    _Inline_BaseSetLastNTError(Status);
    return NULL;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_GetModuleHandleExW(
    _In_ DWORD dwFlags,
    _In_opt_ LPCWSTR lpModuleName,
    _Out_ HMODULE* phModule)
{
    NTSTATUS Status;
    PVOID DllHandle;
    UNICODE_STRING DllName;

    if (phModule != NULL)
    {
        *phModule = NULL;
    } else
    {
        Status = STATUS_INVALID_PARAMETER_2;
        goto _Fail;
    }
    if ((dwFlags & ~(GET_MODULE_HANDLE_EX_FLAG_PIN |
                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT |
                     GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)) ||
        TEST_FLAGS(dwFlags, GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT) ||
        (lpModuleName == NULL && (dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)))
    {
        Status = STATUS_INVALID_PARAMETER_1;
        goto _Fail;
    }
    if (lpModuleName == NULL)
    {
        *phModule = (HMODULE)NtCurrentPeb()->ImageBaseAddress;
        return TRUE;
    }
    if (dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)
    {
        if (RtlPcToFileHeader((PVOID)lpModuleName, &DllHandle) == NULL)
        {
            Status = STATUS_DLL_NOT_FOUND;
            goto _Fail;
        }
    } else
    {
        _Inline_RtlInitUnicodeString(&DllName, lpModuleName);
        Status = _Inline_LdrGetDllHandle(NULL, NULL, &DllName, &DllHandle);
        if (!NT_SUCCESS(Status))
        {
            goto _Fail;
        }
    }
    if (!(dwFlags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))
    {
        Status = LdrAddRefDll((dwFlags & GET_MODULE_HANDLE_EX_FLAG_PIN) ? LDR_ADDREF_DLL_PIN : 0, DllHandle);
        if (!NT_SUCCESS(Status))
        {
            goto _Fail;
        }
    }
    *phModule = (HMODULE)DllHandle;
    return TRUE;

_Fail:
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

_NDK_INLINE
DWORD
WINAPI
_Inline_GetModuleFileNameW(
    _In_opt_ HMODULE hModule,
    _Out_ LPWSTR lpFilename,
    _In_ DWORD nSize)
{
    PVOID DllBase;
    PUNICODE_STRING ImageName;
    BOOLEAN LoaderLocked = FALSE;
    PVOID Cookie = NULL;
    DWORD dwCch;
    PLDR_DATA_TABLE_ENTRY HeadEntry, Entry;

    DllBase = _Inline_BasepMapModuleHandle(hModule, FALSE);
    if (hModule != NULL)
    {
        if (LDR_IS_RESOURCE(hModule))
        {
            _Inline_BaseSetLastNTError(STATUS_DLL_NOT_FOUND);
            return 0;
        }
    } else
    {
        PRTL_PERTHREAD_CURDIR PerThreadCurdir = (PRTL_PERTHREAD_CURDIR)NtReadTeb(NtTib.SubSystemTib);
        if (PerThreadCurdir != NULL && PerThreadCurdir->ImageName != NULL)
        {
            ImageName = PerThreadCurdir->ImageName;
            goto _CopyPath;
        }
    }

    HeadEntry = CONTAINING_RECORD(NtCurrentPeb()->Ldr->InLoadOrderModuleList.Flink,
                                  LDR_DATA_TABLE_ENTRY,
                                  InLoadOrderLinks);
    Entry = HeadEntry;
    LdrLockLoaderLock(LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, NULL, &Cookie);
    LoaderLocked = TRUE;
    do
    {
        if (DllBase == Entry->DllBase)
        {
            ImageName = &Entry->FullDllName;
            goto _CopyPath;
        }
        Entry = CONTAINING_RECORD(Entry->InLoadOrderLinks.Flink, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
    } while (Entry != HeadEntry);
    _Inline_BaseSetLastNTError(STATUS_DLL_NOT_FOUND);
    dwCch = 0;
    goto _Exit;

_CopyPath:
    dwCch = ImageName->Length / sizeof(WCHAR);
    if (dwCch + 1 > nSize)
    {
        dwCch = nSize - 1;
        _Inline_RtlSetLastWin32Error(ERROR_INSUFFICIENT_BUFFER);
    } else
    {
        _Inline_RtlSetLastWin32Error(ERROR_SUCCESS);
    }
    if (nSize != 0)
    {
        memcpy(lpFilename, ImageName->Buffer, dwCch * sizeof(WCHAR));
        lpFilename[dwCch] = UNICODE_NULL;
    } else
    {
        dwCch = 0;
    }

_Exit:
    if (LoaderLocked)
    {
        LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, Cookie);
    }
    return dwCch;
}

#pragma endregion

#pragma region QPC and Time

_NDK_INLINE
BOOL
WINAPI
_Inline_QueryPerformanceCounter(
    _Out_ LARGE_INTEGER* lpPerformanceCount)
{
    RtlQueryPerformanceCounter(lpPerformanceCount);
    return TRUE;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_QueryPerformanceFrequency(
    _Out_ LARGE_INTEGER* lpFrequency)
{
    RtlQueryPerformanceFrequency(lpFrequency);
    return TRUE;
}

_NDK_INLINE
DWORD
WINAPI
_Inline_GetTickCount(VOID)
{
    return (ULONG)((SharedUserData->TickCountMultiplier * SharedUserData->TickCountQuad) >> 24);
}

_NDK_INLINE
ULONGLONG
WINAPI
_Inline_GetTickCount64(VOID)
{
    return (SharedUserData->TickCountMultiplier * SharedUserData->TickCountQuad) >> 24;
}

_NDK_INLINE
VOID
WINAPI
_Inline_GetSystemTimeAsFileTime(
    _Out_ LPFILETIME lpSystemTimeAsFileTime)
{
    /* KSYSTEM_TIME is read as a whole on x64 */
    ULONGLONG Time = *(volatile ULONGLONG*)((ULONG_PTR)SharedUserData + FIELD_OFFSET(KUSER_SHARED_DATA, SystemTime));
    lpSystemTimeAsFileTime->dwLowDateTime = (DWORD)Time;
    lpSystemTimeAsFileTime->dwHighDateTime = (DWORD)(Time >> 32);
}

#pragma endregion

#pragma region Pointer encode / decode

_NDK_INLINE
PVOID
WINAPI
_Inline_EncodePointer(
    _In_opt_ PVOID Ptr)
{
    return RtlEncodePointer(Ptr);
}

_NDK_INLINE
PVOID
WINAPI
_Inline_DecodePointer(
    _In_opt_ PVOID Ptr)
{
    return RtlDecodePointer(Ptr);
}

#pragma endregion

#pragma region Critical Section

_NDK_INLINE
VOID
WINAPI
_Inline_InitializeCriticalSection(
    _Out_ LPCRITICAL_SECTION lpCriticalSection)
{
    RtlInitializeCriticalSection(lpCriticalSection);
}

_NDK_INLINE
BOOL
WINAPI
_Inline_InitializeCriticalSectionAndSpinCount(
    _Out_ LPCRITICAL_SECTION lpCriticalSection,
    _In_ DWORD dwSpinCount)
{
    NTSTATUS Status = RtlInitializeCriticalSectionAndSpinCount(lpCriticalSection, dwSpinCount);
    if (IS_NT_VERSION_GE(NT_VERSION_VISTA) || NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_InitializeCriticalSectionEx(
    _Out_ LPCRITICAL_SECTION lpCriticalSection,
    _In_ DWORD dwSpinCount,
    _In_ DWORD Flags)
{
    NTSTATUS Status = RtlInitializeCriticalSectionEx(lpCriticalSection, dwSpinCount, Flags);
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

_NDK_INLINE
DWORD
WINAPI
_Inline_SetCriticalSectionSpinCount(
    _Inout_ LPCRITICAL_SECTION lpCriticalSection,
    _In_ DWORD dwSpinCount)
{
    return RtlSetCriticalSectionSpinCount(lpCriticalSection, dwSpinCount);
}

_NDK_INLINE
BOOL
WINAPI
_Inline_TryEnterCriticalSection(
    _Inout_ LPCRITICAL_SECTION lpCriticalSection)
{
    return RtlTryEnterCriticalSection(lpCriticalSection);
}

_NDK_INLINE
VOID
WINAPI
_Inline_EnterCriticalSection(
    _Inout_ LPCRITICAL_SECTION lpCriticalSection)
{
    RtlEnterCriticalSection(lpCriticalSection);
}

_NDK_INLINE
VOID
WINAPI
_Inline_LeaveCriticalSection(
    _Inout_ LPCRITICAL_SECTION lpCriticalSection)
{
    RtlLeaveCriticalSection(lpCriticalSection);
}

_NDK_INLINE
VOID
WINAPI
_Inline_DeleteCriticalSection(
    _Inout_ LPCRITICAL_SECTION lpCriticalSection)
{
    RtlDeleteCriticalSection(lpCriticalSection);
}

#pragma endregion

#pragma region TLS

_NDK_INLINE
PVOID*
_Inline_BasepGetTlsExpansionSlots(VOID)
{
    PVOID* Slots = NtReadTeb(TlsExpansionSlots);
    if (Slots == NULL)
    {
        Slots = (PVOID*)RtlAllocateHeap(RtlProcessHeap(), HEAP_ZERO_MEMORY, TLS_EXPANSION_SLOTS * sizeof(PVOID));
        if (Slots != NULL)
        {
            NtWriteTeb(TlsExpansionSlots, Slots);
        }
    }
    return Slots;
}

_NDK_INLINE
DWORD
WINAPI
_Inline_TlsAlloc(VOID)
{
    PPEB Peb = NtCurrentPeb();
    ULONG Index;
    PVOID* Slots;

    _Inline_RtlAcquirePebLock();
    Index = RtlFindClearBitsAndSet(Peb->TlsBitmap, 1, 0);
    if (Index != MAXULONG)
    {
        _Inline_RtlReleasePebLock();
        NtWriteTeb(TlsSlots[Index], NULL);
        return Index;
    }
    _Inline_RtlReleasePebLock();

    Slots = _Inline_BasepGetTlsExpansionSlots();
    if (Slots == NULL)
    {
        goto _Fail;
    }
    _Inline_RtlAcquirePebLock();
    Index = RtlFindClearBitsAndSet(Peb->TlsExpansionBitmap, 1, 0);
    _Inline_RtlReleasePebLock();
    if (Index != MAXULONG)
    {
        Slots[Index] = NULL;
        return Index + TLS_MINIMUM_AVAILABLE;
    }

_Fail:
    _Inline_BaseSetLastNTError(STATUS_NO_MEMORY);
    return TLS_OUT_OF_INDEXES;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_TlsFree(
    _In_ DWORD dwTlsIndex)
{
    PPEB Peb = NtCurrentPeb();
    ULONG Index = dwTlsIndex;
    PRTL_BITMAP Bitmap;

    if (dwTlsIndex >= TLS_MINIMUM_AVAILABLE)
    {
        Index = dwTlsIndex - TLS_MINIMUM_AVAILABLE;
        if (Index >= TLS_EXPANSION_SLOTS)
        {
            goto _Fail;
        }
        Bitmap = Peb->TlsExpansionBitmap;
    } else
    {
        Bitmap = Peb->TlsBitmap;
    }

    _Inline_RtlAcquirePebLock();
    if (RtlAreBitsSet(Bitmap, Index, 1) &&
        NT_SUCCESS(NtSetInformationThread(NtCurrentThread(), ThreadZeroTlsCell, &dwTlsIndex, sizeof(dwTlsIndex))))
    {
        RtlClearBits(Bitmap, Index, 1);
        _Inline_RtlReleasePebLock();
        return TRUE;
    }
    _Inline_RtlReleasePebLock();

_Fail:
    _Inline_BaseSetLastNTError(STATUS_INVALID_PARAMETER);
    return FALSE;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_TlsSetValue(
    _In_ DWORD dwTlsIndex,
    _In_opt_ LPVOID lpTlsValue)
{
    if (dwTlsIndex < TLS_MINIMUM_AVAILABLE)
    {
        NtWriteTeb(TlsSlots[dwTlsIndex], lpTlsValue);
    } else if (dwTlsIndex < TLS_MINIMUM_AVAILABLE + TLS_EXPANSION_SLOTS)
    {
        PVOID* Slots = _Inline_BasepGetTlsExpansionSlots();
        if (Slots == NULL)
        {
            _Inline_BaseSetLastNTError(STATUS_NO_MEMORY);
            return FALSE;
        }
        Slots[dwTlsIndex - TLS_MINIMUM_AVAILABLE] = lpTlsValue;
    } else
    {
        _Inline_BaseSetLastNTError(STATUS_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

_NDK_INLINE
LPVOID
WINAPI
_Inline_TlsGetValue2(
    _In_ DWORD dwTlsIndex)
{
    if (dwTlsIndex < TLS_MINIMUM_AVAILABLE)
    {
        return NtReadTeb(TlsSlots[dwTlsIndex]);
    }
    dwTlsIndex -= TLS_MINIMUM_AVAILABLE;
    if (dwTlsIndex < TLS_EXPANSION_SLOTS)
    {
        PVOID* Slots = NtReadTeb(TlsExpansionSlots);
        if (Slots)
        {
            return Slots[dwTlsIndex];
        }
    }
    return NULL;
}

_NDK_INLINE
LPVOID
WINAPI
_Inline_TlsGetValue(
    _In_ DWORD dwTlsIndex)
{
    if (dwTlsIndex >= TLS_MINIMUM_AVAILABLE + TLS_EXPANSION_SLOTS)
    {
        _Inline_BaseSetLastNTError(STATUS_INVALID_PARAMETER);
        return NULL;
    }
    if (NtReadTeb(LastErrorValue) != ERROR_SUCCESS)
    {
        NtWriteTeb(LastErrorValue, ERROR_SUCCESS);
    }
    return _Inline_TlsGetValue2(dwTlsIndex);
}

_NDK_INLINE
BOOL
WINAPI
_Inline_IsThreadAFiber(VOID)
{
    return NtCurrentTeb()->HasFiberData;
}

#pragma endregion

#pragma region Heap

_NDK_INLINE
HANDLE
WINAPI
_Inline_GetProcessHeap(VOID)
{
    return RtlProcessHeap();
}

_NDK_INLINE
LPVOID
WINAPI
_Inline_HeapAlloc(
    _In_ HANDLE hHeap,
    _In_ DWORD dwFlags,
    _In_ SIZE_T dwBytes)
{
    return RtlAllocateHeap(hHeap, dwFlags, dwBytes);
}

_NDK_INLINE
LPVOID
WINAPI
_Inline_HeapReAlloc(
    _Inout_ HANDLE hHeap,
    _In_ DWORD dwFlags,
    _Frees_ptr_opt_ LPVOID lpMem,
    _In_ SIZE_T dwBytes)
{
    return RtlReAllocateHeap(hHeap, dwFlags, lpMem, dwBytes);
}

_NDK_INLINE
BOOL
WINAPI
_Inline_HeapFree(
    _Inout_ HANDLE hHeap,
    _In_ DWORD dwFlags,
    _Frees_ptr_opt_ LPVOID lpMem)
{
    return RtlFreeHeap(hHeap, dwFlags, lpMem);
}

_NDK_INLINE
SIZE_T
WINAPI
_Inline_HeapSize(
    _In_ HANDLE hHeap,
    _In_ DWORD dwFlags,
    _In_ LPCVOID lpMem)
{
    return RtlSizeHeap(hHeap, dwFlags, (PVOID)lpMem);
}

_NDK_INLINE
BOOL
WINAPI
_Inline_HeapQueryInformation(
    _In_opt_ HANDLE HeapHandle,
    _In_ HEAP_INFORMATION_CLASS HeapInformationClass,
    _Out_opt_ PVOID HeapInformation,
    _In_ SIZE_T HeapInformationLength,
    _Out_opt_ PSIZE_T ReturnLength)
{
    NTSTATUS Status = RtlQueryHeapInformation(HeapHandle,
                                              HeapInformationClass,
                                              HeapInformation,
                                              HeapInformationLength,
                                              ReturnLength);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_HeapValidate(
    _In_ HANDLE hHeap,
    _In_ DWORD dwFlags,
    _In_opt_ LPCVOID lpMem)
{
    return RtlValidateHeap(hHeap, dwFlags, (PVOID)lpMem);
}

_NDK_INLINE
SIZE_T
WINAPI
_Inline_HeapCompact(
    _In_ HANDLE hHeap,
    _In_ DWORD dwFlags)
{
    return RtlCompactHeap(hHeap, dwFlags);
}

#pragma endregion

#pragma region Standard handles

_NDK_INLINE
HANDLE
WINAPI
_Inline_GetStdHandle(
    _In_ DWORD nStdHandle)
{
    HANDLE StdHandle;
    PRTL_USER_PROCESS_PARAMETERS ProcParam = NtCurrentPeb()->ProcessParameters;

    if (nStdHandle == STD_INPUT_HANDLE)
    {
        if (IS_NT_VERSION_GE(NT_VERSION_VISTA) && ProcParam->WindowFlags & STARTF_USEHOTKEY)
        {
            return NULL;
        }
        StdHandle = ProcParam->StandardInput;
    } else if (nStdHandle == STD_OUTPUT_HANDLE)
    {
        if (IS_NT_VERSION_GE(NT_VERSION_VISTA) && ProcParam->WindowFlags & STARTF_USEMONITOR)
        {
            return NULL;
        }
        StdHandle = ProcParam->StandardOutput;
    } else if (nStdHandle == STD_ERROR_HANDLE)
    {
        StdHandle = ProcParam->StandardError;
    } else
    {
        StdHandle = INVALID_HANDLE_VALUE;
    }
    if (StdHandle == INVALID_HANDLE_VALUE)
    {
        _Inline_BaseSetLastNTError(STATUS_INVALID_HANDLE);
    }
    return StdHandle;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_SetStdHandleEx(
    _In_ DWORD nStdHandle,
    _In_ HANDLE hHandle,
    _Out_opt_ PHANDLE phPrevValue)
{
    PHANDLE HandlePtr;
    PRTL_USER_PROCESS_PARAMETERS ProcParam = NtCurrentPeb()->ProcessParameters;

    if (phPrevValue != NULL)
    {
        *phPrevValue = NULL;
    }
    if (nStdHandle == STD_INPUT_HANDLE)
    {
        HandlePtr = &ProcParam->StandardInput;
    } else if (nStdHandle == STD_OUTPUT_HANDLE)
    {
        HandlePtr = &ProcParam->StandardOutput;
    } else if (nStdHandle == STD_ERROR_HANDLE)
    {
        HandlePtr = &ProcParam->StandardError;
    } else
    {
        _Inline_BaseSetLastNTError(STATUS_INVALID_HANDLE);
        return FALSE;
    }
    if (phPrevValue != NULL)
    {
        *phPrevValue = *HandlePtr;
    }
    *HandlePtr = hHandle;
    return TRUE;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_SetStdHandle(
    _In_ DWORD nStdHandle,
    _In_ HANDLE hHandle)
{
    return _Inline_SetStdHandleEx(nStdHandle, hHandle, NULL);
}

_NDK_INLINE
HANDLE
_Inline_BasepMapStdHandle(
    _In_ HANDLE hFile)
{
    DWORD StdHandle = (DWORD)(DWORD_PTR)hFile;
    PRTL_USER_PROCESS_PARAMETERS ProcParam;

    if (StdHandle < STD_ERROR_HANDLE)
    {
        return hFile;
    }
    ProcParam = NtCurrentPeb()->ProcessParameters;
    if (StdHandle == STD_ERROR_HANDLE)
    {
        return ProcParam->StandardError;
    } else if (StdHandle == STD_OUTPUT_HANDLE)
    {
        return ProcParam->StandardOutput;
    } else if (StdHandle == STD_INPUT_HANDLE)
    {
        return ProcParam->StandardInput;
    }
    return hFile;
}

#pragma endregion

#pragma region I/O

_NDK_INLINE
BOOL
WINAPI
_Inline_ReadFile(
    _In_ HANDLE hFile,
    _Out_ LPVOID lpBuffer,
    _In_ DWORD nNumberOfBytesToRead,
    _Out_opt_ LPDWORD lpNumberOfBytesRead,
    _Inout_opt_ LPOVERLAPPED lpOverlapped)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;

    if (lpNumberOfBytesRead != NULL)
    {
        *lpNumberOfBytesRead = 0;
    }
    hFile = _Inline_BasepMapStdHandle(hFile);
    IoStatusBlock.Status = STATUS_SUCCESS;
    IoStatusBlock.Information = 0;
    if (lpOverlapped == NULL)
    {
        Status = NtReadFile(hFile, NULL, NULL, NULL, &IoStatusBlock, lpBuffer, nNumberOfBytesToRead, NULL, NULL);
        if (Status == STATUS_PENDING)
        {
            Status = NtWaitForSingleObject(hFile, FALSE, NULL);
            if (NT_SUCCESS(Status))
            {
                Status = IoStatusBlock.Status;
            }
        }
        if (Status == STATUS_END_OF_FILE)
        {
            /* EOF is a successful zero-byte read for Win32 */
            return TRUE;
        }
        if (NT_SUCCESS(Status))
        {
            if (lpNumberOfBytesRead != NULL)
            {
                *lpNumberOfBytesRead = (ULONG)IoStatusBlock.Information;
            }
            return TRUE;
        }
        if (NT_WARNING(Status) && lpNumberOfBytesRead != NULL)
        {
            *lpNumberOfBytesRead = (ULONG)IoStatusBlock.Information;
        }
    } else
    {
        PVOID ApcContext = (ULONG_PTR)lpOverlapped->hEvent & 1 ? NULL : lpOverlapped;
        lpOverlapped->Internal = STATUS_PENDING;
        Status = NtReadFile(hFile,
                            lpOverlapped->hEvent,
                            NULL,
                            ApcContext,
                            (PIO_STATUS_BLOCK)lpOverlapped,
                            lpBuffer,
                            nNumberOfBytesToRead,
                            (PLARGE_INTEGER)&lpOverlapped->Offset,
                            NULL);
        if (Status != STATUS_PENDING && !NT_ERROR(Status))
        {
            if (lpNumberOfBytesRead != NULL)
            {
                *lpNumberOfBytesRead = (ULONG)lpOverlapped->InternalHigh;
            }
            return TRUE;
        }
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_WriteFile(
    _In_ HANDLE hFile,
    _In_ LPCVOID lpBuffer,
    _In_ DWORD nNumberOfBytesToWrite,
    _Out_opt_ LPDWORD lpNumberOfBytesWritten,
    _Inout_opt_ LPOVERLAPPED lpOverlapped)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;

    if (lpNumberOfBytesWritten != NULL)
    {
        *lpNumberOfBytesWritten = 0;
    }
    hFile = _Inline_BasepMapStdHandle(hFile);
    IoStatusBlock.Status = STATUS_SUCCESS;
    IoStatusBlock.Information = 0;
    if (lpOverlapped == NULL)
    {
        Status = NtWriteFile(hFile,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             (PVOID)lpBuffer,
                             nNumberOfBytesToWrite,
                             NULL,
                             NULL);
        if (Status == STATUS_PENDING)
        {
            Status = NtWaitForSingleObject(hFile, FALSE, NULL);
            if (NT_SUCCESS(Status))
            {
                Status = IoStatusBlock.Status;
            }
        }
        if (NT_SUCCESS(Status))
        {
            if (lpNumberOfBytesWritten != NULL)
            {
                *lpNumberOfBytesWritten = (ULONG)IoStatusBlock.Information;
            }
            return TRUE;
        }
        if (NT_WARNING(Status) && lpNumberOfBytesWritten != NULL)
        {
            *lpNumberOfBytesWritten = (ULONG)IoStatusBlock.Information;
        }
    } else
    {
        PVOID ApcContext = (ULONG_PTR)lpOverlapped->hEvent & 1 ? NULL : lpOverlapped;
        lpOverlapped->Internal = STATUS_PENDING;
        Status = NtWriteFile(hFile,
                             lpOverlapped->hEvent,
                             NULL,
                             ApcContext,
                             (PIO_STATUS_BLOCK)lpOverlapped,
                             (PVOID)lpBuffer,
                             nNumberOfBytesToWrite,
                             (PLARGE_INTEGER)&lpOverlapped->Offset,
                             NULL);
        if (Status != STATUS_PENDING && !NT_ERROR(Status))
        {
            if (lpNumberOfBytesWritten != NULL)
            {
                *lpNumberOfBytesWritten = (ULONG)lpOverlapped->InternalHigh;
            }
            return TRUE;
        }
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_FlushFileBuffers(
    _In_ HANDLE hFile)
{
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status = NtFlushBuffersFile(_Inline_BasepMapStdHandle(hFile), &IoStatusBlock);
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_SetFilePointerEx(
    _In_ HANDLE hFile,
    _In_ LARGE_INTEGER liDistanceToMove,
    _Out_opt_ PLARGE_INTEGER lpNewFilePointer,
    _In_ DWORD dwMoveMethod)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_POSITION_INFORMATION FilePosition;
    FILE_STANDARD_INFORMATION FileStandard;

    if (dwMoveMethod == FILE_BEGIN)
    {
        FilePosition.CurrentByteOffset.QuadPart = liDistanceToMove.QuadPart;
    } else if (dwMoveMethod == FILE_CURRENT)
    {
        Status = NtQueryInformationFile(hFile,
                                        &IoStatusBlock,
                                        &FilePosition,
                                        sizeof(FilePosition),
                                        FilePositionInformation);
        if (!NT_SUCCESS(Status))
        {
            _Inline_BaseSetLastNTError(Status);
            return FALSE;
        }
        FilePosition.CurrentByteOffset.QuadPart += liDistanceToMove.QuadPart;
    } else if (dwMoveMethod == FILE_END)
    {
        Status = NtQueryInformationFile(hFile,
                                        &IoStatusBlock,
                                        &FileStandard,
                                        sizeof(FileStandard),
                                        FileStandardInformation);
        if (!NT_SUCCESS(Status))
        {
            _Inline_BaseSetLastNTError(Status);
            return FALSE;
        }
        FilePosition.CurrentByteOffset.QuadPart = FileStandard.EndOfFile.QuadPart + liDistanceToMove.QuadPart;
    } else
    {
        _Inline_RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (FilePosition.CurrentByteOffset.QuadPart < 0)
    {
        _Inline_RtlSetLastWin32Error(ERROR_NEGATIVE_SEEK);
        return FALSE;
    }
    Status = NtSetInformationFile(hFile,
                                  &IoStatusBlock,
                                  &FilePosition,
                                  sizeof(FilePosition),
                                  FilePositionInformation);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    if (lpNewFilePointer != NULL)
    {
        *lpNewFilePointer = FilePosition.CurrentByteOffset;
    }
    return TRUE;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_GetFileSizeEx(
    _In_ HANDLE hFile,
    _Out_ PLARGE_INTEGER lpFileSize)
{
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_STANDARD_INFORMATION Info;
    NTSTATUS Status;

    Status = NtQueryInformationFile(hFile, &IoStatusBlock, &Info, sizeof(Info), FileStandardInformation);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    lpFileSize->QuadPart = Info.EndOfFile.QuadPart;
    return TRUE;
}

_NDK_INLINE
BOOL
WINAPI
_Inline_CloseHandle(
    _In_ HANDLE hObject)
{
    NTSTATUS Status;
    DWORD StdHandle = (DWORD)(DWORD_PTR)hObject;
    HANDLE PrevStdHandle;

    if (StdHandle >= STD_ERROR_HANDLE && StdHandle <= STD_INPUT_HANDLE)
    {
        if (_Inline_SetStdHandleEx(StdHandle, NULL, &PrevStdHandle))
        {
            hObject = PrevStdHandle;
        }
    }
    Status = NtClose(hObject);
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

#pragma endregion

#pragma region Misc

_NDK_INLINE
DWORD64
WINAPI
_Inline_GetEnabledXStateFeatures(VOID)
{
    ULONG64 u = _Inline_RtlGetEnabledExtendedFeatures(MAXULONGLONG);
    return u ? u : XSTATE_MASK_LEGACY;
}

_NDK_INLINE
DWORD
WINAPI
_Inline_GetLastError(VOID)
{
    return _Inline_RtlGetLastWin32Error();
}

_NDK_INLINE
VOID
WINAPI
_Inline_SetLastError(
    _In_ DWORD dwErrCode)
{
    _Inline_RtlSetLastWin32Error(dwErrCode);
}

/*
 * The code pages of the process: that of the header of its .nls data (PEB::AnsiCodePageData, OemCodePageData), else
 * PEB::ActiveCodePage, OemCodePage, which Windows 10 added (Windows 11 24H2 maps no .nls data for them)
 */
_NDK_INLINE
UINT
WINAPI
_Inline_GetACP(VOID)
{
    PPEB Peb = NtCurrentPeb();

    return Peb->AnsiCodePageData != NULL ? ((const USHORT*)Peb->AnsiCodePageData)[1] : Peb->ActiveCodePage;
}

_NDK_INLINE
UINT
WINAPI
_Inline_GetOEMCP(VOID)
{
    PPEB Peb = NtCurrentPeb();

    return Peb->OemCodePageData != NULL ? ((const USHORT*)Peb->OemCodePageData)[1] : Peb->OemCodePage;
}

_NDK_INLINE
LCID
WINAPI
_Inline_GetThreadLocale(VOID)
{
    return NtReadTeb(CurrentLocale);
}

_NDK_INLINE
BOOL
WINAPI
_Inline_IsProcessorFeaturePresent(
    _In_ DWORD ProcessorFeature)
{
    return _Inline_RtlIsProcessorFeaturePresent(ProcessorFeature);
}

_NDK_INLINE
DECLSPEC_NORETURN
VOID
WINAPI
_Inline_ExitProcess(
    _In_ UINT uExitCode)
{
    RtlExitUserProcess(uExitCode);
}

_NDK_INLINE
BOOL
WINAPI
_Inline_TerminateProcess(
    _In_ HANDLE hProcess,
    _In_ UINT uExitCode)
{
    NTSTATUS Status;

    if (hProcess == NULL)
    {
        _Inline_RtlSetLastWin32Error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    Status = NtTerminateProcess(hProcess, uExitCode);
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

_NDK_INLINE
VOID
WINAPI
_Inline_RaiseException(
    _In_ DWORD dwExceptionCode,
    _In_ DWORD dwExceptionFlags,
    _In_ DWORD nNumberOfArguments,
    _In_opt_ CONST ULONG_PTR* lpArguments)
{
    EXCEPTION_RECORD ExceptionRecord;

    memset(&ExceptionRecord, 0, sizeof(ExceptionRecord));
    ExceptionRecord.ExceptionCode = dwExceptionCode;
    ExceptionRecord.ExceptionFlags = dwExceptionFlags & EXCEPTION_NONCONTINUABLE;
    ExceptionRecord.ExceptionAddress = (PVOID)_Inline_RaiseException;
    if (lpArguments != NULL)
    {
        ExceptionRecord.NumberParameters = nNumberOfArguments > EXCEPTION_MAXIMUM_PARAMETERS ?
            EXCEPTION_MAXIMUM_PARAMETERS :
            nNumberOfArguments;
        memcpy(ExceptionRecord.ExceptionInformation,
               lpArguments,
               ExceptionRecord.NumberParameters * sizeof(*lpArguments));
    }
    RtlRaiseException(&ExceptionRecord);
}

_NDK_INLINE
VOID
WINAPI
_Inline_InitializeSListHead(
    _Out_ PSLIST_HEADER ListHead)
{
    RtlInitializeSListHead(ListHead);
}

_NDK_INLINE
PSLIST_ENTRY
WINAPI
_Inline_InterlockedFlushSList(
    _Inout_ PSLIST_HEADER ListHead)
{
    return RtlInterlockedFlushSList(ListHead);
}

_NDK_INLINE
PSLIST_ENTRY
WINAPI
_Inline_InterlockedPopEntrySList(
    _Inout_ PSLIST_HEADER ListHead)
{
    return RtlInterlockedPopEntrySList(ListHead);
}

_NDK_INLINE
PSLIST_ENTRY
WINAPI
_Inline_InterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER ListHead,
    _Inout_ PSLIST_ENTRY ListEntry)
{
    return RtlInterlockedPushEntrySList(ListHead, ListEntry);
}

#pragma endregion

EXTERN_C_END
