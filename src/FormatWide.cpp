/*
 * The wide printf of mingw-w64 wrapped by the specs (--wrap), formatted by Printf.cpp in the conventions of C: %s
 * and %c are narrow, %S and %C wide.
 */

#include "NtCRT.h"

#include <stdarg.h>
#include <stdio.h>

NTCRT_API int __cdecl __wrap___mingw_vfwprintf(FILE* stream, const wchar_t* format, va_list ap)
{
    return _NtCRT_FormatStreamW(stream, format, ap, NTCRT_PRINTF_LONG_DOUBLE);
}

NTCRT_API int __cdecl __wrap___mingw_vwprintf(const wchar_t* format, va_list ap)
{
    return _NtCRT_FormatStreamW(&_NtCRT_iob[1], format, ap, NTCRT_PRINTF_LONG_DOUBLE);
}

/* swprintf of mingw-w64 without a size */
NTCRT_API int __cdecl __wrap___mingw_vswprintf(wchar_t* buffer, const wchar_t* format, va_list ap)
{
    return _NtCRT_FormatStringW(buffer, SIZE_MAX, format, ap, NTCRT_PRINTF_LONG_DOUBLE);
}

NTCRT_API int __cdecl __wrap___mingw_vsnwprintf(wchar_t* buffer, size_t count, const wchar_t* format, va_list ap)
{
    return _NtCRT_FormatStringW(buffer, count, format, ap, NTCRT_PRINTF_LONG_DOUBLE);
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

/*
 * The functions under their names too: lld takes them from libntcrt, which precedes libmingwex, so ntcrt.cfg does not
 * wrap them and the printf of libmingwex is not loaded. ld takes libmingwex first and wraps them (ntcrt.specs).
 */
EXTERN_C int __cdecl __mingw_vfwprintf(FILE*, const wchar_t*, va_list) __attribute__((alias("__wrap___mingw_vfwprintf")));
EXTERN_C int __cdecl __mingw_vwprintf(const wchar_t*, va_list) __attribute__((alias("__wrap___mingw_vwprintf")));
EXTERN_C int __cdecl __mingw_vswprintf(wchar_t*, const wchar_t*, va_list) __attribute__((alias("__wrap___mingw_vswprintf")));
EXTERN_C int __cdecl __mingw_vsnwprintf(wchar_t*, size_t, const wchar_t*, va_list)
    __attribute__((alias("__wrap___mingw_vsnwprintf")));
EXTERN_C int __cdecl __mingw_fwprintf(FILE*, const wchar_t*, ...) __attribute__((alias("__wrap___mingw_fwprintf")));
EXTERN_C int __cdecl __mingw_wprintf(const wchar_t*, ...) __attribute__((alias("__wrap___mingw_wprintf")));
EXTERN_C int __cdecl __mingw_swprintf(wchar_t*, const wchar_t*, ...) __attribute__((alias("__wrap___mingw_swprintf")));
EXTERN_C int __cdecl __mingw_snwprintf(wchar_t*, size_t, const wchar_t*, ...) __attribute__((alias("__wrap___mingw_snwprintf")));
