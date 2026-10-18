/* msvcrt low-level I/O: file descriptors on NT file handles */

#include "NtCRT.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <sys/types.h>

/* File descriptor flags of msvcrt */
#define FOPEN       0x01
#define FEOFLAG     0x02
#define FCRLF       0x04
#define FPIPE       0x08
#define FNOINHERIT  0x10
#define FAPPEND     0x20
#define FDEV        0x40
#define FTEXT       0x80

#define NTCRT_FD_BLOCK_SIZE 64
#define NTCRT_FD_MAX 2048

/* The handle of a standard stream without a console, see _NO_CONSOLE_FILENO */
#define NTCRT_NO_CONSOLE_HANDLE ((HANDLE)(LONG_PTR)-2)

/* Text modes of msvcrt for FTEXT, a Unicode mode passes UTF-16 to _read and _write */
#define NTCRT_TM_ANSI       0
#define NTCRT_TM_UTF8       1
#define NTCRT_TM_UTF16LE    2

typedef struct _NTCRT_FD
{
    HANDLE Handle;
    UCHAR Flags;
    UCHAR PipeChar;
    BOOLEAN HasPipeChar;
    UCHAR TextMode;
    BOOLEAN WideOnly;       /* Opened with _O_WTEXT, narrow stream I/O is rejected */
    BOOLEAN HasPipeWide;
    WCHAR PipeWide;         /* A UTF-16 unit read ahead */
    UCHAR CarryLength;
    UCHAR Carry[4];         /* Bytes read ahead from a pipe, the start of a UTF-16 unit or UTF-8 character */
    UCHAR PendingCount;
    WCHAR Pending[4];       /* UTF-16 units read from a UTF-8 fd beyond the buffer */
    WCHAR WriteHigh;        /* A high surrogate written to a console or UTF-8 fd, waiting for its low surrogate */
    UCHAR WriteCarryLength;
    UCHAR WriteCarry[4];    /* The start of a UTF-8 character written to a console in "C.UTF-8" */
    BOOLEAN ConsoleEof;     /* Ctrl+Z was read from a console in "C.UTF-8" after ConsoleText */
    char* ConsoleText;      /* UTF-8 read from a console in "C.UTF-8" and not returned yet */
    ULONG ConsoleTextLength;
    ULONG ConsoleTextPos;
    RTL_CRITICAL_SECTION Lock;
} NTCRT_FD, *PNTCRT_FD;

static PNTCRT_FD g_FdBlocks[NTCRT_FD_MAX / NTCRT_FD_BLOCK_SIZE];
static RTL_SRWLOCK g_FdTableLock = RTL_SRWLOCK_INIT;

static
PNTCRT_FD
_NtCRT_FdEntry(
    _In_ int fd)
{
    PNTCRT_FD Block;

    if ((unsigned int)fd >= NTCRT_FD_MAX)
    {
        return NULL;
    }
    Block = g_FdBlocks[fd / NTCRT_FD_BLOCK_SIZE];
    return Block != NULL ? &Block[fd % NTCRT_FD_BLOCK_SIZE] : NULL;
}

/* Get an opened fd and lock it, or set errno to EBADF */
static
PNTCRT_FD
_NtCRT_LockFd(
    _In_ int fd)
{
    PNTCRT_FD Entry = _NtCRT_FdEntry(fd);

    if (Entry == NULL || !(Entry->Flags & FOPEN))
    {
        NTCRT_ERRNO = EBADF;
        _NtCRT_GetPtd()->Doserrno = 0;
        return NULL;
    }
    RtlEnterCriticalSection(&Entry->Lock);
    if (!(Entry->Flags & FOPEN))
    {
        RtlLeaveCriticalSection(&Entry->Lock);
        NTCRT_ERRNO = EBADF;
        return NULL;
    }
    return Entry;
}

static
void
_NtCRT_UnlockFd(
    _In_ PNTCRT_FD Entry)
{
    RtlLeaveCriticalSection(&Entry->Lock);
}

static
void
_NtCRT_InitFdState(
    _Out_ PNTCRT_FD Entry,
    _In_ HANDLE Handle,
    _In_ UCHAR Flags)
{
    Entry->Handle = Handle;
    Entry->Flags = Flags | FOPEN;
    Entry->HasPipeChar = FALSE;
    Entry->TextMode = NTCRT_TM_ANSI;
    Entry->WideOnly = FALSE;
    Entry->HasPipeWide = FALSE;
    Entry->CarryLength = 0;
    Entry->PendingCount = 0;
    Entry->WriteHigh = 0;
    Entry->WriteCarryLength = 0;
    Entry->ConsoleEof = FALSE;
    Entry->ConsoleTextLength = Entry->ConsoleTextPos = 0;
}

static
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
        g_FdBlocks[BlockIndex] = Block;
    }
    return Block;
}

/* Allocate a free fd at or above MinFd, returns it locked with FOPEN set */
static
int
_NtCRT_AllocFd(
    _In_ int MinFd,
    _In_ HANDLE Handle,
    _In_ UCHAR Flags)
{
    int fd = -1;

    RtlAcquireSRWLockExclusive(&g_FdTableLock);
    for (unsigned int b = MinFd / NTCRT_FD_BLOCK_SIZE; b < ARRAYSIZE(g_FdBlocks) && fd < 0; b++)
    {
        PNTCRT_FD Block = g_FdBlocks[b];
        if (Block == NULL && (Block = _NtCRT_AllocBlock(b)) == NULL)
        {
            break;
        }
        for (int i = 0; i < NTCRT_FD_BLOCK_SIZE; i++)
        {
            int Candidate = b * NTCRT_FD_BLOCK_SIZE + i;
            if (Candidate >= MinFd && !(Block[i].Flags & FOPEN))
            {
                _NtCRT_InitFdState(&Block[i], Handle, Flags);
                fd = Candidate;
                break;
            }
        }
    }
    RtlReleaseSRWLockExclusive(&g_FdTableLock);
    if (fd < 0)
    {
        NTCRT_ERRNO = EMFILE;
        _NtCRT_GetPtd()->Doserrno = 0;
    }
    return fd;
}

/* Set an fd to a specific slot, used by initialization and _dup2 */
static
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
    RtlAcquireSRWLockExclusive(&g_FdTableLock);
    if (g_FdBlocks[fd / NTCRT_FD_BLOCK_SIZE] == NULL)
    {
        _NtCRT_AllocBlock(fd / NTCRT_FD_BLOCK_SIZE);
    }
    Entry = _NtCRT_FdEntry(fd);
    if (Entry != NULL)
    {
        _NtCRT_InitFdState(Entry, Handle, Flags);
    }
    RtlReleaseSRWLockExclusive(&g_FdTableLock);
    return Entry != NULL;
}

static
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

/* Build lpReserved2 of STARTUPINFO for a child process in the format parsed above, free by RtlFreeHeap */
EXTERN_C PUCHAR _NtCRT_BuildInheritBlock(_Out_ PUSHORT Size)
{
    int Count = 0;
    PUCHAR Block, Flags;
    UNALIGNED intptr_t* Handles;

    RtlAcquireSRWLockShared(&g_FdTableLock);
    for (int i = 0; i < NTCRT_FD_MAX; i++)
    {
        PNTCRT_FD Entry = _NtCRT_FdEntry(i);
        if (Entry != NULL && (Entry->Flags & FOPEN))
        {
            Count = i + 1;
        }
    }
    *Size = (USHORT)(sizeof(int) + Count * (1 + sizeof(intptr_t)));
    Block = (PUCHAR)RtlAllocateHeap(NTCRT_HEAP, 0, *Size);
    if (Block != NULL)
    {
        *(UNALIGNED int*)Block = Count;
        Flags = Block + sizeof(int);
        Handles = (UNALIGNED intptr_t*)(Flags + Count);
        for (int i = 0; i < Count; i++)
        {
            PNTCRT_FD Entry = _NtCRT_FdEntry(i);
            if (Entry != NULL && (Entry->Flags & (FOPEN | FNOINHERIT)) == FOPEN)
            {
                Flags[i] = Entry->Flags;
                Handles[i] = (intptr_t)Entry->Handle;
            } else
            {
                Flags[i] = 0;
                Handles[i] = (intptr_t)INVALID_HANDLE_VALUE;
            }
        }
    }
    RtlReleaseSRWLockShared(&g_FdTableLock);
    return Block;
}

/* Raw I/O, console handles of Windows 7 and earlier are not NT handles */

typedef BOOL(WINAPI* PNTCRT_RW_FILE)(HANDLE, PVOID, DWORD, LPDWORD, LPOVERLAPPED);

static
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

static
BOOL
_NtCRT_IsLegacyConsoleHandle(
    _In_ HANDLE Handle)
{
    return ((ULONG_PTR)Handle & 3) == 3 && !IS_NT_VERSION_GE(NT_VERSION_WIN8);
}

