/* stdio positioning: ftell, fseek, rewind, fgetpos and fsetpos */

#include "NtCRT.h"
#include "Stdio.inl"

#include <errno.h>

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
    if (Stream->_flag & _IOWRT)
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
        /* The bytes of the unread units in the file, a newline was CRLF */
        BOOL Utf8 = _NtCRT_fd_textmode(Stream->_file) == NTCRT_FD_UTF8;
        const char* p = Stream->_ptr;

        Position -= _NtCRT_fd_pending_bytes(Stream->_file);
        for (int i = 0; i + (int)sizeof(wchar_t) <= Stream->_cnt; i += sizeof(wchar_t))
        {
            wchar_t c;

            memcpy(&c, p + i, sizeof(wchar_t));
            Position -= _NtCRT_UnitBytes(c, Utf8);
        }
        if (Position < 0)
        {
            Position = 0;
        }
    } else if (Stream->_flag & _IOREAD)
    {
        /* Unread data, a text mode newline is assumed to be CRLF in the file */
        Position -= Stream->_cnt;
        if (_NtCRT_fd_textmode(Stream->_file))
        {
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
    _NtCRT_flush_nolock(stream);
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
NTCRT_IMPORT(fseek, _NtCRT_fseek);
NTCRT_IMPORT(rewind, _NtCRT_rewind);
NTCRT_IMPORT(fgetpos, _NtCRT_fgetpos);
NTCRT_IMPORT(fsetpos, _NtCRT_fsetpos);
