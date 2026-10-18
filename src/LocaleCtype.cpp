/*
 * The "_l" functions of character classes and case mapping, see Locale.cpp. They run the functions without "_l" in the
 * code page of the locale with NTCRT_LOCALE_SCOPE.
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

EXTERN_C int __cdecl _NtCRT__isctype(int, int);
EXTERN_C int __cdecl _NtCRT_isleadbyte(int);
EXTERN_C int __cdecl _NtCRT_iswalnum(wint_t);
EXTERN_C int __cdecl _NtCRT_iswalpha(wint_t);
EXTERN_C int __cdecl _NtCRT_iswcntrl(wint_t);
EXTERN_C int __cdecl _NtCRT_iswctype(wint_t, wctype_t);
EXTERN_C int __cdecl _NtCRT_iswdigit(wint_t);
EXTERN_C int __cdecl _NtCRT_iswgraph(wint_t);
EXTERN_C int __cdecl _NtCRT_iswlower(wint_t);
EXTERN_C int __cdecl _NtCRT_iswprint(wint_t);
EXTERN_C int __cdecl _NtCRT_iswpunct(wint_t);
EXTERN_C int __cdecl _NtCRT_iswspace(wint_t);
EXTERN_C int __cdecl _NtCRT_iswupper(wint_t);
EXTERN_C int __cdecl _NtCRT_iswxdigit(wint_t);
EXTERN_C int __cdecl _NtCRT_tolower(int);
EXTERN_C int __cdecl _NtCRT_toupper(int);
EXTERN_C wint_t __cdecl _NtCRT_towlower(wint_t);
EXTERN_C wint_t __cdecl _NtCRT_towupper(wint_t);

EXTERN_C int __cdecl _NtCRT__isalnum_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return isalnum(c);
}

EXTERN_C int __cdecl _NtCRT__isalpha_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return isalpha(c);
}

EXTERN_C int __cdecl _NtCRT__iscntrl_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return iscntrl(c);
}

EXTERN_C int __cdecl _NtCRT__isdigit_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return isdigit(c);
}

EXTERN_C int __cdecl _NtCRT__isgraph_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return isgraph(c);
}

EXTERN_C int __cdecl _NtCRT__islower_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return islower(c);
}

EXTERN_C int __cdecl _NtCRT__isprint_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return isprint(c);
}

EXTERN_C int __cdecl _NtCRT__ispunct_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return ispunct(c);
}

EXTERN_C int __cdecl _NtCRT__isspace_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return isspace(c);
}

EXTERN_C int __cdecl _NtCRT__isupper_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return isupper(c);
}

EXTERN_C int __cdecl _NtCRT__isxdigit_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return isxdigit(c);
}

EXTERN_C int __cdecl _NtCRT__iswalnum_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswalnum(c);
}

EXTERN_C int __cdecl _NtCRT__iswalpha_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswalpha(c);
}

EXTERN_C int __cdecl _NtCRT__iswcntrl_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswcntrl(c);
}

EXTERN_C int __cdecl _NtCRT__iswdigit_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswdigit(c);
}

EXTERN_C int __cdecl _NtCRT__iswgraph_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswgraph(c);
}

EXTERN_C int __cdecl _NtCRT__iswlower_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswlower(c);
}

EXTERN_C int __cdecl _NtCRT__iswprint_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswprint(c);
}

EXTERN_C int __cdecl _NtCRT__iswpunct_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswpunct(c);
}

EXTERN_C int __cdecl _NtCRT__iswspace_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswspace(c);
}

EXTERN_C int __cdecl _NtCRT__iswupper_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswupper(c);
}

EXTERN_C int __cdecl _NtCRT__iswxdigit_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswxdigit(c);
}

EXTERN_C int __cdecl _NtCRT__isctype_l(int c, int mask, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT__isctype(c, mask);
}

EXTERN_C int __cdecl _NtCRT__iswctype_l(wint_t c, wctype_t mask, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_iswctype(c, mask);
}

EXTERN_C int __cdecl _NtCRT__isleadbyte_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_isleadbyte(c);
}

EXTERN_C int __cdecl _NtCRT__tolower_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_tolower(c);
}

EXTERN_C int __cdecl _NtCRT__toupper_l(int c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_toupper(c);
}

EXTERN_C wint_t __cdecl _NtCRT__towlower_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_towlower(c);
}

EXTERN_C wint_t __cdecl _NtCRT__towupper_l(wint_t c, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_towupper(c);
}

NTCRT_IMPORT(_isalnum_l, _NtCRT__isalnum_l);
NTCRT_IMPORT(_isalpha_l, _NtCRT__isalpha_l);
NTCRT_IMPORT(_iscntrl_l, _NtCRT__iscntrl_l);
NTCRT_IMPORT(_isdigit_l, _NtCRT__isdigit_l);
NTCRT_IMPORT(_isgraph_l, _NtCRT__isgraph_l);
NTCRT_IMPORT(_islower_l, _NtCRT__islower_l);
NTCRT_IMPORT(_isprint_l, _NtCRT__isprint_l);
NTCRT_IMPORT(_ispunct_l, _NtCRT__ispunct_l);
NTCRT_IMPORT(_isspace_l, _NtCRT__isspace_l);
NTCRT_IMPORT(_isupper_l, _NtCRT__isupper_l);
NTCRT_IMPORT(_isxdigit_l, _NtCRT__isxdigit_l);
NTCRT_IMPORT(_iswalnum_l, _NtCRT__iswalnum_l);
NTCRT_IMPORT(_iswalpha_l, _NtCRT__iswalpha_l);
NTCRT_IMPORT(_iswcntrl_l, _NtCRT__iswcntrl_l);
NTCRT_IMPORT(_iswdigit_l, _NtCRT__iswdigit_l);
NTCRT_IMPORT(_iswgraph_l, _NtCRT__iswgraph_l);
NTCRT_IMPORT(_iswlower_l, _NtCRT__iswlower_l);
NTCRT_IMPORT(_iswprint_l, _NtCRT__iswprint_l);
NTCRT_IMPORT(_iswpunct_l, _NtCRT__iswpunct_l);
NTCRT_IMPORT(_iswspace_l, _NtCRT__iswspace_l);
NTCRT_IMPORT(_iswupper_l, _NtCRT__iswupper_l);
NTCRT_IMPORT(_iswxdigit_l, _NtCRT__iswxdigit_l);
NTCRT_IMPORT(_isctype_l, _NtCRT__isctype_l);
NTCRT_IMPORT(_iswctype_l, _NtCRT__iswctype_l);
NTCRT_IMPORT(_isleadbyte_l, _NtCRT__isleadbyte_l);
NTCRT_IMPORT(_tolower_l, _NtCRT__tolower_l);
NTCRT_IMPORT(_toupper_l, _NtCRT__toupper_l);
NTCRT_IMPORT(_towlower_l, _NtCRT__towlower_l);
NTCRT_IMPORT(_towupper_l, _NtCRT__towupper_l);