/* Returns bytes read, 0 for EOF, -1 for failure with errno set */
static
int
_NtCRT_RawRead(
    _In_ HANDLE Handle,
    _Out_writes_bytes_(Count) void* Buffer,
    _In_ ULONG Count)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;

    if (_NtCRT_IsLegacyConsoleHandle(Handle))
    {
        DWORD Read;
        PNTCRT_RW_FILE ReadFile = _NtCRT_GetLegacyConsoleIo(FALSE);
        if (ReadFile != NULL && ReadFile(Handle, Buffer, Count, &Read, NULL))
        {
            return (int)Read;
        }
        _NtCRT_SetErrnoFromWin32(ReadFile != NULL ? _Inline_GetLastError() : ERROR_INVALID_HANDLE);
        return -1;
    }

    IoStatusBlock.Information = 0;
    Status = NtReadFile(Handle, NULL, NULL, NULL, &IoStatusBlock, Buffer, Count, NULL, NULL);
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
    if (Status == STATUS_END_OF_FILE || Status == STATUS_PIPE_BROKEN)
    {
        return 0;
    }
    _NtCRT_SetErrnoFromNtStatus(Status);
    if (NTCRT_ERRNO == EACCES)
    {
        /* Reading a handle without read access */
        NTCRT_ERRNO = EBADF;
    }
    return -1;
}

/* Returns bytes written or -1 for failure with errno set */
static
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

static
long long
_NtCRT_SeekHandle(
    _In_ HANDLE Handle,
    _In_ long long Offset,
    _In_ int Origin)
{
    LARGE_INTEGER Distance, NewPointer;

    Distance.QuadPart = Offset;
    if (Origin != SEEK_SET && Origin != SEEK_CUR && Origin != SEEK_END)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (!_Inline_SetFilePointerEx(Handle, Distance, &NewPointer, (DWORD)Origin))
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return -1;
    }
    return NewPointer.QuadPart;
}

/* Unicode text modes */

/* By the device type, GetConsoleMode needs read access the handles opened for writing do not have */
static
BOOL
_NtCRT_IsConsoleFd(
    _In_ PNTCRT_FD Entry)
{
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_FS_DEVICE_INFORMATION Info;
    DWORD Mode;

    if (!(Entry->Flags & FDEV))
    {
        return FALSE;
    }
    if (NT_SUCCESS(NtQueryVolumeInformationFile(
            Entry->Handle, &IoStatusBlock, &Info, sizeof(Info), FileFsDeviceInformation)))
    {
        return Info.DeviceType == 0x50; /* FILE_DEVICE_CONSOLE */
    }
    return _CRT_GetConsoleMode(Entry->Handle, &Mode);
}

/* Read the bytes kept for a pipe first, returns bytes read, 0 for EOF, -1 for failure with errno set */
static
int
_NtCRT_ReadAhead(
    _In_ PNTCRT_FD Entry,
    _Out_writes_bytes_(Count) UCHAR* Buffer,
    _In_ ULONG Count)
{
    ULONG Taken = Count < Entry->CarryLength ? Count : Entry->CarryLength;
    int Read;

    memcpy(Buffer, Entry->Carry, Taken);
    memmove(Entry->Carry, Entry->Carry + Taken, Entry->CarryLength - Taken);
    Entry->CarryLength -= (UCHAR)Taken;
    if (Taken == Count)
    {
        return (int)Taken;
    }
    Read = _NtCRT_RawRead(Entry->Handle, Buffer + Taken, Count - Taken);
    if (Read < 0)
    {
        return Taken != 0 ? (int)Taken : -1;
    }
    return (int)Taken + Read;
}

/* Return bytes read ahead, a file seeks back and a pipe keeps them */
static
void
_NtCRT_PutBack(
    _In_ PNTCRT_FD Entry,
    _In_reads_bytes_(Count) const UCHAR* Bytes,
    _In_ ULONG Count)
{
    if (Count == 0)
    {
        return;
    }
    if (Entry->Flags & (FPIPE | FDEV))
    {
        memcpy(Entry->Carry + Entry->CarryLength, Bytes, Count);
        Entry->CarryLength += (UCHAR)Count;
    } else
    {
        /* Seeking clears the end of a text file */
        _NtCRT_SeekHandle(Entry->Handle, -(long long)Count, SEEK_CUR);
        Entry->Flags &= ~FEOFLAG;
    }
}

/*
 * The character for a CR at the end of the buffer followed by the peeked unit: LF for CRLF, CR, or 0 to drop it.
 * As msvcrt, a file seeks back to read the LF next time unless nothing has been read, a pipe keeps the peeked unit
 * if it is not LF.
 */
static
WCHAR
_NtCRT_CrAtEnd(
    _In_ PNTCRT_FD Entry,
    _In_reads_bytes_(PeekLength) const UCHAR* Peek,
    _In_ ULONG PeekLength,
    _In_ ULONG UnitLength,
    _In_ BOOL Empty)
{
    BOOL IsLf = PeekLength == UnitLength && Peek[0] == '\n' && (UnitLength == 1 || Peek[1] == 0);

    if (PeekLength != UnitLength)
    {
        _NtCRT_PutBack(Entry, Peek, PeekLength);
        return L'\r';
    }
    if (IsLf && ((Entry->Flags & (FPIPE | FDEV)) || Empty))
    {
        return L'\n';
    }
    _NtCRT_PutBack(Entry, Peek, PeekLength);
    return IsLf ? 0 : L'\r';
}

/*
 * Write UTF-16 to a Unicode text mode fd, a newline is written as CRLF. A high surrogate at the end is kept for
 * the low surrogate written next if the fd converts the text. Returns the bytes taken or -1.
 */
static
int
_NtCRT_WriteUnicode(
    _In_ PNTCRT_FD Entry,
    _In_reads_bytes_(Count) const char* Buffer,
    _In_ unsigned int Count)
{
    BOOL Console = _NtCRT_IsConsoleFd(Entry);
    BOOL Convert = Console || Entry->TextMode == NTCRT_TM_UTF8;
    unsigned int Chars = Count / sizeof(WCHAR), Done = 0;

    while (Done < Chars)
    {
        WCHAR Line[1024];
        char Utf8[ARRAYSIZE(Line) * 3];
        unsigned int Used = 0, Taken = 0;
        BOOL Success = TRUE;

        if (Entry->WriteHigh != 0)
        {
            Line[Used++] = Entry->WriteHigh;
            Entry->WriteHigh = 0;
        }
        while (Done + Taken < Chars && Used < ARRAYSIZE(Line) - 1)
        {
            WCHAR c;

            memcpy(&c, Buffer + (Done + Taken) * sizeof(WCHAR), sizeof(WCHAR));
            if (c == L'\n')
            {
                Line[Used++] = L'\r';
            }
            Line[Used++] = c;
            Taken++;
        }
        if (Convert && Used > 0 && IS_HIGH_SURROGATE(Line[Used - 1]))
        {
            Entry->WriteHigh = Line[--Used];
        }
        if (Used == 0)
        {
            /* Only a high surrogate */
        } else if (Console)
        {
            DWORD Written;

            Success = _NtCRT_WriteConsoleW(Entry->Handle, Line, Used, &Written) && Written == Used;
            if (!Success)
            {
                _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
            }
        } else if (Entry->TextMode == NTCRT_TM_UTF8)
        {
            int Size = _NtCRT_WideCharToMultiByte(CP_UTF8, 0, Line, (int)Used, Utf8, sizeof(Utf8), NULL, NULL);

            Success = _NtCRT_RawWrite(Entry->Handle, Utf8, (ULONG)Size) == Size;
        } else
        {
            Success = _NtCRT_RawWrite(Entry->Handle, Line, Used * sizeof(WCHAR)) == (int)(Used * sizeof(WCHAR));
        }
        if (!Success)
        {
            break;
        }
        Done += Taken;
    }
    return Done != 0 ? (int)(Done * sizeof(WCHAR)) : -1;
}

/* Bytes of an incomplete UTF-8 character at the end of a buffer */
static
unsigned int
_NtCRT_Utf8Tail(
    _In_reads_(Length) const UCHAR* Bytes,
    _In_ unsigned int Length)
{
    for (unsigned int n = 1; n <= 3 && n <= Length; n++)
    {
        UCHAR c = Bytes[Length - n];

        if ((c & 0xC0) != 0x80)
        {
            unsigned int Need = c >= 0xF0 && c <= 0xF4 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 ? 2 : 1;

            return c >= 0xC2 && c <= 0xF4 && Need > n ? n : 0;
        }
    }
    return 0;
}

/*
 * Write UTF-8 of "C.UTF-8" to a text mode console by its characters, a newline is written as CRLF and an
 * incomplete character at the end is kept for the next write. Returns the bytes taken or -1.
 */
static
int
_NtCRT_WriteConsoleUtf8(
    _In_ PNTCRT_FD Entry,
    _In_reads_bytes_(Count) const char* Buffer,
    _In_ unsigned int Count)
{
    unsigned int Consumed = 0;

    while (Consumed < Count)
    {
        UCHAR Bytes[1024];
        WCHAR Wide[ARRAYSIZE(Bytes)];
        unsigned int Used = Entry->WriteCarryLength, Taken = 0, Tail;
        int Length;
        DWORD Written;

        memcpy(Bytes, Entry->WriteCarry, Used);
        Entry->WriteCarryLength = 0;
        while (Consumed + Taken < Count && Used < sizeof(Bytes) - 1)
        {
            char c = Buffer[Consumed + Taken];

            if (c == '\n')
            {
                Bytes[Used++] = '\r';
            }
            Bytes[Used++] = (UCHAR)c;
            Taken++;
        }
        Tail = _NtCRT_Utf8Tail(Bytes, Used);
        Used -= Tail;
        memcpy(Entry->WriteCarry, Bytes + Used, Tail);
        Entry->WriteCarryLength = (UCHAR)Tail;
        Length = Used != 0 ? _NtCRT_MultiByteToWideChar(CP_UTF8, 0, (LPCCH)Bytes, (int)Used, Wide, ARRAYSIZE(Wide)) : 0;
        if (Length > 0 && (!_NtCRT_WriteConsoleW(Entry->Handle, Wide, (DWORD)Length, &Written) ||
                           Written != (DWORD)Length))
        {
            _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
            break;
        }
        Consumed += Taken;
    }
    return Consumed != 0 ? (int)Consumed : -1;
}

