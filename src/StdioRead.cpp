/* stdio input: the buffer filled by _read, characters, strings and blocks of narrow streams */

#include "NtCRT.h"
#include "Stdio.inl"

#include <errno.h>

EXTERN_C int __cdecl _NtCRT_filbuf(FILE* stream)
{
    int Count;

    if (!(stream->_flag & (_IOREAD | _IORW)) || (stream->_flag & _IOSTRG))
    {
        return EOF;
    }
    if (stream->_flag & _IOWRT)
    {
        stream->_flag |= _IOERR;
        return EOF;
    }
    stream->_flag |= _IOREAD;
    if (stream->_base == NULL)
    {
        _NtCRT_getbuf(stream);
    }
    Count = _NtCRT_read(stream->_file, stream->_base, stream->_bufsiz);
    stream->_ptr = stream->_base;
    if (Count <= 0)
    {
        stream->_flag |= Count == 0 ? _IOEOF : _IOERR;
        stream->_cnt = 0;
        return EOF;
    }
    stream->_cnt = Count - 1;
    return (unsigned char)*stream->_ptr++;
}

EXTERN_C
int
_NtCRT_ungetc_nolock(
    _In_ int Ch,
    _In_ FILE* Stream)
{
    if (Ch == EOF)
    {
        return EOF;
    }
    if (!((Stream->_flag & _IOREAD) || ((Stream->_flag & _IORW) && !(Stream->_flag & _IOWRT))))
    {
        return EOF;
    }
    if (Stream->_base == NULL)
    {
        _NtCRT_getbuf(Stream);
    }
    if (Stream->_ptr == Stream->_base)
    {
        /*
         * A character pushed back at the start of the buffer, by scanf at the first of a fill: the unread characters
         * move up when the buffer has room. Not those of a string (sscanf), nor of a Unicode text mode, whose
         * position takes the units of the buffer as those read (_NtCRT_UnitPosition)
         */
        if (Stream->_cnt > 0)
        {
            if ((Stream->_flag & _IOSTRG) || Stream->_cnt >= Stream->_bufsiz ||
                _NtCRT_fd_textmode(Stream->_file) >= NTCRT_FD_UTF8)
            {
                return EOF;
            }
            memmove(Stream->_base + 1, Stream->_base, Stream->_cnt);
        }
        Stream->_ptr++;
    }
    if (Stream->_flag & _IOSTRG)
    {
        if (*--Stream->_ptr != (char)Ch)
        {
            Stream->_ptr++;
            return EOF;
        }
    } else
    {
        *--Stream->_ptr = (char)Ch;
    }
    if (Stream->_cnt < 0)
    {
        Stream->_cnt = 0;
    }
    Stream->_cnt++;
    Stream->_flag &= ~_IOEOF;
    Stream->_flag |= _IOREAD;
    return (unsigned char)Ch;
}

EXTERN_C int __cdecl _NtCRT_fgetc(FILE* stream)
{
    int Ch;

    NTCRT_CHECK_STREAM(stream, EOF);
    NTCRT_CHECK_STREAM_ANSI(stream, EOF);
    _NtCRT_lock_file(stream);
    Ch = _NtCRT_getc_nolock(stream);
    _NtCRT_unlock_file(stream);
    return Ch;
}

