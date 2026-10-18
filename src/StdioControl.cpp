/* stdio status and control: feof, ferror, clearerr, fileno, fflush, _flushall, setvbuf, _putw */

#include "NtCRT.h"
#include "Stdio.inl"

#include <errno.h>

/* Status */

EXTERN_C int __cdecl _NtCRT_feof(FILE* stream)
{
    NTCRT_CHECK_STREAM(stream, 0);
    return stream->_flag & _IOEOF;
}

EXTERN_C int __cdecl _NtCRT_ferror(FILE* stream)
{
    NTCRT_CHECK_STREAM(stream, 0);
    return stream->_flag & _IOERR;
}

EXTERN_C void __cdecl _NtCRT_clearerr(FILE* stream)
{
    if (stream == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return;
    }
    _NtCRT_lock_file(stream);
    stream->_flag &= ~(_IOERR | _IOEOF);
    _NtCRT_unlock_file(stream);
}

EXTERN_C int __cdecl _NtCRT_fileno(FILE* stream)
{
    NTCRT_CHECK_STREAM(stream, -1);
    return stream->_file;
}

EXTERN_C int __cdecl _NtCRT_fflush(FILE* stream)
{
    int Ret;

    if (stream == NULL)
    {
        return _NtCRT_flushall_impl(TRUE);
    }
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_flush_nolock(stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__fflush_nolock(FILE* stream)
{
    if (stream == NULL)
    {
        return _NtCRT_flushall_impl(TRUE);
    }
    return _NtCRT_flush_nolock(stream);
}

EXTERN_C int __cdecl _NtCRT_setvbuf(FILE* stream, char* buffer, int mode, size_t size)
{
    if (stream == NULL || (mode != _IOFBF && mode != _IOLBF && mode != _IONBF) ||
        (mode != _IONBF && (size < 2 || size > INT_MAX)))
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_lock_file(stream);
    _NtCRT_flush_nolock(stream);
    _NtCRT_freebuf(stream);
    stream->_flag &= ~(_IONBF | NTCRT_IOFLUSHUNLOCK);
    if (stream->_file == 2 || _NtCRT_isatty(stream->_file))
    {
        stream->_flag |= NTCRT_IOFLUSHUNLOCK;
    }
    if (mode == _IONBF)
    {
        stream->_flag |= _IONBF;
        stream->_base = (char*)&stream->_charbuf;
        stream->_bufsiz = 2;
    } else
    {
        /* Line buffering is full buffering, as msvcrt does */
        size &= ~(size_t)1;
        if (buffer == NULL)
        {
            buffer = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, size);
            if (buffer == NULL)
            {
                _NtCRT_unlock_file(stream);
                return -1;
            }
            stream->_flag |= _IOMYBUF | NTCRT_IOSETVBUF;
        } else
        {
            stream->_flag |= NTCRT_IOYOURBUF | NTCRT_IOSETVBUF;
        }
        stream->_base = buffer;
        stream->_bufsiz = (int)size;
    }
    stream->_ptr = stream->_base;
    stream->_cnt = 0;
    _NtCRT_unlock_file(stream);
    return 0;
}

EXTERN_C void __cdecl _NtCRT_setbuf(FILE* stream, char* buffer)
{
    _NtCRT_setvbuf(stream, buffer, buffer != NULL ? _IOFBF : _IONBF, BUFSIZ);
}

EXTERN_C int __cdecl _NtCRT__flushall(void)
{
    return _NtCRT_flushall_impl(FALSE);
}

EXTERN_C int __cdecl _NtCRT__getmaxstdio(void)
{
    return NTCRT_NSTREAM;
}

EXTERN_C int __cdecl _NtCRT__setmaxstdio(int newmax)
{
    if (newmax < NTCRT_IOB_ENTRIES || newmax > NTCRT_NSTREAM)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return newmax;
}

EXTERN_C int __cdecl _NtCRT__putw(int word, FILE* stream)
{
    const unsigned char* p = (const unsigned char*)&word;

    NTCRT_CHECK_STREAM(stream, EOF);
    _NtCRT_lock_file(stream);
    for (size_t i = 0; i < sizeof(word); i++)
    {
        _NtCRT_putc_nolock(p[i], stream);
    }
    if (stream->_flag & _IOERR)
    {
        word = EOF;
    }
    _NtCRT_unlock_file(stream);
    return word;
}

EXTERN_C errno_t __cdecl _NtCRT_clearerr_s(FILE* stream)
{
    if (stream == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    _NtCRT_clearerr(stream);
    return 0;
}

NTCRT_IMPORT(feof, _NtCRT_feof);
NTCRT_IMPORT(ferror, _NtCRT_ferror);
NTCRT_IMPORT(clearerr, _NtCRT_clearerr);
NTCRT_IMPORT(_fileno, _NtCRT_fileno);
NTCRT_IMPORT(fileno, _NtCRT_fileno);
NTCRT_IMPORT(fflush, _NtCRT_fflush);
NTCRT_IMPORT(_fflush_nolock, _NtCRT__fflush_nolock);
NTCRT_IMPORT(setvbuf, _NtCRT_setvbuf);
NTCRT_IMPORT(setbuf, _NtCRT_setbuf);
NTCRT_IMPORT(_flushall, _NtCRT__flushall);
NTCRT_IMPORT(_getmaxstdio, _NtCRT__getmaxstdio);
NTCRT_IMPORT(_setmaxstdio, _NtCRT__setmaxstdio);
NTCRT_IMPORT(_putw, _NtCRT__putw);
NTCRT_IMPORT(putw, _NtCRT__putw);
NTCRT_IMPORT(clearerr_s, _NtCRT_clearerr_s);