/*
 * Read UTF-8 of "C.UTF-8" from a text mode console by its characters, CRLF is read as a newline and Ctrl+Z ends
 * the input. Returns the bytes stored, 0 for EOF or -1.
 */
static
int
_NtCRT_ReadConsoleUtf8(
    _In_ PNTCRT_FD Entry,
    _Out_writes_bytes_(Count) char* Buffer,
    _In_ unsigned int Count)
{
    typedef BOOL(WINAPI* PNTCRT_READ_CONSOLE_W)(HANDLE, PVOID, DWORD, LPDWORD, PVOID);
    unsigned int Length;

    while (Entry->ConsoleTextPos == Entry->ConsoleTextLength)
    {
        auto const ReadConsoleW = (PNTCRT_READ_CONSOLE_W)_NtCRT_GetKernel32Function("ReadConsoleW");
        WCHAR Wide[1024];
        DWORD Units = 0, Got = 0, Kept = 0;
        int Size;

        if (Entry->ConsoleEof)
        {
            Entry->ConsoleEof = FALSE;
            Entry->Flags |= FEOFLAG;
            return 0;
        }
        if (Entry->HasPipeWide)
        {
            Wide[Units++] = Entry->PipeWide;
            Entry->HasPipeWide = FALSE;
        }
        if (ReadConsoleW == NULL || !ReadConsoleW(Entry->Handle, Wide + Units, ARRAYSIZE(Wide) - Units, &Got, NULL))
        {
            _NtCRT_SetErrnoFromWin32(ReadConsoleW == NULL ? ERROR_INVALID_HANDLE : _Inline_GetLastError());
            return -1;
        }
        Units += Got;
        if (Units == 0)
        {
            return 0;
        }
        for (DWORD i = 0; i < Units; i++)
        {
            if (Wide[i] == 0x1A)
            {
                Entry->ConsoleEof = TRUE;
                break;
            }
            if (Wide[i] == L'\r' && i + 1 < Units && Wide[i + 1] == L'\n')
            {
                continue;
            }
            Wide[Kept++] = Wide[i];
        }
        if (!Entry->ConsoleEof && Kept > 0 && IS_HIGH_SURROGATE(Wide[Kept - 1]))
        {
            Entry->PipeWide = Wide[--Kept];
            Entry->HasPipeWide = TRUE;
        }
        Size = Kept != 0 ? _NtCRT_WideCharToMultiByte(CP_UTF8, 0, Wide, (int)Kept, NULL, 0, NULL, NULL) : 0;
        if (Size > 0)
        {
            char* Text = (char*)(Entry->ConsoleText == NULL
                                     ? RtlAllocateHeap(NTCRT_HEAP, 0, ARRAYSIZE(Wide) * 3)
                                     : Entry->ConsoleText);
            if (Text == NULL)
            {
                NTCRT_ERRNO = ENOMEM;
                return -1;
            }
            Entry->ConsoleText = Text;
            _NtCRT_WideCharToMultiByte(CP_UTF8, 0, Wide, (int)Kept, Text, ARRAYSIZE(Wide) * 3, NULL, NULL);
        }
        Entry->ConsoleTextLength = Size > 0 ? (ULONG)Size : 0;
        Entry->ConsoleTextPos = 0;
    }
    Length = Entry->ConsoleTextLength - Entry->ConsoleTextPos;
    if (Length > Count)
    {
        Length = Count;
    }
    memcpy(Buffer, Entry->ConsoleText + Entry->ConsoleTextPos, Length);
    Entry->ConsoleTextPos += Length;
    return (int)Length;
}

/*
 * Read UTF-16 from a Unicode text mode fd, CRLF is read as a newline and Ctrl+Z ends a file. Returns the bytes
 * stored, 0 for EOF or -1.
 */
static
int
_NtCRT_ReadUnicode(
    _In_ PNTCRT_FD Entry,
    _Out_writes_bytes_(Count) char* Buffer,
    _In_ unsigned int Count)
{
    WCHAR* Out = (WCHAR*)Buffer;
    unsigned int Units = Count / sizeof(WCHAR), Kept = 0;
    UCHAR* Raw;
    ULONG RawCount;
    int Read;

    if (_NtCRT_IsConsoleFd(Entry))
    {
        typedef BOOL(WINAPI* PNTCRT_READ_CONSOLE_W)(HANDLE, PVOID, DWORD, LPDWORD, PVOID);
        PNTCRT_READ_CONSOLE_W ReadConsoleW = (PNTCRT_READ_CONSOLE_W)_NtCRT_GetKernel32Function("ReadConsoleW");
        DWORD Got = 0;

        if (Entry->HasPipeWide)
        {
            Out[Kept++] = Entry->PipeWide;
            Entry->HasPipeWide = FALSE;
        }
        if (Kept < Units)
        {
            if (ReadConsoleW == NULL || !ReadConsoleW(Entry->Handle, Out + Kept, Units - Kept, &Got, NULL))
            {
                if (Kept != 0)
                {
                    return (int)(Kept * sizeof(WCHAR));
                }
                _NtCRT_SetErrnoFromWin32(ReadConsoleW == NULL ? ERROR_INVALID_HANDLE : _Inline_GetLastError());
                return -1;
            }
        }
        Units = Kept + Got;
        Kept = 0;
        for (unsigned int i = 0; i < Units; i++)
        {
            if (Out[i] == 0x1A)
            {
                /* As msvcrt, Ctrl+Z ends the console input of the fd */
                Entry->Flags |= FEOFLAG;
                break;
            }
            if (Out[i] == L'\r' && i + 1 < Units && Out[i + 1] == L'\n')
            {
                i++;
            }
            Out[Kept++] = Out[i];
        }
        return (int)(Kept * sizeof(WCHAR));
    }

    if (Entry->TextMode == NTCRT_TM_UTF16LE)
    {
        Raw = (UCHAR*)Buffer;
        RawCount = 0;
        if (Entry->HasPipeWide)
        {
            memcpy(Raw, &Entry->PipeWide, sizeof(WCHAR));
            RawCount = sizeof(WCHAR);
            Entry->HasPipeWide = FALSE;
        }
        Read = RawCount < Count ? _NtCRT_ReadAhead(Entry, Raw + RawCount, Count - RawCount) : 0;
        if (Read < 0 && RawCount == 0)
        {
            return -1;
        }
        RawCount += Read > 0 ? Read : 0;
        if (RawCount & 1)
        {
            _NtCRT_PutBack(Entry, Raw + RawCount - 1, 1);
            RawCount--;
        }
        Units = RawCount / sizeof(WCHAR);
        if (Units != 0 && Out[0] == L'\n')
        {
            Entry->Flags |= FCRLF;
        } else
        {
            Entry->Flags &= ~FCRLF;
        }
        for (unsigned int i = 0; i < Units;)
        {
            WCHAR c = Out[i], Peek;

            if (c == 0x1A)
            {
                Entry->Flags |= FEOFLAG;
                break;
            }
            if (c != L'\r')
            {
                Out[Kept++] = c;
                i++;
                continue;
            }
            if (i + 1 < Units)
            {
                if (Out[i + 1] == L'\n')
                {
                    Out[Kept++] = L'\n';
                    i += 2;
                } else
                {
                    Out[Kept++] = c;
                    i++;
                }
                continue;
            }

            /* CR at the end of the buffer, peek the next unit */
            i++;
            Read = _NtCRT_ReadAhead(Entry, (UCHAR*)&Peek, sizeof(Peek));
            c = _NtCRT_CrAtEnd(Entry, (UCHAR*)&Peek, Read > 0 ? (ULONG)Read : 0, sizeof(Peek), Kept == 0);
            if (c != 0)
            {
                Out[Kept++] = c;
            }
        }
        return (int)(Kept * sizeof(WCHAR));
    }

    /*
     * UTF-8, at most as many bytes as the units of the buffer are read, an incomplete character at the end is read
     * next time and invalid bytes are U+FFFD. The units beyond the buffer are kept for the next read.
     */
    while (Kept < Units && Entry->PendingCount != 0)
    {
        Out[Kept++] = Entry->Pending[0];
        memmove(Entry->Pending, Entry->Pending + 1, --Entry->PendingCount * sizeof(WCHAR));
    }
    for (ULONG Want = Units; Kept == 0;)
    {
        ULONG Tail;
        WCHAR* Wide;
        int Length;
        BOOL Eof;

        Raw = (UCHAR*)RtlAllocateHeap(NTCRT_HEAP, 0, Entry->CarryLength + Want);
        if (Raw == NULL)
        {
            NTCRT_ERRNO = ENOMEM;
            return -1;
        }
        RawCount = Entry->CarryLength;
        memcpy(Raw, Entry->Carry, RawCount);
        Entry->CarryLength = 0;
        Read = _NtCRT_RawRead(Entry->Handle, Raw + RawCount, Want);
        if (Read < 0)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, Raw);
            return -1;
        }
        /* A short read of a file is its end */
        Eof = Read == 0 || ((ULONG)Read < Want && !(Entry->Flags & (FPIPE | FDEV)));
        RawCount += (ULONG)Read;
        {
            UCHAR* Src = Raw;
            UCHAR* Dst = Raw;
            UCHAR* End = Raw + RawCount;

            while (Src < End)
            {
                if (*Src == 0x1A)
                {
                    /* Ctrl+Z ends a file, it is read from a device */
                    if (!(Entry->Flags & FDEV))
                    {
                        Entry->Flags |= FEOFLAG;
                        Eof = TRUE;
                    } else
                    {
                        *Dst++ = *Src;
                    }
                    break;
                }
                if (*Src != '\r')
                {
                    *Dst++ = *Src++;
                } else if (Src + 1 < End)
                {
                    if (Src[1] == '\n')
                    {
                        Src++;
                    }
                    *Dst++ = *Src++;
                } else
                {
                    UCHAR Peek;
                    WCHAR c;

                    Src++;
                    Read = _NtCRT_ReadAhead(Entry, &Peek, 1);
                    c = _NtCRT_CrAtEnd(Entry, &Peek, Read > 0 ? (ULONG)Read : 0, 1, Dst == Raw && Kept == 0);
                    if (c != 0)
                    {
                        *Dst++ = (UCHAR)c;
                    }
                }
            }
            RawCount = (ULONG)(Dst - Raw);
        }
        Tail = Eof ? 0 : _NtCRT_Utf8Tail(Raw, RawCount);
        if (Tail != 0)
        {
            RawCount -= Tail;
            _NtCRT_PutBack(Entry, Raw + RawCount, Tail);
        }
        if (RawCount == 0)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, Raw);
            if (Eof)
            {
                break;
            }
            /* Only the start of a character, read it whole next time */
            Want = Want < 4 ? 4 : Want;
            continue;
        }
        Wide = (WCHAR*)RtlAllocateHeap(NTCRT_HEAP, 0, RawCount * sizeof(WCHAR));
        Length = Wide != NULL ? _NtCRT_MultiByteToWideChar(CP_UTF8, 0, (LPCCH)Raw, (int)RawCount, Wide, (int)RawCount)
                              : 0;
        RtlFreeHeap(NTCRT_HEAP, 0, Raw);
        if (Length <= 0)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, Wide);
            NTCRT_ERRNO = ENOMEM;
            return -1;
        }
        for (int i = 0; i < Length; i++)
        {
            if (Kept < Units)
            {
                Out[Kept++] = Wide[i];
            } else
            {
                Entry->Pending[Entry->PendingCount++] = Wide[i];
            }
        }
        RtlFreeHeap(NTCRT_HEAP, 0, Wide);
    }
    return (int)(Kept * sizeof(WCHAR));
}

