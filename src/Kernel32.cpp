/* Process, thread, synchronization, memory and other kernel32 APIs */

#include "NtCRT.h"

/* Process environment */

NTCRT_API HANDLE WINAPI _K32_GetCurrentProcess(VOID) { return NtCurrentProcess(); }
NTCRT_API HANDLE WINAPI _K32_GetCurrentThread(VOID) { return NtCurrentThread(); }
NTCRT_API DWORD WINAPI _K32_GetCurrentProcessId(VOID) { return _Inline_GetCurrentProcessId(); }
NTCRT_API DWORD WINAPI _K32_GetCurrentThreadId(VOID) { return _Inline_GetCurrentThreadId(); }
NTCRT_API BOOL WINAPI _K32_IsDebuggerPresent(VOID) { return _Inline_IsDebuggerPresent(); }
NTCRT_API HANDLE WINAPI _K32_GetProcessHeap(VOID) { return RtlProcessHeap(); }
NTCRT_API HANDLE WINAPI _K32_GetStdHandle(_In_ DWORD nStdHandle) { return _Inline_GetStdHandle(nStdHandle); }
NTCRT_API BOOL WINAPI _K32_SetStdHandle(_In_ DWORD nStdHandle, _In_ HANDLE hHandle)
{
    return _Inline_SetStdHandle(nStdHandle, hHandle);
}
NTCRT_API LPWCH WINAPI _K32_GetEnvironmentStringsW(VOID) { return _Inline_GetEnvironmentStringsW(); }
NTCRT_API BOOL WINAPI _K32_FreeEnvironmentStringsW(_In_ LPWCH penv) { return _Inline_FreeEnvironmentStringsW(penv); }
NTCRT_API VOID WINAPI _K32_GetStartupInfoW(_Out_ LPSTARTUPINFOW lpStartupInfo)
{
    _Inline_GetStartupInfoW(lpStartupInfo);
}
NTCRT_API __attribute__((noreturn)) VOID WINAPI _K32_ExitProcess(_In_ UINT uExitCode)
{
    _Inline_ExitProcess(uExitCode);
}
NTCRT_API BOOL WINAPI _K32_TerminateProcess(_In_ HANDLE hProcess, _In_ UINT uExitCode)
{
    return _Inline_TerminateProcess(hProcess, uExitCode);
}
NTCRT_API LPWSTR WINAPI _K32_GetCommandLineW(VOID) { return NtCurrentPeb()->ProcessParameters->CommandLine.Buffer; }

NTCRT_API
VOID
WINAPI
_K32_GetStartupInfoA(
    _Out_ LPSTARTUPINFOA lpStartupInfo)
{
    static char* Desktop = NULL;
    static char* Title = NULL;
    STARTUPINFOW Info;

    _Inline_GetStartupInfoW(&Info);
    if (Desktop == NULL && Info.lpDesktop != NULL)
    {
        Desktop = _NtCRT_WideToAnsi(Info.lpDesktop);
    }
    if (Title == NULL && Info.lpTitle != NULL)
    {
        Title = _NtCRT_WideToAnsi(Info.lpTitle);
    }
    memcpy(lpStartupInfo, &Info, sizeof(*lpStartupInfo));
    lpStartupInfo->lpReserved = NULL;
    lpStartupInfo->lpDesktop = Desktop;
    lpStartupInfo->lpTitle = Title;
}

NTCRT_API
LPSTR
WINAPI
_K32_GetCommandLineA(VOID)
{
    static char* CommandLine = NULL;

    if (CommandLine == NULL)
    {
        CommandLine = _NtCRT_WideToAnsi(NtCurrentPeb()->ProcessParameters->CommandLine.Buffer);
    }
    return CommandLine;
}

NTCRT_API
HMODULE
WINAPI
_K32_GetModuleHandleW(
    _In_opt_ LPCWSTR lpModuleName)
{
    return _Inline_GetModuleHandleW(lpModuleName);
}

NTCRT_API
HMODULE
WINAPI
_K32_GetModuleHandleA(
    _In_opt_ LPCSTR lpModuleName)
{
    HMODULE Module;
    PWSTR Name;

    if (lpModuleName == NULL)
    {
        return (HMODULE)NtCurrentPeb()->ImageBaseAddress;
    }
    Name = _NtCRT_AnsiToWide(lpModuleName);
    if (Name == NULL)
    {
        return NULL;
    }
    Module = _Inline_GetModuleHandleW(Name);
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    return Module;
}

NTCRT_API
BOOL
WINAPI
_K32_GetModuleHandleExW(
    _In_ DWORD dwFlags,
    _In_opt_ LPCWSTR lpModuleName,
    _Out_ HMODULE* phModule)
{
    return _Inline_GetModuleHandleExW(dwFlags, lpModuleName, phModule);
}

NTCRT_API
DWORD
WINAPI
_K32_GetModuleFileNameW(
    _In_opt_ HMODULE hModule,
    _Out_writes_to_(nSize, ((return < nSize) ? (return + 1) : nSize)) LPWSTR lpFilename,
    _In_ DWORD nSize)
{
    DWORD Length = _Inline_GetModuleFileNameW(hModule, lpFilename, nSize);

    /* Truncated to nSize characters with the terminator, kernel32 returns nSize */
    return nSize != 0 && _Inline_GetLastError() == ERROR_INSUFFICIENT_BUFFER ? nSize : Length;
}

NTCRT_API
DWORD
WINAPI
_K32_GetModuleFileNameA(
    _In_opt_ HMODULE hModule,
    _Out_writes_to_(nSize, ((return < nSize) ? (return + 1) : nSize)) LPSTR lpFilename,
    _In_ DWORD nSize)
{
    WCHAR Path[MAX_PATH * 4];
    CHAR Ansi[ARRAYSIZE(Path) * 3];
    DWORD Length, Bytes;

    Length = _Inline_GetModuleFileNameW(hModule, Path, ARRAYSIZE(Path));
    if (Length == 0 || nSize == 0)
    {
        return 0;
    }
    Bytes = (DWORD)_NtCRT_WideCharToMultiByte(_NtCRT_CodePage(), 0, Path, Length, Ansi, sizeof(Ansi), NULL, NULL);
    if (Bytes >= nSize)
    {
        /* Truncated to nSize bytes with the terminator, kernel32 returns nSize */
        memcpy(lpFilename, Ansi, nSize - 1);
        lpFilename[nSize - 1] = ANSI_NULL;
        _Inline_SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return nSize;
    }
    memcpy(lpFilename, Ansi, Bytes);
    lpFilename[Bytes] = ANSI_NULL;
    return Bytes;
}

/* Exception */

NTCRT_API
VOID
WINAPI
_K32_RaiseException(
    _In_ DWORD dwExceptionCode,
    _In_ DWORD dwExceptionFlags,
    _In_ DWORD nNumberOfArguments,
    _In_reads_opt_(nNumberOfArguments) CONST ULONG_PTR* lpArguments)
{
    _Inline_RaiseException(dwExceptionCode, dwExceptionFlags, nNumberOfArguments, lpArguments);
}

NTCRT_API
PVOID
WINAPI
_K32_AddVectoredExceptionHandler(
    _In_ ULONG First,
    _In_ PVECTORED_EXCEPTION_HANDLER Handler)
{
    return RtlAddVectoredExceptionHandler(First, Handler);
}

NTCRT_API ULONG WINAPI _K32_RemoveVectoredExceptionHandler(_In_ PVOID Handle)
{
    return RtlRemoveVectoredExceptionHandler(Handle);
}

NTCRT_API
PVOID
WINAPI
_K32_AddVectoredContinueHandler(
    _In_ ULONG First,
    _In_ PVECTORED_EXCEPTION_HANDLER Handler)
{
    return RtlAddVectoredContinueHandler(First, Handler);
}

NTCRT_API ULONG WINAPI _K32_RemoveVectoredContinueHandler(_In_ PVOID Handle)
{
    return RtlRemoveVectoredContinueHandler(Handle);
}

/*
 * OutputDebugStringA raises DBG_PRINTEXCEPTION_C for the debugger. Without a debugger or when the debugger passes it,
 * a last-chance VEH swallows it like kernel32's __except block does.
 */

