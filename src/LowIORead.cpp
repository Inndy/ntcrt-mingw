/* _read: bytes, the Unicode text modes and the console in "C.UTF-8" */

#include "NtCRT.h"
#include "LowIO.inl"

#include <errno.h>

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
 * A file seeks back to read CRLF next time unless nothing has been read (msvcrt reads the LF alone), so that its
 * position is that of the newline. A pipe keeps the peeked unit if it is not LF.
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
    if (IsLf)
    {
        _NtCRT_SeekHandle(Entry->Handle, -2LL * UnitLength, SEEK_CUR);
        return 0;
    }
    _NtCRT_PutBack(Entry, Peek, PeekLength);
    return L'\r';
}

typedef BOOL(WINAPI* PNTCRT_READ_CONSOLE_W)(HANDLE, PVOID, DWORD, LPDWORD, PVOID);

/*
 * The character for a CR that ends the units read from a console. In line input mode the console has the rest of
 * the line: the peeked unit makes CRLF one LF, another unit is kept for the next read.
 */
static
WCHAR
_NtCRT_ConsoleCrAtEnd(
    _In_ PNTCRT_FD Entry,
    _In_opt_ PNTCRT_READ_CONSOLE_W ReadConsoleW)
{
    DWORD Mode, Got = 0;
    WCHAR Peek;

    if (ReadConsoleW == NULL || !_CRT_GetConsoleMode(Entry->Handle, &Mode) || !(Mode & ENABLE_LINE_INPUT) ||
        !ReadConsoleW(Entry->Handle, &Peek, 1, &Got, NULL) || Got == 0)
    {
        return L'\r';
    }
    if (Peek == L'\n')
    {
        return L'\n';
    }
    Entry->PipeWide = Peek;
    Entry->HasPipeWide = TRUE;
    return L'\r';
}

/*
 * The text of the units read from a console by ReadConsoleW, in place, as ReadFile of a console and msvcrt read it
 * in text mode: CRLF is a newline, Ctrl+Z at the start of a line ends the input once, in the middle of a line it is
 * kept, and the rest of that line is dropped. Returns the units kept, *Eof for the end of the input after them.
 */
static
ULONG
_NtCRT_ConsoleText(
    _In_ PNTCRT_FD Entry,
    _Inout_updates_(Count) PWCHAR Units,
    _In_ ULONG Count,
    _In_opt_ PNTCRT_READ_CONSOLE_W ReadConsoleW,
    _Out_ PBOOL Eof)
{
    ULONG Kept = 0;

    *Eof = FALSE;
    /* A console returns a line by a read, what follows the line of the end is lost */
    for (ULONG i = 0; i < Count && !(*Eof && Entry->ConsoleLine == NTCRT_LINE_START); i++)
    {
        WCHAR c = Units[i];

        if (Entry->ConsoleLine == NTCRT_LINE_SKIP)
        {
            if (c == L'\n')
            {
                Entry->ConsoleLine = NTCRT_LINE_START;
            }
            continue;
        }
        if (c == 0x1A)
        {
            if (Entry->ConsoleLine == NTCRT_LINE_START)
            {
                *Eof = TRUE;
            } else
            {
                Units[Kept++] = c;
            }
            Entry->ConsoleLine = NTCRT_LINE_SKIP;
            continue;
        }
        if (c == L'\r' && i + 1 < Count && Units[i + 1] == L'\n')
        {
            continue;
        }
        if (c == L'\r' && i + 1 == Count)
        {
            c = _NtCRT_ConsoleCrAtEnd(Entry, ReadConsoleW);
        }
        Units[Kept++] = c;
        Entry->ConsoleLine = c == L'\n' ? NTCRT_LINE_START : NTCRT_LINE_MIDDLE;
    }
    return Kept;
}

/*
 * Read UTF-8 of "C.UTF-8" from a text mode console by its characters, see _NtCRT_ConsoleText. Returns the bytes
 * stored, 0 for EOF or -1.
 */