/* Public functions */

EXTERN_C int __cdecl _NtCRT_read(int fd, void* buffer, unsigned int count)
{
    PNTCRT_FD Entry;
    char* Buffer = (char*)buffer;
    char* p = Buffer;
    int Total = 0, Read;

    Entry = _NtCRT_LockFd(fd);
    if (Entry == NULL)
    {
        return -1;
    }
    if (count > INT_MAX)
    {
        goto _Invalid;
    }
    if (count == 0 || (Entry->Flags & FEOFLAG))
    {
        goto _Exit;
    }
    if (buffer == NULL || ((Entry->Flags & FTEXT) && Entry->TextMode != NTCRT_TM_ANSI && (count & 1)))
    {
        goto _Invalid;
    }
    if (Entry->Handle == NTCRT_NO_CONSOLE_HANDLE)
    {
        goto _Exit;
    }
    if ((Entry->Flags & FTEXT) && Entry->TextMode != NTCRT_TM_ANSI)
    {
        Total = _NtCRT_ReadUnicode(Entry, Buffer, count);
        goto _Exit;
    }
    if ((Entry->Flags & FTEXT) && _NtCRT_CurrentCodePage() == CP_UTF8 && _NtCRT_IsConsoleFd(Entry))
    {
        Total = _NtCRT_ReadConsoleUtf8(Entry, Buffer, count);
        goto _Exit;
    }

    if (Entry->HasPipeChar && (Entry->Flags & (FPIPE | FDEV)))
    {
        *p++ = (char)Entry->PipeChar;
        Entry->HasPipeChar = FALSE;
        count--;
        Total = 1;
    }
    if (count != 0)
    {
        Read = _NtCRT_RawRead(Entry->Handle, p, count);
        if (Read < 0)
        {
            Total = Total != 0 ? Total : -1;
            goto _Exit;
        }
        Total += Read;
    }

    if ((Entry->Flags & FTEXT) && Total > 0)
    {
        char* Src = Buffer;
        char* Dst = Buffer;
        char* End = Buffer + Total;

        if (*Buffer == '\n')
        {
            Entry->Flags |= FCRLF;
        } else
        {
            Entry->Flags &= ~FCRLF;
        }
        while (Src < End)
        {
            if (*Src == 0x1A)
            {
                /* Ctrl+Z is the end of a text file, as msvcrt it is read from a device */
                if (!(Entry->Flags & FDEV))
                {
                    Entry->Flags |= FEOFLAG;
                } else
                {
                    *Dst++ = *Src;
                }
                break;
            }
            if (*Src != '\r')
            {
                *Dst++ = *Src++;
                continue;
            }
            if (Src + 1 < End)
            {
                if (Src[1] == '\n')
                {
                    Src += 2;
                    *Dst++ = '\n';
                } else
                {
                    *Dst++ = *Src++;
                }
                continue;
            }

            /* CR at the end of the buffer, peek the next byte */
            Src++;
            {
                char Peek;
                int PeekRead = _NtCRT_RawRead(Entry->Handle, &Peek, 1);
                if (PeekRead == 1)
                {
                    if (Peek == '\n' && ((Entry->Flags & (FPIPE | FDEV)) || Dst == Buffer))
                    {
                        *Dst++ = '\n';
                    } else if (Entry->Flags & (FPIPE | FDEV))
                    {
                        *Dst++ = '\r';
                        Entry->PipeChar = (UCHAR)Peek;
                        Entry->HasPipeChar = TRUE;
                    } else
                    {
                        /* As msvcrt, a file seeks back and the LF is read next time */
                        _NtCRT_SeekHandle(Entry->Handle, -1, SEEK_CUR);
                        if (Peek != '\n')
                        {
                            *Dst++ = '\r';
                        }
                    }
                } else
                {
                    *Dst++ = '\r';
                }
            }
        }
        Total = (int)(Dst - Buffer);
    }
    goto _Exit;

_Invalid:
    _NtCRT_GetPtd()->Doserrno = 0;
    _NtCRT_InvalidParameter(EINVAL);
    Total = -1;
_Exit:
    _NtCRT_UnlockFd(Entry);
    return Total;
}

EXTERN_C int __cdecl _NtCRT_write(int fd, const void* buffer, unsigned int count)
{
    PNTCRT_FD Entry;
    const char* Src = (const char*)buffer;
    int Result = 0;

    Entry = _NtCRT_LockFd(fd);
    if (Entry == NULL)
    {
        return -1;
    }
    if (count == 0)
    {
        goto _Exit;
    }
    if (buffer == NULL || ((Entry->Flags & FTEXT) && Entry->TextMode != NTCRT_TM_ANSI && (count & 1)))
    {
        _NtCRT_GetPtd()->Doserrno = 0;
        _NtCRT_InvalidParameter(EINVAL);
        Result = -1;
        goto _Exit;
    }
    if (Entry->Handle == NTCRT_NO_CONSOLE_HANDLE)
    {
        /* No console attached, discard the output as msvcrt does */
        Result = (int)count;
        goto _Exit;
    }
    if (Entry->Flags & FAPPEND)
    {
        _NtCRT_SeekHandle(Entry->Handle, 0, SEEK_END);
    }

    if ((Entry->Flags & FTEXT) && Entry->TextMode != NTCRT_TM_ANSI)
    {
        Result = _NtCRT_WriteUnicode(Entry, Src, count);
    } else if (!(Entry->Flags & FTEXT))
    {
        unsigned int Done = 0;
        while (Done < count)
        {
            int Written = _NtCRT_RawWrite(Entry->Handle, Src + Done, count - Done);
            if (Written <= 0)
            {
                break;
            }
            Done += Written;
        }
        Result = Done != 0 || count == 0 ? (int)Done : -1;
    } else if (_NtCRT_CurrentCodePage() == CP_UTF8 && _NtCRT_IsConsoleFd(Entry))
    {
        Result = _NtCRT_WriteConsoleUtf8(Entry, Src, count);
    } else
    {
        char Line[1024];
        unsigned int Consumed = 0;

        while (Consumed < count)
        {
            unsigned int Used = 0, Taken = 0;
            int Written;

            while (Consumed + Taken < count && Used < sizeof(Line) - 1)
            {
                char c = Src[Consumed + Taken];
                if (c == '\n')
                {
                    Line[Used++] = '\r';
                }
                Line[Used++] = c;
                Taken++;
            }
            Written = _NtCRT_RawWrite(Entry->Handle, Line, Used);
            if (Written < (int)Used)
            {
                if (Written > 0)
                {
                    Consumed += Taken;
                }
                break;
            }
            Consumed += Taken;
        }
        Result = Consumed != 0 ? (int)Consumed : -1;
    }
    if (Result < 0 && NTCRT_ERRNO == EPIPE)
    {
        Result = -1;
    }

_Exit:
    _NtCRT_UnlockFd(Entry);
    return Result;
}

