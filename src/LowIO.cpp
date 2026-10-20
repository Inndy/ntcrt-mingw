/* msvcrt low-level I/O: file descriptors on NT file handles */

#include "NtCRT.h"
#include "LowIO.inl"

#include <errno.h>
#include <io.h>

EXTERN_C
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
    if (!_NtCRT_EnterFd(Entry))
    {
        return NULL;
    }
    if (!(Entry->Flags & FOPEN))
    {
        RtlLeaveCriticalSection(&Entry->Lock);
        NTCRT_ERRNO = EBADF;
        return NULL;
    }
    return Entry;
}

EXTERN_C
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
    if (_NtCRT_IsLegacyConsoleHandle(Handle))
    {
        NTCRT_ERRNO = EBADF;
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

EXTERN_C
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
    if (_NtCRT_IsLegacyConsoleHandle(Entry->Handle))
    {
        return TRUE;
    }
    if (NT_SUCCESS(NtQueryVolumeInformationFile(
            Entry->Handle, &IoStatusBlock, &Info, sizeof(Info), FileFsDeviceInformation)))
    {
        return Info.DeviceType == 0x50; /* FILE_DEVICE_CONSOLE */
    }
    return _CRT_GetConsoleMode(Entry->Handle, &Mode);
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

EXTERN_C
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

/* Public functions */

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
EXTERN_C int __cdecl _NtCRT_fd_crlf(int fd)
{
    PNTCRT_FD Entry = _NtCRT_FdEntry(fd);
    return Entry != NULL && (Entry->Flags & FOPEN) && (Entry->Flags & FCRLF);
}

/* Narrow stream I/O is invalid on a Unicode text mode fd or one opened with _O_WTEXT, not after _O_BINARY */
EXTERN_C int __cdecl _NtCRT_fd_narrow_invalid(int fd)
{
    PNTCRT_FD Entry = _NtCRT_FdEntry(fd);
    return Entry != NULL && (Entry->Flags & (FOPEN | FTEXT)) == (FOPEN | FTEXT) &&
           (Entry->TextMode != NTCRT_TM_ANSI || Entry->WideOnly);
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

NTCRT_IMPORT(_write, _NtCRT_write);
NTCRT_IMPORT(_isatty, _NtCRT_isatty);
NTCRT_IMPORT(_get_osfhandle, _NtCRT_get_osfhandle);

/* POSIX names */
NTCRT_IMPORT(write, _NtCRT_write);
NTCRT_IMPORT(isatty, _NtCRT_isatty);
