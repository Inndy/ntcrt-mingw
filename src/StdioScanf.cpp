/*
 * The scanf family, see Stdio.cpp.
 */

#include "NtCRT.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* Narrow scanf */

EXTERN_C int __cdecl _NtCRT_vfscanf(FILE* stream, const char* format, va_list ap)
{
    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    return __mingw_vfscanf(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT_fscanf(FILE* stream, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfscanf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vscanf(const char* format, va_list ap)
{
    return _NtCRT_vfscanf(&_NtCRT_iob[0], format, ap);
}

EXTERN_C int __cdecl _NtCRT_scanf(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfscanf(&_NtCRT_iob[0], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vsscanf(const char* str, const char* format, va_list ap)
{
    if (str == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    return __mingw_vsscanf(str, format, ap);
}

EXTERN_C int __cdecl _NtCRT_sscanf(const char* str, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vsscanf(str, format, ap);
    va_end(ap);
    return Ret;
}

/* Wide scanf */

EXTERN_C int __cdecl _NtCRT_vfwscanf(FILE* stream, const wchar_t* format, va_list ap)
{
    int Ret;

    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    NTCRT_WIDE_FORMAT(format, TRUE);
    Ret = __mingw_vfwscanf(stream, _Format, ap);
    NTCRT_WIDE_FORMAT_FREE();
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_fwscanf(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwscanf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vwscanf(const wchar_t* format, va_list ap)
{
    return _NtCRT_vfwscanf(&_NtCRT_iob[0], format, ap);
}

EXTERN_C int __cdecl _NtCRT_wscanf(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwscanf(&_NtCRT_iob[0], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vswscanf(const wchar_t* str, const wchar_t* format, va_list ap)
{
    int Ret;

    if (str == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    NTCRT_WIDE_FORMAT(format, TRUE);
    Ret = __mingw_vswscanf(str, _Format, ap);
    NTCRT_WIDE_FORMAT_FREE();
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_swscanf(const wchar_t* str, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vswscanf(str, format, ap);
    va_end(ap);
    return Ret;
}

NTCRT_IMPORT_ONLY(vfscanf, _NtCRT_vfscanf);
NTCRT_IMPORT(fscanf, _NtCRT_fscanf);
NTCRT_IMPORT_ONLY(vscanf, _NtCRT_vscanf);
NTCRT_IMPORT(scanf, _NtCRT_scanf);
NTCRT_IMPORT_ONLY(vsscanf, _NtCRT_vsscanf);
NTCRT_IMPORT(sscanf, _NtCRT_sscanf);
NTCRT_IMPORT_ONLY(vfwscanf, _NtCRT_vfwscanf);
NTCRT_IMPORT(fwscanf, _NtCRT_fwscanf);
NTCRT_IMPORT_ONLY(vwscanf, _NtCRT_vwscanf);
NTCRT_IMPORT(wscanf, _NtCRT_wscanf);
NTCRT_IMPORT_ONLY(vswscanf, _NtCRT_vswscanf);
NTCRT_IMPORT(swscanf, _NtCRT_swscanf);
