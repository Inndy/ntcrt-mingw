/*
 * The wide printf family, see Stdio.cpp.
 */

#include "NtCRT.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <stdio.h>
#include <wchar.h>

/* Wide printf */

EXTERN_C int __cdecl _NtCRT_vfwprintf(FILE* stream, const wchar_t* format, va_list ap)
{
    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return _NtCRT_FormatStreamW(stream, format, ap, NTCRT_PRINTF_LEGACY_WIDE);
}

EXTERN_C int __cdecl _NtCRT_fwprintf(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwprintf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vwprintf(const wchar_t* format, va_list ap)
{
    return _NtCRT_vfwprintf(&_NtCRT_iob[1], format, ap);
}

EXTERN_C int __cdecl _NtCRT_wprintf(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwprintf(&_NtCRT_iob[1], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsnwprintf(wchar_t* buffer, size_t count, const wchar_t* format, va_list ap)
{
    if (format == NULL || (buffer == NULL && count != 0))
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return _NtCRT_FormatLegacyW(buffer, count, format, ap, NTCRT_PRINTF_LEGACY_WIDE);
}

EXTERN_C int __cdecl _NtCRT__snwprintf(wchar_t* buffer, size_t count, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vsnwprintf(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

/* Legacy swprintf of msvcrt without a buffer size */
EXTERN_C int __cdecl _NtCRT_vswprintf(wchar_t* buffer, const wchar_t* format, va_list ap)
{
    return _NtCRT__vsnwprintf(buffer, INT_MAX / sizeof(wchar_t), format, ap);
}

EXTERN_C int __cdecl _NtCRT_swprintf(wchar_t* buffer, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vswprintf(buffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vscwprintf(const wchar_t* format, va_list ap)
{
    if (format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return _NtCRT_FormatStringW(NULL, 0, format, ap, NTCRT_PRINTF_LEGACY_WIDE);
}

EXTERN_C int __cdecl _NtCRT__scwprintf(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vscwprintf(format, ap);
    va_end(ap);
    return Ret;
}

/* The wide printf of msvcrt under its name in libmsvcrt of mingw-w64 */
EXTERN_C int __cdecl _NtCRT___ms_vfwprintf(FILE* stream, const wchar_t* format, va_list ap)
{
    return _NtCRT_vfwprintf(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT___ms_fwprintf(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT___ms_vfwprintf(stream, format, ap);
    va_end(ap);
    return Ret;
}

/*
 * The internal wide output of msvcrt, UTF-16 units are put into the stream buffer regardless of the text mode,
 * and it fails on a stream without a buffer
 */
EXTERN_C int __cdecl _NtCRT__woutput_s(FILE* stream, const wchar_t* format, va_list ap)
{
    wchar_t Stack[256], *Buffer = Stack;
    int Length;

    if (stream == NULL || format == NULL)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return -1;
    }
    /* Formatted once, a %n of the format may change the arguments */
    Length = _NtCRT_FormatGrowW(&Buffer, ARRAYSIZE(Stack), format, ap, NTCRT_PRINTF_LEGACY_WIDE);
    for (int i = 0; i < Length; i++)
    {
        if ((stream->_cnt -= sizeof(wchar_t)) >= 0)
        {
            memcpy(stream->_ptr, &Buffer[i], sizeof(wchar_t));
            stream->_ptr += sizeof(wchar_t);
        } else if (stream->_base == NULL || _NtCRT_FlushPut((const char*)&Buffer[i], sizeof(wchar_t), stream) != 0)
        {
            stream->_flag |= _IOERR;
            Length = -1;
            break;
        }
    }
    if (Buffer != Stack)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Buffer);
    }
    return Length;
}

NTCRT_IMPORT(vfwprintf, _NtCRT_vfwprintf);
NTCRT_IMPORT(fwprintf, _NtCRT_fwprintf);
NTCRT_IMPORT(__ms_fwprintf, _NtCRT___ms_fwprintf);
NTCRT_IMPORT(_woutput_s, _NtCRT__woutput_s);
NTCRT_IMPORT(__ms_vfwprintf, _NtCRT___ms_vfwprintf);
NTCRT_IMPORT(vwprintf, _NtCRT_vwprintf);
NTCRT_IMPORT(wprintf, _NtCRT_wprintf);
NTCRT_IMPORT(_vsnwprintf, _NtCRT__vsnwprintf);
NTCRT_IMPORT(_snwprintf, _NtCRT__snwprintf);
NTCRT_IMPORT(snwprintf, _NtCRT__snwprintf);
NTCRT_IMPORT(vsnwprintf, _NtCRT__vsnwprintf);
NTCRT_IMPORT(vswprintf, _NtCRT_vswprintf);
NTCRT_IMPORT(swprintf, _NtCRT_swprintf);
NTCRT_IMPORT(_vswprintf, _NtCRT_vswprintf);
NTCRT_IMPORT(_swprintf, _NtCRT_swprintf);
NTCRT_IMPORT(_vscwprintf, _NtCRT__vscwprintf);
NTCRT_IMPORT(_scwprintf, _NtCRT__scwprintf);
