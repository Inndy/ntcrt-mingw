/* The table of file descriptors, its initialization at startup and the raw writes of fd 2 by the runtime */

#include "NtCRT.h"
#include "LowIO.inl"

#include <errno.h>

PNTCRT_FD _NtCRT_FdBlocks[NTCRT_FD_MAX / NTCRT_FD_BLOCK_SIZE];
RTL_SRWLOCK _NtCRT_FdTableLock = RTL_SRWLOCK_INIT;

EXTERN_C
PNTCRT_FD
_NtCRT_AllocBlock(
    _In_ unsigned int BlockIndex)
{
    PNTCRT_FD Block = (PNTCRT_FD)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, NTCRT_FD_BLOCK_SIZE * sizeof(NTCRT_FD));

    if (Block != NULL)
    {
        for (int i = 0; i < NTCRT_FD_BLOCK_SIZE; i++)
        {
            Block[i].Handle = INVALID_HANDLE_VALUE;
            RtlInitializeCriticalSection(&Block[i].Lock);
        }
        _NtCRT_FdBlocks[BlockIndex] = Block;
    }
    return Block;
}

EXTERN_C
BOOL
_NtCRT_SetFd(
    _In_ int fd,
    _In_ HANDLE Handle,
    _In_ UCHAR Flags)
{
    PNTCRT_FD Entry;

    if ((unsigned int)fd >= NTCRT_FD_MAX)
    {
        return FALSE;
    }
    RtlAcquireSRWLockExclusive(&_NtCRT_FdTableLock);
    if (_NtCRT_FdBlocks[fd / NTCRT_FD_BLOCK_SIZE] == NULL)
    {
        _NtCRT_AllocBlock(fd / NTCRT_FD_BLOCK_SIZE);
    }
    Entry = _NtCRT_FdEntry(fd);
    if (Entry != NULL)
    {
        _NtCRT_InitFdState(Entry, Handle, Flags);
    }
    RtlReleaseSRWLockExclusive(&_NtCRT_FdTableLock);
    return Entry != NULL;
}

EXTERN_C
UCHAR
_NtCRT_DeviceFlags(
    _In_ HANDLE Handle)
{
    switch (_CRT_GetFileType(Handle))
    {
        case FILE_TYPE_CHAR:
            return FDEV;
        case FILE_TYPE_PIPE:
            return FPIPE;
        default:
            return 0;
    }
}

EXTERN_C void _NtCRT_InitLowIO(void)
{
    STARTUPINFOW StartupInfo;
    PRTL_USER_PROCESS_PARAMETERS ProcParam = NtCurrentPeb()->ProcessParameters;
    HANDLE StdHandles[3] = { ProcParam->StandardInput, ProcParam->StandardOutput, ProcParam->StandardError };

    /* Inherited descriptors, see _ioinit of msvcrt: int count, UCHAR flags[count], intptr_t handles[count] */
    _Inline_GetStartupInfoW(&StartupInfo);
    if (StartupInfo.cbReserved2 >= sizeof(int) && StartupInfo.lpReserved2 != NULL)
    {
        int Count = *(UNALIGNED int*)StartupInfo.lpReserved2;
        PUCHAR Flags = StartupInfo.lpReserved2 + sizeof(int);
        UNALIGNED intptr_t* Handles = (UNALIGNED intptr_t*)(Flags + Count);

        if (Count > 0 && (size_t)StartupInfo.cbReserved2 >= sizeof(int) + Count * (1 + sizeof(intptr_t)))
        {
            if (Count > NTCRT_FD_MAX)
            {
                Count = NTCRT_FD_MAX;
            }
            for (int i = 0; i < Count; i++)
            {
                HANDLE Handle = (HANDLE)Handles[i];
                if ((Flags[i] & FOPEN) && Handle != INVALID_HANDLE_VALUE && Handle != NULL &&
                    _CRT_GetFileType(Handle) != FILE_TYPE_UNKNOWN)
                {
                    _NtCRT_SetFd(i, Handle, Flags[i] & ~FEOFLAG);
                }
            }
        }
    }

    /* Standard streams */
    for (int i = 0; i < 3; i++)
    {
        PNTCRT_FD Entry = _NtCRT_FdEntry(i);
        HANDLE Handle;

        if (Entry != NULL && (Entry->Flags & FOPEN))
        {
            continue;
        }
        Handle = StdHandles[i];
        if (Handle == NULL || Handle == INVALID_HANDLE_VALUE || _CRT_GetFileType(Handle) == FILE_TYPE_UNKNOWN)
        {
            _NtCRT_SetFd(i, NTCRT_NO_CONSOLE_HANDLE, FTEXT | FDEV);
        } else
        {
            _NtCRT_SetFd(i, Handle, FTEXT | _NtCRT_DeviceFlags(Handle));
        }
    }
}

