/* The file descriptors of LowIO*.cpp and Stat.cpp */

#pragma once

#include <errno.h>

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

/* The start of a console line, the middle, or the rest of the line after Ctrl+Z, dropped */
#define NTCRT_LINE_START    0
#define NTCRT_LINE_MIDDLE   1
#define NTCRT_LINE_SKIP     2

typedef struct _NTCRT_FD
{
    HANDLE Handle;
    UCHAR Flags;
    UCHAR PipeChar;
    BOOLEAN HasPipeChar;
    BOOLEAN CrBehind;       /* The last read of a text mode file left out the CR at its end, its LF is read next */
    UCHAR TextMode;
    BOOLEAN WideOnly;       /* Opened with _O_WTEXT, narrow stream I/O is rejected */
    BOOLEAN HasPipeWide;
    WCHAR PipeWide;         /* A UTF-16 unit read ahead */
    UCHAR CarryLength;
    UCHAR Carry[4];         /* Bytes read ahead from a pipe, the start of a UTF-16 unit or UTF-8 character */
    UCHAR PendingCount;
    WCHAR Pending[4];       /* UTF-16 units read from a UTF-8 pipe beyond the buffer, of a file only a low surrogate */
    int ReadCount;          /* Bytes the last Unicode read of a file returned, read from the position ReadStart */
    long long ReadStart;    /* -1 if unknown */
    unsigned int UnitIndex; /* A unit of that read whose position ftell found, UnitStart, 0 for none */
    long long UnitStart;
    WCHAR WriteHigh;        /* A high surrogate written to a console or UTF-8 fd, waiting for its low surrogate */
    UCHAR WriteCarryLength;
    UCHAR WriteCarry[4];    /* The start of a UTF-8 character written to a console in "C.UTF-8" */
    BOOLEAN ConsoleEof;     /* Ctrl+Z at the start of a line was read from a console after the text returned */
    UCHAR ConsoleLine;      /* Where reads by ReadConsoleW are in the line, NTCRT_LINE_* */
    char* ConsoleText;      /* UTF-8 read from a console in "C.UTF-8" and not returned yet */
    ULONG ConsoleTextLength;
    ULONG ConsoleTextPos;
    RTL_CRITICAL_SECTION Lock;
} NTCRT_FD, *PNTCRT_FD;

/* The descriptors in blocks of NTCRT_FD_BLOCK_SIZE, allocated on use, see LowIOInit.cpp */
extern "C" PNTCRT_FD _NtCRT_FdBlocks[NTCRT_FD_MAX / NTCRT_FD_BLOCK_SIZE];
extern "C" RTL_SRWLOCK _NtCRT_FdTableLock;

static inline
PNTCRT_FD
_NtCRT_FdEntry(
    _In_ int fd)
{
    PNTCRT_FD Block;

    if ((unsigned int)fd >= NTCRT_FD_MAX)
    {
        return NULL;
    }
    Block = _NtCRT_FdBlocks[fd / NTCRT_FD_BLOCK_SIZE];
    return Block != NULL ? &Block[fd % NTCRT_FD_BLOCK_SIZE] : NULL;
}

static inline
void
_NtCRT_InitFdState(
    _Out_ PNTCRT_FD Entry,
    _In_ HANDLE Handle,
    _In_ UCHAR Flags)
{
    Entry->Handle = Handle;
    Entry->Flags = Flags | FOPEN;
    Entry->HasPipeChar = FALSE;
    Entry->CrBehind = FALSE;
    Entry->TextMode = NTCRT_TM_ANSI;
    Entry->WideOnly = FALSE;
    Entry->HasPipeWide = FALSE;
    Entry->CarryLength = 0;
    Entry->PendingCount = 0;
    Entry->ReadCount = 0;
    Entry->UnitIndex = 0;
    Entry->WriteHigh = 0;
    Entry->WriteCarryLength = 0;
    Entry->ConsoleEof = FALSE;
    Entry->ConsoleLine = NTCRT_LINE_START;
    Entry->ConsoleTextLength = Entry->ConsoleTextPos = 0;
}

/*
 * Enter the lock of an fd, or set errno to EBUSY. While the process ends without exit the other threads are gone, a
 * lock that one of them held is never released and entering its critical section would end the process.
 */
static inline
BOOL
_NtCRT_EnterFd(
    _In_ PNTCRT_FD Entry)
{
    if (!NtCurrentPeb()->Ldr->ShutdownInProgress)
    {
        RtlEnterCriticalSection(&Entry->Lock);
    } else if (!RtlTryEnterCriticalSection(&Entry->Lock))
    {
        NTCRT_ERRNO = EBUSY;
        return FALSE;
    }
    return TRUE;
}

/* Get an opened fd and lock it, or set errno to EBADF (EBUSY), see LowIO.cpp */
extern "C" PNTCRT_FD _NtCRT_LockFd(_In_ int fd);

static inline
void
_NtCRT_UnlockFd(
    _In_ PNTCRT_FD Entry)
{
    RtlLeaveCriticalSection(&Entry->Lock);
}

/* Allocate the block BlockIndex of the table, called with _NtCRT_FdTableLock held exclusively */
extern "C" PNTCRT_FD _NtCRT_AllocBlock(_In_ unsigned int BlockIndex);
/* Set an fd to a specific slot, used by initialization */
extern "C" BOOL _NtCRT_SetFd(_In_ int fd, _In_ HANDLE Handle, _In_ UCHAR Flags);
/* FDEV or FPIPE by the type of a handle */
extern "C" UCHAR _NtCRT_DeviceFlags(_In_ HANDLE Handle);

/* Raw I/O, console handles of Windows 7 and earlier are not NT handles */

typedef BOOL(WINAPI* PNTCRT_RW_FILE)(HANDLE, PVOID, DWORD, LPDWORD, LPOVERLAPPED);

/* ReadFile (Write FALSE) or WriteFile of kernel32 */
extern "C" PNTCRT_RW_FILE _NtCRT_GetLegacyConsoleIo(_In_ BOOL Write);

/* A console handle of Windows 7 (see NtCRT.h) goes to kernel32, what a console does not do fails with EBADF */

/* An inheritable duplicate in this process, DuplicateHandle of kernel32 for a console handle of Windows 7 */
extern "C" NTSTATUS _NtCRT_DuplicateHandle(_In_ HANDLE Handle, _Out_ PHANDLE NewHandle);

/* Returns bytes written or -1 for failure with errno set */
extern "C" int _NtCRT_RawWrite(_In_ HANDLE Handle, _In_reads_bytes_(Count) const void* Buffer, _In_ ULONG Count);
extern "C" long long _NtCRT_SeekHandle(_In_ HANDLE Handle, _In_ long long Offset, _In_ int Origin);

/* Unicode text modes, see LowIO.cpp */

/* By the device type, GetConsoleMode needs read access the handles opened for writing do not have */
extern "C" BOOL _NtCRT_IsConsoleFd(_In_ PNTCRT_FD Entry);
/* Bytes of an incomplete UTF-8 character at the end of a buffer */
extern "C" unsigned int _NtCRT_Utf8Tail(_In_reads_(Length) const UCHAR* Bytes, _In_ unsigned int Length);
