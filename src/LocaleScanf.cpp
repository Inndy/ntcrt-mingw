/*
 * The console functions of the scanf family (_cscanf, ...) and the "_l" functions, see Locale.cpp. They run the
 * functions without "_l" in the code page of the locale with NTCRT_LOCALE_SCOPE.
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

EXTERN_C BOOL _NtCRT_ConsoleScanLineA(char* Line, DWORD Size);
EXTERN_C BOOL _NtCRT_ConsoleScanLineW(PWSTR Line, DWORD Count);
EXTERN_C int __cdecl _NtCRT_vfscanf(FILE*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vfscanf_s(FILE*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vfwscanf(FILE*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vfwscanf_s(FILE*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vscanf(const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vscanf_s(const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vsscanf(const char*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vsscanf_s(const char*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vswscanf(const wchar_t*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vswscanf_s(const wchar_t*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vwscanf(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vwscanf_s(const wchar_t*, va_list);

/* msvcrt scans the characters of the console as they are typed, a line is read and scanned here instead */

EXTERN_C int __cdecl _NtCRT_ConsoleVScanA(const char* format, va_list ap)
{
    char Line[1024];

    if (format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), EOF;
    }
    return _NtCRT_ConsoleScanLineA(Line, sizeof(Line)) ? _NtCRT_vsscanf(Line, format, ap) : EOF;
}

EXTERN_C int __cdecl _NtCRT_ConsoleVScanAS(const char* format, va_list ap)
{
    char Line[1024];

    if (format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), EOF;
    }
    return _NtCRT_ConsoleScanLineA(Line, sizeof(Line)) ? _NtCRT_vsscanf_s(Line, format, ap) : EOF;
}

EXTERN_C int __cdecl _NtCRT_ConsoleVScanW(const wchar_t* format, va_list ap)
{
    WCHAR Line[512];

    if (format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), EOF;
    }
    return _NtCRT_ConsoleScanLineW(Line, ARRAYSIZE(Line)) ? _NtCRT_vswscanf(Line, format, ap) : EOF;
}

EXTERN_C int __cdecl _NtCRT_ConsoleVScanWS(const wchar_t* format, va_list ap)
{
    WCHAR Line[512];

    if (format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), EOF;
    }
    return _NtCRT_ConsoleScanLineW(Line, ARRAYSIZE(Line)) ? _NtCRT_vswscanf_s(Line, format, ap) : EOF;
}

NTCRT_WRAP_VARIADIC(_NtCRT__cscanf, _NtCRT_ConsoleVScanA, (const char* format, ...), format, (format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT__cscanf_s, _NtCRT_ConsoleVScanAS, (const char* format, ...), format, (format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT__cwscanf, _NtCRT_ConsoleVScanW, (const wchar_t* format, ...), format, (format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT__cwscanf_s, _NtCRT_ConsoleVScanWS, (const wchar_t* format, ...), format, (format, ap))

EXTERN_C int __cdecl _NtCRT__scanf_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vscanf(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__fscanf_l(FILE* stream, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vfscanf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__sscanf_l(const char* buffer, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vsscanf(buffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__cscanf_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_ConsoleVScanA(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__scanf_s_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vscanf_s(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__fscanf_s_l(FILE* stream, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vfscanf_s(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__sscanf_s_l(const char* buffer, const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vsscanf_s(buffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__cscanf_s_l(const char* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_ConsoleVScanAS(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__wscanf_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vwscanf(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__fwscanf_l(FILE* stream, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vfwscanf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__swscanf_l(const wchar_t* buffer, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vswscanf(buffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__cwscanf_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_ConsoleVScanW(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__wscanf_s_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vwscanf_s(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__fwscanf_s_l(FILE* stream, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vfwscanf_s(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__swscanf_s_l(const wchar_t* buffer, const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_vswscanf_s(buffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__cwscanf_s_l(const wchar_t* format, _locale_t locale, ...)
{
    va_list ap;
    int Ret;

    NTCRT_LOCALE_SCOPE Scope(locale);
    va_start(ap, locale);
    Ret = _NtCRT_ConsoleVScanWS(format, ap);
    va_end(ap);
    return Ret;
}

/* Length characters of the input at most */
#define NTCRT_SNSCANF_L(Name, TChar, Scan, Options) \
    EXTERN_C int __cdecl Name(const TChar* input, size_t length, const TChar* format, _locale_t locale, ...) \
    { \
        va_list ap; \
        int Ret; \
\
        NTCRT_LOCALE_SCOPE Scope(locale); \
        va_start(ap, locale); \
        Ret = Scan(input, length, format, ap, Options); \
        va_end(ap); \
        return Ret; \
    }

NTCRT_SNSCANF_L(_NtCRT__snscanf_l, char, _NtCRT_ScanString, 0)
NTCRT_SNSCANF_L(_NtCRT__snscanf_s_l, char, _NtCRT_ScanString, NTCRT_SCANF_SECURE)
NTCRT_SNSCANF_L(_NtCRT__snwscanf_l, wchar_t, _NtCRT_ScanStringW, NTCRT_SCANF_LEGACY_WIDE)
NTCRT_SNSCANF_L(_NtCRT__snwscanf_s_l, wchar_t, _NtCRT_ScanStringW, NTCRT_SCANF_LEGACY_WIDE | NTCRT_SCANF_SECURE)

NTCRT_IMPORT(_scanf_l, _NtCRT__scanf_l);
NTCRT_IMPORT(_fscanf_l, _NtCRT__fscanf_l);
NTCRT_IMPORT(_sscanf_l, _NtCRT__sscanf_l);
NTCRT_IMPORT(_cscanf_l, _NtCRT__cscanf_l);
NTCRT_IMPORT(_scanf_s_l, _NtCRT__scanf_s_l);
NTCRT_IMPORT(_fscanf_s_l, _NtCRT__fscanf_s_l);
NTCRT_IMPORT(_sscanf_s_l, _NtCRT__sscanf_s_l);
NTCRT_IMPORT(_cscanf_s_l, _NtCRT__cscanf_s_l);
NTCRT_IMPORT(_wscanf_l, _NtCRT__wscanf_l);
NTCRT_IMPORT(_fwscanf_l, _NtCRT__fwscanf_l);
NTCRT_IMPORT(_swscanf_l, _NtCRT__swscanf_l);
NTCRT_IMPORT(_cwscanf_l, _NtCRT__cwscanf_l);
NTCRT_IMPORT(_wscanf_s_l, _NtCRT__wscanf_s_l);
NTCRT_IMPORT(_fwscanf_s_l, _NtCRT__fwscanf_s_l);
NTCRT_IMPORT(_swscanf_s_l, _NtCRT__swscanf_s_l);
NTCRT_IMPORT(_cwscanf_s_l, _NtCRT__cwscanf_s_l);
NTCRT_IMPORT(_cscanf, _NtCRT__cscanf);
NTCRT_IMPORT(_cscanf_s, _NtCRT__cscanf_s);
NTCRT_IMPORT(_cwscanf, _NtCRT__cwscanf);
NTCRT_IMPORT(_cwscanf_s, _NtCRT__cwscanf_s);
NTCRT_IMPORT(_snscanf_l, _NtCRT__snscanf_l);
NTCRT_IMPORT(_snscanf_s_l, _NtCRT__snscanf_s_l);
NTCRT_IMPORT(_snwscanf_l, _NtCRT__snwscanf_l);
NTCRT_IMPORT(_snwscanf_s_l, _NtCRT__snwscanf_s_l);
