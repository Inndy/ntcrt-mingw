/*
 * Locale objects of msvcrt: _create_locale, _get_current_locale and _free_locale, see Locale.cpp.
 */

#include "NtCRT.h"

#include <errno.h>
#include <locale.h>
#include <string.h>

EXTERN_C struct lconv* __cdecl _NtCRT_localeconv(void);

#pragma region Locale objects

/* A locale object with its own locale information and multibyte code page */
typedef struct _NTCRT_LOCALE
{
    _locale_tstruct Locale;
    threadlocinfo LocInfo;
    NTCRT_MBCINFO MbcInfo;
    char AllName[96];
} NTCRT_LOCALE, *PNTCRT_LOCALE;

static unsigned char g_LowerMap[256];
static unsigned char g_UpperMap[256];

static
_locale_t
_NtCRT_NewLocale(
    _In_reads_(LC_MAX + 1) const BOOL* Utf8,
    _In_ BOOL CurrentMbcs)
{
    PNTCRT_LOCALE Locale;

    if (g_LowerMap['A'] == 0)
    {
        for (int i = 0; i < 256; i++)
        {
            g_LowerMap[i] = (unsigned char)(i >= 'A' && i <= 'Z' ? i - 'A' + 'a' : i);
            g_UpperMap[i] = (unsigned char)(i >= 'a' && i <= 'z' ? i - 'a' + 'A' : i);
        }
    }
    Locale = (PNTCRT_LOCALE)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, sizeof(*Locale));
    if (Locale == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    for (int i = LC_COLLATE; i <= LC_MAX; i++)
    {
        Locale->LocInfo.lc_category[i].locale = _NtCRT_LocaleName(i, Utf8, NULL);
    }
    Locale->LocInfo.lc_category[LC_ALL].locale = _NtCRT_LocaleName(LC_ALL, Utf8, Locale->AllName);
    Locale->LocInfo.refcount = 1;
    Locale->LocInfo.lc_codepage = Utf8[LC_CTYPE] ? CP_UTF8 : 0;
    Locale->LocInfo.lc_clike = 1;
    Locale->LocInfo.mb_cur_max = Utf8[LC_CTYPE] ? 4 : 1;
    Locale->LocInfo.lconv = _NtCRT_localeconv();
    Locale->LocInfo.pctype = Utf8[LC_CTYPE] ? _NtCRT_CtypeUtf8() : _NtCRT_ctype.Table + 1;
    Locale->LocInfo.pclmap = g_LowerMap;
    Locale->LocInfo.pcumap = g_UpperMap;
    _NtCRT_GetMbcInfo(&Locale->MbcInfo, CurrentMbcs);
    Locale->Locale.locinfo = &Locale->LocInfo;
    Locale->Locale.mbcinfo = (pthreadmbcinfo)&Locale->MbcInfo;
    return &Locale->Locale;
}

/* The categories not specified are the "C" locale, the multibyte code page of the "C" locale is single-byte */
EXTERN_C _locale_t __cdecl _NtCRT__create_locale(int category, const char* locale)
{
    BOOL Utf8[LC_MAX + 1] = { FALSE };

    if (category < LC_ALL || category > LC_MAX || locale == NULL || !_NtCRT_ParseLocale(category, locale, Utf8))
    {
        return NULL;
    }
    return _NtCRT_NewLocale(Utf8, FALSE);
}

/* The current locale and the multibyte code page set by _setmbcp */
EXTERN_C _locale_t __cdecl _NtCRT__get_current_locale(void)
{
    BOOL Utf8[LC_MAX + 1];

    _NtCRT_GetLocaleUtf8(Utf8);
    return _NtCRT_NewLocale(Utf8, TRUE);
}

EXTERN_C void __cdecl _NtCRT__free_locale(_locale_t locale)
{
    if (locale != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, CONTAINING_RECORD(locale, NTCRT_LOCALE, Locale));
    }
}

#pragma endregion

NTCRT_IMPORT(_create_locale, _NtCRT__create_locale);
NTCRT_IMPORT(_get_current_locale, _NtCRT__get_current_locale);
NTCRT_IMPORT(_free_locale, _NtCRT__free_locale);
