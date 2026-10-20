/* WriteConsoleW of kernel32, the console output of _write */

#include "NtCRT.h"

/* WriteConsoleW through condrv, the request carries the message and the text as two input buffers */

typedef BOOL(WINAPI* PNTCRT_WRITE_CONSOLE_W)(HANDLE, const VOID*, DWORD, LPDWORD, LPVOID);

EXTERN_C
BOOL
WINAPI
_NtCRT_WriteConsoleW(
    _In_ HANDLE hConsoleOutput,
    _In_reads_(nNumberOfCharsToWrite) const VOID* lpBuffer,
    _In_ DWORD nNumberOfCharsToWrite,
    _Out_opt_ LPDWORD lpNumberOfCharsWritten)
{
    NTSTATUS Status;
    CONSOLE_MSG_L1 Message;

    if (lpNumberOfCharsWritten != NULL)
    {
        *lpNumberOfCharsWritten = 0;
    }
    hConsoleOutput = _Inline_BasepMapStdHandle(hConsoleOutput);

    if (!IS_NT_VERSION_GE(NT_VERSION_WIN8))
    {
        auto const WriteConsoleW = (PNTCRT_WRITE_CONSOLE_W)_NtCRT_GetKernel32Function("WriteConsoleW");
        if (WriteConsoleW == NULL)
        {
            _Inline_SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
            return FALSE;
        }
        return WriteConsoleW(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten, NULL);
    }

    if (nNumberOfCharsToWrite > MAXULONG / sizeof(WCHAR))
    {
        _Inline_BaseSetLastNTError(STATUS_INVALID_PARAMETER);
        return FALSE;
    }

    RtlZeroMemory(&Message, sizeof(Message));
    Message.ApiNumber = ConsolepWriteConsole;
    Message.ApiDescriptorSize = sizeof(Message.u.WriteConsole);
    Message.u.WriteConsole.NumBytes = nNumberOfCharsToWrite * sizeof(WCHAR);
    Message.u.WriteConsole.Unicode = TRUE;

    Status = _CRT_ConsoleApiCall(hConsoleOutput,
                                 &Message,
                                 sizeof(CONSOLE_MSG_HEADER) + sizeof(Message.u.WriteConsole),
                                 2,
                                 lpBuffer,
                                 nNumberOfCharsToWrite * sizeof(WCHAR),
                                 sizeof(Message.u.WriteConsole));
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    if (lpNumberOfCharsWritten != NULL)
    {
        *lpNumberOfCharsWritten = Message.u.WriteConsole.NumBytes / sizeof(WCHAR);
    }
    return TRUE;
}

NTCRT_API
BOOL
WINAPI
_K32_WriteConsoleW(
    _In_ HANDLE hConsoleOutput,
    _In_reads_(nNumberOfCharsToWrite) const VOID* lpBuffer,
    _In_ DWORD nNumberOfCharsToWrite,
    _Out_opt_ LPDWORD lpNumberOfCharsWritten,
    _Reserved_ LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(lpReserved);
    return _NtCRT_WriteConsoleW(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten);
}

NTCRT_IMPORT(WriteConsoleW, _K32_WriteConsoleW);