static
int
_NtCRT_ReadConsoleUtf8(
    _In_ PNTCRT_FD Entry,
    _Out_writes_bytes_(Count) char* Buffer,
    _In_ unsigned int Count)
{
    unsigned int Length;

    while (Entry->ConsoleTextPos == Entry->ConsoleTextLength)
    {
        auto const ReadConsoleW = (PNTCRT_READ_CONSOLE_W)_NtCRT_GetKernel32Function("ReadConsoleW");
        WCHAR Wide[1024];
        DWORD Units = 0, Got = 0, Kept;
        BOOL Eof;
        int Size;

        if (Entry->ConsoleEof)
        {
            /* Not sticky, the next read waits for the console as ReadFile does */
            Entry->ConsoleEof = FALSE;
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
        Kept = _NtCRT_ConsoleText(Entry, Wide, Units, ReadConsoleW, &Eof);
        Entry->ConsoleEof = (BOOLEAN)Eof;
        if (!Eof && Kept > 0 && IS_HIGH_SURROGATE(Wide[Kept - 1]))
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
    BOOL Whole = FALSE;     /* The UTF-8 read ends with whole characters, valid or not */

    /* The position of the units returned, a file reads them again for ftell, see StdioSeek.cpp */
    Entry->UnitIndex = 0;
    Entry->ReadStart = (Entry->Flags & (FPIPE | FDEV)) || Entry->PendingCount != 0
                           ? -1
                           : _NtCRT_SeekHandle(Entry->Handle, 0, SEEK_CUR);
    if (_NtCRT_IsConsoleFd(Entry))
    {
        /*
         * Ctrl+Z as in the narrow text mode (_NtCRT_ConsoleText): msvcrt and UCRT drop it and the rest of the line in
         * _O_U16TEXT and end the input of the fd for good
         */
        PNTCRT_READ_CONSOLE_W ReadConsoleW = (PNTCRT_READ_CONSOLE_W)_NtCRT_GetKernel32Function("ReadConsoleW");

        for (;;)
        {
            DWORD Got = 0;
            BOOL Eof;

            if (Entry->ConsoleEof)
            {
                Entry->ConsoleEof = FALSE;
                return 0;
            }
            Kept = 0;
            if (Entry->HasPipeWide)
            {
                Out[Kept++] = Entry->PipeWide;
                Entry->HasPipeWide = FALSE;
            }
            if (Kept < Units &&
                (ReadConsoleW == NULL || !ReadConsoleW(Entry->Handle, Out + Kept, Units - Kept, &Got, NULL)))
            {
                if (Kept == 0)
                {
                    _NtCRT_SetErrnoFromWin32(ReadConsoleW == NULL ? ERROR_INVALID_HANDLE : _Inline_GetLastError());
                    return -1;
                }
                Got = 0;
            }
            if (Kept + Got == 0)
            {
                return 0;
            }
            Kept = _NtCRT_ConsoleText(Entry, Out, Kept + Got, ReadConsoleW, &Eof);
            if (Kept != 0)
            {
                Entry->ConsoleEof = (BOOLEAN)Eof;
                return (int)(Kept * sizeof(WCHAR));
            }
            if (Eof)
            {
                return 0;
            }
        }
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
                    } else
                    {
                        /* The CRLF read next time ends the character before it */
                        Whole = TRUE;
                    }
                }
            }
            RawCount = (ULONG)(Dst - Raw);
        }
        Tail = Eof || Whole ? 0 : _NtCRT_Utf8Tail(Raw, RawCount);
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
            /*
             * Only the start of a character, read it whole next time. With the bytes kept (3 at most) a read has at
             * most 3 bytes more than the units of the buffer, which convert to at most 3 units in Pending.
             */
            Want = Units > 4u - Entry->CarryLength ? Units : 4u - Entry->CarryLength;
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
        if (Length > (int)Units && Entry->ReadStart >= 0 && !(Length == 2 && IS_LOW_SURROGATE(Wide[1])))
        {
            /*
             * A file keeps no units but the low surrogate of a character returned by halves, so that its position
             * stays exact: it reads one byte less again, the last character complete or not, until the units fit.
             */
            RtlFreeHeap(NTCRT_HEAP, 0, Wide);
            _NtCRT_SeekHandle(Entry->Handle, Entry->ReadStart, SEEK_SET);
            Entry->Flags &= ~FEOFLAG;
            Want--;
            Whole = TRUE;
            continue;
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
        Entry->ReadCount = Total;
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

NTCRT_IMPORT(_read, _NtCRT_read);

/* POSIX names */
NTCRT_IMPORT(read, _NtCRT_read);
