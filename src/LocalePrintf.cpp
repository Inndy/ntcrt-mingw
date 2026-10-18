/*
 * The "_l" functions of the printf family, see Locale.cpp. They run the functions without "_l" in the code page of the
 * locale with NTCRT_LOCALE_SCOPE.
 */

#include "NtCRT.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

EXTERN_C int __cdecl _NtCRT__vcprintf(const char*, va_list);
EXTERN_C int __cdecl _NtCRT__vcprintf_p(const char*, va_list);
EXTERN_C int __cdecl _NtCRT__vcprintf_s(const char*, va_list);
EXTERN_C int __cdecl _NtCRT__vcwprintf(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT__vcwprintf_p(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT__vcwprintf_s(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT__vfprintf_p(FILE*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT__vfwprintf_p(FILE*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT__vprintf_p(const char*, va_list);
EXTERN_C int __cdecl _NtCRT__vscprintf(const char*, va_list);
EXTERN_C int __cdecl _NtCRT__vscprintf_p(const char*, va_list);
EXTERN_C int __cdecl _NtCRT__vscwprintf(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT__vscwprintf_p(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT__vsnprintf(char*, size_t, const char*, va_list);
EXTERN_C int __cdecl _NtCRT__vsnprintf_c(char*, size_t, const char*, va_list);
EXTERN_C int __cdecl _NtCRT__vsnprintf_s(char*, size_t, size_t, const char*, va_list);
EXTERN_C int __cdecl _NtCRT__vsnwprintf(wchar_t*, size_t, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT__vsnwprintf_s(wchar_t*, size_t, size_t, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT__vsprintf_p(char*, size_t, const char*, va_list);
EXTERN_C int __cdecl _NtCRT__vswprintf_c(wchar_t*, size_t, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT__vswprintf_p(wchar_t*, size_t, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT__vwprintf_p(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vfprintf(FILE*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vfprintf_s(FILE*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vfwprintf(FILE*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vfwprintf_s(FILE*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vprintf(const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vprintf_s(const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vsprintf(char*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vsprintf_s(char*, size_t, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vswprintf_s(wchar_t*, size_t, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vwprintf(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vwprintf_s(const wchar_t*, va_list);

EXTERN_C int __cdecl _NtCRT__printf_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vprintf(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vprintf_l(const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_vprintf(format, ap);
}

EXTERN_C int __cdecl _NtCRT__printf_p_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vprintf_p(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vprintf_p_l(const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vprintf_p(format, ap);
}

EXTERN_C int __cdecl _NtCRT__printf_s_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vprintf_s(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vprintf_s_l(const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_vprintf_s(format, ap);
}

EXTERN_C int __cdecl _NtCRT__fprintf_l(FILE* stream, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vfprintf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vfprintf_l(FILE* stream, const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_vfprintf(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT__fprintf_p_l(FILE* stream, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vfprintf_p(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vfprintf_p_l(FILE* stream, const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vfprintf_p(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT__fprintf_s_l(FILE* stream, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vfprintf_s(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vfprintf_s_l(FILE* stream, const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_vfprintf_s(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT__cprintf_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vcprintf(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vcprintf_l(const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vcprintf(format, ap);
}

EXTERN_C int __cdecl _NtCRT__cprintf_p_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vcprintf_p(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vcprintf_p_l(const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vcprintf_p(format, ap);
}

EXTERN_C int __cdecl _NtCRT__cprintf_s_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vcprintf_s(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vcprintf_s_l(const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vcprintf_s(format, ap);
}

EXTERN_C int __cdecl _NtCRT__scprintf_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vscprintf(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vscprintf_l(const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vscprintf(format, ap);
}

EXTERN_C int __cdecl _NtCRT__scprintf_p_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vscprintf_p(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vscprintf_p_l(const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vscprintf_p(format, ap);
}

EXTERN_C int __cdecl _NtCRT__wprintf_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vwprintf(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vwprintf_l(const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_vwprintf(format, ap);
}

EXTERN_C int __cdecl _NtCRT__wprintf_p_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vwprintf_p(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vwprintf_p_l(const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vwprintf_p(format, ap);
}

EXTERN_C int __cdecl _NtCRT__wprintf_s_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vwprintf_s(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vwprintf_s_l(const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_vwprintf_s(format, ap);
}

EXTERN_C int __cdecl _NtCRT__fwprintf_l(FILE* stream, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vfwprintf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vfwprintf_l(FILE* stream, const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_vfwprintf(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT__fwprintf_p_l(FILE* stream, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vfwprintf_p(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vfwprintf_p_l(FILE* stream, const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vfwprintf_p(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT__fwprintf_s_l(FILE* stream, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vfwprintf_s(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vfwprintf_s_l(FILE* stream, const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_vfwprintf_s(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT__cwprintf_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vcwprintf(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vcwprintf_l(const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vcwprintf(format, ap);
}

EXTERN_C int __cdecl _NtCRT__cwprintf_p_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vcwprintf_p(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vcwprintf_p_l(const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vcwprintf_p(format, ap);
}

EXTERN_C int __cdecl _NtCRT__cwprintf_s_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vcwprintf_s(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vcwprintf_s_l(const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vcwprintf_s(format, ap);
}

EXTERN_C int __cdecl _NtCRT__scwprintf_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vscwprintf(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vscwprintf_l(const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vscwprintf(format, ap);
}

EXTERN_C int __cdecl _NtCRT__scwprintf_p_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vscwprintf_p(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vscwprintf_p_l(const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vscwprintf_p(format, ap);
}

EXTERN_C int __cdecl _NtCRT__sprintf_l(char* buffer, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vsprintf(buffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsprintf_l(char* buffer, const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_vsprintf(buffer, format, ap);
}

EXTERN_C int __cdecl _NtCRT__sprintf_p_l(char* buffer, size_t size, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vsprintf_p(buffer, size, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsprintf_p_l(char* buffer, size_t size, const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vsprintf_p(buffer, size, format, ap);
}

EXTERN_C int __cdecl _NtCRT__sprintf_s_l(char* buffer, size_t size, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vsprintf_s(buffer, size, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsprintf_s_l(char* buffer, size_t size, const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_vsprintf_s(buffer, size, format, ap);
}

EXTERN_C int __cdecl _NtCRT__snprintf_l(char* buffer, size_t count, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vsnprintf(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsnprintf_l(char* buffer, size_t count, const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vsnprintf(buffer, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT__snprintf_c_l(char* buffer, size_t count, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vsnprintf_c(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsnprintf_c_l(char* buffer, size_t count, const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vsnprintf_c(buffer, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT__snprintf_s_l(
    char* buffer, size_t size, size_t count, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vsnprintf_s(buffer, size, count, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsnprintf_s_l(
    char* buffer, size_t size, size_t count, const char* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vsnprintf_s(buffer, size, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT__swprintf_l(wchar_t* buffer, size_t count, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vswprintf_c(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vswprintf_l(
    wchar_t* buffer, size_t count, const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vswprintf_c(buffer, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT__swprintf_c_l(wchar_t* buffer, size_t count, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vswprintf_c(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vswprintf_c_l(
    wchar_t* buffer, size_t count, const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vswprintf_c(buffer, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT__swprintf_p_l(wchar_t* buffer, size_t size, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vswprintf_p(buffer, size, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vswprintf_p_l(
    wchar_t* buffer, size_t size, const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vswprintf_p(buffer, size, format, ap);
}

EXTERN_C int __cdecl _NtCRT__swprintf_s_l(wchar_t* buffer, size_t size, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vswprintf_s(buffer, size, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vswprintf_s_l(
    wchar_t* buffer, size_t size, const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_vswprintf_s(buffer, size, format, ap);
}

EXTERN_C int __cdecl _NtCRT__snwprintf_l(wchar_t* buffer, size_t count, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vsnwprintf(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsnwprintf_l(
    wchar_t* buffer, size_t count, const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vsnwprintf(buffer, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT__snwprintf_s_l(
    wchar_t* buffer, size_t size, size_t count, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT__vsnwprintf_s(buffer, size, count, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsnwprintf_s_l(
    wchar_t* buffer, size_t size, size_t count, const wchar_t* format, _locale_t locale, va_list ap)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__vsnwprintf_s(buffer, size, count, format, ap);
}

NTCRT_IMPORT(_printf_l, _NtCRT__printf_l);
NTCRT_IMPORT(_vprintf_l, _NtCRT__vprintf_l);
NTCRT_IMPORT(_printf_p_l, _NtCRT__printf_p_l);
NTCRT_IMPORT(_vprintf_p_l, _NtCRT__vprintf_p_l);
NTCRT_IMPORT(_printf_s_l, _NtCRT__printf_s_l);
NTCRT_IMPORT(_vprintf_s_l, _NtCRT__vprintf_s_l);
NTCRT_IMPORT(_fprintf_l, _NtCRT__fprintf_l);
NTCRT_IMPORT(_vfprintf_l, _NtCRT__vfprintf_l);
NTCRT_IMPORT(_fprintf_p_l, _NtCRT__fprintf_p_l);
NTCRT_IMPORT(_vfprintf_p_l, _NtCRT__vfprintf_p_l);
NTCRT_IMPORT(_fprintf_s_l, _NtCRT__fprintf_s_l);
NTCRT_IMPORT(_vfprintf_s_l, _NtCRT__vfprintf_s_l);
NTCRT_IMPORT(_cprintf_l, _NtCRT__cprintf_l);
NTCRT_IMPORT(_vcprintf_l, _NtCRT__vcprintf_l);
NTCRT_IMPORT(_cprintf_p_l, _NtCRT__cprintf_p_l);
NTCRT_IMPORT(_vcprintf_p_l, _NtCRT__vcprintf_p_l);
NTCRT_IMPORT(_cprintf_s_l, _NtCRT__cprintf_s_l);
NTCRT_IMPORT(_vcprintf_s_l, _NtCRT__vcprintf_s_l);
NTCRT_IMPORT(_scprintf_l, _NtCRT__scprintf_l);
NTCRT_IMPORT(_vscprintf_l, _NtCRT__vscprintf_l);
NTCRT_IMPORT(_scprintf_p_l, _NtCRT__scprintf_p_l);
NTCRT_IMPORT(_vscprintf_p_l, _NtCRT__vscprintf_p_l);
NTCRT_IMPORT(_wprintf_l, _NtCRT__wprintf_l);
NTCRT_IMPORT(_vwprintf_l, _NtCRT__vwprintf_l);
NTCRT_IMPORT(_wprintf_p_l, _NtCRT__wprintf_p_l);
NTCRT_IMPORT(_vwprintf_p_l, _NtCRT__vwprintf_p_l);
NTCRT_IMPORT(_wprintf_s_l, _NtCRT__wprintf_s_l);
NTCRT_IMPORT(_vwprintf_s_l, _NtCRT__vwprintf_s_l);
NTCRT_IMPORT(_fwprintf_l, _NtCRT__fwprintf_l);
NTCRT_IMPORT(_vfwprintf_l, _NtCRT__vfwprintf_l);
NTCRT_IMPORT(_fwprintf_p_l, _NtCRT__fwprintf_p_l);
NTCRT_IMPORT(_vfwprintf_p_l, _NtCRT__vfwprintf_p_l);
NTCRT_IMPORT(_fwprintf_s_l, _NtCRT__fwprintf_s_l);
NTCRT_IMPORT(_vfwprintf_s_l, _NtCRT__vfwprintf_s_l);
NTCRT_IMPORT(_cwprintf_l, _NtCRT__cwprintf_l);
NTCRT_IMPORT(_vcwprintf_l, _NtCRT__vcwprintf_l);
NTCRT_IMPORT(_cwprintf_p_l, _NtCRT__cwprintf_p_l);
NTCRT_IMPORT(_vcwprintf_p_l, _NtCRT__vcwprintf_p_l);
NTCRT_IMPORT(_cwprintf_s_l, _NtCRT__cwprintf_s_l);
NTCRT_IMPORT(_vcwprintf_s_l, _NtCRT__vcwprintf_s_l);
NTCRT_IMPORT(_scwprintf_l, _NtCRT__scwprintf_l);
NTCRT_IMPORT(_vscwprintf_l, _NtCRT__vscwprintf_l);
NTCRT_IMPORT(_scwprintf_p_l, _NtCRT__scwprintf_p_l);
NTCRT_IMPORT(_vscwprintf_p_l, _NtCRT__vscwprintf_p_l);
NTCRT_IMPORT(_sprintf_l, _NtCRT__sprintf_l);
NTCRT_IMPORT(_vsprintf_l, _NtCRT__vsprintf_l);
NTCRT_IMPORT(_sprintf_p_l, _NtCRT__sprintf_p_l);
NTCRT_IMPORT(_vsprintf_p_l, _NtCRT__vsprintf_p_l);
NTCRT_IMPORT(_sprintf_s_l, _NtCRT__sprintf_s_l);
NTCRT_IMPORT(_vsprintf_s_l, _NtCRT__vsprintf_s_l);
NTCRT_IMPORT(_snprintf_l, _NtCRT__snprintf_l);
NTCRT_IMPORT(_vsnprintf_l, _NtCRT__vsnprintf_l);
NTCRT_IMPORT(_snprintf_c_l, _NtCRT__snprintf_c_l);
NTCRT_IMPORT(_vsnprintf_c_l, _NtCRT__vsnprintf_c_l);
NTCRT_IMPORT(_snprintf_s_l, _NtCRT__snprintf_s_l);
NTCRT_IMPORT(_vsnprintf_s_l, _NtCRT__vsnprintf_s_l);
NTCRT_IMPORT(_swprintf_l, _NtCRT__swprintf_l);
NTCRT_IMPORT(_vswprintf_l, _NtCRT__vswprintf_l);
NTCRT_IMPORT(_swprintf_c_l, _NtCRT__swprintf_c_l);
NTCRT_IMPORT(_vswprintf_c_l, _NtCRT__vswprintf_c_l);
NTCRT_IMPORT(_swprintf_p_l, _NtCRT__swprintf_p_l);
NTCRT_IMPORT(_vswprintf_p_l, _NtCRT__vswprintf_p_l);
NTCRT_IMPORT(_swprintf_s_l, _NtCRT__swprintf_s_l);
NTCRT_IMPORT(_vswprintf_s_l, _NtCRT__vswprintf_s_l);
NTCRT_IMPORT(_snwprintf_l, _NtCRT__snwprintf_l);
NTCRT_IMPORT(_vsnwprintf_l, _NtCRT__vsnwprintf_l);
NTCRT_IMPORT(_snwprintf_s_l, _NtCRT__snwprintf_s_l);
NTCRT_IMPORT(_vsnwprintf_s_l, _NtCRT__vsnwprintf_s_l);