EXTERN_C int __cdecl _NtCRT_close(int fd)
{
    PNTCRT_FD Entry = _NtCRT_LockFd(fd);
    HANDLE Handle;
    int Result = 0;

    if (Entry == NULL)
    {
        return -1;
    }
    Handle = Entry->Handle;
    Entry->Handle = INVALID_HANDLE_VALUE;
    Entry->Flags = 0;
    if (Entry->ConsoleText != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Entry->ConsoleText);
        Entry->ConsoleText = NULL;
    }
    if (fd <= 2)
    {
        _Inline_SetStdHandle(fd == 0 ? STD_INPUT_HANDLE : fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE, NULL);
    }
    _NtCRT_UnlockFd(Entry);
    if (Handle != NTCRT_NO_CONSOLE_HANDLE && Handle != INVALID_HANDLE_VALUE && Handle != NULL)
    {
        NTSTATUS Status = NtClose(Handle);
        if (!NT_SUCCESS(Status))
        {
            _NtCRT_SetErrnoFromNtStatus(Status);
            Result = -1;
        }
    }
    return Result;
}

EXTERN_C long long __cdecl _NtCRT_lseeki64(int fd, long long offset, int origin)
{
    PNTCRT_FD Entry = _NtCRT_LockFd(fd);
    long long Result;

    if (Entry == NULL)
    {
        return -1;
    }
    Result = _NtCRT_SeekHandle(Entry->Handle, offset, origin);
    if (Result >= 0)
    {
        Entry->Flags &= ~FEOFLAG;
        Entry->HasPipeChar = FALSE;
        Entry->HasPipeWide = FALSE;
        Entry->CarryLength = 0;
        Entry->PendingCount = 0;
    }
    _NtCRT_UnlockFd(Entry);
    return Result;
}

EXTERN_C long __cdecl _NtCRT_lseek(int fd, long offset, int origin)
{
    long long Result = _NtCRT_lseeki64(fd, offset, origin);

    if (Result > LONG_MAX)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return (long)Result;
}

EXTERN_C long long __cdecl _NtCRT_telli64(int fd)
{
    return _NtCRT_lseeki64(fd, 0, SEEK_CUR);
}

EXTERN_C long __cdecl _NtCRT_tell(int fd)
{
    return _NtCRT_lseek(fd, 0, SEEK_CUR);
}

EXTERN_C long long __cdecl _NtCRT_filelengthi64(int fd)
{
    PNTCRT_FD Entry = _NtCRT_LockFd(fd);
    LARGE_INTEGER Size;
    long long Result = -1;

    if (Entry == NULL)
    {
        return -1;
    }
    if (_Inline_GetFileSizeEx(Entry->Handle, &Size))
    {
        Result = Size.QuadPart;
    } else
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
    }
    _NtCRT_UnlockFd(Entry);
    return Result;
}

EXTERN_C long __cdecl _NtCRT_filelength(int fd)
{
    long long Result = _NtCRT_filelengthi64(fd);

    if (Result > LONG_MAX)
    {
        NTCRT_ERRNO = EFBIG;
        return -1;
    }
    return (long)Result;
}

EXTERN_C int __cdecl _NtCRT_chsize_s(int fd, long long size)
{
    PNTCRT_FD Entry;
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_END_OF_FILE_INFORMATION EndOfFile;

    if (size < 0)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    Entry = _NtCRT_LockFd(fd);
    if (Entry == NULL)
    {
        return EBADF;
    }
    EndOfFile.EndOfFile.QuadPart = size;
    Status = NtSetInformationFile(Entry->Handle,
                                  &IoStatusBlock,
                                  &EndOfFile,
                                  sizeof(EndOfFile),
                                  FileEndOfFileInformation);
    _NtCRT_UnlockFd(Entry);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        if (NTCRT_ERRNO == EACCES)
        {
            NTCRT_ERRNO = EBADF;
        }
        return NTCRT_ERRNO;
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT_chsize(int fd, long size)
{
    return _NtCRT_chsize_s(fd, size) == 0 ? 0 : -1;
}

/*
 * ftruncate64 of libmingwex (std::filesystem::resize_file, truncate) checks the free space of the volume by
 * kernel32 functions NtCRT does not have, the specs wrap it to this
 */
EXTERN_C int __cdecl __wrap_ftruncate64(int fd, long long length)
{
    return _NtCRT_chsize_s(fd, length) == 0 ? 0 : -1;
}

EXTERN_C int __cdecl _NtCRT_eof(int fd)
{
    long long Current, End;

    Current = _NtCRT_lseeki64(fd, 0, SEEK_CUR);
    if (Current < 0)
    {
        return -1;
    }
    End = _NtCRT_lseeki64(fd, 0, SEEK_END);
    if (End < 0)
    {
        return -1;
    }
    if (Current == End)
    {
        return 1;
    }
    _NtCRT_lseeki64(fd, Current, SEEK_SET);
    return 0;
}

EXTERN_C int __cdecl _NtCRT_commit(int fd)
{
    PNTCRT_FD Entry = _NtCRT_LockFd(fd);
    int Result = 0;

    if (Entry == NULL)
    {
        return -1;
    }
    if (!_Inline_FlushFileBuffers(Entry->Handle))
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        Result = -1;
    }
    _NtCRT_UnlockFd(Entry);
    return Result;
}

EXTERN_C int __cdecl _NtCRT_setmode(int fd, int mode)
{
    PNTCRT_FD Entry;
    int Old;

    if (mode != _O_TEXT && mode != _O_BINARY && mode != _O_WTEXT && mode != _O_U8TEXT && mode != _O_U16TEXT)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Entry = _NtCRT_LockFd(fd);
    if (Entry == NULL)
    {
        return -1;
    }
    Old = !(Entry->Flags & FTEXT) ? _O_BINARY : Entry->TextMode == NTCRT_TM_ANSI ? _O_TEXT : _O_WTEXT;
    if (mode == _O_BINARY)
    {
        /* The text mode is kept */
        Entry->Flags &= ~FTEXT;
    } else
    {
        Entry->Flags |= FTEXT;
        Entry->TextMode = mode == _O_TEXT ? NTCRT_TM_ANSI : mode == _O_U8TEXT ? NTCRT_TM_UTF8 : NTCRT_TM_UTF16LE;
    }
    _NtCRT_UnlockFd(Entry);
    return Old;
}

EXTERN_C int __cdecl _NtCRT_fd_textmode(int fd)
{
    PNTCRT_FD Entry = _NtCRT_FdEntry(fd);

    if (Entry == NULL || !(Entry->Flags & FOPEN) || !(Entry->Flags & FTEXT))
    {
        return NTCRT_FD_BINARY;
    }
    return Entry->TextMode == NTCRT_TM_UTF8 ? NTCRT_FD_UTF8 :
           Entry->TextMode == NTCRT_TM_UTF16LE ? NTCRT_FD_UTF16 : NTCRT_FD_ANSI;
}

/* The last read of a text mode fd started with a newline, which was CRLF */
/* Bytes of a UTF-16 unit read from a Unicode text mode file, a newline was CRLF */
EXTERN_C int _NtCRT_UnitBytes(wchar_t Unit, BOOL Utf8)
{
    int Bytes;

    if (!Utf8)
    {
        Bytes = sizeof(wchar_t);
    } else
    {
        Bytes = Unit < 0x80 ? 1 : Unit < 0x800 || (Unit >= 0xD800 && Unit <= 0xDFFF) ? 2 : 3;
    }
    return Unit == L'\n' ? Bytes * 2 : Bytes;
}

EXTERN_C long long __cdecl _NtCRT_fd_pending_bytes(int fd)
{
    PNTCRT_FD Entry = _NtCRT_LockFd(fd);
    long long Bytes = 0;

    if (Entry == NULL)
    {
        return 0;
    }
    for (int i = 0; i < Entry->PendingCount; i++)
    {
        Bytes += _NtCRT_UnitBytes(Entry->Pending[i], Entry->TextMode == NTCRT_TM_UTF8);
    }
    _NtCRT_UnlockFd(Entry);
    return Bytes;
}

EXTERN_C int __cdecl _NtCRT_fd_crlf(int fd)
{
    PNTCRT_FD Entry = _NtCRT_FdEntry(fd);
    return Entry != NULL && (Entry->Flags & FOPEN) && (Entry->Flags & FCRLF);
}

/* Narrow stream I/O is invalid on a Unicode text mode fd or one opened with _O_WTEXT */
EXTERN_C int __cdecl _NtCRT_fd_narrow_invalid(int fd)
{
    PNTCRT_FD Entry = _NtCRT_FdEntry(fd);
    return Entry != NULL && (Entry->Flags & FOPEN) && (Entry->TextMode != NTCRT_TM_ANSI || Entry->WideOnly);
}

EXTERN_C int __cdecl _NtCRT_isatty(int fd)
{
    PNTCRT_FD Entry = _NtCRT_FdEntry(fd);

    if (Entry == NULL || !(Entry->Flags & FOPEN))
    {
        NTCRT_ERRNO = EBADF;
        return 0;
    }
    return (Entry->Flags & FDEV) ? FDEV : 0;
}

EXTERN_C intptr_t __cdecl _NtCRT_get_osfhandle(int fd)
{
    PNTCRT_FD Entry = _NtCRT_FdEntry(fd);

    if (Entry == NULL || !(Entry->Flags & FOPEN))
    {
        NTCRT_ERRNO = EBADF;
        _NtCRT_GetPtd()->Doserrno = 0;
        return -1;
    }
    return (intptr_t)Entry->Handle;
}

