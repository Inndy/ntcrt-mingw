/*
 * The narrow printf of mingw-w64 wrapped by the specs (--wrap), see Format.cpp. The conversions between narrow and
 * wide strings are done by NTCRT_FORMAT_ARGS before the format reaches mingw-w64.
 */

#include "NtCRT.h"

#include <stdarg.h>
#include <stdio.h>

extern "C"
{
    int __cdecl __real___mingw_vfprintf(FILE*, const char*, va_list);
    int __cdecl __real___mingw_vprintf(const char*, va_list);
    int __cdecl __real___mingw_vsprintf(char*, const char*, va_list);
    int __cdecl __real___mingw_vsnprintf(char*, size_t, const char*, va_list);
    int __cdecl __real___mingw_vasprintf(char**, const char*, va_list);
}

NTCRT_API int __cdecl __wrap___mingw_vfprintf(FILE* stream, const char* format, va_list ap)
{
    NTCRT_FORMAT_ARGS f(format, ap);

    return f.Failed ? -1 : __real___mingw_vfprintf(stream, NTCRT_A(f), f.Args);
}

NTCRT_API int __cdecl __wrap___mingw_vprintf(const char* format, va_list ap)
{
    NTCRT_FORMAT_ARGS f(format, ap);

    return f.Failed ? -1 : __real___mingw_vprintf(NTCRT_A(f), f.Args);
}

NTCRT_API int __cdecl __wrap___mingw_vsprintf(char* buffer, const char* format, va_list ap)
{
    NTCRT_FORMAT_ARGS f(format, ap);

    if (f.Failed)
    {
        *buffer = '\0';
        return -1;
    }
    return __real___mingw_vsprintf(buffer, NTCRT_A(f), f.Args);
}

NTCRT_API int __cdecl __wrap___mingw_vsnprintf(char* buffer, size_t count, const char* format, va_list ap)
{
    NTCRT_FORMAT_ARGS f(format, ap);

    if (f.Failed)
    {
        if (count != 0)
        {
            *buffer = '\0';
        }
        return -1;
    }
    return __real___mingw_vsnprintf(buffer, count, NTCRT_A(f), f.Args);
}

NTCRT_API int __cdecl __wrap___mingw_vasprintf(char** result, const char* format, va_list ap)
{
    NTCRT_FORMAT_ARGS f(format, ap);

    if (f.Failed)
    {
        *result = NULL;
        return -1;
    }
    return __real___mingw_vasprintf(result, NTCRT_A(f), f.Args);
}

NTCRT_WRAP_VARIADIC(__wrap___mingw_fprintf,
                    __wrap___mingw_vfprintf,
                    (FILE * stream, const char* format, ...),
                    format,
                    (stream, format, ap))
NTCRT_WRAP_VARIADIC(__wrap___mingw_printf, __wrap___mingw_vprintf, (const char* format, ...), format, (format, ap))
NTCRT_WRAP_VARIADIC(__wrap___mingw_sprintf,
                    __wrap___mingw_vsprintf,
                    (char* buffer, const char* format, ...),
                    format,
                    (buffer, format, ap))
NTCRT_WRAP_VARIADIC(__wrap___mingw_snprintf,
                    __wrap___mingw_vsnprintf,
                    (char* buffer, size_t count, const char* format, ...),
                    format,
                    (buffer, count, format, ap))
NTCRT_WRAP_VARIADIC(__wrap___mingw_asprintf,
                    __wrap___mingw_vasprintf,
                    (char** result, const char* format, ...),
                    format,
                    (result, format, ap))
