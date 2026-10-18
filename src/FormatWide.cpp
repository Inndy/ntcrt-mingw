/*
 * The wide printf of mingw-w64 wrapped by the specs (--wrap), see Format.cpp. The conversions between narrow and
 * wide strings are done by NTCRT_FORMAT_ARGS before the format reaches mingw-w64.
 */

#include "NtCRT.h"

#include <stdarg.h>
#include <stdio.h>

extern "C"
{
    int __cdecl __real___mingw_vfwprintf(FILE*, const wchar_t*, va_list);
    int __cdecl __real___mingw_vwprintf(const wchar_t*, va_list);
    int __cdecl __real___mingw_vswprintf(wchar_t*, const wchar_t*, va_list);
    int __cdecl __real___mingw_vsnwprintf(wchar_t*, size_t, const wchar_t*, va_list);
}

NTCRT_API int __cdecl __wrap___mingw_vfwprintf(FILE* stream, const wchar_t* format, va_list ap)
{
    NTCRT_FORMAT_ARGS f(format, ap);

    return f.Failed ? -1 : __real___mingw_vfwprintf(stream, NTCRT_W(f), f.Args);
}

NTCRT_API int __cdecl __wrap___mingw_vwprintf(const wchar_t* format, va_list ap)
{
    NTCRT_FORMAT_ARGS f(format, ap);

    return f.Failed ? -1 : __real___mingw_vwprintf(NTCRT_W(f), f.Args);
}

NTCRT_API int __cdecl __wrap___mingw_vswprintf(wchar_t* buffer, const wchar_t* format, va_list ap)
{
    NTCRT_FORMAT_ARGS f(format, ap);

    if (f.Failed)
    {
        *buffer = L'\0';
        return -1;
    }
    return __real___mingw_vswprintf(buffer, NTCRT_W(f), f.Args);
}

NTCRT_API int __cdecl __wrap___mingw_vsnwprintf(wchar_t* buffer, size_t count, const wchar_t* format, va_list ap)
{
    NTCRT_FORMAT_ARGS f(format, ap);

    if (f.Failed)
    {
        if (count != 0)
        {
            *buffer = L'\0';
        }
        return -1;
    }
    return __real___mingw_vsnwprintf(buffer, count, NTCRT_W(f), f.Args);
}

NTCRT_WRAP_VARIADIC(__wrap___mingw_fwprintf,
                    __wrap___mingw_vfwprintf,
                    (FILE * stream, const wchar_t* format, ...),
                    format,
                    (stream, format, ap))
NTCRT_WRAP_VARIADIC(__wrap___mingw_wprintf, __wrap___mingw_vwprintf, (const wchar_t* format, ...), format, (format, ap))
NTCRT_WRAP_VARIADIC(__wrap___mingw_swprintf,
                    __wrap___mingw_vswprintf,
                    (wchar_t* buffer, const wchar_t* format, ...),
                    format,
                    (buffer, format, ap))
NTCRT_WRAP_VARIADIC(__wrap___mingw_snwprintf,
                    __wrap___mingw_vsnwprintf,
                    (wchar_t* buffer, size_t count, const wchar_t* format, ...),
                    format,
                    (buffer, count, format, ap))
