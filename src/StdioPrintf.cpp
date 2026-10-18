/*
 * The printf family, see Stdio.cpp.
 */

#include "NtCRT.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <stdio.h>
#include <wchar.h>

/* Narrow printf */

EXTERN_C int __cdecl _NtCRT_vfprintf(FILE* stream, const char* format, va_list ap)
{
    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return __mingw_vfprintf(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT_fprintf(FILE* stream, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfprintf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vprintf(const char* format, va_list ap)
{
    return _NtCRT_vfprintf(&_NtCRT_iob[1], format, ap);
}

EXTERN_C int __cdecl _NtCRT_printf(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfprintf(&_NtCRT_iob[1], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vsprintf(char* buffer, const char* format, va_list ap)
{
    if (buffer == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return __mingw_vsprintf(buffer, format, ap);
}

EXTERN_C int __cdecl _NtCRT_sprintf(char* buffer, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vsprintf(buffer, format, ap);
    va_end(ap);
    return Ret;
}

/* msvcrt semantics: no terminator and -1 returned on truncation */
EXTERN_C int __cdecl _NtCRT__vsnprintf(char* buffer, size_t count, const char* format, va_list ap)
{
    va_list Copy;
    int Length;

    if (format == NULL || (buffer == NULL && count != 0))
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    va_copy(Copy, ap);
    Length = __mingw_vsnprintf(buffer, count, format, ap);
    if (Length >= 0 && (size_t)Length >= count && count != 0)
    {
        /* C99 truncation dropped the last character for the terminator, format again in full */
        char* Full = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, (size_t)Length + 1);
        if (Full != NULL)
        {
            __mingw_vsnprintf(Full, (size_t)Length + 1, format, Copy);
            memcpy(buffer, Full, count);
            RtlFreeHeap(NTCRT_HEAP, 0, Full);
        }
        if ((size_t)Length > count)
        {
            Length = -1;
        }
    } else if (Length >= 0 && count == 0 && buffer != NULL)
    {
        Length = Length == 0 ? 0 : -1;
    }
    va_end(Copy);
    return Length;
}

EXTERN_C int __cdecl _NtCRT__snprintf(char* buffer, size_t count, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vsnprintf(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

/* C99 semantics */
EXTERN_C int __cdecl _NtCRT_vsnprintf(char* buffer, size_t count, const char* format, va_list ap)
{
    return __mingw_vsnprintf(buffer, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT_snprintf(char* buffer, size_t count, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = __mingw_vsnprintf(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vscprintf(const char* format, va_list ap)
{
    if (format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return __mingw_vsnprintf(NULL, 0, format, ap);
}

EXTERN_C int __cdecl _NtCRT__scprintf(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vscprintf(format, ap);
    va_end(ap);
    return Ret;
}

NTCRT_IMPORT(vfprintf, _NtCRT_vfprintf);
NTCRT_IMPORT(fprintf, _NtCRT_fprintf);
NTCRT_IMPORT(vprintf, _NtCRT_vprintf);
NTCRT_IMPORT(printf, _NtCRT_printf);
NTCRT_IMPORT(vsprintf, _NtCRT_vsprintf);
NTCRT_IMPORT(sprintf, _NtCRT_sprintf);
NTCRT_IMPORT(_vsnprintf, _NtCRT__vsnprintf);
NTCRT_IMPORT(_snprintf, _NtCRT__snprintf);
NTCRT_IMPORT(_vscprintf, _NtCRT__vscprintf);
NTCRT_IMPORT(_scprintf, _NtCRT__scprintf);
