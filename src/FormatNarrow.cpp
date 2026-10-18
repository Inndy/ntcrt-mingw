/*
 * The narrow printf of mingw-w64 wrapped by the specs (--wrap), formatted by Printf.cpp. libmingwex is not called:
 * its printf (pformat, gdtoa) is not linked.
 */

#include "NtCRT.h"

#include <stdarg.h>
#include <stdio.h>

NTCRT_API int __cdecl __wrap___mingw_vfprintf(FILE* stream, const char* format, va_list ap)
{
    return _NtCRT_FormatStream(stream, format, ap, 0);
}

NTCRT_API int __cdecl __wrap___mingw_vprintf(const char* format, va_list ap)
{
    return _NtCRT_FormatStream(&_NtCRT_iob[1], format, ap, 0);
}

NTCRT_API int __cdecl __wrap___mingw_vsprintf(char* buffer, const char* format, va_list ap)
{
    return _NtCRT_FormatString(buffer, SIZE_MAX, format, ap, 0);
}

NTCRT_API int __cdecl __wrap___mingw_vsnprintf(char* buffer, size_t count, const char* format, va_list ap)
{
    return _NtCRT_FormatString(buffer, count, format, ap, 0);
}

NTCRT_API int __cdecl __wrap___mingw_vasprintf(char** result, const char* format, va_list ap)
{
    return _NtCRT_FormatAlloc(result, format, ap, 0);
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

/*
 * The functions under their names too: lld takes them from libntcrt, which precedes libmingwex, so ntcrt.cfg does not
 * wrap them and the printf of libmingwex is not loaded. ld takes libmingwex first and wraps them (ntcrt.specs).
 */
EXTERN_C int __cdecl __mingw_vfprintf(FILE*, const char*, va_list) __attribute__((alias("__wrap___mingw_vfprintf")));
EXTERN_C int __cdecl __mingw_vprintf(const char*, va_list) __attribute__((alias("__wrap___mingw_vprintf")));
EXTERN_C int __cdecl __mingw_vsprintf(char*, const char*, va_list) __attribute__((alias("__wrap___mingw_vsprintf")));
EXTERN_C int __cdecl __mingw_vsnprintf(char*, size_t, const char*, va_list)
    __attribute__((alias("__wrap___mingw_vsnprintf")));
EXTERN_C int __cdecl __mingw_vasprintf(char**, const char*, va_list) __attribute__((alias("__wrap___mingw_vasprintf")));
EXTERN_C int __cdecl __mingw_fprintf(FILE*, const char*, ...) __attribute__((alias("__wrap___mingw_fprintf")));
EXTERN_C int __cdecl __mingw_printf(const char*, ...) __attribute__((alias("__wrap___mingw_printf")));
EXTERN_C int __cdecl __mingw_sprintf(char*, const char*, ...) __attribute__((alias("__wrap___mingw_sprintf")));
EXTERN_C int __cdecl __mingw_snprintf(char*, size_t, const char*, ...) __attribute__((alias("__wrap___mingw_snprintf")));
EXTERN_C int __cdecl __mingw_asprintf(char**, const char*, ...) __attribute__((alias("__wrap___mingw_asprintf")));