EXTERN_C int __cdecl _NtCRT_open_handle(HANDLE Handle, int flags)
{
    UCHAR Flags = 0;
    int fd;

    if (flags & _O_APPEND)
    {
        Flags |= FAPPEND;
    }
    if (flags & _O_TEXT)
    {
        Flags |= FTEXT;
    }
    if (flags & _O_NOINHERIT)
    {
        Flags |= FNOINHERIT;
    }
    Flags |= _NtCRT_DeviceFlags(Handle);
    fd = _NtCRT_AllocFd(0, Handle, Flags);
    return fd;
}

EXTERN_C int __cdecl _NtCRT_open_osfhandle(intptr_t osfhandle, int flags)
{
    HANDLE Handle = (HANDLE)osfhandle;

    if (_CRT_GetFileType(Handle) == FILE_TYPE_UNKNOWN && _Inline_GetLastError() != ERROR_SUCCESS)
    {
        NTCRT_ERRNO = EBADF;
        return -1;
    }
    return _NtCRT_open_handle(Handle, flags);
}

EXTERN_C int __cdecl _NtCRT_dup2(int fd1, int fd2)
{
    PNTCRT_FD Entry;
    HANDLE NewHandle;
    UCHAR Flags, TextMode;
    BOOLEAN WideOnly;
    NTSTATUS Status;

    if (fd1 == fd2)
    {
        return _NtCRT_FdEntry(fd1) != NULL && (_NtCRT_FdEntry(fd1)->Flags & FOPEN) ? 0 : (NTCRT_ERRNO = EBADF, -1);
    }
    if ((unsigned int)fd2 >= NTCRT_FD_MAX)
    {
        NTCRT_ERRNO = EBADF;
        return -1;
    }
    Entry = _NtCRT_LockFd(fd1);
    if (Entry == NULL)
    {
        return -1;
    }
    Flags = Entry->Flags & ~FNOINHERIT;
    TextMode = Entry->TextMode;
    WideOnly = Entry->WideOnly;
    Status = NtDuplicateObject(NtCurrentProcess(),
                               Entry->Handle,
                               NtCurrentProcess(),
                               &NewHandle,
                               0,
                               OBJ_INHERIT,
                               DUPLICATE_SAME_ACCESS);
    _NtCRT_UnlockFd(Entry);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }
    if (_NtCRT_FdEntry(fd2) != NULL && (_NtCRT_FdEntry(fd2)->Flags & FOPEN))
    {
        _NtCRT_close(fd2);
    }
    _NtCRT_SetFd(fd2, NewHandle, Flags);
    _NtCRT_FdEntry(fd2)->TextMode = TextMode;
    _NtCRT_FdEntry(fd2)->WideOnly = WideOnly;
    if (fd2 <= 2)
    {
        _Inline_SetStdHandle(fd2 == 0 ? STD_INPUT_HANDLE : fd2 == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE, NewHandle);
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT_dup(int fd)
{
    PNTCRT_FD Entry;
    HANDLE NewHandle;
    UCHAR Flags, TextMode;
    BOOLEAN WideOnly;
    NTSTATUS Status;
    int NewFd;

    Entry = _NtCRT_LockFd(fd);
    if (Entry == NULL)
    {
        return -1;
    }
    Flags = Entry->Flags & ~FNOINHERIT;
    TextMode = Entry->TextMode;
    WideOnly = Entry->WideOnly;
    Status = NtDuplicateObject(NtCurrentProcess(),
                               Entry->Handle,
                               NtCurrentProcess(),
                               &NewHandle,
                               0,
                               OBJ_INHERIT,
                               DUPLICATE_SAME_ACCESS);
    _NtCRT_UnlockFd(Entry);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }
    NewFd = _NtCRT_AllocFd(0, NewHandle, Flags);
    if (NewFd < 0)
    {
        NtClose(NewHandle);
        return -1;
    }
    _NtCRT_FdEntry(NewFd)->TextMode = TextMode;
    _NtCRT_FdEntry(NewFd)->WideOnly = WideOnly;
    return NewFd;
}

/* Set the text mode of a newly opened text fd by oflag, _fmode and the BOM of the file as msvcrt */
static
int
_NtCRT_SetUnicodeMode(
    _In_ int fd,
    _In_ int oflag,
    _In_ ULONG Disposition)
{
    const int UnicodeModes = _O_WTEXT | _O_U16TEXT | _O_U8TEXT;
    PNTCRT_FD Entry = _NtCRT_FdEntry(fd);
    UCHAR TextMode = NTCRT_TM_ANSI;
    BOOL CheckBom = FALSE, WriteBom = FALSE;
    int Access = oflag & (_O_RDONLY | _O_WRONLY | _O_RDWR);

    if ((oflag & (_O_TEXT | UnicodeModes)) == 0)
    {
        oflag |= (_NtCRT_fmode & (_O_TEXT | UnicodeModes)) != 0 ? _NtCRT_fmode & (_O_TEXT | UnicodeModes) : _O_TEXT;
    }
    if (oflag & _O_U8TEXT)
    {
        TextMode = NTCRT_TM_UTF8;
    } else if ((oflag & _O_U16TEXT) ||
               ((oflag & _O_WTEXT) && (oflag & (_O_WRONLY | _O_CREAT | _O_TRUNC)) == (_O_WRONLY | _O_CREAT | _O_TRUNC)))
    {
        TextMode = NTCRT_TM_UTF16LE;
    }

    if ((oflag & UnicodeModes) && !(Entry->Flags & FDEV))
    {
        BOOL Opening = Disposition == FILE_OPEN || Disposition == FILE_OPEN_IF;

        if (Access == _O_RDONLY)
        {
            CheckBom = TRUE;
        } else if (Opening)
        {
            long long Size = _NtCRT_lseeki64(fd, 0, SEEK_END);

            if (Size == -1)
            {
                return -1;
            }
            if (Size == 0)
            {
                WriteBom = TRUE;
            } else if (_NtCRT_lseeki64(fd, 0, SEEK_SET) == -1)
            {
                return -1;
            } else
            {
                CheckBom = Access == _O_RDWR;
            }
        } else
        {
            WriteBom = TRUE;
        }
    }
    if (CheckBom)
    {
        ULONG Bom = 0;
        int Count = _NtCRT_read(fd, &Bom, 3);
        long long Position = 0;

        if (Count == -1)
        {
            return -1;
        }
        if (Count == 3 && Bom == 0xBFBBEF)
        {
            TextMode = NTCRT_TM_UTF8;
            Position = -1;
        } else if (Count >= 2 && (Bom & 0xFFFF) == 0xFFFE)
        {
            NTCRT_ERRNO = EINVAL;
            return -1;
        } else if (Count >= 2 && (Bom & 0xFFFF) == 0xFEFF)
        {
            TextMode = NTCRT_TM_UTF16LE;
            Position = 2;
        }
        if (Position != -1 && _NtCRT_lseeki64(fd, Position, SEEK_SET) == -1)
        {
            return -1;
        }
    }
    if (WriteBom && TextMode != NTCRT_TM_ANSI)
    {
        static const UCHAR Utf8Bom[] = { 0xEF, 0xBB, 0xBF }, Utf16Bom[] = { 0xFF, 0xFE };
        const UCHAR* Bom = TextMode == NTCRT_TM_UTF8 ? Utf8Bom : Utf16Bom;
        int Length = TextMode == NTCRT_TM_UTF8 ? sizeof(Utf8Bom) : sizeof(Utf16Bom), Written = 0;

        while (Written < Length)
        {
            int Result = _NtCRT_write(fd, Bom + Written, (unsigned int)(Length - Written));
            if (Result == -1)
            {
                return -1;
            }
            Written += Result;
        }
    }
    Entry->TextMode = TextMode;
    Entry->WideOnly = (oflag & _O_WTEXT) != 0;
    return 0;
}

