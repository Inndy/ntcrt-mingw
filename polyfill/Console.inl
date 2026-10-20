#pragma once

#include <KNSoft/NDK/NDK.inl>

#include "Process.inl"

typedef BOOL(WINAPI* PCRT_GET_CONSOLE_MODE)(
    _In_ HANDLE hConsoleHandle,
    _Out_ LPDWORD lpMode);

typedef UINT(WINAPI* PCRT_GET_CONSOLE_OUTPUT_CP)(VOID);

static
PCRT_GET_CONSOLE_MODE
__cdecl
try_get_console_mode(VOID) throw();

static
PCRT_GET_CONSOLE_OUTPUT_CP
__cdecl
try_get_console_output_cp(VOID) throw();

/*
 * A request to the console driver: the message, the data if InputCount is 2, and the result (the part of the message
 * after its header). A WOW64 process sends it in the layout of x64.
 */
typedef struct _CRT_CONSOLE_USER_IO
{
    HANDLE Client;
    ULONG InputCount;
    ULONG OutputCount;
    CD_IO_BUFFER Buffers[3];
} _CRT_CONSOLE_USER_IO;

/* Not CD_USER_DEFINED_IO64 of NDK.h: its PVOID64 has 32 bits in GCC, which has no __ptr64 */
typedef struct _CRT_CONSOLE_IO_BUFFER64
{
    ULONG Size;
    ULONGLONG Buffer;
} _CRT_CONSOLE_IO_BUFFER64;

typedef struct _CRT_CONSOLE_USER_IO64
{
    ULONGLONG Client;
    ULONG InputCount;
    ULONG OutputCount;
    _CRT_CONSOLE_IO_BUFFER64 Buffers[3];
} _CRT_CONSOLE_USER_IO64;

static_assert(sizeof(_CRT_CONSOLE_USER_IO64) == 64, "the request of x64");

EXTERN_C
NTSTATUS
WINAPI
_CRT_ConsoleApiCall(
    _In_ HANDLE ConsoleHandle,
    _Inout_ PCONSOLE_MSG_L1 Message,
    _In_ ULONG MessageSize,
    _In_ ULONG InputCount,
    _In_reads_bytes_opt_(DataSize) const VOID* Data,
    _In_ ULONG DataSize,
    _In_ ULONG ApiDescriptorSize)
{
    IO_STATUS_BLOCK IoStatusBlock;
    _CRT_CONSOLE_USER_IO Request;
    HANDLE ConsoleServerHandle;

    ConsoleServerHandle = NtCurrentPeb()->ProcessParameters->ConsoleHandle;
    if ((LONG_PTR)ConsoleServerHandle <= 0)
    {
        return STATUS_INVALID_HANDLE;
    }

#if defined(_M_IX86)
    if (NtReadTeb(WowTebOffset) < 0)
    {
        _CRT_CONSOLE_USER_IO64 Request64;

        RtlZeroMemory(&Request64, sizeof(Request64));
        /* A handle is sign extended, as WOW64 does */
        Request64.Client = (ULONGLONG)(LONGLONG)(LONG_PTR)ConsoleHandle;
        Request64.InputCount = InputCount;
        Request64.OutputCount = 1;
        Request64.Buffers[0].Size = MessageSize;
        Request64.Buffers[0].Buffer = (ULONG_PTR)Message;
        Request64.Buffers[1].Size = DataSize;
        Request64.Buffers[1].Buffer = (ULONG_PTR)Data;
        Request64.Buffers[InputCount].Size = ApiDescriptorSize;
        Request64.Buffers[InputCount].Buffer = (ULONG_PTR)&Message->u;

        return NtDeviceIoControlFile(ConsoleServerHandle,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &IoStatusBlock,
                                     IOCTL_CONDRV_ISSUE_USER_IO,
                                     &Request64,
                                     FIELD_OFFSET(_CRT_CONSOLE_USER_IO64, Buffers) +
                                         (InputCount + 1) * sizeof(_CRT_CONSOLE_IO_BUFFER64),
                                     NULL,
                                     0);
    }
#endif

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Client = ConsoleHandle;
    Request.InputCount = InputCount;
    Request.OutputCount = 1;
    Request.Buffers[0].Size = MessageSize;
    Request.Buffers[0].Buffer = Message;
    Request.Buffers[1].Size = DataSize;
    Request.Buffers[1].Buffer = (PVOID)Data;
    Request.Buffers[InputCount].Size = ApiDescriptorSize;
    Request.Buffers[InputCount].Buffer = &Message->u;

    return NtDeviceIoControlFile(ConsoleServerHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 IOCTL_CONDRV_ISSUE_USER_IO,
                                 &Request,
                                 FIELD_OFFSET(_CRT_CONSOLE_USER_IO, Buffers) + (InputCount + 1) * sizeof(CD_IO_BUFFER),
                                 NULL,
                                 0);
}

EXTERN_C
BOOL
WINAPI
_CRT_GetConsoleMode(
    _In_ HANDLE hConsoleHandle,
    _Out_ LPDWORD lpMode)
{
    NTSTATUS Status;
    CONSOLE_MSG_L1 Message;

    if (!IS_NT_VERSION_GE(NT_VERSION_WIN8))
    {
        /* Win7 kernel32 uses a private ConsoleLpcHandle initialized by ConsoleConnect. */
        auto const GetConsoleMode = try_get_console_mode();
        if (GetConsoleMode == NULL)
        {
            _Inline_SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
            return FALSE;
        }
        return GetConsoleMode(hConsoleHandle, lpMode);
    }

    RtlZeroMemory(&Message, sizeof(Message));
    Message.ApiNumber = ConsolepGetMode;
    Message.ApiDescriptorSize = sizeof(Message.u.GetConsoleMode);

    Status = _CRT_ConsoleApiCall(hConsoleHandle,
                                 &Message,
                                 sizeof(CONSOLE_MSG_HEADER) + sizeof(Message.u.GetConsoleMode),
                                 1,
                                 NULL,
                                 0,
                                 sizeof(Message.u.GetConsoleMode));
    if (NT_SUCCESS(Status))
    {
        *lpMode = Message.u.GetConsoleMode.Mode;
        return TRUE;
    }

    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

EXTERN_C
UINT
WINAPI
_CRT_GetConsoleOutputCP(VOID)
{
    NTSTATUS Status;
    CONSOLE_MSG_L1 Message;

    if (!IS_NT_VERSION_GE(NT_VERSION_WIN8))
    {
        /* Win7 kernel32 uses a private ConsoleLpcHandle initialized by ConsoleConnect. */
        auto const GetConsoleOutputCP = try_get_console_output_cp();
        if (GetConsoleOutputCP == NULL)
        {
            _Inline_SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
            return 0;
        }
        return GetConsoleOutputCP();
    }

    RtlZeroMemory(&Message, sizeof(Message));
    Message.ApiNumber = ConsolepGetCP;
    Message.ApiDescriptorSize = sizeof(Message.u.GetConsoleCP);
    Message.u.GetConsoleCP.Output = TRUE;

    Status = _CRT_ConsoleApiCall(NULL,
                                 &Message,
                                 sizeof(CONSOLE_MSG_HEADER) + sizeof(Message.u.GetConsoleCP),
                                 1,
                                 NULL,
                                 0,
                                 sizeof(Message.u.GetConsoleCP));
    if (NT_SUCCESS(Status))
    {
        return (UINT)Message.u.GetConsoleCP.CodePage;
    }

    _Inline_BaseSetLastNTError(Status);
    return 0;
}