/* Raw I/O */

EXTERN_C
PNTCRT_RW_FILE
_NtCRT_GetLegacyConsoleIo(
    _In_ BOOL Write)
{
    static PVOID Functions[2];

    if (Functions[Write] == NULL)
    {
        ANSI_STRING Name;
        PVOID Kernel32 = _Inline_GetModuleHandleW(L"kernel32.dll");
        if (Kernel32 != NULL)
        {
            _Inline_RtlInitAnsiString(&Name, Write ? "WriteFile" : "ReadFile");
            LdrGetProcedureAddress(Kernel32, &Name, 0, &Functions[Write]);
        }
    }
    return (PNTCRT_RW_FILE)Functions[Write];
}

EXTERN_C
NTSTATUS
_NtCRT_CloseHandle(
    _In_ HANDLE Handle)
{
    if (_NtCRT_IsLegacyConsoleHandle(Handle))
    {
        auto const CloseHandle = (BOOL(WINAPI*)(HANDLE))_NtCRT_GetKernel32Function("CloseHandle");
        return CloseHandle != NULL && CloseHandle(Handle) ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
    }
    return NtClose(Handle);
}

EXTERN_C
NTSTATUS
_NtCRT_DuplicateHandle(
    _In_ HANDLE Handle,
    _Out_ PHANDLE NewHandle)
{
    if (_NtCRT_IsLegacyConsoleHandle(Handle))
    {
        typedef BOOL(WINAPI * PNTCRT_DUPLICATE_HANDLE)(HANDLE, HANDLE, HANDLE, LPHANDLE, DWORD, BOOL, DWORD);
        auto const DuplicateHandle = (PNTCRT_DUPLICATE_HANDLE)_NtCRT_GetKernel32Function("DuplicateHandle");
        return DuplicateHandle != NULL &&
                       DuplicateHandle(
                           NtCurrentProcess(), Handle, NtCurrentProcess(), NewHandle, 0, TRUE, DUPLICATE_SAME_ACCESS)
                   ? STATUS_SUCCESS
                   : STATUS_INVALID_HANDLE;
    }
    return NtDuplicateObject(
        NtCurrentProcess(), Handle, NtCurrentProcess(), NewHandle, 0, OBJ_INHERIT, DUPLICATE_SAME_ACCESS);
}

EXTERN_C
int
_NtCRT_RawWrite(
    _In_ HANDLE Handle,
    _In_reads_bytes_(Count) const void* Buffer,
    _In_ ULONG Count)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;

    if (_NtCRT_IsLegacyConsoleHandle(Handle))
    {
        DWORD Written;
        PNTCRT_RW_FILE WriteFile = _NtCRT_GetLegacyConsoleIo(TRUE);
        if (WriteFile != NULL && WriteFile(Handle, (PVOID)Buffer, Count, &Written, NULL))
        {
            return (int)Written;
        }
        _NtCRT_SetErrnoFromWin32(WriteFile != NULL ? _Inline_GetLastError() : ERROR_INVALID_HANDLE);
        return -1;
    }

    IoStatusBlock.Information = 0;
    Status = NtWriteFile(Handle, NULL, NULL, NULL, &IoStatusBlock, (PVOID)Buffer, Count, NULL, NULL);
    if (Status == STATUS_PENDING)
    {
        Status = NtWaitForSingleObject(Handle, FALSE, NULL);
        if (NT_SUCCESS(Status))
        {
            Status = IoStatusBlock.Status;
        }
    }
    if (NT_SUCCESS(Status))
    {
        return (int)IoStatusBlock.Information;
    }
    _NtCRT_SetErrnoFromNtStatus(Status);
    if (NTCRT_ERRNO == EACCES)
    {
        NTCRT_ERRNO = EBADF;
    }
    return -1;
}

/*
 * ASCII text of the runtime (_amsg_exit, abort, _matherr) to the handle of fd 2 as it is, like _NMSG_WRITE of
 * msvcrt. _write would link the console and text mode conversions into every program.
 */
EXTERN_C void __cdecl _NtCRT_WriteStderrRaw(const char* Text)
{
    PNTCRT_FD Entry = _NtCRT_FdEntry(2);

    if (Entry != NULL && (Entry->Flags & FOPEN))
    {
        _NtCRT_RawWrite(Entry->Handle, Text, (ULONG)strlen(Text));
    }
}
