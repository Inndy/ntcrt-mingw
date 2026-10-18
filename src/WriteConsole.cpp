/* WriteConsoleW of kernel32, the console output of _write */

#include "NtCRT.h"

/* WriteConsoleW through condrv, the request carries the message and the text as two input buffers */

typedef struct _NTCRT_CONSOLE_WRITE_IO
{
    CD_USER_DEFINED_IO Header;
    CD_IO_BUFFER DataBuffer;
    CD_IO_BUFFER ResultBuffer;
} NTCRT_CONSOLE_WRITE_IO;

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
    IO_STATUS_BLOCK IoStatusBlock;
    NTCRT_CONSOLE_WRITE_IO Request;
    CONSOLE_MSG_L1 Message;
    HANDLE ConsoleServerHandle;

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

    ConsoleServerHandle = NtCurrentPeb()->ProcessParameters->ConsoleHandle;
    if ((LONG_PTR)ConsoleServerHandle <= 0)
    {
        _Inline_BaseSetLastNTError(STATUS_INVALID_HANDLE);
        return FALSE;
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

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Header.Client = hConsoleOutput;
    Request.Header.InputCount = 2;
    Request.Header.OutputCount = 1;
    Request.Header.Buffers[0].Size = sizeof(CONSOLE_MSG_HEADER) + sizeof(Message.u.WriteConsole);
    Request.Header.Buffers[0].Buffer = &Message;
    Request.DataBuffer.Size = nNumberOfCharsToWrite * sizeof(WCHAR);
    Request.DataBuffer.Buffer = (PVOID)lpBuffer;
    Request.ResultBuffer.Size = sizeof(Message.u.WriteConsole);
    Request.ResultBuffer.Buffer = &Message.u;

    Status = NtDeviceIoControlFile(ConsoleServerHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_CONDRV_ISSUE_USER_IO,
                                   &Request,
                                   sizeof(Request),
                                   NULL,
                                   0);
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
