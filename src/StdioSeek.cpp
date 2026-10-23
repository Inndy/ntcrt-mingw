/* stdio positioning: ftell, fseek, rewind, fgetpos and fsetpos */

#include "NtCRT.h"
#include "LowIO.inl"
#include "Stdio.inl"

#include <errno.h>

/* Bytes of a UTF-16 unit written to a Unicode text mode file, a newline as CRLF */
static
int
_NtCRT_UnitBytes(
    _In_ wchar_t Unit,
    _In_ BOOL Utf8)
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

/*
 * The position of the unit Index of the Count bytes that the last read of a Unicode text mode file returned. The
 * bytes of a unit in the file are not known (a newline is LF or CRLF, U+FFFD replaced 1 to 3 bytes): the units
 * before it are read again from the position of the first, then the fd is restored. -1 if the read is not known.
 * The units up to the position found last are not read again, so that ftell after each character stays linear.
 */
static
long long
_NtCRT_UnitPosition(
    _In_ int fd,
    _In_ unsigned int Index,
    _In_ int Count)
{
    PNTCRT_FD Entry = _NtCRT_LockFd(fd);
    long long Result = -1, End, Start;
    unsigned int Left;
    BOOL Whole = FALSE;
    NTCRT_FD Saved;
    WCHAR Buffer[256];

    if (Entry == NULL)
    {
        return -1;
    }
    if (Entry->ReadStart < 0 || Entry->ReadCount != Count)
    {
        NTCRT_ERRNO = EINVAL;
    } else if ((End = _NtCRT_SeekHandle(Entry->Handle, 0, SEEK_CUR)) >= 0)
    {
        Left = Entry->UnitIndex != 0 && Entry->UnitIndex <= Index ? Index - Entry->UnitIndex : Index;
        Start = Left != Index ? Entry->UnitStart : Entry->ReadStart;
        memcpy(&Saved, Entry, FIELD_OFFSET(NTCRT_FD, Lock));
        Result = _NtCRT_SeekHandle(Entry->Handle, Start, SEEK_SET);
        Entry->Flags &= ~FEOFLAG;
        Entry->PendingCount = 0;
        while (Result >= 0 && Left != 0)
        {
            int Read = _NtCRT_read(fd, Buffer, (Left < ARRAYSIZE(Buffer) ? Left : ARRAYSIZE(Buffer)) * sizeof(WCHAR));

            Result = Read > 0 ? Result : -1;
            Left -= Read > 0 ? Read / sizeof(WCHAR) : 0;
        }
        if (Result >= 0)
        {
            Result = _NtCRT_lseeki64(fd, 0, SEEK_CUR);
            /* Not between the surrogates of a character, where a read cannot start */
            Whole = Entry->PendingCount == 0;
        }
        _NtCRT_SeekHandle(Entry->Handle, End, SEEK_SET);
        memcpy(Entry, &Saved, FIELD_OFFSET(NTCRT_FD, Lock));
        if (Whole)
        {
            Entry->UnitIndex = Index;
            Entry->UnitStart = Result;
        }
    }
    _NtCRT_UnlockFd(Entry);
    return Result;
}

static
long long
_NtCRT_ftell_nolock(
    _In_ FILE* Stream)
{
    long long Position;

    if (Stream->_cnt < 0)
    {
        Stream->_cnt = 0;
    }
    Position = _NtCRT_lseeki64(Stream->_file, 0, SEEK_CUR);
    if (Position < 0)
    {
        return -1;
    }
    if (Stream->_base == NULL || ((Stream->_flag & _IONBF) && !(Stream->_flag & _IOREAD)))
    {
        return Position;
    }
    if ((Stream->_flag & _IOWRT) && _NtCRT_fd_textmode(Stream->_file) >= NTCRT_FD_UTF8)
    {
        /* The buffer holds UTF-16 units, written as UTF-8 or UTF-16 with LF as CRLF */
        BOOL Utf8 = _NtCRT_fd_textmode(Stream->_file) == NTCRT_FD_UTF8;

        for (const char* p = Stream->_base; p + sizeof(wchar_t) <= Stream->_ptr; p += sizeof(wchar_t))
        {
            wchar_t c;

            memcpy(&c, p, sizeof(wchar_t));
            Position += _NtCRT_UnitBytes(c, Utf8);
        }
    } else if (Stream->_flag & _IOWRT)
    {
        Position += Stream->_ptr - Stream->_base;
        if (_NtCRT_fd_textmode(Stream->_file))
        {
            for (char* p = Stream->_base; p < Stream->_ptr; p++)
            {
                if (*p == '\n')
                {
                    Position++;
                }
            }
        }
    } else if ((Stream->_flag & _IOREAD) && _NtCRT_fd_textmode(Stream->_file) >= NTCRT_FD_UTF8)
    {
        if (Stream->_cnt > 0)
        {
            Position = _NtCRT_UnitPosition(Stream->_file,
                                           (unsigned int)(Stream->_ptr - Stream->_base) / sizeof(wchar_t),
                                           (int)(Stream->_ptr - Stream->_base) + Stream->_cnt);
        }
    } else if (Stream->_flag & _IOREAD)
    {
        /*
         * Unread data, a text mode newline is assumed to be CRLF in the file. The CR at the end of a fill that the
         * read left out is behind the unread characters too (msvcrt: FCRLF).
         */
        Position -= Stream->_cnt;
        if (_NtCRT_fd_textmode(Stream->_file))
        {
            if (Stream->_cnt > 0 && _NtCRT_fd_crbehind(Stream->_file))
            {
                Position--;
            }
            for (char* p = Stream->_ptr; p < Stream->_ptr + Stream->_cnt; p++)
            {
                if (*p == '\n')
                {
                    Position--;
                }
            }
        }
        if (Position < 0)
        {
            Position = 0;
        }
    }
    return Position;
}

