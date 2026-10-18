/*
 * The "_l" functions of collation, case-insensitive comparison and case conversion of strings, see Locale.cpp. They run
 * the functions without "_l" in the code page of the locale with NTCRT_LOCALE_SCOPE.
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

EXTERN_C errno_t __cdecl _NtCRT__strlwr_s(char*, size_t);
EXTERN_C errno_t __cdecl _NtCRT__strupr_s(char*, size_t);
EXTERN_C errno_t __cdecl _NtCRT__wcslwr_s(wchar_t*, size_t);
EXTERN_C errno_t __cdecl _NtCRT__wcsupr_s(wchar_t*, size_t);
EXTERN_C int __cdecl _NtCRT__stricoll(const char*, const char*);
EXTERN_C int __cdecl _NtCRT__strncoll(const char*, const char*, size_t);
EXTERN_C int __cdecl _NtCRT__strnicoll(const char*, const char*, size_t);
EXTERN_C int __cdecl _NtCRT__wcsicoll(const wchar_t*, const wchar_t*);
EXTERN_C int __cdecl _NtCRT__wcsncoll(const wchar_t*, const wchar_t*, size_t);
EXTERN_C int __cdecl _NtCRT__wcsnicoll(const wchar_t*, const wchar_t*, size_t);
EXTERN_C int __cdecl _NtCRT_strcoll(const char*, const char*);
EXTERN_C int __cdecl _NtCRT_wcscoll(const wchar_t*, const wchar_t*);
EXTERN_C size_t __cdecl _NtCRT_strxfrm(char*, const char*, size_t);
EXTERN_C size_t __cdecl _NtCRT_wcsxfrm(wchar_t*, const wchar_t*, size_t);

EXTERN_C int __cdecl _NtCRT__strcoll_l(const char* s1, const char* s2, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_strcoll(s1, s2);
}

EXTERN_C int __cdecl _NtCRT__stricoll_l(const char* s1, const char* s2, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__stricoll(s1, s2);
}

EXTERN_C int __cdecl _NtCRT__strncoll_l(const char* s1, const char* s2, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__strncoll(s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__strnicoll_l(const char* s1, const char* s2, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__strnicoll(s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__stricmp_l(const char* s1, const char* s2, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _stricmp(s1, s2);
}

EXTERN_C int __cdecl _NtCRT__strnicmp_l(const char* s1, const char* s2, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _strnicmp(s1, s2, count);
}

EXTERN_C char* __cdecl _NtCRT__strlwr_l(char* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _strlwr(str);
}

EXTERN_C char* __cdecl _NtCRT__strupr_l(char* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _strupr(str);
}

EXTERN_C errno_t __cdecl _NtCRT__strlwr_s_l(char* str, size_t size, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__strlwr_s(str, size);
}

EXTERN_C errno_t __cdecl _NtCRT__strupr_s_l(char* str, size_t size, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__strupr_s(str, size);
}

EXTERN_C size_t __cdecl _NtCRT__strxfrm_l(char* dst, const char* src, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_strxfrm(dst, src, count);
}

EXTERN_C int __cdecl _NtCRT__wcscoll_l(const wchar_t* s1, const wchar_t* s2, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wcscoll(s1, s2);
}

EXTERN_C int __cdecl _NtCRT__wcsicoll_l(const wchar_t* s1, const wchar_t* s2, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__wcsicoll(s1, s2);
}

EXTERN_C int __cdecl _NtCRT__wcsncoll_l(const wchar_t* s1, const wchar_t* s2, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__wcsncoll(s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__wcsnicoll_l(const wchar_t* s1, const wchar_t* s2, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__wcsnicoll(s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__wcsicmp_l(const wchar_t* s1, const wchar_t* s2, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _wcsicmp(s1, s2);
}

EXTERN_C int __cdecl _NtCRT__wcsnicmp_l(const wchar_t* s1, const wchar_t* s2, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _wcsnicmp(s1, s2, count);
}

EXTERN_C wchar_t* __cdecl _NtCRT__wcslwr_l(wchar_t* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _wcslwr(str);
}

EXTERN_C wchar_t* __cdecl _NtCRT__wcsupr_l(wchar_t* str, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _wcsupr(str);
}

EXTERN_C errno_t __cdecl _NtCRT__wcslwr_s_l(wchar_t* str, size_t size, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__wcslwr_s(str, size);
}

EXTERN_C errno_t __cdecl _NtCRT__wcsupr_s_l(wchar_t* str, size_t size, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__wcsupr_s(str, size);
}

EXTERN_C size_t __cdecl _NtCRT__wcsxfrm_l(wchar_t* dst, const wchar_t* src, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wcsxfrm(dst, src, count);
}

EXTERN_C int __cdecl _NtCRT__memicmp_l(const void* buf1, const void* buf2, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _memicmp(buf1, buf2, count);
}

NTCRT_IMPORT(_strcoll_l, _NtCRT__strcoll_l);
NTCRT_IMPORT(_stricoll_l, _NtCRT__stricoll_l);
NTCRT_IMPORT(_strncoll_l, _NtCRT__strncoll_l);
NTCRT_IMPORT(_strnicoll_l, _NtCRT__strnicoll_l);
NTCRT_IMPORT(_stricmp_l, _NtCRT__stricmp_l);
NTCRT_IMPORT(_strnicmp_l, _NtCRT__strnicmp_l);
NTCRT_IMPORT(_strlwr_l, _NtCRT__strlwr_l);
NTCRT_IMPORT(_strupr_l, _NtCRT__strupr_l);
NTCRT_IMPORT(_strlwr_s_l, _NtCRT__strlwr_s_l);
NTCRT_IMPORT(_strupr_s_l, _NtCRT__strupr_s_l);
NTCRT_IMPORT(_strxfrm_l, _NtCRT__strxfrm_l);
NTCRT_IMPORT(_wcscoll_l, _NtCRT__wcscoll_l);
NTCRT_IMPORT(_wcsicoll_l, _NtCRT__wcsicoll_l);
NTCRT_IMPORT(_wcsncoll_l, _NtCRT__wcsncoll_l);
NTCRT_IMPORT(_wcsnicoll_l, _NtCRT__wcsnicoll_l);
NTCRT_IMPORT(_wcsicmp_l, _NtCRT__wcsicmp_l);
NTCRT_IMPORT(_wcsnicmp_l, _NtCRT__wcsnicmp_l);
NTCRT_IMPORT(_wcslwr_l, _NtCRT__wcslwr_l);
NTCRT_IMPORT(_wcsupr_l, _NtCRT__wcsupr_l);
NTCRT_IMPORT(_wcslwr_s_l, _NtCRT__wcslwr_s_l);
NTCRT_IMPORT(_wcsupr_s_l, _NtCRT__wcsupr_s_l);
NTCRT_IMPORT(_wcsxfrm_l, _NtCRT__wcsxfrm_l);
NTCRT_IMPORT(_memicmp_l, _NtCRT__memicmp_l);
