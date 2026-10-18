/*
 * The "_l" functions of numeric conversions, see Locale.cpp. They run the functions without "_l" in the code page of
 * the locale with NTCRT_LOCALE_SCOPE.
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

EXTERN_C double __cdecl _NtCRT__wtof(const wchar_t*);
EXTERN_C double __cdecl _NtCRT_atof(const char*);
EXTERN_C double __cdecl _NtCRT_strtod(const char*, char**);
EXTERN_C double __cdecl _NtCRT_wcstod(const wchar_t*, wchar_t**);
EXTERN_C intmax_t __cdecl _NtCRT_strtoimax(const char*, char**, int);
EXTERN_C long __cdecl _NtCRT_strtol(const char*, char**, int);
EXTERN_C long __cdecl _NtCRT_wcstol(const wchar_t*, wchar_t**, int);
EXTERN_C long long __cdecl _NtCRT_atoll(const char*);
EXTERN_C long long __cdecl _NtCRT_strtoll(const char*, char**, int);
EXTERN_C long long __cdecl _NtCRT_wcstoll(const wchar_t*, wchar_t**, int);
EXTERN_C uintmax_t __cdecl _NtCRT_strtoumax(const char*, char**, int);
EXTERN_C unsigned long __cdecl _NtCRT_strtoul(const char*, char**, int);
EXTERN_C unsigned long __cdecl _NtCRT_wcstoul(const wchar_t*, wchar_t**, int);
EXTERN_C unsigned long long __cdecl _NtCRT_strtoull(const char*, char**, int);
EXTERN_C unsigned long long __cdecl _NtCRT_wcstoull(const wchar_t*, wchar_t**, int);

/*
 * The conversions of msvcrt without "_l". Returns _OVERFLOW of <math.h> for an infinity and _UNDERFLOW for zero or a
 * denormal, which strtod reports by ERANGE alike. Not in String.cpp: lld loads it for its __wrap_ functions, and the
 * gdtoa of libmingwex with it for __mingw_strtold.
 */