EXTERN_C int __cdecl _NtCRT_wsopen(const wchar_t* path, int oflag, int shflag, int pmode)
{
    ACCESS_MASK Access;
    ULONG Share, Disposition, Options, Attributes;
    HANDLE Handle;
    ULONG Error;
    UCHAR Flags = 0;
    int fd;

    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    switch (oflag & (_O_RDONLY | _O_WRONLY | _O_RDWR))
    {
        case _O_RDONLY: Access = GENERIC_READ; break;
        case _O_WRONLY: Access = (oflag & _O_APPEND) ? GENERIC_WRITE | GENERIC_READ : GENERIC_WRITE; break;
        case _O_RDWR: Access = GENERIC_READ | GENERIC_WRITE; break;
        default:
            NTCRT_ERRNO = EINVAL;
            return -1;
    }
    switch (shflag)
    {
        case _SH_DENYRW: Share = 0; break;
        case _SH_DENYWR: Share = FILE_SHARE_READ; break;
        case _SH_DENYRD: Share = FILE_SHARE_WRITE; break;
        case _SH_DENYNO: Share = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
        default:
            NTCRT_ERRNO = EINVAL;
            return -1;
    }
    switch (oflag & (_O_CREAT | _O_EXCL | _O_TRUNC))
    {
        case 0:
        case _O_EXCL: Disposition = FILE_OPEN; break;
        case _O_CREAT: Disposition = FILE_OPEN_IF; break;
        case _O_CREAT | _O_EXCL:
        case _O_CREAT | _O_TRUNC | _O_EXCL: Disposition = FILE_CREATE; break;
        case _O_CREAT | _O_TRUNC: Disposition = FILE_OVERWRITE_IF; break;
        case _O_TRUNC:
        case _O_TRUNC | _O_EXCL: Disposition = FILE_OVERWRITE; break;
        default:
            NTCRT_ERRNO = EINVAL;
            return -1;
    }
    Options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE;
    Attributes = FILE_ATTRIBUTE_NORMAL;
    if ((oflag & _O_CREAT) && !(pmode & _S_IWRITE))
    {
        Attributes = FILE_ATTRIBUTE_READONLY;
    }
    if (oflag & _O_TEMPORARY)
    {
        Options |= FILE_DELETE_ON_CLOSE;
        Access |= DELETE;
        Share |= FILE_SHARE_DELETE;
    }
    if (oflag & _O_SHORT_LIVED)
    {
        Attributes |= FILE_ATTRIBUTE_TEMPORARY;
    }
    if (oflag & _O_SEQUENTIAL)
    {
        Options |= FILE_SEQUENTIAL_ONLY;
    } else if (oflag & _O_RANDOM)
    {
        Options |= FILE_RANDOM_ACCESS;
    }

    Error = _NtCRT_CreateFile(&Handle,
                              path,
                              Access | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                              Share,
                              Disposition,
                              Options,
                              Attributes,
                              NULL);
    if (Error != ERROR_SUCCESS && (Access & GENERIC_READ) && (oflag & _O_APPEND) &&
        (oflag & (_O_RDONLY | _O_WRONLY | _O_RDWR)) == _O_WRONLY)
    {
        /* Write-only file opened for appending, retry without read access */
        Access &= ~GENERIC_READ;
        Error = _NtCRT_CreateFile(&Handle,
                                  path,
                                  Access | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                                  Share,
                                  Disposition,
                                  Options,
                                  Attributes,
                                  NULL);
    }
    if (Error != ERROR_SUCCESS)
    {
        _NtCRT_SetErrnoFromWin32(Error);
        return -1;
    }

    if (oflag & _O_APPEND)
    {
        Flags |= FAPPEND;
    }
    if (oflag & _O_NOINHERIT)
    {
        Flags |= FNOINHERIT;
    } else
    {
        /* msvcrt opens inheritable handles, spawned children receive them by lpReserved2 */
        OBJECT_HANDLE_FLAG_INFORMATION Info;

        if (NT_SUCCESS(NtQueryObject(Handle, ObjectHandleFlagInformation, &Info, sizeof(Info), NULL)))
        {
            Info.Inherit = TRUE;
            NtSetInformationObject(Handle, ObjectHandleFlagInformation, &Info, sizeof(Info));
        }
    }
    if ((oflag & _O_TEXT) ||
        (!(oflag & _O_BINARY) && ((oflag & (_O_WTEXT | _O_U16TEXT | _O_U8TEXT)) || _NtCRT_fmode != _O_BINARY)))
    {
        Flags |= FTEXT;
    }
    Flags |= _NtCRT_DeviceFlags(Handle);
    fd = _NtCRT_AllocFd(0, Handle, Flags);
    if (fd < 0)
    {
        NtClose(Handle);
        return -1;
    }

    /* As msvcrt, a Ctrl+Z at the end of a text file opened for reading and writing is removed for appending */
    if ((Flags & (FTEXT | FDEV | FPIPE)) == FTEXT && (oflag & _O_RDWR))
    {
        long long Position = _NtCRT_lseeki64(fd, -1, SEEK_END);
        char c = 0;

        if (Position == -1)
        {
            /* An empty file, errno is left as EINVAL */
            if (_NtCRT_GetPtd()->Doserrno != ERROR_NEGATIVE_SEEK)
            {
                _NtCRT_close(fd);
                return -1;
            }
        } else if ((_NtCRT_read(fd, &c, 1) == 0 && c == 0x1A && _NtCRT_chsize_s(fd, Position) != 0) ||
                   _NtCRT_lseeki64(fd, 0, SEEK_SET) == -1)
        {
            _NtCRT_close(fd);
            return -1;
        }
    }
    if ((Flags & FTEXT) && _NtCRT_SetUnicodeMode(fd, oflag, Disposition) != 0)
    {
        _NtCRT_close(fd);
        return -1;
    }
    return fd;
}