EXTERN_C long long __cdecl _NtCRT_ftelli64(FILE* stream)
{
    long long Ret;

    NTCRT_CHECK_STREAM(stream, -1);
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_ftell_nolock(stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C long __cdecl _NtCRT_ftell(FILE* stream)
{
    long long Ret = _NtCRT_ftelli64(stream);

    if (Ret > LONG_MAX)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return (long)Ret;
}

EXTERN_C int __cdecl _NtCRT_fseeki64(FILE* stream, long long offset, int origin)
{
    int Ret = 0;

    NTCRT_CHECK_STREAM(stream, -1);
    if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_lock_file(stream);
    stream->_flag &= ~_IOEOF;
    memset(_NtCRT_StreamMb(stream), 0, sizeof(NTCRT_STREAM_MB));
    if (origin == SEEK_CUR)
    {
        long long Current = _NtCRT_ftell_nolock(stream);
        if (Current < 0)
        {
            Ret = -1;
            goto _Exit;
        }
        offset += Current;
        origin = SEEK_SET;
    }
    if (_NtCRT_flush_nolock(stream) != 0)
    {
        /* A write error fails the seek */
        Ret = -1;
        goto _Exit;
    }
    if (stream->_flag & _IORW)
    {
        stream->_flag &= ~(_IOWRT | _IOREAD);
    } else if ((stream->_flag & _IOREAD) && (stream->_flag & _IOMYBUF) && !(stream->_flag & NTCRT_IOSETVBUF))
    {
        /* msvcrt shrinks the buffer of a read stream after seeking */
        stream->_bufsiz = NTCRT_SMALL_BUFSIZ;
    }
    if (_NtCRT_lseeki64(stream->_file, offset, origin) < 0)
    {
        Ret = -1;
    }

_Exit:
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_fseek(FILE* stream, long offset, int origin)
{
    return _NtCRT_fseeki64(stream, offset, origin);
}

EXTERN_C void __cdecl _NtCRT_rewind(FILE* stream)
{
    if (stream == NULL)
    {
        return;
    }
    _NtCRT_lock_file(stream);
    _NtCRT_fseeki64(stream, 0, SEEK_SET);
    stream->_flag &= ~(_IOERR | _IOEOF);
    _NtCRT_unlock_file(stream);
}

EXTERN_C int __cdecl _NtCRT_fgetpos(FILE* stream, fpos_t* pos)
{
    long long Position;

    if (stream == NULL || pos == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Position = _NtCRT_ftelli64(stream);
    if (Position < 0)
    {
        return -1;
    }
    *pos = Position;
    return 0;
}

EXTERN_C int __cdecl _NtCRT_fsetpos(FILE* stream, const fpos_t* pos)
{
    if (stream == NULL || pos == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return _NtCRT_fseeki64(stream, *pos, SEEK_SET);
}

NTCRT_IMPORT(_ftelli64, _NtCRT_ftelli64);
NTCRT_IMPORT(ftell, _NtCRT_ftell);
NTCRT_IMPORT(_fseeki64, _NtCRT_fseeki64);
/* Of libmsvcrt.a in MinGW-w64 13 and later (libstdc++ of x86 imports them), of libmingwex.a before */
NTCRT_IMPORT_ONLY(fseeko64, _NtCRT_fseeki64);
NTCRT_IMPORT_ONLY(ftello64, _NtCRT_ftelli64);
NTCRT_IMPORT(fseek, _NtCRT_fseek);
NTCRT_IMPORT(rewind, _NtCRT_rewind);
NTCRT_IMPORT(fgetpos, _NtCRT_fgetpos);
NTCRT_IMPORT(fsetpos, _NtCRT_fsetpos);