static
LONG
NTAPI
_NtCRT_DebugPrintHandler(
    _In_ PEXCEPTION_POINTERS ExceptionInfo)
{
    if (ExceptionInfo->ExceptionRecord->ExceptionCode == DBG_PRINTEXCEPTION_C &&
        ExceptionInfo->ExceptionRecord->NumberParameters == 2 &&
        NtReadTeb(InDebugPrint))
    {
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

NTCRT_API
VOID
WINAPI
_K32_OutputDebugStringA(
    _In_opt_ LPCSTR lpOutputString)
{
    static PVOID Handler = NULL;
    ULONG_PTR Arguments[2];

    if (lpOutputString == NULL)
    {
        lpOutputString = "";
    }
    if (Handler == NULL)
    {
        PVOID NewHandler = RtlAddVectoredExceptionHandler(FALSE, _NtCRT_DebugPrintHandler);
        if (_InterlockedCompareExchangePointer(&Handler, NewHandler, NULL) != NULL)
        {
            RtlRemoveVectoredExceptionHandler(NewHandler);
        }
    }
    if (Handler == NULL || !NtCurrentPeb()->BeingDebugged)
    {
        DbgPrint("%s", lpOutputString);
        return;
    }
    Arguments[0] = strlen(lpOutputString) + 1;
    Arguments[1] = (ULONG_PTR)lpOutputString;
    NtCurrentTeb()->InDebugPrint = TRUE;
    _Inline_RaiseException(DBG_PRINTEXCEPTION_C, 0, 2, Arguments);
    NtCurrentTeb()->InDebugPrint = FALSE;
}

NTCRT_API
VOID
WINAPI
_K32_OutputDebugStringW(
    _In_opt_ LPCWSTR lpOutputString)
{
    char* String = lpOutputString != NULL ? _NtCRT_WideToAnsi(lpOutputString) : NULL;

    _K32_OutputDebugStringA(String);
    if (String != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, String);
    }
}

/* Handle */

NTCRT_API BOOL WINAPI _K32_CloseHandle(_In_ HANDLE hObject) { return _Inline_CloseHandle(hObject); }

NTCRT_API
BOOL
WINAPI
_K32_DuplicateHandle(
    _In_ HANDLE hSourceProcessHandle,
    _In_ HANDLE hSourceHandle,
    _In_ HANDLE hTargetProcessHandle,
    _Outptr_ LPHANDLE lpTargetHandle,
    _In_ DWORD dwDesiredAccess,
    _In_ BOOL bInheritHandle,
    _In_ DWORD dwOptions)
{
    NTSTATUS Status = NtDuplicateObject(hSourceProcessHandle,
                                        _Inline_BasepMapStdHandle(hSourceHandle),
                                        hTargetProcessHandle,
                                        lpTargetHandle,
                                        dwDesiredAccess,
                                        bInheritHandle ? OBJ_INHERIT : 0,
                                        dwOptions);
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_API
BOOL
WINAPI
_K32_GetHandleInformation(
    _In_ HANDLE hObject,
    _Out_ LPDWORD lpdwFlags)
{
    OBJECT_HANDLE_FLAG_INFORMATION Info;
    NTSTATUS Status = NtQueryObject(_Inline_BasepMapStdHandle(hObject),
                                    ObjectHandleFlagInformation,
                                    &Info,
                                    sizeof(Info),
                                    NULL);
    if (NT_SUCCESS(Status))
    {
        *lpdwFlags =
            (Info.Inherit ? HANDLE_FLAG_INHERIT : 0) | (Info.ProtectFromClose ? HANDLE_FLAG_PROTECT_FROM_CLOSE : 0);
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_API
BOOL
WINAPI
_K32_SetHandleInformation(
    _In_ HANDLE hObject,
    _In_ DWORD dwMask,
    _In_ DWORD dwFlags)
{
    OBJECT_HANDLE_FLAG_INFORMATION Info;
    NTSTATUS Status;

    hObject = _Inline_BasepMapStdHandle(hObject);
    Status = NtQueryObject(hObject, ObjectHandleFlagInformation, &Info, sizeof(Info), NULL);
    if (NT_SUCCESS(Status))
    {
        if (dwMask & HANDLE_FLAG_INHERIT)
        {
            Info.Inherit = (dwFlags & HANDLE_FLAG_INHERIT) != 0;
        }
        if (dwMask & HANDLE_FLAG_PROTECT_FROM_CLOSE)
        {
            Info.ProtectFromClose = (dwFlags & HANDLE_FLAG_PROTECT_FROM_CLOSE) != 0;
        }
        Status = NtSetInformationObject(hObject, ObjectHandleFlagInformation, &Info, sizeof(Info));
    }
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

/* Synchronization */

/* Build the object attributes for a named object in the session BaseNamedObjects directory */
static
POBJECT_ATTRIBUTES
_NtCRT_FormatObjectAttributes(
    _Out_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PUNICODE_STRING ObjectName,
    _Out_writes_(Size) PWCHAR Buffer,
    _In_ ULONG Size,
    _In_opt_ LPSECURITY_ATTRIBUTES SecurityAttributes,
    _In_opt_ LPCWSTR Name)
{
    ULONG Attributes = 0;

    if (SecurityAttributes != NULL && SecurityAttributes->bInheritHandle)
    {
        Attributes |= OBJ_INHERIT;
    }
    if (Name != NULL)
    {
        /* \Sessions\<id>\BaseNamedObjects\<name>, \BaseNamedObjects\<name> for session 0 */
        ULONG SessionId = NtCurrentPeb()->SessionId;
        WCHAR Digits[11];
        PWCHAR d = Digits + ARRAYSIZE(Digits) - 1;

        *d = L'\0';
        do
        {
            *--d = (WCHAR)(L'0' + SessionId % 10);
            SessionId /= 10;
        } while (SessionId != 0);
        if (NtCurrentPeb()->SessionId != 0)
        {
            if (wcslen(L"\\Sessions\\") + wcslen(d) + wcslen(L"\\BaseNamedObjects\\") + wcslen(Name) >= Size)
            {
                return NULL;
            }
            wcscpy(Buffer, L"\\Sessions\\");
            wcscat(Buffer, d);
            wcscat(Buffer, L"\\BaseNamedObjects\\");
        } else
        {
            if (wcslen(L"\\BaseNamedObjects\\") + wcslen(Name) >= Size)
            {
                return NULL;
            }
            wcscpy(Buffer, L"\\BaseNamedObjects\\");
        }
        wcscat(Buffer, Name);
        _Inline_RtlInitUnicodeString(ObjectName, Buffer);
        Attributes |= OBJ_OPENIF;
    } else if (Attributes == 0 && SecurityAttributes == NULL)
    {
        InitializeObjectAttributes(ObjectAttributes, NULL, 0, NULL, NULL);
        return ObjectAttributes;
    }
    InitializeObjectAttributes(ObjectAttributes,
                               Name != NULL ? ObjectName : NULL,
                               Attributes,
                               NULL,
                               SecurityAttributes != NULL ? SecurityAttributes->lpSecurityDescriptor : NULL);
    return ObjectAttributes;
}

static
HANDLE
_NtCRT_CreateObjectResult(
    _In_ NTSTATUS Status,
    _In_ PHANDLE Handle)
{
    if (NT_SUCCESS(Status))
    {
        _Inline_SetLastError(Status == STATUS_OBJECT_NAME_EXISTS ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
        return *Handle;
    }
    _Inline_BaseSetLastNTError(Status);
    return NULL;
}

NTCRT_API
HANDLE
WINAPI
_K32_CreateEventW(
    _In_opt_ LPSECURITY_ATTRIBUTES lpEventAttributes,
    _In_ BOOL bManualReset,
    _In_ BOOL bInitialState,
    _In_opt_ LPCWSTR lpName)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING ObjectName;
    WCHAR Buffer[MAX_PATH];
    HANDLE Handle;
    POBJECT_ATTRIBUTES Attributes;

    Attributes = _NtCRT_FormatObjectAttributes(&ObjectAttributes,
                                               &ObjectName,
                                               Buffer,
                                               ARRAYSIZE(Buffer),
                                               lpEventAttributes,
                                               lpName);
    if (Attributes == NULL)
    {
        _Inline_SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    return _NtCRT_CreateObjectResult(NtCreateEvent(&Handle,
                                                   EVENT_ALL_ACCESS,
                                                   Attributes,
                                                   bManualReset ? NotificationEvent : SynchronizationEvent,
                                                   (BOOLEAN)bInitialState),
                                     &Handle);
}

NTCRT_API
HANDLE
WINAPI
_K32_CreateEventA(
    _In_opt_ LPSECURITY_ATTRIBUTES lpEventAttributes,
    _In_ BOOL bManualReset,
    _In_ BOOL bInitialState,
    _In_opt_ LPCSTR lpName)
{
    HANDLE Handle;
    PWSTR Name = NULL;

    if (lpName != NULL && (Name = _NtCRT_AnsiToWide(lpName)) == NULL)
    {
        return NULL;
    }
    Handle = _K32_CreateEventW(lpEventAttributes, bManualReset, bInitialState, Name);
    if (Name != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Name);
    }
    return Handle;
}

NTCRT_API
HANDLE
WINAPI
_K32_CreateSemaphoreW(
    _In_opt_ LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
    _In_ LONG lInitialCount,
    _In_ LONG lMaximumCount,
    _In_opt_ LPCWSTR lpName)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING ObjectName;
    WCHAR Buffer[MAX_PATH];
    HANDLE Handle;
    POBJECT_ATTRIBUTES Attributes;

    Attributes = _NtCRT_FormatObjectAttributes(&ObjectAttributes,
                                               &ObjectName,
                                               Buffer,
                                               ARRAYSIZE(Buffer),
                                               lpSemaphoreAttributes,
                                               lpName);
    if (Attributes == NULL)
    {
        _Inline_SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    return _NtCRT_CreateObjectResult(NtCreateSemaphore(&Handle,
                                                       SEMAPHORE_ALL_ACCESS,
                                                       Attributes,
                                                       lInitialCount,
                                                       lMaximumCount),
                                     &Handle);
}

NTCRT_API
HANDLE
WINAPI
_K32_CreateSemaphoreA(
    _In_opt_ LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
    _In_ LONG lInitialCount,
    _In_ LONG lMaximumCount,
    _In_opt_ LPCSTR lpName)
{
    HANDLE Handle;
    PWSTR Name = NULL;

    if (lpName != NULL && (Name = _NtCRT_AnsiToWide(lpName)) == NULL)
    {
        return NULL;
    }
    Handle = _K32_CreateSemaphoreW(lpSemaphoreAttributes, lInitialCount, lMaximumCount, Name);
    if (Name != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Name);
    }
    return Handle;
}

NTCRT_API
HANDLE
WINAPI
_K32_CreateMutexW(
    _In_opt_ LPSECURITY_ATTRIBUTES lpMutexAttributes,
    _In_ BOOL bInitialOwner,
    _In_opt_ LPCWSTR lpName)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING ObjectName;
    WCHAR Buffer[MAX_PATH];
    HANDLE Handle;
    POBJECT_ATTRIBUTES Attributes;

    Attributes = _NtCRT_FormatObjectAttributes(&ObjectAttributes,
                                               &ObjectName,
                                               Buffer,
                                               ARRAYSIZE(Buffer),
                                               lpMutexAttributes,
                                               lpName);
    if (Attributes == NULL)
    {
        _Inline_SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    return _NtCRT_CreateObjectResult(NtCreateMutant(&Handle, MUTANT_ALL_ACCESS, Attributes, (BOOLEAN)bInitialOwner),
                                     &Handle);
}

static
BOOL
_NtCRT_StatusToBool(
    _In_ NTSTATUS Status)
{
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_API BOOL WINAPI _K32_SetEvent(_In_ HANDLE hEvent) { return _NtCRT_StatusToBool(NtSetEvent(hEvent, NULL)); }
NTCRT_API BOOL WINAPI _K32_ResetEvent(_In_ HANDLE hEvent) { return _NtCRT_StatusToBool(NtResetEvent(hEvent, NULL)); }
NTCRT_API BOOL WINAPI _K32_ReleaseMutex(_In_ HANDLE hMutex)
{
    return _NtCRT_StatusToBool(NtReleaseMutant(hMutex, NULL));
}

NTCRT_API
BOOL
WINAPI
_K32_ReleaseSemaphore(
    _In_ HANDLE hSemaphore,
    _In_ LONG lReleaseCount,
    _Out_opt_ LPLONG lpPreviousCount)
{
    return _NtCRT_StatusToBool(NtReleaseSemaphore(hSemaphore, lReleaseCount, lpPreviousCount));
}

static
DWORD
_NtCRT_WaitResult(
    _In_ NTSTATUS Status)
{
    if (NT_SUCCESS(Status))
    {
        /* STATUS_WAIT_0, STATUS_ABANDONED_WAIT_0, STATUS_USER_APC, STATUS_TIMEOUT have the same values as WAIT_* */
        return (DWORD)Status;
    }
    _Inline_BaseSetLastNTError(Status);
    return WAIT_FAILED;
}

NTCRT_API
DWORD
WINAPI
_K32_WaitForSingleObjectEx(
    _In_ HANDLE hHandle,
    _In_ DWORD dwMilliseconds,
    _In_ BOOL bAlertable)
{
    LARGE_INTEGER Timeout;

    return _NtCRT_WaitResult(NtWaitForSingleObject(_Inline_BasepMapStdHandle(hHandle),
                                                   (BOOLEAN)bAlertable,
                                                   _Inline_BaseFormatTimeOut(&Timeout, dwMilliseconds)));
}

NTCRT_API DWORD WINAPI _K32_WaitForSingleObject(_In_ HANDLE hHandle, _In_ DWORD dwMilliseconds)
{
    return _K32_WaitForSingleObjectEx(hHandle, dwMilliseconds, FALSE);
}

NTCRT_API
DWORD
WINAPI
_K32_WaitForMultipleObjectsEx(
    _In_ DWORD nCount,
    _In_reads_(nCount) CONST HANDLE* lpHandles,
    _In_ BOOL bWaitAll,
    _In_ DWORD dwMilliseconds,
    _In_ BOOL bAlertable)
{
    HANDLE Handles[MAXIMUM_WAIT_OBJECTS];
    LARGE_INTEGER Timeout;

    if (nCount == 0 || nCount > MAXIMUM_WAIT_OBJECTS)
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    for (DWORD i = 0; i < nCount; i++)
    {
        Handles[i] = _Inline_BasepMapStdHandle(lpHandles[i]);
    }
    return _NtCRT_WaitResult(NtWaitForMultipleObjects(nCount,
                                                      Handles,
                                                      bWaitAll ? WaitAll : WaitAny,
                                                      (BOOLEAN)bAlertable,
                                                      _Inline_BaseFormatTimeOut(&Timeout, dwMilliseconds)));
}

NTCRT_API
DWORD
WINAPI
_K32_WaitForMultipleObjects(
    _In_ DWORD nCount,
    _In_reads_(nCount) CONST HANDLE* lpHandles,
    _In_ BOOL bWaitAll,
    _In_ DWORD dwMilliseconds)
{
    return _K32_WaitForMultipleObjectsEx(nCount, lpHandles, bWaitAll, dwMilliseconds, FALSE);
}

/* SRW lock, condition variable, init once, see Kernel32Lock.cpp for critical sections */

NTCRT_API VOID WINAPI _K32_InitializeSRWLock(_Out_ PSRWLOCK l) { RtlInitializeSRWLock(l); }
NTCRT_API VOID WINAPI _K32_AcquireSRWLockExclusive(_Inout_ PSRWLOCK l) { RtlAcquireSRWLockExclusive(l); }
NTCRT_API VOID WINAPI _K32_ReleaseSRWLockExclusive(_Inout_ PSRWLOCK l) { RtlReleaseSRWLockExclusive(l); }
NTCRT_API VOID WINAPI _K32_AcquireSRWLockShared(_Inout_ PSRWLOCK l) { RtlAcquireSRWLockShared(l); }
NTCRT_API VOID WINAPI _K32_ReleaseSRWLockShared(_Inout_ PSRWLOCK l) { RtlReleaseSRWLockShared(l); }
NTCRT_API BOOLEAN WINAPI _K32_TryAcquireSRWLockExclusive(_Inout_ PSRWLOCK l)
{
    return RtlTryAcquireSRWLockExclusive(l);
}
NTCRT_API BOOLEAN WINAPI _K32_TryAcquireSRWLockShared(_Inout_ PSRWLOCK l) { return RtlTryAcquireSRWLockShared(l); }
NTCRT_API VOID WINAPI _K32_InitializeConditionVariable(_Out_ PCONDITION_VARIABLE c)
{
    RtlInitializeConditionVariable(c);
}
NTCRT_API VOID WINAPI _K32_WakeConditionVariable(_Inout_ PCONDITION_VARIABLE c) { RtlWakeConditionVariable(c); }
NTCRT_API VOID WINAPI _K32_WakeAllConditionVariable(_Inout_ PCONDITION_VARIABLE c) { RtlWakeAllConditionVariable(c); }

NTCRT_API
BOOL
WINAPI
_K32_SleepConditionVariableCS(
    _Inout_ PCONDITION_VARIABLE ConditionVariable,
    _Inout_ PCRITICAL_SECTION CriticalSection,
    _In_ DWORD dwMilliseconds)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status = RtlSleepConditionVariableCS(ConditionVariable,
                                                  CriticalSection,
                                                  _Inline_BaseFormatTimeOut(&Timeout, dwMilliseconds));
    if (Status == STATUS_TIMEOUT)
    {
        _Inline_SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    return _NtCRT_StatusToBool(Status);
}

NTCRT_API
BOOL
WINAPI
_K32_SleepConditionVariableSRW(
    _Inout_ PCONDITION_VARIABLE ConditionVariable,
    _Inout_ PSRWLOCK SRWLock,
    _In_ DWORD dwMilliseconds,
    _In_ ULONG Flags)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status = RtlSleepConditionVariableSRW(ConditionVariable,
                                                   SRWLock,
                                                   _Inline_BaseFormatTimeOut(&Timeout, dwMilliseconds),
                                                   Flags);
    if (Status == STATUS_TIMEOUT)
    {
        _Inline_SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    return _NtCRT_StatusToBool(Status);
}

NTCRT_API
BOOL
WINAPI
_K32_InitOnceExecuteOnce(
    _Inout_ PINIT_ONCE InitOnce,
    _In_ PINIT_ONCE_FN InitFn,
    _Inout_opt_ PVOID Parameter,
    _Outptr_opt_result_maybenull_ LPVOID* Context)
{
    return NT_SUCCESS(RtlRunOnceExecuteOnce(InitOnce, (PRTL_RUN_ONCE_INIT_FN)InitFn, Parameter, Context));
}

/* Process and thread */

NTCRT_API
HANDLE
WINAPI
_K32_OpenProcess(
    _In_ DWORD dwDesiredAccess,
    _In_ BOOL bInheritHandle,
    _In_ DWORD dwProcessId)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    CLIENT_ID ClientId = { (HANDLE)(ULONG_PTR)dwProcessId, NULL };
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, NULL, bInheritHandle ? OBJ_INHERIT : 0, NULL, NULL);
    Status = NtOpenProcess(&Handle, dwDesiredAccess, &ObjectAttributes, &ClientId);
    if (NT_SUCCESS(Status))
    {
        return Handle;
    }
    _Inline_BaseSetLastNTError(Status);
    return NULL;
}

NTCRT_API
HANDLE
WINAPI
_K32_OpenThread(
    _In_ DWORD dwDesiredAccess,
    _In_ BOOL bInheritHandle,
    _In_ DWORD dwThreadId)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    CLIENT_ID ClientId = { NULL, (HANDLE)(ULONG_PTR)dwThreadId };
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, NULL, bInheritHandle ? OBJ_INHERIT : 0, NULL, NULL);
    Status = NtOpenThread(&Handle, dwDesiredAccess, &ObjectAttributes, &ClientId);
    if (NT_SUCCESS(Status))
    {
        return Handle;
    }
    _Inline_BaseSetLastNTError(Status);
    return NULL;
}

NTCRT_API
BOOL
WINAPI
_K32_GetExitCodeProcess(
    _In_ HANDLE hProcess,
    _Out_ LPDWORD lpExitCode)
{
    PROCESS_BASIC_INFORMATION Info;
    NTSTATUS Status = NtQueryInformationProcess(hProcess, ProcessBasicInformation, &Info, sizeof(Info), NULL);

    if (NT_SUCCESS(Status))
    {
        *lpExitCode = Info.ExitStatus;
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_API
BOOL
WINAPI
_K32_GetExitCodeThread(
    _In_ HANDLE hThread,
    _Out_ LPDWORD lpExitCode)
{
    THREAD_BASIC_INFORMATION Info;
    NTSTATUS Status = NtQueryInformationThread(hThread, ThreadBasicInformation, &Info, sizeof(Info), NULL);

    if (NT_SUCCESS(Status))
    {
        *lpExitCode = Info.ExitStatus;
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_API
DWORD
WINAPI
_K32_GetThreadId(
    _In_ HANDLE Thread)
{
    THREAD_BASIC_INFORMATION Info;
    NTSTATUS Status = NtQueryInformationThread(Thread, ThreadBasicInformation, &Info, sizeof(Info), NULL);

    if (NT_SUCCESS(Status))
    {
        return (DWORD)(ULONG_PTR)Info.ClientId.UniqueThread;
    }
    _Inline_BaseSetLastNTError(Status);
    return 0;
}

NTCRT_API
BOOL
WINAPI
_K32_GetProcessAffinityMask(
    _In_ HANDLE hProcess,
    _Out_ PDWORD_PTR lpProcessAffinityMask,
    _Out_ PDWORD_PTR lpSystemAffinityMask)
{
    PROCESS_BASIC_INFORMATION ProcessInfo;
    SYSTEM_BASIC_INFORMATION SystemInfo;
    NTSTATUS Status;

    Status = NtQueryInformationProcess(hProcess, ProcessBasicInformation, &ProcessInfo, sizeof(ProcessInfo), NULL);
    if (NT_SUCCESS(Status))
    {
        Status = NtQuerySystemInformation(SystemBasicInformation, &SystemInfo, sizeof(SystemInfo), NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    *lpProcessAffinityMask = ProcessInfo.AffinityMask;
    *lpSystemAffinityMask = SystemInfo.ActiveProcessorsAffinityMask;
    return TRUE;
}

NTCRT_API
BOOL
WINAPI
_K32_SetProcessAffinityMask(
    _In_ HANDLE hProcess,
    _In_ DWORD_PTR dwProcessAffinityMask)
{
    KAFFINITY Mask = dwProcessAffinityMask;
    return _NtCRT_StatusToBool(NtSetInformationProcess(hProcess, ProcessAffinityMask, &Mask, sizeof(Mask)));
}

NTCRT_API
DWORD_PTR
WINAPI
_K32_SetThreadAffinityMask(
    _In_ HANDLE hThread,
    _In_ DWORD_PTR dwThreadAffinityMask)
{
    THREAD_BASIC_INFORMATION Info;
    KAFFINITY Mask = dwThreadAffinityMask;
    NTSTATUS Status;

    Status = NtQueryInformationThread(hThread, ThreadBasicInformation, &Info, sizeof(Info), NULL);
    if (NT_SUCCESS(Status))
    {
        Status = NtSetInformationThread(hThread, ThreadAffinityMask, &Mask, sizeof(Mask));
    }
    if (NT_SUCCESS(Status))
    {
        return Info.AffinityMask;
    }
    _Inline_BaseSetLastNTError(Status);
    return 0;
}

NTCRT_API
BOOL
WINAPI
_K32_GetThreadContext(
    _In_ HANDLE hThread,
    _Inout_ LPCONTEXT lpContext)
{
    return _NtCRT_StatusToBool(NtGetContextThread(hThread, lpContext));
}

NTCRT_API
BOOL
WINAPI
_K32_SetThreadContext(
    _In_ HANDLE hThread,
    _In_ CONST CONTEXT* lpContext)
{
    return _NtCRT_StatusToBool(NtSetContextThread(hThread, (PCONTEXT)lpContext));
}

NTCRT_API
int
WINAPI
_K32_GetThreadPriority(
    _In_ HANDLE hThread)
{
    THREAD_BASIC_INFORMATION Info;
    NTSTATUS Status = NtQueryInformationThread(hThread, ThreadBasicInformation, &Info, sizeof(Info), NULL);

    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return THREAD_PRIORITY_ERROR_RETURN;
    }
    /* Saturation values, see kernel32!GetThreadPriority */
    if (Info.BasePriority == ((HIGH_PRIORITY + 1) / 2))
    {
        return THREAD_PRIORITY_TIME_CRITICAL;
    } else if (Info.BasePriority == -((HIGH_PRIORITY + 1) / 2))
    {
        return THREAD_PRIORITY_IDLE;
    }
    return Info.BasePriority;
}

NTCRT_API
BOOL
WINAPI
_K32_SetThreadPriority(
    _In_ HANDLE hThread,
    _In_ int nPriority)
{
    LONG Priority = nPriority;

    if (Priority == THREAD_PRIORITY_TIME_CRITICAL)
    {
        Priority = (HIGH_PRIORITY + 1) / 2;
    } else if (Priority == THREAD_PRIORITY_IDLE)
    {
        Priority = -((HIGH_PRIORITY + 1) / 2);
    }
    return _NtCRT_StatusToBool(NtSetInformationThread(hThread, ThreadBasePriority, &Priority, sizeof(Priority)));
}

NTCRT_API
DWORD
WINAPI
_K32_SuspendThread(
    _In_ HANDLE hThread)
{
    ULONG Count;
    NTSTATUS Status = NtSuspendThread(hThread, &Count);

    if (NT_SUCCESS(Status))
    {
        return Count;
    }
    _Inline_BaseSetLastNTError(Status);
    return (DWORD)-1;
}

NTCRT_API
DWORD
WINAPI
_K32_ResumeThread(
    _In_ HANDLE hThread)
{
    ULONG Count;
    NTSTATUS Status = NtResumeThread(hThread, &Count);

    if (NT_SUCCESS(Status))
    {
        return Count;
    }
    _Inline_BaseSetLastNTError(Status);
    return (DWORD)-1;
}

static
VOID
_NtCRT_TimesToFileTimes(
    _In_ PKERNEL_USER_TIMES Times,
    _Out_ LPFILETIME lpCreationTime,
    _Out_ LPFILETIME lpExitTime,
    _Out_ LPFILETIME lpKernelTime,
    _Out_ LPFILETIME lpUserTime)
{
    lpCreationTime->dwLowDateTime = Times->CreateTime.LowPart;
    lpCreationTime->dwHighDateTime = Times->CreateTime.HighPart;
    lpExitTime->dwLowDateTime = Times->ExitTime.LowPart;
    lpExitTime->dwHighDateTime = Times->ExitTime.HighPart;
    lpKernelTime->dwLowDateTime = Times->KernelTime.LowPart;
    lpKernelTime->dwHighDateTime = Times->KernelTime.HighPart;
    lpUserTime->dwLowDateTime = Times->UserTime.LowPart;
    lpUserTime->dwHighDateTime = Times->UserTime.HighPart;
}

NTCRT_API
BOOL
WINAPI
_K32_GetProcessTimes(
    _In_ HANDLE hProcess,
    _Out_ LPFILETIME lpCreationTime,
    _Out_ LPFILETIME lpExitTime,
    _Out_ LPFILETIME lpKernelTime,
    _Out_ LPFILETIME lpUserTime)
{
    KERNEL_USER_TIMES Times;
    NTSTATUS Status = NtQueryInformationProcess(hProcess, ProcessTimes, &Times, sizeof(Times), NULL);

    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    _NtCRT_TimesToFileTimes(&Times, lpCreationTime, lpExitTime, lpKernelTime, lpUserTime);
    return TRUE;
}

NTCRT_API
BOOL
WINAPI
_K32_GetThreadTimes(
    _In_ HANDLE hThread,
    _Out_ LPFILETIME lpCreationTime,
    _Out_ LPFILETIME lpExitTime,
    _Out_ LPFILETIME lpKernelTime,
    _Out_ LPFILETIME lpUserTime)
{
    KERNEL_USER_TIMES Times;
    NTSTATUS Status = NtQueryInformationThread(hThread, ThreadTimes, &Times, sizeof(Times), NULL);

    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    _NtCRT_TimesToFileTimes(&Times, lpCreationTime, lpExitTime, lpKernelTime, lpUserTime);
    return TRUE;
}

/*
 * Threads created by CreateThread run through RtlCreateUserThread. A Win32 thread start routine returns DWORD,
 * and the thread must exit by RtlExitUserThread so the loader sends DLL_THREAD_DETACH.
 */

typedef struct _NTCRT_THREAD_START
{
    LPTHREAD_START_ROUTINE StartAddress;
    LPVOID Parameter;
} NTCRT_THREAD_START, *PNTCRT_THREAD_START;

static
NTSTATUS
NTAPI
_NtCRT_ThreadStart(
    _In_ PVOID Parameter)
{
    NTCRT_THREAD_START Start = *(PNTCRT_THREAD_START)Parameter;

    RtlFreeHeap(NTCRT_HEAP, 0, Parameter);
    RtlExitUserThread((NTSTATUS)Start.StartAddress(Start.Parameter));
}

NTCRT_API
HANDLE
WINAPI
_K32_CreateThread(
    _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
    _In_ SIZE_T dwStackSize,
    _In_ LPTHREAD_START_ROUTINE lpStartAddress,
    _In_opt_ __drv_aliasesMem LPVOID lpParameter,
    _In_ DWORD dwCreationFlags,
    _Out_opt_ LPDWORD lpThreadId)
{
    NTSTATUS Status;
    HANDLE Handle;
    CLIENT_ID ClientId;
    PNTCRT_THREAD_START Start;

    Start = (PNTCRT_THREAD_START)RtlAllocateHeap(NTCRT_HEAP, 0, sizeof(*Start));
    if (Start == NULL)
    {
        _Inline_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    Start->StartAddress = lpStartAddress;
    Start->Parameter = lpParameter;
    Status = RtlCreateUserThread(NtCurrentProcess(),
                                 lpThreadAttributes != NULL ? lpThreadAttributes->lpSecurityDescriptor : NULL,
                                 (dwCreationFlags & CREATE_SUSPENDED) != 0,
                                 0,
                                 (dwCreationFlags & STACK_SIZE_PARAM_IS_A_RESERVATION) ? dwStackSize : 0,
                                 (dwCreationFlags & STACK_SIZE_PARAM_IS_A_RESERVATION) ? 0 : dwStackSize,
                                 (PTHREAD_START_ROUTINE)_NtCRT_ThreadStart,
                                 Start,
                                 &Handle,
                                 &ClientId);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Start);
        _Inline_BaseSetLastNTError(Status);
        return NULL;
    }
    if (lpThreadId != NULL)
    {
        *lpThreadId = (DWORD)(ULONG_PTR)ClientId.UniqueThread;
    }
    return Handle;
}

NTCRT_API __attribute__((noreturn)) VOID WINAPI _K32_ExitThread(_In_ DWORD dwExitCode)
{
    RtlExitUserThread((NTSTATUS)dwExitCode);
}

NTCRT_API
BOOL
WINAPI
_K32_TerminateThread(
    _In_ HANDLE hThread,
    _In_ DWORD dwExitCode)
{
    return _NtCRT_StatusToBool(NtTerminateThread(hThread, (NTSTATUS)dwExitCode));
}

/* Memory */

NTCRT_API
LPVOID
WINAPI
_K32_VirtualAlloc(
    _In_opt_ LPVOID lpAddress,
    _In_ SIZE_T dwSize,
    _In_ DWORD flAllocationType,
    _In_ DWORD flProtect)
{
    NTSTATUS Status = NtAllocateVirtualMemory(NtCurrentProcess(), &lpAddress, 0, &dwSize, flAllocationType, flProtect);

    if (NT_SUCCESS(Status))
    {
        return lpAddress;
    }
    _Inline_BaseSetLastNTError(Status);
    return NULL;
}

NTCRT_API
BOOL
WINAPI
_K32_VirtualFree(
    _In_ LPVOID lpAddress,
    _In_ SIZE_T dwSize,
    _In_ DWORD dwFreeType)
{
    if ((dwFreeType & MEM_RELEASE) && dwSize != 0)
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return _NtCRT_StatusToBool(NtFreeVirtualMemory(NtCurrentProcess(), &lpAddress, &dwSize, dwFreeType));
}

NTCRT_API
BOOL
WINAPI
_K32_VirtualProtect(
    _In_ LPVOID lpAddress,
    _In_ SIZE_T dwSize,
    _In_ DWORD flNewProtect,
    _Out_ PDWORD lpflOldProtect)
{
    return _NtCRT_StatusToBool(NtProtectVirtualMemory(NtCurrentProcess(),
                                                      &lpAddress,
                                                      &dwSize,
                                                      flNewProtect,
                                                      lpflOldProtect));
}

NTCRT_API
SIZE_T
WINAPI
_K32_VirtualQuery(
    _In_opt_ LPCVOID lpAddress,
    _Out_writes_bytes_to_(dwLength, return) PMEMORY_BASIC_INFORMATION lpBuffer,
    _In_ SIZE_T dwLength)
{
    SIZE_T ReturnLength;
    NTSTATUS Status = NtQueryVirtualMemory(NtCurrentProcess(),
                                           (PVOID)lpAddress,
                                           MemoryBasicInformation,
                                           lpBuffer,
                                           dwLength,
                                           &ReturnLength);
    if (NT_SUCCESS(Status))
    {
        return ReturnLength;
    }
    _Inline_BaseSetLastNTError(Status);
    return 0;
}

NTCRT_API
BOOL
WINAPI
_K32_FlushInstructionCache(
    _In_ HANDLE hProcess,
    _In_reads_bytes_opt_(dwSize) LPCVOID lpBaseAddress,
    _In_ SIZE_T dwSize)
{
    return _NtCRT_StatusToBool(NtFlushInstructionCache(hProcess, (PVOID)lpBaseAddress, dwSize));
}

NTCRT_API LPVOID WINAPI _K32_HeapAlloc(_In_ HANDLE hHeap, _In_ DWORD dwFlags, _In_ SIZE_T dwBytes)
{
    return RtlAllocateHeap(hHeap, dwFlags, dwBytes);
}
NTCRT_API LPVOID WINAPI _K32_HeapReAlloc(_Inout_ HANDLE hHeap,
                                         _In_ DWORD dwFlags,
                                         _In_ LPVOID lpMem,
                                         _In_ SIZE_T dwBytes)
{
    return RtlReAllocateHeap(hHeap, dwFlags, lpMem, dwBytes);
}
NTCRT_API BOOL WINAPI _K32_HeapFree(_Inout_ HANDLE hHeap, _In_ DWORD dwFlags, _In_opt_ LPVOID lpMem)
{
    return RtlFreeHeap(hHeap, dwFlags, lpMem);
}
NTCRT_API SIZE_T WINAPI _K32_HeapSize(_In_ HANDLE hHeap, _In_ DWORD dwFlags, _In_ LPCVOID lpMem)
{
    return RtlSizeHeap(hHeap, dwFlags, (PVOID)lpMem);
}

NTCRT_API
HLOCAL
WINAPI
_K32_LocalAlloc(
    _In_ UINT uFlags,
    _In_ SIZE_T uBytes)
{
    PVOID Memory;

    /* Only LMEM_FIXED memory is supported, which is a process heap block */
    if (uFlags & LMEM_MOVEABLE)
    {
        _Inline_SetLastError(ERROR_NOT_SUPPORTED);
        return NULL;
    }
    Memory = RtlAllocateHeap(RtlProcessHeap(), (uFlags & LMEM_ZEROINIT) ? HEAP_ZERO_MEMORY : 0, uBytes);
    if (Memory == NULL)
    {
        _Inline_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    return Memory;
}

NTCRT_API
HLOCAL
WINAPI
_K32_LocalFree(
    _Frees_ptr_opt_ HLOCAL hMem)
{
    if (hMem != NULL && !RtlFreeHeap(RtlProcessHeap(), 0, hMem))
    {
        _Inline_SetLastError(ERROR_INVALID_HANDLE);
        return hMem;
    }
    return NULL;
}

/* Time */

NTCRT_API ULONGLONG WINAPI _K32_GetTickCount64(VOID) { return _Inline_GetTickCount64(); }
NTCRT_API DWORD WINAPI _K32_GetTickCount(VOID) { return _Inline_GetTickCount(); }
NTCRT_API BOOL WINAPI _K32_QueryPerformanceCounter(_Out_ LARGE_INTEGER* lpPerformanceCount)
{
    return _Inline_QueryPerformanceCounter(lpPerformanceCount);
}
NTCRT_API BOOL WINAPI _K32_QueryPerformanceFrequency(_Out_ LARGE_INTEGER* lpFrequency)
{
    return _Inline_QueryPerformanceFrequency(lpFrequency);
}
NTCRT_API VOID WINAPI _K32_GetSystemTimeAsFileTime(_Out_ LPFILETIME lpSystemTimeAsFileTime)
{
    _Inline_GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
}
NTCRT_API VOID WINAPI _K32_GetSystemTimePreciseAsFileTime(_Out_ LPFILETIME lpSystemTimeAsFileTime)
{
    _Inline_GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
}

static
VOID
_NtCRT_TimeFieldsToSystemTime(
    _In_ PTIME_FIELDS TimeFields,
    _Out_ LPSYSTEMTIME lpSystemTime)
{
    lpSystemTime->wYear = TimeFields->Year;
    lpSystemTime->wMonth = TimeFields->Month;
    lpSystemTime->wDayOfWeek = TimeFields->Weekday;
    lpSystemTime->wDay = TimeFields->Day;
    lpSystemTime->wHour = TimeFields->Hour;
    lpSystemTime->wMinute = TimeFields->Minute;
    lpSystemTime->wSecond = TimeFields->Second;
    lpSystemTime->wMilliseconds = TimeFields->Milliseconds;
}

static
VOID
_NtCRT_SystemTimeToTimeFields(
    _In_ const SYSTEMTIME* lpSystemTime,
    _Out_ PTIME_FIELDS TimeFields)
{
    TimeFields->Year = lpSystemTime->wYear;
    TimeFields->Month = lpSystemTime->wMonth;
    TimeFields->Weekday = lpSystemTime->wDayOfWeek;
    TimeFields->Day = lpSystemTime->wDay;
    TimeFields->Hour = lpSystemTime->wHour;
    TimeFields->Minute = lpSystemTime->wMinute;
    TimeFields->Second = lpSystemTime->wSecond;
    TimeFields->Milliseconds = lpSystemTime->wMilliseconds;
}

NTCRT_API
BOOL
WINAPI
_K32_FileTimeToSystemTime(
    _In_ CONST FILETIME* lpFileTime,
    _Out_ LPSYSTEMTIME lpSystemTime)
{
    LARGE_INTEGER Time;
    TIME_FIELDS TimeFields;

    Time.LowPart = lpFileTime->dwLowDateTime;
    Time.HighPart = (LONG)lpFileTime->dwHighDateTime;
    if (Time.QuadPart < 0)
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtlTimeToTimeFields(&Time, &TimeFields);
    _NtCRT_TimeFieldsToSystemTime(&TimeFields, lpSystemTime);
    return TRUE;
}

NTCRT_API
BOOL
WINAPI
_K32_SystemTimeToFileTime(
    _In_ CONST SYSTEMTIME* lpSystemTime,
    _Out_ LPFILETIME lpFileTime)
{
    LARGE_INTEGER Time;
    TIME_FIELDS TimeFields;

    _NtCRT_SystemTimeToTimeFields(lpSystemTime, &TimeFields);
    if (!RtlTimeFieldsToTime(&TimeFields, &Time))
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    lpFileTime->dwLowDateTime = Time.LowPart;
    lpFileTime->dwHighDateTime = Time.HighPart;
    return TRUE;
}

NTCRT_API
VOID
WINAPI
_K32_GetSystemTime(
    _Out_ LPSYSTEMTIME lpSystemTime)
{
    FILETIME FileTime;

    _Inline_GetSystemTimeAsFileTime(&FileTime);
    _K32_FileTimeToSystemTime(&FileTime, lpSystemTime);
}

NTCRT_API
VOID
WINAPI
_K32_GetLocalTime(
    _Out_ LPSYSTEMTIME lpSystemTime)
{
    LARGE_INTEGER SystemTime, LocalTime;
    TIME_FIELDS TimeFields;

    NtQuerySystemTime(&SystemTime);
    RtlSystemTimeToLocalTime(&SystemTime, &LocalTime);
    RtlTimeToTimeFields(&LocalTime, &TimeFields);
    _NtCRT_TimeFieldsToSystemTime(&TimeFields, lpSystemTime);
}

NTCRT_API
BOOL
WINAPI
_K32_SetSystemTime(
    _In_ CONST SYSTEMTIME* lpSystemTime)
{
    LARGE_INTEGER Time;
    TIME_FIELDS TimeFields;

    _NtCRT_SystemTimeToTimeFields(lpSystemTime, &TimeFields);
    if (!RtlTimeFieldsToTime(&TimeFields, &Time))
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return _NtCRT_StatusToBool(NtSetSystemTime(&Time, NULL));
}

NTCRT_API
BOOL
WINAPI
_K32_GetSystemTimeAdjustment(
    _Out_ PDWORD lpTimeAdjustment,
    _Out_ PDWORD lpTimeIncrement,
    _Out_ PBOOL lpTimeAdjustmentDisabled)
{
    SYSTEM_QUERY_TIME_ADJUST_INFORMATION Info;
    NTSTATUS Status = NtQuerySystemInformation(SystemTimeAdjustmentInformation, &Info, sizeof(Info), NULL);

    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    *lpTimeAdjustment = Info.TimeAdjustment;
    *lpTimeIncrement = Info.TimeIncrement;
    *lpTimeAdjustmentDisabled = Info.Enable;
    return TRUE;
}

NTCRT_API
DWORD
WINAPI
_K32_GetTimeZoneInformation(
    _Out_ LPTIME_ZONE_INFORMATION lpTimeZoneInformation)
{
    RTL_TIME_ZONE_INFORMATION Info;
    NTSTATUS Status = NtQuerySystemInformation(SystemCurrentTimeZoneInformation, &Info, sizeof(Info), NULL);

    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return TIME_ZONE_ID_INVALID;
    }
    lpTimeZoneInformation->Bias = Info.Bias;
    memcpy(lpTimeZoneInformation->StandardName, Info.StandardName, sizeof(Info.StandardName));
    _NtCRT_TimeFieldsToSystemTime(&Info.StandardStart, &lpTimeZoneInformation->StandardDate);
    lpTimeZoneInformation->StandardBias = Info.StandardBias;
    memcpy(lpTimeZoneInformation->DaylightName, Info.DaylightName, sizeof(Info.DaylightName));
    _NtCRT_TimeFieldsToSystemTime(&Info.DaylightStart, &lpTimeZoneInformation->DaylightDate);
    lpTimeZoneInformation->DaylightBias = Info.DaylightBias;
    return SharedUserData->TimeZoneId;
}

/* System information */

NTCRT_API
VOID
WINAPI
_K32_GetSystemInfo(
    _Out_ LPSYSTEM_INFO lpSystemInfo)
{
    SYSTEM_BASIC_INFORMATION Info;

    RtlZeroMemory(lpSystemInfo, sizeof(*lpSystemInfo));
    if (!NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &Info, sizeof(Info), NULL)))
    {
        return;
    }
    lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
    lpSystemInfo->dwPageSize = Info.PageSize;
    lpSystemInfo->lpMinimumApplicationAddress = (LPVOID)Info.MinimumUserModeAddress;
    lpSystemInfo->lpMaximumApplicationAddress = (LPVOID)Info.MaximumUserModeAddress;
    lpSystemInfo->dwActiveProcessorMask = Info.ActiveProcessorsAffinityMask;
    lpSystemInfo->dwNumberOfProcessors = (BYTE)Info.NumberOfProcessors;
    lpSystemInfo->dwProcessorType = PROCESSOR_AMD_X8664;
    lpSystemInfo->dwAllocationGranularity = Info.AllocationGranularity;
}

NTCRT_API BOOL WINAPI _K32_IsProcessorFeaturePresent(_In_ DWORD ProcessorFeature)
{
    return _Inline_IsProcessorFeaturePresent(ProcessorFeature);
}

/* Message table resources of kernelbase.dll (or kernel32.dll before Win7) contain the system messages */
static
PVOID
_NtCRT_GetSystemMessageModule(VOID)
{
    static PVOID Module = NULL;

    if (Module == NULL)
    {
        Module = _Inline_GetModuleHandleW(L"kernelbase.dll");
        if (Module == NULL)
        {
            Module = _Inline_GetModuleHandleW(L"kernel32.dll");
        }
    }
    return Module;
}

NTCRT_API
DWORD
WINAPI
_K32_FormatMessageW(
    _In_ DWORD dwFlags,
    _In_opt_ LPCVOID lpSource,
    _In_ DWORD dwMessageId,
    _In_ DWORD dwLanguageId,
    _When_((dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) != 0, _At_((LPWSTR*)lpBuffer, _Outptr_result_z_))
    _When_((dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) == 0, _Out_writes_z_(nSize))
    LPWSTR lpBuffer,
    _In_ DWORD nSize,
    _In_opt_ va_list* Arguments)
{
    static const LANGID Languages[] = {
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_SYS_DEFAULT),
        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
    };
    NTSTATUS Status = STATUS_INVALID_PARAMETER;
    PMESSAGE_RESOURCE_ENTRY Entry = NULL;
    PVOID Module;
    PWSTR Text;
    ULONG Length;

    UNREFERENCED_PARAMETER(Arguments);

    /* Only system or module message tables without inserts are supported */
    if (dwFlags & FORMAT_MESSAGE_FROM_STRING)
    {
        _Inline_SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (dwFlags & FORMAT_MESSAGE_FROM_HMODULE)
    {
        Module = lpSource != NULL ? (PVOID)lpSource : NtCurrentPeb()->ImageBaseAddress;
    } else if (dwFlags & FORMAT_MESSAGE_FROM_SYSTEM)
    {
        Module = _NtCRT_GetSystemMessageModule();
    } else
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (Module != NULL)
    {
        if (dwLanguageId != 0)
        {
            Status = RtlFindMessage(Module, 11, dwLanguageId, dwMessageId, &Entry);
        } else
        {
            for (UINT i = 0; i < ARRAYSIZE(Languages); i++)
            {
                Status = RtlFindMessage(Module, 11, Languages[i], dwMessageId, &Entry);
                if (NT_SUCCESS(Status))
                {
                    break;
                }
            }
        }
    }
    if (!NT_SUCCESS(Status) || Entry == NULL)
    {
        _Inline_SetLastError(ERROR_MR_MID_NOT_FOUND);
        return 0;
    }

    /* Convert the entry to a temporary wide string */
    Length = Entry->Length - FIELD_OFFSET(MESSAGE_RESOURCE_ENTRY, Text);
    Text = (PWSTR)RtlAllocateHeap(RtlProcessHeap(), 0, (Length + 1) * sizeof(WCHAR));
    if (Text == NULL)
    {
        _Inline_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    if (Entry->Flags & MESSAGE_RESOURCE_UNICODE)
    {
        Length /= sizeof(WCHAR);
        memcpy(Text, Entry->Text, Length * sizeof(WCHAR));
    } else
    {
        Length = _NtCRT_MultiByteToWideChar((Entry->Flags & MESSAGE_RESOURCE_UTF8) ? CP_UTF8 : CP_ACP,
                                            0,
                                            (LPCCH)Entry->Text,
                                            Length,
                                            Text,
                                            Length);
    }
    while (Length != 0 && Text[Length - 1] == UNICODE_NULL)
    {
        Length--;
    }
    Text[Length] = UNICODE_NULL;

    if (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
    {
        *(PWSTR*)lpBuffer = Text;
        return Length;
    }
    if (Length + 1 > nSize)
    {
        RtlFreeHeap(RtlProcessHeap(), 0, Text);
        _Inline_SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(lpBuffer, Text, (Length + 1) * sizeof(WCHAR));
    RtlFreeHeap(RtlProcessHeap(), 0, Text);
    return Length;
}

NTCRT_API
DWORD
WINAPI
_K32_FormatMessageA(
    _In_ DWORD dwFlags,
    _In_opt_ LPCVOID lpSource,
    _In_ DWORD dwMessageId,
    _In_ DWORD dwLanguageId,
    _When_((dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) != 0, _At_((LPSTR*)lpBuffer, _Outptr_result_z_))
    _When_((dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) == 0, _Out_writes_z_(nSize))
    LPSTR lpBuffer,
    _In_ DWORD nSize,
    _In_opt_ va_list* Arguments)
{
    PWSTR Text;
    DWORD Length;
    int Bytes;

    Length = _K32_FormatMessageW(dwFlags | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                 lpSource,
                                 dwMessageId,
                                 dwLanguageId,
                                 (LPWSTR)&Text,
                                 0,
                                 Arguments);
    if (Length == 0)
    {
        return 0;
    }
    Bytes = _NtCRT_WideCharToMultiByte(CP_ACP, 0, Text, Length + 1, NULL, 0, NULL, NULL);
    if (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
    {
        char* Buffer = (char*)RtlAllocateHeap(RtlProcessHeap(), 0, Bytes);
        if (Buffer == NULL)
        {
            RtlFreeHeap(RtlProcessHeap(), 0, Text);
            _Inline_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        *(char**)lpBuffer = Buffer;
        lpBuffer = Buffer;
        nSize = Bytes;
    } else if ((DWORD)Bytes > nSize)
    {
        RtlFreeHeap(RtlProcessHeap(), 0, Text);
        _Inline_SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    _NtCRT_WideCharToMultiByte(CP_ACP, 0, Text, Length + 1, lpBuffer, nSize, NULL, NULL);
    RtlFreeHeap(RtlProcessHeap(), 0, Text);
    return Bytes - 1;
}

NTCRT_IMPORT(GetCurrentProcess, _K32_GetCurrentProcess);
NTCRT_IMPORT(GetCurrentThread, _K32_GetCurrentThread);
NTCRT_IMPORT(GetCurrentProcessId, _K32_GetCurrentProcessId);
NTCRT_IMPORT(GetCurrentThreadId, _K32_GetCurrentThreadId);
NTCRT_IMPORT(IsDebuggerPresent, _K32_IsDebuggerPresent);
NTCRT_IMPORT(GetProcessHeap, _K32_GetProcessHeap);
NTCRT_IMPORT(GetStdHandle, _K32_GetStdHandle);
NTCRT_IMPORT(SetStdHandle, _K32_SetStdHandle);
NTCRT_IMPORT(GetEnvironmentStringsW, _K32_GetEnvironmentStringsW);
NTCRT_IMPORT(FreeEnvironmentStringsW, _K32_FreeEnvironmentStringsW);
NTCRT_IMPORT(GetStartupInfoW, _K32_GetStartupInfoW);
NTCRT_IMPORT(GetStartupInfoA, _K32_GetStartupInfoA);
NTCRT_IMPORT(GetCommandLineW, _K32_GetCommandLineW);
NTCRT_IMPORT(GetCommandLineA, _K32_GetCommandLineA);
NTCRT_IMPORT(ExitProcess, _K32_ExitProcess);
NTCRT_IMPORT(TerminateProcess, _K32_TerminateProcess);
NTCRT_IMPORT(GetModuleHandleW, _K32_GetModuleHandleW);
NTCRT_IMPORT(GetModuleHandleA, _K32_GetModuleHandleA);
NTCRT_IMPORT(GetModuleHandleExW, _K32_GetModuleHandleExW);
NTCRT_IMPORT(GetModuleFileNameW, _K32_GetModuleFileNameW);
NTCRT_IMPORT(GetModuleFileNameA, _K32_GetModuleFileNameA);
NTCRT_IMPORT(RaiseException, _K32_RaiseException);
NTCRT_IMPORT(AddVectoredExceptionHandler, _K32_AddVectoredExceptionHandler);
NTCRT_IMPORT(RemoveVectoredExceptionHandler, _K32_RemoveVectoredExceptionHandler);
NTCRT_IMPORT(AddVectoredContinueHandler, _K32_AddVectoredContinueHandler);
NTCRT_IMPORT(RemoveVectoredContinueHandler, _K32_RemoveVectoredContinueHandler);
NTCRT_IMPORT(OutputDebugStringA, _K32_OutputDebugStringA);
NTCRT_IMPORT(OutputDebugStringW, _K32_OutputDebugStringW);
NTCRT_IMPORT(CloseHandle, _K32_CloseHandle);
NTCRT_IMPORT(DuplicateHandle, _K32_DuplicateHandle);
NTCRT_IMPORT(GetHandleInformation, _K32_GetHandleInformation);
NTCRT_IMPORT(SetHandleInformation, _K32_SetHandleInformation);
NTCRT_IMPORT(CreateEventW, _K32_CreateEventW);
NTCRT_IMPORT(CreateEventA, _K32_CreateEventA);
NTCRT_IMPORT(CreateSemaphoreW, _K32_CreateSemaphoreW);
NTCRT_IMPORT(CreateSemaphoreA, _K32_CreateSemaphoreA);
NTCRT_IMPORT(CreateMutexW, _K32_CreateMutexW);
NTCRT_IMPORT(SetEvent, _K32_SetEvent);
NTCRT_IMPORT(ResetEvent, _K32_ResetEvent);
NTCRT_IMPORT(ReleaseMutex, _K32_ReleaseMutex);
NTCRT_IMPORT(ReleaseSemaphore, _K32_ReleaseSemaphore);
NTCRT_IMPORT(WaitForSingleObjectEx, _K32_WaitForSingleObjectEx);
NTCRT_IMPORT(WaitForSingleObject, _K32_WaitForSingleObject);
NTCRT_IMPORT(WaitForMultipleObjectsEx, _K32_WaitForMultipleObjectsEx);
NTCRT_IMPORT(WaitForMultipleObjects, _K32_WaitForMultipleObjects);
NTCRT_IMPORT(InitializeSRWLock, _K32_InitializeSRWLock);
NTCRT_IMPORT(AcquireSRWLockExclusive, _K32_AcquireSRWLockExclusive);
NTCRT_IMPORT(ReleaseSRWLockExclusive, _K32_ReleaseSRWLockExclusive);
NTCRT_IMPORT(AcquireSRWLockShared, _K32_AcquireSRWLockShared);
NTCRT_IMPORT(ReleaseSRWLockShared, _K32_ReleaseSRWLockShared);
NTCRT_IMPORT(TryAcquireSRWLockExclusive, _K32_TryAcquireSRWLockExclusive);
NTCRT_IMPORT(TryAcquireSRWLockShared, _K32_TryAcquireSRWLockShared);
NTCRT_IMPORT(InitializeConditionVariable, _K32_InitializeConditionVariable);
NTCRT_IMPORT(WakeConditionVariable, _K32_WakeConditionVariable);
NTCRT_IMPORT(WakeAllConditionVariable, _K32_WakeAllConditionVariable);
NTCRT_IMPORT(SleepConditionVariableCS, _K32_SleepConditionVariableCS);
NTCRT_IMPORT(SleepConditionVariableSRW, _K32_SleepConditionVariableSRW);
NTCRT_IMPORT(InitOnceExecuteOnce, _K32_InitOnceExecuteOnce);
NTCRT_IMPORT(OpenProcess, _K32_OpenProcess);
NTCRT_IMPORT(OpenThread, _K32_OpenThread);
NTCRT_IMPORT(GetExitCodeProcess, _K32_GetExitCodeProcess);
NTCRT_IMPORT(GetExitCodeThread, _K32_GetExitCodeThread);
NTCRT_IMPORT(GetThreadId, _K32_GetThreadId);
NTCRT_IMPORT(GetProcessAffinityMask, _K32_GetProcessAffinityMask);
NTCRT_IMPORT(SetProcessAffinityMask, _K32_SetProcessAffinityMask);
NTCRT_IMPORT(SetThreadAffinityMask, _K32_SetThreadAffinityMask);
NTCRT_IMPORT(GetThreadContext, _K32_GetThreadContext);
NTCRT_IMPORT(SetThreadContext, _K32_SetThreadContext);
NTCRT_IMPORT(GetThreadPriority, _K32_GetThreadPriority);
NTCRT_IMPORT(SetThreadPriority, _K32_SetThreadPriority);
NTCRT_IMPORT(SuspendThread, _K32_SuspendThread);
NTCRT_IMPORT(ResumeThread, _K32_ResumeThread);
NTCRT_IMPORT(GetProcessTimes, _K32_GetProcessTimes);
NTCRT_IMPORT(GetThreadTimes, _K32_GetThreadTimes);
NTCRT_IMPORT(CreateThread, _K32_CreateThread);
NTCRT_IMPORT(ExitThread, _K32_ExitThread);
NTCRT_IMPORT(TerminateThread, _K32_TerminateThread);
NTCRT_IMPORT(VirtualAlloc, _K32_VirtualAlloc);
NTCRT_IMPORT(VirtualFree, _K32_VirtualFree);
NTCRT_IMPORT(VirtualProtect, _K32_VirtualProtect);
NTCRT_IMPORT(VirtualQuery, _K32_VirtualQuery);
NTCRT_IMPORT(FlushInstructionCache, _K32_FlushInstructionCache);
NTCRT_IMPORT(HeapAlloc, _K32_HeapAlloc);
NTCRT_IMPORT(HeapReAlloc, _K32_HeapReAlloc);
NTCRT_IMPORT(HeapFree, _K32_HeapFree);
NTCRT_IMPORT(HeapSize, _K32_HeapSize);
NTCRT_IMPORT(LocalAlloc, _K32_LocalAlloc);
NTCRT_IMPORT(LocalFree, _K32_LocalFree);
NTCRT_IMPORT(GetTickCount64, _K32_GetTickCount64);
NTCRT_IMPORT(GetTickCount, _K32_GetTickCount);
NTCRT_IMPORT(QueryPerformanceCounter, _K32_QueryPerformanceCounter);
NTCRT_IMPORT(QueryPerformanceFrequency, _K32_QueryPerformanceFrequency);
NTCRT_IMPORT(GetSystemTimeAsFileTime, _K32_GetSystemTimeAsFileTime);
NTCRT_IMPORT(GetSystemTimePreciseAsFileTime, _K32_GetSystemTimePreciseAsFileTime);
NTCRT_IMPORT(FileTimeToSystemTime, _K32_FileTimeToSystemTime);
NTCRT_IMPORT(SystemTimeToFileTime, _K32_SystemTimeToFileTime);
NTCRT_IMPORT(GetSystemTime, _K32_GetSystemTime);
NTCRT_IMPORT(GetLocalTime, _K32_GetLocalTime);
NTCRT_IMPORT(SetSystemTime, _K32_SetSystemTime);
NTCRT_IMPORT(GetSystemTimeAdjustment, _K32_GetSystemTimeAdjustment);
NTCRT_IMPORT(GetTimeZoneInformation, _K32_GetTimeZoneInformation);
NTCRT_IMPORT(GetSystemInfo, _K32_GetSystemInfo);
NTCRT_IMPORT(IsProcessorFeaturePresent, _K32_IsProcessorFeaturePresent);
NTCRT_IMPORT(FormatMessageW, _K32_FormatMessageW);
NTCRT_IMPORT(FormatMessageA, _K32_FormatMessageA);