EXTERN_C int __cdecl _NtCRT_sopen(const char* path, int oflag, int shflag, ...)
{
    int pmode = 0;
    int fd;
    PWSTR WidePath;

    if (oflag & _O_CREAT)
    {
        va_list ap;
        va_start(ap, shflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    WidePath = _NtCRT_FileToWide(path);
    if (WidePath == NULL)
    {
        return -1;
    }
    fd = _NtCRT_wsopen(WidePath, oflag, shflag, pmode);
    RtlFreeHeap(NTCRT_HEAP, 0, WidePath);
    return fd;
}

EXTERN_C int __cdecl _NtCRT__wsopen(const wchar_t* path, int oflag, int shflag, ...)
{
    int pmode = 0;

    if (oflag & _O_CREAT)
    {
        va_list ap;
        va_start(ap, shflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    return _NtCRT_wsopen(path, oflag, shflag, pmode);
}

EXTERN_C int __cdecl _NtCRT_open(const char* path, int oflag, ...)
{
    int pmode = 0;

    if (oflag & _O_CREAT)
    {
        va_list ap;
        va_start(ap, oflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    return _NtCRT_sopen(path, oflag, _SH_DENYNO, pmode);
}

EXTERN_C int __cdecl _NtCRT_wopen(const wchar_t* path, int oflag, ...)
{
    int pmode = 0;

    if (oflag & _O_CREAT)
    {
        va_list ap;
        va_start(ap, oflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    return _NtCRT_wsopen(path, oflag, _SH_DENYNO, pmode);
}

EXTERN_C int __cdecl _NtCRT_creat(const char* path, int pmode)
{
    return _NtCRT_sopen(path, _O_CREAT | _O_TRUNC | _O_RDWR, _SH_DENYNO, pmode);
}

EXTERN_C int __cdecl _NtCRT_wcreat(const wchar_t* path, int pmode)
{
    return _NtCRT_wsopen(path, _O_CREAT | _O_TRUNC | _O_RDWR, _SH_DENYNO, pmode);
}

/* The permission of a new file is validated */
EXTERN_C int __cdecl _NtCRT_wsopen_s(int* pfh, const wchar_t* path, int oflag, int shflag, int pmode)
{
    if (pfh == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pfh = -1;
    if ((oflag & _O_CREAT) && (pmode & ~(_S_IREAD | _S_IWRITE)))
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pfh = _NtCRT_wsopen(path, oflag, shflag, pmode);
    return *pfh < 0 ? NTCRT_ERRNO : 0;
}

EXTERN_C int __cdecl _NtCRT__sopen_s(int* pfh, const char* path, int oflag, int shflag, int pmode)
{
    if (pfh == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pfh = -1;
    if ((oflag & _O_CREAT) && (pmode & ~(_S_IREAD | _S_IWRITE)))
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pfh = _NtCRT_sopen(path, oflag, shflag, pmode);
    return *pfh < 0 ? NTCRT_ERRNO : 0;
}

/* stat */

#define NTCRT_EPOCH_DIFFERENCE 116444736000000000LL

static
__time64_t
_NtCRT_FileTimeToTime64(
    _In_ LONGLONG Time)
{
    if (Time <= NTCRT_EPOCH_DIFFERENCE)
    {
        return 0;
    }
    return (Time - NTCRT_EPOCH_DIFFERENCE) / 10000000LL;
}

static
unsigned short
_NtCRT_ModeFromAttributes(
    _In_ ULONG Attributes,
    _In_opt_ PCWSTR Path)
{
    unsigned short Mode;

    if (Attributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        Mode = _S_IFDIR | _S_IEXEC;
    } else
    {
        Mode = _S_IFREG;
        if (Path != NULL)
        {
            static const WCHAR Executables[][5] = { L".exe", L".com", L".bat", L".cmd" };
            PCWSTR Ext = wcsrchr(Path, L'.');

            for (size_t i = 0; Ext != NULL && wcslen(Ext) == 4 && i < ARRAYSIZE(Executables); i++)
            {
                size_t k = 1;

                /* ASCII case-insensitive */
                while (k < 4 && (Ext[k] | 0x20) == Executables[i][k])
                {
                    k++;
                }
                if (k == 4)
                {
                    Mode |= _S_IEXEC;
                    break;
                }
            }
        }
    }
    Mode |= _S_IREAD;
    if (!(Attributes & FILE_ATTRIBUTE_READONLY))
    {
        Mode |= _S_IWRITE;
    }
    /* Copy the user permissions to group and others */
    Mode |= (Mode & 0700) >> 3;
    Mode |= (Mode & 0700) >> 6;
    return Mode;
}

EXTERN_C int __cdecl _NtCRT_wstat64(const wchar_t* path, struct _stat64* buffer)
{
    NTSTATUS Status;
    HANDLE Handle;
    UNICODE_STRING NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_BASIC_INFORMATION Basic;
    FILE_STANDARD_INFORMATION Standard;
    int Drive;

    if (path == NULL || buffer == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    /* Wildcards are not allowed */
    if (wcspbrk(path, L"?*") != NULL)
    {
        NTCRT_ERRNO = ENOENT;
        return -1;
    }
    Status = RtlDosPathNameToNtPathName_U_WithStatus(path, &NtPath, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }
    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle,
                        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
    RtlFreeUnicodeString(&NtPath);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }
    Status = NtQueryInformationFile(Handle, &IoStatusBlock, &Basic, sizeof(Basic), FileBasicInformation);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryInformationFile(Handle, &IoStatusBlock, &Standard, sizeof(Standard), FileStandardInformation);
    }
    NtClose(Handle);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }

    if (path[0] != L'\0' && path[1] == L':')
    {
        Drive = (path[0] | 0x20) - L'a';
    } else
    {
        WCHAR Current[MAX_PATH];
        Drive = RtlGetCurrentDirectory_U(sizeof(Current), Current) >= 2 * sizeof(WCHAR) && Current[1] == L':' ?
            (Current[0] | 0x20) - L'a' :
            0;
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->st_dev = buffer->st_rdev = (_dev_t)Drive;
    buffer->st_mode = _NtCRT_ModeFromAttributes(Basic.FileAttributes, path);
    buffer->st_nlink = (short)Standard.NumberOfLinks;
    buffer->st_size = (Basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : Standard.EndOfFile.QuadPart;
    buffer->st_atime = _NtCRT_FileTimeToTime64(Basic.LastAccessTime.QuadPart);
    buffer->st_mtime = _NtCRT_FileTimeToTime64(Basic.LastWriteTime.QuadPart);
    buffer->st_ctime = _NtCRT_FileTimeToTime64(Basic.CreationTime.QuadPart);
    return 0;
}

EXTERN_C int __cdecl _NtCRT_stat64(const char* path, struct _stat64* buffer)
{
    int Result;
    PWSTR WidePath;

    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    WidePath = _NtCRT_FileToWide(path);
    if (WidePath == NULL)
    {
        return -1;
    }
    Result = _NtCRT_wstat64(WidePath, buffer);
    RtlFreeHeap(NTCRT_HEAP, 0, WidePath);
    return Result;
}

EXTERN_C int __cdecl _NtCRT_fstat64(int fd, struct _stat64* buffer)
{
    PNTCRT_FD Entry;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_BASIC_INFORMATION Basic;
    FILE_STANDARD_INFORMATION Standard;
    NTSTATUS Status;
    int Result = 0;

    if (buffer == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Entry = _NtCRT_LockFd(fd);
    if (Entry == NULL)
    {
        return -1;
    }
    memset(buffer, 0, sizeof(*buffer));
    if (Entry->Flags & (FDEV | FPIPE))
    {
        buffer->st_mode = (Entry->Flags & FDEV) ? _S_IFCHR : _S_IFIFO;
        buffer->st_nlink = 1;
        buffer->st_dev = buffer->st_rdev = (_dev_t)fd;
        if (Entry->Flags & FPIPE)
        {
            buffer->st_mode |= _S_IREAD | _S_IWRITE;
        }
        goto _Exit;
    }
    Status = NtQueryInformationFile(Entry->Handle, &IoStatusBlock, &Basic, sizeof(Basic), FileBasicInformation);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryInformationFile(Entry->Handle,
                                        &IoStatusBlock,
                                        &Standard,
                                        sizeof(Standard),
                                        FileStandardInformation);
    }
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        Result = -1;
        goto _Exit;
    }
    buffer->st_dev = buffer->st_rdev = (_dev_t)fd;
    buffer->st_mode = _NtCRT_ModeFromAttributes(Basic.FileAttributes, NULL);
    buffer->st_nlink = (short)Standard.NumberOfLinks;
    buffer->st_size = Standard.EndOfFile.QuadPart;
    buffer->st_atime = _NtCRT_FileTimeToTime64(Basic.LastAccessTime.QuadPart);
    buffer->st_mtime = _NtCRT_FileTimeToTime64(Basic.LastWriteTime.QuadPart);
    buffer->st_ctime = _NtCRT_FileTimeToTime64(Basic.CreationTime.QuadPart);

_Exit:
    _NtCRT_UnlockFd(Entry);
    return Result;
}

/* Other stat variants convert from _stat64 */

#define NTCRT_STAT_CONVERT(Dst, Src) \
    do { \
        (Dst)->st_dev = (Src).st_dev; \
        (Dst)->st_ino = (Src).st_ino; \
        (Dst)->st_mode = (Src).st_mode; \
        (Dst)->st_nlink = (Src).st_nlink; \
        (Dst)->st_uid = (Src).st_uid; \
        (Dst)->st_gid = (Src).st_gid; \
        (Dst)->st_rdev = (Src).st_rdev; \
        (Dst)->st_size = (Src).st_size; \
        (Dst)->st_atime = (Src).st_atime; \
        (Dst)->st_mtime = (Src).st_mtime; \
        (Dst)->st_ctime = (Src).st_ctime; \
    } while (0)

EXTERN_C int __cdecl _NtCRT_fstati64(int fd, struct _stati64* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_fstat64(fd, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

EXTERN_C int __cdecl _NtCRT_stati64(const char* path, struct _stati64* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_stat64(path, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

EXTERN_C int __cdecl _NtCRT_wstati64(const wchar_t* path, struct _stati64* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_wstat64(path, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

EXTERN_C int __cdecl _NtCRT_fstat32(int fd, struct _stat32* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_fstat64(fd, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

EXTERN_C int __cdecl _NtCRT_stat32(const char* path, struct _stat32* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_stat64(path, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

EXTERN_C int __cdecl _NtCRT_wstat32(const wchar_t* path, struct _stat32* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_wstat64(path, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

NTCRT_IMPORT(_read, _NtCRT_read);
NTCRT_IMPORT(_write, _NtCRT_write);
NTCRT_IMPORT(_close, _NtCRT_close);
NTCRT_IMPORT(_lseeki64, _NtCRT_lseeki64);
NTCRT_IMPORT(_lseek, _NtCRT_lseek);
NTCRT_IMPORT(_telli64, _NtCRT_telli64);
NTCRT_IMPORT(_tell, _NtCRT_tell);
NTCRT_IMPORT(_filelengthi64, _NtCRT_filelengthi64);
NTCRT_IMPORT(_filelength, _NtCRT_filelength);
NTCRT_IMPORT(_chsize_s, _NtCRT_chsize_s);
NTCRT_IMPORT(_chsize, _NtCRT_chsize);
NTCRT_IMPORT(_eof, _NtCRT_eof);
NTCRT_IMPORT(_commit, _NtCRT_commit);
NTCRT_IMPORT(_setmode, _NtCRT_setmode);
NTCRT_IMPORT(_isatty, _NtCRT_isatty);
NTCRT_IMPORT(_get_osfhandle, _NtCRT_get_osfhandle);
NTCRT_IMPORT(_open_osfhandle, _NtCRT_open_osfhandle);
NTCRT_IMPORT(_dup, _NtCRT_dup);
NTCRT_IMPORT(_dup2, _NtCRT_dup2);
NTCRT_IMPORT(_sopen, _NtCRT_sopen);
NTCRT_IMPORT(_wsopen, _NtCRT__wsopen);
NTCRT_IMPORT(_wsopen_s, _NtCRT_wsopen_s);
NTCRT_IMPORT(_sopen_s, _NtCRT__sopen_s);
NTCRT_IMPORT(_open, _NtCRT_open);
NTCRT_IMPORT(_wopen, _NtCRT_wopen);
NTCRT_IMPORT(_creat, _NtCRT_creat);
NTCRT_IMPORT(_wcreat, _NtCRT_wcreat);
NTCRT_IMPORT(_wstat64, _NtCRT_wstat64);
NTCRT_IMPORT(_stat64, _NtCRT_stat64);
NTCRT_IMPORT(_fstat64, _NtCRT_fstat64);
NTCRT_IMPORT(_fstati64, _NtCRT_fstati64);
NTCRT_IMPORT(_stati64, _NtCRT_stati64);
NTCRT_IMPORT(_wstati64, _NtCRT_wstati64);
NTCRT_IMPORT(_fstat32, _NtCRT_fstat32);
NTCRT_IMPORT(_stat32, _NtCRT_stat32);
NTCRT_IMPORT(_wstat32, _NtCRT_wstat32);
NTCRT_IMPORT(_fstat, _NtCRT_fstat32);
NTCRT_IMPORT(_stat, _NtCRT_stat32);
NTCRT_IMPORT(_wstat, _NtCRT_wstat32);

/* POSIX names */
NTCRT_IMPORT(read, _NtCRT_read);
NTCRT_IMPORT(write, _NtCRT_write);
NTCRT_IMPORT(close, _NtCRT_close);
NTCRT_IMPORT(lseek, _NtCRT_lseek);
NTCRT_IMPORT(tell, _NtCRT_tell);
NTCRT_IMPORT(chsize, _NtCRT_chsize);
NTCRT_IMPORT(eof, _NtCRT_eof);
NTCRT_IMPORT(filelength, _NtCRT_filelength);
NTCRT_IMPORT(setmode, _NtCRT_setmode);
NTCRT_IMPORT(isatty, _NtCRT_isatty);
NTCRT_IMPORT(dup, _NtCRT_dup);
NTCRT_IMPORT(dup2, _NtCRT_dup2);
NTCRT_IMPORT(sopen, _NtCRT_sopen);
NTCRT_IMPORT(open, _NtCRT_open);
NTCRT_IMPORT(creat, _NtCRT_creat);
