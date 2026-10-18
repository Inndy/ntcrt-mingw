/* Last error, sleep, critical sections and TLS of kernel32, which libmingw32 uses in every program */

#include "NtCRT.h"

/* Error */

NTCRT_API DWORD WINAPI _K32_GetLastError(VOID) { return _Inline_GetLastError(); }
NTCRT_API VOID WINAPI _K32_SetLastError(_In_ DWORD dwErrCode) { _Inline_SetLastError(dwErrCode); }

NTCRT_API
DWORD
WINAPI
_K32_SleepEx(
    _In_ DWORD dwMilliseconds,
    _In_ BOOL bAlertable)
{
    LARGE_INTEGER Interval;
    NTSTATUS Status;

    if (dwMilliseconds == INFINITE)
    {
        Interval.QuadPart = MINLONGLONG;
    } else
    {
        Interval.QuadPart = Int32x32To64(dwMilliseconds, -10000);
    }
    do
    {
        Status = NtDelayExecution((BOOLEAN)bAlertable, &Interval);
    } while (bAlertable && Status == STATUS_ALERTED);
    return Status == STATUS_USER_APC ? WAIT_IO_COMPLETION : 0;
}

NTCRT_API VOID WINAPI _K32_Sleep(_In_ DWORD dwMilliseconds) { _K32_SleepEx(dwMilliseconds, FALSE); }
NTCRT_API BOOL WINAPI _K32_SwitchToThread(VOID) { return NtYieldExecution() != STATUS_NO_YIELD_PERFORMED; }

/* Critical section */

NTCRT_API VOID WINAPI _K32_InitializeCriticalSection(_Out_ LPCRITICAL_SECTION cs)
{
    _Inline_InitializeCriticalSection(cs);
}
NTCRT_API BOOL WINAPI _K32_InitializeCriticalSectionAndSpinCount(_Out_ LPCRITICAL_SECTION cs, _In_ DWORD n)
{
    return _Inline_InitializeCriticalSectionAndSpinCount(cs, n);
}
NTCRT_API BOOL WINAPI _K32_InitializeCriticalSectionEx(_Out_ LPCRITICAL_SECTION cs, _In_ DWORD n, _In_ DWORD f)
{
    return _Inline_InitializeCriticalSectionEx(cs, n, f);
}
NTCRT_API VOID WINAPI _K32_DeleteCriticalSection(_Inout_ LPCRITICAL_SECTION cs) { _Inline_DeleteCriticalSection(cs); }
NTCRT_API VOID WINAPI _K32_EnterCriticalSection(_Inout_ LPCRITICAL_SECTION cs) { _Inline_EnterCriticalSection(cs); }
NTCRT_API VOID WINAPI _K32_LeaveCriticalSection(_Inout_ LPCRITICAL_SECTION cs) { _Inline_LeaveCriticalSection(cs); }
NTCRT_API BOOL WINAPI _K32_TryEnterCriticalSection(_Inout_ LPCRITICAL_SECTION cs)
{
    return _Inline_TryEnterCriticalSection(cs);
}
NTCRT_API DWORD WINAPI _K32_SetCriticalSectionSpinCount(_Inout_ LPCRITICAL_SECTION cs, _In_ DWORD n)
{
    return _Inline_SetCriticalSectionSpinCount(cs, n);
}

/* TLS */

NTCRT_API DWORD WINAPI _K32_TlsAlloc(VOID) { return _Inline_TlsAlloc(); }
NTCRT_API BOOL WINAPI _K32_TlsFree(_In_ DWORD dwTlsIndex) { return _Inline_TlsFree(dwTlsIndex); }
NTCRT_API LPVOID WINAPI _K32_TlsGetValue(_In_ DWORD dwTlsIndex) { return _Inline_TlsGetValue(dwTlsIndex); }
NTCRT_API BOOL WINAPI _K32_TlsSetValue(_In_ DWORD dwTlsIndex, _In_opt_ LPVOID lpTlsValue)
{
    return _Inline_TlsSetValue(dwTlsIndex, lpTlsValue);
}

NTCRT_IMPORT(GetLastError, _K32_GetLastError);
NTCRT_IMPORT(SetLastError, _K32_SetLastError);
NTCRT_IMPORT(SleepEx, _K32_SleepEx);
NTCRT_IMPORT(Sleep, _K32_Sleep);
NTCRT_IMPORT(SwitchToThread, _K32_SwitchToThread);
NTCRT_IMPORT(InitializeCriticalSection, _K32_InitializeCriticalSection);
NTCRT_IMPORT(InitializeCriticalSectionAndSpinCount, _K32_InitializeCriticalSectionAndSpinCount);
NTCRT_IMPORT(InitializeCriticalSectionEx, _K32_InitializeCriticalSectionEx);
NTCRT_IMPORT(DeleteCriticalSection, _K32_DeleteCriticalSection);
NTCRT_IMPORT(EnterCriticalSection, _K32_EnterCriticalSection);
NTCRT_IMPORT(LeaveCriticalSection, _K32_LeaveCriticalSection);
NTCRT_IMPORT(TryEnterCriticalSection, _K32_TryEnterCriticalSection);
NTCRT_IMPORT(SetCriticalSectionSpinCount, _K32_SetCriticalSectionSpinCount);
NTCRT_IMPORT(TlsAlloc, _K32_TlsAlloc);
NTCRT_IMPORT(TlsFree, _K32_TlsFree);
NTCRT_IMPORT(TlsGetValue, _K32_TlsGetValue);
NTCRT_IMPORT(TlsSetValue, _K32_TlsSetValue);