EXTERN_C int __cdecl _NtCRT_ungetc(int ch, FILE* stream)
{
    int Ret;

    NTCRT_CHECK_STREAM(stream, EOF);
    NTCRT_CHECK_STREAM_ANSI(stream, EOF);
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_ungetc_nolock(ch, stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_getchar(void)
{
    return _NtCRT_fgetc(&_NtCRT_iob[0]);
}

EXTERN_C int __cdecl _NtCRT__fgetc_nolock(FILE* stream)
{
    return _NtCRT_getc_nolock(stream);
}

EXTERN_C int __cdecl _NtCRT__ungetc_nolock(int ch, FILE* stream)
{
    return _NtCRT_ungetc_nolock(ch, stream);
}

/* String I/O */

EXTERN_C char* __cdecl _NtCRT_fgets(char* str, int n, FILE* stream)
{
    char* p = str;

    if (str == NULL || n <= 0 || stream == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    NTCRT_CHECK_STREAM_ANSI(stream, NULL);
    _NtCRT_lock_file(stream);
    while (--n > 0)
    {
        int Ch = _NtCRT_getc_nolock(stream);
        if (Ch == EOF)
        {
            if (p == str)
            {
                str = NULL;
            }
            break;
        }
        *p++ = (char)Ch;
        if (Ch == '\n')
        {
            break;
        }
    }
    if (str != NULL)
    {
        *p = '\0';
    }
    _NtCRT_unlock_file(stream);
    return str;
}

EXTERN_C char* __cdecl _NtCRT_gets(char* str)
{
    FILE* Stream = &_NtCRT_iob[0];
    char* p = str;
    int Ch;

    if (str == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    NTCRT_CHECK_STREAM_ANSI(Stream, NULL);
    _NtCRT_lock_file(Stream);
    while ((Ch = _NtCRT_getc_nolock(Stream)) != '\n')
    {
        if (Ch == EOF)
        {
            if (p == str)
            {
                str = NULL;
            }
            break;
        }
        *p++ = (char)Ch;
    }
    if (str != NULL)
    {
        *p = '\0';
    }
    _NtCRT_unlock_file(Stream);
    return str;
}

/* Block I/O */

EXTERN_C size_t __cdecl _NtCRT_fread(void* buffer, size_t size, size_t count, FILE* stream)
{
    char* Dst = (char*)buffer;
    size_t Total, Remaining, BufferSize;

    if (size == 0 || count == 0)
    {
        return 0;
    }
    if (buffer == NULL || stream == NULL || count > SIZE_MAX / size)
    {
        NTCRT_ERRNO = EINVAL;
        return 0;
    }
    Total = Remaining = size * count;
    _NtCRT_lock_file(stream);

    /* As msvcrt, whole buffers are read directly and the rest through the buffer */
    BufferSize = stream->_base != NULL ? (size_t)stream->_bufsiz : NTCRT_BUFSIZ;
    while (Remaining != 0)
    {
        if (stream->_base != NULL && stream->_cnt > 0)
        {
            size_t Chunk = (size_t)stream->_cnt < Remaining ? (size_t)stream->_cnt : Remaining;
            memcpy(Dst, stream->_ptr, Chunk);
            stream->_ptr += Chunk;
            stream->_cnt -= (int)Chunk;
            Dst += Chunk;
            Remaining -= Chunk;
        } else if (Remaining >= BufferSize)
        {
            size_t Chunk = BufferSize != 0 ? Remaining - Remaining % BufferSize : Remaining;
            int Read;

            if (!(stream->_flag & (_IOREAD | _IORW)) || (stream->_flag & _IOWRT))
            {
                stream->_flag |= _IOERR;
                break;
            }
            stream->_flag |= _IOREAD;
            if (Chunk > INT_MAX)
            {
                Chunk = BufferSize != 0 && BufferSize <= INT_MAX ? INT_MAX - INT_MAX % BufferSize : INT_MAX;
            }
            Read = _NtCRT_read(stream->_file, Dst, (unsigned int)Chunk);
            if (Read <= 0)
            {
                stream->_flag |= Read == 0 ? _IOEOF : _IOERR;
                break;
            }
            Dst += Read;
            Remaining -= Read;
        } else
        {
            int Ch = _NtCRT_filbuf(stream);
            if (Ch == EOF)
            {
                break;
            }
            *Dst++ = (char)Ch;
            Remaining--;
            BufferSize = (size_t)stream->_bufsiz;
        }
    }
    _NtCRT_unlock_file(stream);
    return (Total - Remaining) / size;
}

/* Characters of stdin and stdout */
EXTERN_C int __cdecl _NtCRT__fgetchar(void)
{
    return _NtCRT_getchar();
}

/* The bytes of an int */
EXTERN_C int __cdecl _NtCRT__getw(FILE* stream)
{
    int Word;
    unsigned char* p = (unsigned char*)&Word;

    NTCRT_CHECK_STREAM(stream, EOF);
    _NtCRT_lock_file(stream);
    for (size_t i = 0; i < sizeof(Word); i++)
    {
        p[i] = (unsigned char)_NtCRT__fgetc_nolock(stream);
    }
    if (stream->_flag & (_IOEOF | _IOERR))
    {
        Word = EOF;
    }
    _NtCRT_unlock_file(stream);
    return Word;
}

NTCRT_IMPORT(_filbuf, _NtCRT_filbuf);
NTCRT_IMPORT(fgetc, _NtCRT_fgetc);
NTCRT_IMPORT(getc, _NtCRT_fgetc);
NTCRT_IMPORT(ungetc, _NtCRT_ungetc);
NTCRT_IMPORT(getchar, _NtCRT_getchar);
NTCRT_IMPORT(_fgetc_nolock, _NtCRT__fgetc_nolock);
NTCRT_IMPORT(_ungetc_nolock, _NtCRT__ungetc_nolock);
NTCRT_IMPORT(fgets, _NtCRT_fgets);
NTCRT_IMPORT(gets, _NtCRT_gets);
NTCRT_IMPORT(fread, _NtCRT_fread);
NTCRT_IMPORT(_fgetchar, _NtCRT__fgetchar);
NTCRT_IMPORT(fgetchar, _NtCRT__fgetchar);
NTCRT_IMPORT(_getw, _NtCRT__getw);
NTCRT_IMPORT(getw, _NtCRT__getw);