EXTERN_C int __cdecl _NtCRT__atodbl(_CRT_DOUBLE* value, char* str)
{
    int Saved = NTCRT_ERRNO, Ret = 0;

    NTCRT_ERRNO = 0;
    value->x = _NtCRT_strtod(str, NULL);
    if (NTCRT_ERRNO == ERANGE)
    {
        Ret = isinf(value->x) ? _OVERFLOW : _UNDERFLOW;
    }
    NTCRT_ERRNO = Saved;
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__atoldbl(_LDOUBLE* value, char* str)
{
    int Saved = NTCRT_ERRNO, Ret = 0;
    long double x;

    NTCRT_ERRNO = 0;
    x = __mingw_strtold(str, NULL);
    if (NTCRT_ERRNO == ERANGE)
    {
        Ret = isinf(x) ? _OVERFLOW : _UNDERFLOW;
    }
    memcpy(value->ld, &x, sizeof(value->ld));
    NTCRT_ERRNO = Saved;
    return Ret;
}

/* Flags of __STRINGTOLD */
#define NTCRT_SLD_UNDERFLOW 1
#define NTCRT_SLD_OVERFLOW 2
#define NTCRT_SLD_NODIGITS 4

/* The conversion of _atoldbl with the end of the number, mult12 is not used by msvcrt */
EXTERN_C unsigned int __cdecl _NtCRT___STRINGTOLD(_LDOUBLE* pld, char** end, const char* str, int mult12)
{
    int Saved = NTCRT_ERRNO;
    unsigned int Ret = 0;
    char* End;
    long double x;

    UNREFERENCED_PARAMETER(mult12);
    NTCRT_ERRNO = 0;
    x = __mingw_strtold(str, &End);
    if (End == str)
    {
        Ret = NTCRT_SLD_NODIGITS;
    } else if (NTCRT_ERRNO == ERANGE)
    {
        /* msvcrt: no flag for a denormal */
        Ret = isinf(x) ? NTCRT_SLD_OVERFLOW : x == 0 ? NTCRT_SLD_UNDERFLOW : 0;
    }
    memcpy(pld->ld, &x, sizeof(pld->ld));
    if (end != NULL)
    {
        *end = End;
    }
    NTCRT_ERRNO = Saved;
    return Ret;
}

/* msvcrt has the "_l" form only */
EXTERN_C int __cdecl _NtCRT__atoflt(_CRT_FLOAT* value, char* str)
{
    _CRT_DOUBLE Double;
    int Ret = _NtCRT__atodbl(&Double, str);

    value->f = (float)Double.x;
    if (Ret == 0 && isinf(value->f) && !isinf(Double.x))
    {
        Ret = _OVERFLOW;
    } else if (Ret == 0 && (value->f == 0 ? Double.x != 0 : fpclassify(value->f) == FP_SUBNORMAL))
    {
        Ret = _UNDERFLOW;
    }
    return Ret;
}

EXTERN_C double __cdecl _NtCRT__atof_l(const char* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_atof(str);
}

EXTERN_C int __cdecl _NtCRT__atoi_l(const char* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return atoi(str);
}

EXTERN_C long __cdecl _NtCRT__atol_l(const char* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return atol(str);
}

EXTERN_C __int64 __cdecl _NtCRT__atoi64_l(const char* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _atoi64(str);
}

EXTERN_C long long __cdecl _NtCRT__atoll_l(const char* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_atoll(str);
}

EXTERN_C double __cdecl _NtCRT__wtof_l(const wchar_t* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__wtof(str);
}

EXTERN_C int __cdecl _NtCRT__wtoi_l(const wchar_t* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _wtoi(str);
}

EXTERN_C long __cdecl _NtCRT__wtol_l(const wchar_t* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _wtol(str);
}

EXTERN_C __int64 __cdecl _NtCRT__wtoi64_l(const wchar_t* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _wtoi64(str);
}

EXTERN_C int __cdecl _NtCRT__atodbl_l(_CRT_DOUBLE* value, char* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__atodbl(value, str);
}

EXTERN_C int __cdecl _NtCRT__atoldbl_l(_LDOUBLE* value, char* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__atoldbl(value, str);
}

EXTERN_C int __cdecl _NtCRT__atoflt_l(_CRT_FLOAT* value, char* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__atoflt(value, str);
}

EXTERN_C double __cdecl _NtCRT__strtod_l(const char* str, char** end, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_strtod(str, end);
}

EXTERN_C double __cdecl _NtCRT__wcstod_l(const wchar_t* str, wchar_t** end, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wcstod(str, end);
}

EXTERN_C long __cdecl _NtCRT__strtol_l(const char* str, char** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_strtol(str, end, base);
}

EXTERN_C unsigned long __cdecl _NtCRT__strtoul_l(const char* str, char** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_strtoul(str, end, base);
}

EXTERN_C long long __cdecl _NtCRT__strtoll_l(const char* str, char** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_strtoll(str, end, base);
}

EXTERN_C long long __cdecl _NtCRT__strtoi64_l(const char* str, char** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_strtoll(str, end, base);
}

EXTERN_C unsigned long long __cdecl _NtCRT__strtoull_l(const char* str, char** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_strtoull(str, end, base);
}

EXTERN_C unsigned long long __cdecl _NtCRT__strtoui64_l(const char* str, char** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_strtoull(str, end, base);
}

EXTERN_C intmax_t __cdecl _NtCRT__strtoimax_l(const char* str, char** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_strtoimax(str, end, base);
}

EXTERN_C uintmax_t __cdecl _NtCRT__strtoumax_l(const char* str, char** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_strtoumax(str, end, base);
}

EXTERN_C long __cdecl _NtCRT__wcstol_l(const wchar_t* str, wchar_t** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wcstol(str, end, base);
}

EXTERN_C unsigned long __cdecl _NtCRT__wcstoul_l(const wchar_t* str, wchar_t** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wcstoul(str, end, base);
}

EXTERN_C long long __cdecl _NtCRT__wcstoi64_l(const wchar_t* str, wchar_t** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wcstoll(str, end, base);
}

EXTERN_C unsigned long long __cdecl _NtCRT__wcstoui64_l(const wchar_t* str, wchar_t** end, int base, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wcstoull(str, end, base);
}

NTCRT_IMPORT(_atodbl, _NtCRT__atodbl);
NTCRT_IMPORT(_atoldbl, _NtCRT__atoldbl);
NTCRT_IMPORT(__STRINGTOLD, _NtCRT___STRINGTOLD);
NTCRT_IMPORT(_atof_l, _NtCRT__atof_l);
NTCRT_IMPORT(_atoi_l, _NtCRT__atoi_l);
NTCRT_IMPORT(_atol_l, _NtCRT__atol_l);
NTCRT_IMPORT(_atoi64_l, _NtCRT__atoi64_l);
NTCRT_IMPORT(_atoll_l, _NtCRT__atoll_l);
NTCRT_IMPORT(_wtof_l, _NtCRT__wtof_l);
NTCRT_IMPORT(_wtoi_l, _NtCRT__wtoi_l);
NTCRT_IMPORT(_wtol_l, _NtCRT__wtol_l);
NTCRT_IMPORT(_wtoi64_l, _NtCRT__wtoi64_l);
NTCRT_IMPORT(_atodbl_l, _NtCRT__atodbl_l);
NTCRT_IMPORT(_atoldbl_l, _NtCRT__atoldbl_l);
NTCRT_IMPORT(_atoflt_l, _NtCRT__atoflt_l);
NTCRT_IMPORT(_strtod_l, _NtCRT__strtod_l);
NTCRT_IMPORT(_wcstod_l, _NtCRT__wcstod_l);
NTCRT_IMPORT(_strtol_l, _NtCRT__strtol_l);
NTCRT_IMPORT(_strtoul_l, _NtCRT__strtoul_l);
NTCRT_IMPORT(_strtoll_l, _NtCRT__strtoll_l);
NTCRT_IMPORT(_strtoi64_l, _NtCRT__strtoi64_l);
NTCRT_IMPORT(_strtoull_l, _NtCRT__strtoull_l);
NTCRT_IMPORT(_strtoui64_l, _NtCRT__strtoui64_l);
NTCRT_IMPORT(_strtoimax_l, _NtCRT__strtoimax_l);
NTCRT_IMPORT(_strtoumax_l, _NtCRT__strtoumax_l);
NTCRT_IMPORT(_wcstol_l, _NtCRT__wcstol_l);
NTCRT_IMPORT(_wcstoul_l, _NtCRT__wcstoul_l);
NTCRT_IMPORT(_wcstoi64_l, _NtCRT__wcstoi64_l);
NTCRT_IMPORT(_wcstoui64_l, _NtCRT__wcstoui64_l);
