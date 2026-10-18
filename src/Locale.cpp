/*
 * setlocale, msvcrt locale objects, the "_l" functions and the locale services of the runtime.
 * The locales are "C" and "C.UTF-8", see NtCRT.h. The "_l" functions call the functions without it, those
 * depending on the multibyte characters run by the code page of the locale with NTCRT_LOCALE_SCOPE.
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

EXTERN_C char* __cdecl _NtCRT_setlocale(int category, const char* locale);
EXTERN_C struct lconv* __cdecl _NtCRT_localeconv(void);
EXTERN_C BOOL _NtCRT_ConsoleScanLineA(char* Line, DWORD Size);
EXTERN_C BOOL _NtCRT_ConsoleScanLineW(PWSTR Line, DWORD Count);
EXTERN_C PVOID _NtCRT_GetKernel32Function(PCSTR Name);
EXTERN_C int __cdecl _NtCRT__getmbcp(void);
extern "C" unsigned short* _NtCRT_pctype;
extern "C" unsigned int _NtCRT_lc_codepage;
extern "C" int _NtCRT___setlc_active;
extern "C" int _NtCRT___unguarded_readlc_active;
EXTERN_C double __cdecl _NtCRT__wtof(const wchar_t*);
EXTERN_C double __cdecl _NtCRT_atof(const char*);
EXTERN_C double __cdecl _NtCRT_strtod(const char*, char**);
EXTERN_C double __cdecl _NtCRT_wcstod(const wchar_t*, wchar_t**);
EXTERN_C errno_t __cdecl _NtCRT__strlwr_s(char*, size_t);
EXTERN_C errno_t __cdecl _NtCRT__strupr_s(char*, size_t);
EXTERN_C errno_t __cdecl _NtCRT__wcslwr_s(wchar_t*, size_t);
EXTERN_C errno_t __cdecl _NtCRT__wcsupr_s(wchar_t*, size_t);
EXTERN_C errno_t __cdecl _NtCRT_mbstowcs_s(size_t*, wchar_t*, size_t, const char*, size_t);
EXTERN_C errno_t __cdecl _NtCRT_wcstombs_s(size_t*, char*, size_t, const wchar_t*, size_t);
EXTERN_C errno_t __cdecl _NtCRT_wctomb_s(int*, char*, size_t, wchar_t);
EXTERN_C int __cdecl _NtCRT_ConsoleVScanA(const char*, va_list);
EXTERN_C int __cdecl _NtCRT_ConsoleVScanAS(const char*, va_list);
EXTERN_C int __cdecl _NtCRT_ConsoleVScanW(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_ConsoleVScanWS(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT__atodbl(_CRT_DOUBLE*, char*);
EXTERN_C int __cdecl _NtCRT__atoflt(_CRT_FLOAT*, char*);
EXTERN_C int __cdecl _NtCRT__atoldbl(_LDOUBLE*, char*);
EXTERN_C int __cdecl _NtCRT__isctype(int, int);
EXTERN_C int __cdecl _NtCRT__stricoll(const char*, const char*);
EXTERN_C int __cdecl _NtCRT__strncoll(const char*, const char*, size_t);
EXTERN_C int __cdecl _NtCRT__strnicoll(const char*, const char*, size_t);
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
EXTERN_C int __cdecl _NtCRT__wcsicoll(const wchar_t*, const wchar_t*);
EXTERN_C int __cdecl _NtCRT__wcsncoll(const wchar_t*, const wchar_t*, size_t);
EXTERN_C int __cdecl _NtCRT__wcsnicoll(const wchar_t*, const wchar_t*, size_t);
EXTERN_C int __cdecl _NtCRT_fprintf();
EXTERN_C int __cdecl _NtCRT_fscanf();
EXTERN_C int __cdecl _NtCRT_fwscanf();
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
EXTERN_C int __cdecl _NtCRT_mblen(const char*, size_t);
EXTERN_C int __cdecl _NtCRT_mbtowc(wchar_t*, const char*, size_t);
EXTERN_C int __cdecl _NtCRT_printf();
EXTERN_C int __cdecl _NtCRT_scanf();
EXTERN_C int __cdecl _NtCRT_sprintf();
EXTERN_C int __cdecl _NtCRT_sscanf();
EXTERN_C int __cdecl _NtCRT_strcoll(const char*, const char*);
EXTERN_C int __cdecl _NtCRT_swprintf();
EXTERN_C int __cdecl _NtCRT_swscanf();
EXTERN_C int __cdecl _NtCRT_tolower(int);
EXTERN_C int __cdecl _NtCRT_toupper(int);
EXTERN_C int __cdecl _NtCRT_vfprintf(FILE*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vfprintf_s(FILE*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vfscanf(FILE*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vfscanf_s(FILE*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vfwprintf(FILE*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vfwprintf_s(FILE*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vfwscanf(FILE*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vfwscanf_s(FILE*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vprintf(const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vprintf_s(const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vscanf(const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vscanf_s(const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vsprintf(char*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vsprintf_s(char*, size_t, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vsscanf(const char*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vsscanf_s(const char*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vswprintf();
EXTERN_C int __cdecl _NtCRT_vswprintf_s(wchar_t*, size_t, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vswscanf(const wchar_t*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vswscanf_s(const wchar_t*, const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vwprintf(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vwprintf_s(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vwscanf(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_vwscanf_s(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_wcscoll(const wchar_t*, const wchar_t*);
EXTERN_C int __cdecl _NtCRT_wctomb(char*, wchar_t);
EXTERN_C int __cdecl _NtCRT_wprintf();
EXTERN_C int __cdecl _NtCRT_wscanf();
EXTERN_C intmax_t __cdecl _NtCRT_strtoimax(const char*, char**, int);
EXTERN_C long __cdecl _NtCRT_strtol(const char*, char**, int);
EXTERN_C long __cdecl _NtCRT_wcstol(const wchar_t*, wchar_t**, int);
EXTERN_C long long __cdecl _NtCRT_atoll(const char*);
EXTERN_C long long __cdecl _NtCRT_strtoll(const char*, char**, int);
EXTERN_C long long __cdecl _NtCRT_wcstoll(const wchar_t*, wchar_t**, int);
EXTERN_C size_t __cdecl _NtCRT_mbstowcs(wchar_t*, const char*, size_t);
EXTERN_C size_t __cdecl _NtCRT_strxfrm(char*, const char*, size_t);
EXTERN_C size_t __cdecl _NtCRT_wcsftime(wchar_t*, size_t, const wchar_t*, const struct tm*);
EXTERN_C size_t __cdecl _NtCRT_wcstombs(char*, const wchar_t*, size_t);
EXTERN_C size_t __cdecl _NtCRT_wcsxfrm(wchar_t*, const wchar_t*, size_t);
EXTERN_C uintmax_t __cdecl _NtCRT_strtoumax(const char*, char**, int);
EXTERN_C unsigned long __cdecl _NtCRT_strtoul(const char*, char**, int);
EXTERN_C unsigned long __cdecl _NtCRT_wcstoul(const wchar_t*, wchar_t**, int);
EXTERN_C unsigned long long __cdecl _NtCRT_strtoull(const char*, char**, int);
EXTERN_C unsigned long long __cdecl _NtCRT_wcstoull(const wchar_t*, wchar_t**, int);
EXTERN_C wint_t __cdecl _NtCRT_towlower(wint_t);
EXTERN_C wint_t __cdecl _NtCRT_towupper(wint_t);

#pragma region Locale state

static char g_NameC[] = "C";
static char g_NameUtf8[] = "C.UTF-8";
static const char* const g_CategoryNames[LC_MAX + 1] = {
    "LC_ALL", "LC_COLLATE", "LC_CTYPE", "LC_MONETARY", "LC_NUMERIC", "LC_TIME",
};

/* Whether each category of the current locale is "C.UTF-8", LC_ALL is unused */
static BOOL g_Utf8[LC_MAX + 1];
static char g_AllName[96];
static wchar_t g_WideName[96];
static unsigned short g_CtypeUtf8[257];
static unsigned char g_LowerMap[256];
static unsigned char g_UpperMap[256];
static volatile LONG g_LocaleScopes;

extern "C" int _NtCRT_mb_cur_max;
extern "C" unsigned short _NtCRT_ctype[257];

EXTERN_C UINT _NtCRT_CurrentCodePage(void)
{
    if (g_LocaleScopes != 0)
    {
        unsigned int CodePage = _NtCRT_GetPtd()->LocaleCodePage;

        if (CodePage != 0)
        {
            return CodePage - 1;
        }
    }
    return _NtCRT_lc_codepage;
}

EXTERN_C UINT _NtCRT_LocaleCodePage(_locale_t Locale)
{
    return Locale != NULL ? Locale->locinfo->lc_codepage : _NtCRT_CurrentCodePage();
}

EXTERN_C UINT _NtCRT_FileCodePage(void)
{
    return _NtCRT_lc_codepage == CP_UTF8 ? CP_UTF8 : _NtCRT_CodePage();
}

NTCRT_LOCALE_SCOPE::NTCRT_LOCALE_SCOPE(_locale_t Locale)
{
    m_Ptd = NULL;
    if (Locale != NULL)
    {
        m_Ptd = _NtCRT_GetPtd();
        m_Saved = m_Ptd->LocaleCodePage;
        _InterlockedIncrement(&g_LocaleScopes);
        m_Ptd->LocaleCodePage = Locale->locinfo->lc_codepage + 1;
    }
}

NTCRT_LOCALE_SCOPE::~NTCRT_LOCALE_SCOPE()
{
    if (m_Ptd != NULL)
    {
        m_Ptd->LocaleCodePage = m_Saved;
        _InterlockedDecrement(&g_LocaleScopes);
    }
}

/*
 * Parse the locale name of one category: 1 for "C.UTF-8", 0 for "C", -1 if not supported.
 * "" is "C.UTF-8" if the ANSI code page is UTF-8, else "C". Only "C" and "POSIX" can have a code set, as
 * ".UTF-8" and "C.UTF-8", other language names are not supported.
 */
static
int
_NtCRT_ParseLocaleName(
    _In_reads_(Length) const char* Name,
    _In_ size_t Length)
{
    const char* Dot = (const char*)memchr(Name, '.', Length);
    size_t Base = Dot != NULL ? (size_t)(Dot - Name) : Length;
    size_t SetLength;

    if (Length == 0)
    {
        return _NtCRT_CodePage() == CP_UTF8;
    }
    if (!(Base == 0 || (Base == 1 && Name[0] == 'C') || (Base == 5 && memcmp(Name, "POSIX", 5) == 0)))
    {
        return -1;
    }
    if (Dot == NULL)
    {
        return 0;
    }
    Dot++;
    SetLength = Length - Base - 1;
    if ((SetLength == 5 && _strnicmp(Dot, "UTF-8", 5) == 0) || (SetLength == 4 && _strnicmp(Dot, "UTF8", 4) == 0) ||
        (SetLength == 5 && memcmp(Dot, "65001", 5) == 0))
    {
        return 1;
    }
    return -1;
}

/* Parse a locale name of setlocale or _create_locale into Utf8, FALSE if not supported */
static
BOOL
_NtCRT_ParseLocale(
    _In_ int Category,
    _In_z_ const char* Name,
    _Inout_updates_(LC_MAX + 1) BOOL* Utf8)
{
    BOOL New[LC_MAX + 1];
    int Value;

    memcpy(New, Utf8, sizeof(New));
    if (Category == LC_ALL && strncmp(Name, "LC_", 3) == 0)
    {
        /* "LC_COLLATE=C;LC_CTYPE=C.UTF-8;...", as setlocale(LC_ALL, NULL) returns for mixed categories */
        while (*Name != '\0')
        {
            const char* Equal = strchr(Name, '=');
            const char* End;
            int i;

            if (Equal == NULL)
            {
                return FALSE;
            }
            for (i = LC_COLLATE; i <= LC_MAX; i++)
            {
                size_t Length = strlen(g_CategoryNames[i]);

                if ((size_t)(Equal - Name) == Length && memcmp(Name, g_CategoryNames[i], Length) == 0)
                {
                    break;
                }
            }
            if (i > LC_MAX)
            {
                return FALSE;
            }
            End = strchr(Equal + 1, ';');
            if (End == NULL)
            {
                End = Equal + 1 + strlen(Equal + 1);
            }
            Value = _NtCRT_ParseLocaleName(Equal + 1, (size_t)(End - Equal - 1));
            if (Value < 0)
            {
                return FALSE;
            }
            New[i] = Value;
            Name = *End == ';' ? End + 1 : End;
        }
    } else
    {
        Value = _NtCRT_ParseLocaleName(Name, strlen(Name));
        if (Value < 0)
        {
            return FALSE;
        }
        for (int i = LC_COLLATE; i <= LC_MAX; i++)
        {
            if (Category == LC_ALL || Category == i)
            {
                New[i] = Value;
            }
        }
    }
    memcpy(Utf8, New, sizeof(New));
    return TRUE;
}

/* The name of a category, or of all categories in the format accepted by _NtCRT_ParseLocale */
static
char*
_NtCRT_LocaleName(
    _In_ int Category,
    _In_reads_(LC_MAX + 1) const BOOL* Utf8,
    _Out_writes_(96) char* Buffer)
{
    BOOL Same = TRUE;

    if (Category != LC_ALL)
    {
        return Utf8[Category] ? g_NameUtf8 : g_NameC;
    }
    for (int i = LC_COLLATE + 1; i <= LC_MAX; i++)
    {
        Same &= Utf8[i] == Utf8[LC_COLLATE];
    }
    if (Same)
    {
        return Utf8[LC_COLLATE] ? g_NameUtf8 : g_NameC;
    }
    Buffer[0] = '\0';
    for (int i = LC_COLLATE; i <= LC_MAX; i++)
    {
        strcat(Buffer, g_CategoryNames[i]);
        strcat(Buffer, "=");
        strcat(Buffer, Utf8[i] ? g_NameUtf8 : g_NameC);
        if (i != LC_MAX)
        {
            strcat(Buffer, ";");
        }
    }
    return Buffer;
}

/* The ctype table of "C.UTF-8", bytes that can start a multibyte character are lead bytes */
static
unsigned short*
_NtCRT_CtypeUtf8(void)
{
    if (g_CtypeUtf8['A' + 1] == 0)
    {
        g_CtypeUtf8[0] = 0;
        for (int i = 0; i < 256; i++)
        {
            g_CtypeUtf8[i + 1] = i < 0x80 ? _NtCRT_ctype[i + 1] : (i >= 0xC2 && i <= 0xF4 ? _LEADBYTE : 0);
        }
    }
    return g_CtypeUtf8 + 1;
}

EXTERN_C char* __cdecl _NtCRT_setlocale(int category, const char* locale)
{
    char* Name = NULL;

    if (category < LC_ALL || category > LC_MAX)
    {
        return NULL;
    }
    _NtCRT_lock(_SETLOCALE_LOCK);
    if (locale != NULL)
    {
        if (!_NtCRT_ParseLocale(category, locale, g_Utf8))
        {
            goto _Exit;
        }
        _NtCRT_lc_codepage = g_Utf8[LC_CTYPE] ? CP_UTF8 : 0;
        _NtCRT_mb_cur_max = g_Utf8[LC_CTYPE] ? 4 : 1;
        _NtCRT_pctype = g_Utf8[LC_CTYPE] ? _NtCRT_CtypeUtf8() : _NtCRT_ctype + 1;
    }
    Name = _NtCRT_LocaleName(category, g_Utf8, g_AllName);
_Exit:
    _NtCRT_unlock(_SETLOCALE_LOCK);
    return Name;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wsetlocale(int category, const wchar_t* locale)
{
    char Locale[96], *Name;
    size_t i;

    if (locale != NULL)
    {
        for (i = 0; locale[i] != L'\0'; i++)
        {
            if (locale[i] >= 0x80 || i + 1 >= sizeof(Locale))
            {
                return NULL;
            }
            Locale[i] = (char)locale[i];
        }
        Locale[i] = '\0';
    }
    Name = _NtCRT_setlocale(category, locale != NULL ? Locale : NULL);
    if (Name == NULL)
    {
        return NULL;
    }
    for (i = 0; Name[i] != '\0'; i++)
    {
        g_WideName[i] = (unsigned char)Name[i];
    }
    g_WideName[i] = L'\0';
    return g_WideName;
}

#pragma endregion

#pragma region Locale objects

/* A locale object with its own locale information and multibyte code page */
typedef struct _NTCRT_LOCALE
{
    _locale_tstruct Locale;
    threadlocinfo LocInfo;
    NTCRT_MBCINFO MbcInfo;
    char AllName[96];
} NTCRT_LOCALE, *PNTCRT_LOCALE;

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
    Locale->LocInfo.pctype = Utf8[LC_CTYPE] ? _NtCRT_CtypeUtf8() : _NtCRT_ctype + 1;
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
    _locale_t Locale;

    _NtCRT_lock(_SETLOCALE_LOCK);
    memcpy(Utf8, g_Utf8, sizeof(Utf8));
    _NtCRT_unlock(_SETLOCALE_LOCK);
    Locale = _NtCRT_NewLocale(Utf8, TRUE);
    return Locale;
}

EXTERN_C void __cdecl _NtCRT__free_locale(_locale_t locale)
{
    if (locale != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, CONTAINING_RECORD(locale, NTCRT_LOCALE, Locale));
    }
}

#pragma endregion

#pragma region Locale state of the runtime

extern "C"
{
    unsigned int _NtCRT___lc_collate_cp = 0;
    LCID _NtCRT___lc_handle[LC_MAX + 1] = { 0 };
}

EXTERN_C unsigned int __cdecl _NtCRT____lc_collate_cp_func(void)
{
    return _NtCRT___lc_collate_cp;
}

EXTERN_C LCID* __cdecl _NtCRT____lc_handle_func(void)
{
    return _NtCRT___lc_handle;
}

EXTERN_C int __cdecl _NtCRT____setlc_active_func(void)
{
    return _NtCRT___setlc_active;
}

EXTERN_C int* __cdecl _NtCRT____unguarded_readlc_active_add_func(void)
{
    return &_NtCRT___unguarded_readlc_active;
}

/* Recorded only, exponents of printf always have two digits as C99 */

static unsigned int g_OutputFormat;

EXTERN_C unsigned int __cdecl _NtCRT__get_output_format(void)
{
    return g_OutputFormat;
}

EXTERN_C unsigned int __cdecl _NtCRT__set_output_format(unsigned int format)
{
    unsigned int Old = g_OutputFormat;

    g_OutputFormat = format;
    return Old;
}

#pragma endregion

#pragma region Helpers of the "_l" functions

/* msvcrt has the "_l" form only */
EXTERN_C int __cdecl _NtCRT__atoflt(_CRT_FLOAT* value, char* str)
{
    _CRT_DOUBLE Double;
    int Ret = _NtCRT__atodbl(&Double, str);

    value->f = (float)Double.x;
    if (Ret == 0 && isinf(value->f) && !isinf(Double.x))
    {
        Ret = _OVERFLOW;
    } else if (Ret == 0 && value->f == 0 && Double.x != 0)
    {
        Ret = _UNDERFLOW;
    }
    return Ret;
}

/* Console scanning reads a line and scans it, as _cscanf */

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

#pragma endregion

#pragma region "_l" functions

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

EXTERN_C size_t __cdecl _NtCRT__wcsftime_l(
    wchar_t* buffer, size_t size, const wchar_t* format, const struct tm* tm, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wcsftime(buffer, size, format, tm);
}

EXTERN_C int __cdecl _NtCRT__mblen_l(const char* s, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_mblen(s, count);
}

EXTERN_C int __cdecl _NtCRT__mbtowc_l(wchar_t* pwc, const char* s, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_mbtowc(pwc, s, count);
}

EXTERN_C size_t __cdecl _NtCRT__mbstowcs_l(wchar_t* dst, const char* src, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_mbstowcs(dst, src, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbstowcs_s_l(
    size_t* converted, wchar_t* dst, size_t size, const char* src, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_mbstowcs_s(converted, dst, size, src, count);
}

EXTERN_C size_t __cdecl _NtCRT__wcstombs_l(char* dst, const wchar_t* src, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wcstombs(dst, src, count);
}

EXTERN_C errno_t __cdecl _NtCRT__wcstombs_s_l(
    size_t* converted, char* dst, size_t size, const wchar_t* src, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wcstombs_s(converted, dst, size, src, count);
}

EXTERN_C int __cdecl _NtCRT__wctomb_l(char* s, wchar_t wc, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wctomb(s, wc);
}

EXTERN_C errno_t __cdecl _NtCRT__wctomb_s_l(int* size, char* s, size_t max, wchar_t wc, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wctomb_s(size, s, max, wc);
}

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

#pragma endregion


#pragma region National language support of the runtime

typedef int(WINAPI* PNTCRT_COMPARE_STRING_W)(
    LCID Locale, DWORD dwCmpFlags, PCNZWCH lpString1, int cchCount1, PCNZWCH lpString2, int cchCount2);
typedef int(WINAPI* PNTCRT_LCMAP_STRING_W)(
    LCID Locale, DWORD dwMapFlags, LPCWSTR lpSrcStr, int cchSrc, LPWSTR lpDestStr, int cchDest);
typedef int(WINAPI* PNTCRT_GET_LOCALE_INFO_W)(LCID Locale, LCTYPE LCType, LPWSTR lpLCData, int cchData);

EXTERN_C BOOL WINAPI _CRT_GetCPInfo(UINT CodePage, LPCPINFO lpCPInfo);
EXTERN_C BOOL WINAPI _CRT_GetStringTypeW(DWORD dwInfoType, LPCWCH lpSrcStr, int cchSrc, LPWORD lpCharType);

/* Characters before the terminator within count */

static
int
_NtCRT_StrNCount(
    _In_reads_(Count) const char* String,
    _In_ int Count)
{
    int n = 0;

    while (n < Count && String[n] != '\0')
    {
        n++;
    }
    return n;
}

static
int
_NtCRT_WcsNCount(
    _In_reads_(Count) const wchar_t* String,
    _In_ int Count)
{
    int n = 0;

    while (n < Count && String[n] != L'\0')
    {
        n++;
    }
    return n;
}

/* A multibyte string converted to a heap buffer, the count includes the terminator if Count is -1 */
static
PWSTR
_NtCRT_ToWide(
    _In_ UINT CodePage,
    _In_ DWORD Flags,
    _In_reads_(Count) const char* String,
    _In_ int Count,
    _Out_ int* WideCount)
{
    int n = _NtCRT_MultiByteToWideChar(CodePage, Flags, String, Count, NULL, 0);
    PWSTR Wide;

    if (n <= 0)
    {
        return NULL;
    }
    Wide = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, n * sizeof(WCHAR));
    if (Wide == NULL)
    {
        return NULL;
    }
    if (_NtCRT_MultiByteToWideChar(CodePage, Flags, String, Count, Wide, n) != n)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Wide);
        return NULL;
    }
    *WideCount = n;
    return Wide;
}

/* Whether the first byte of String is a lead byte of the code page */
static
BOOL
_NtCRT_IsLeadByteOf(
    _In_ const CPINFO* CpInfo,
    _In_ unsigned char c)
{
    for (int i = 0; i + 1 < MAX_LEADBYTES && CpInfo->LeadByte[i] != 0; i += 2)
    {
        if (c >= CpInfo->LeadByte[i] && c <= CpInfo->LeadByte[i + 1])
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* CompareStringA by CompareStringW, returns CSTR_* or 0 on errors */
EXTERN_C int __cdecl _NtCRT___crtCompareStringA(
    LCID Locale, DWORD Flags, LPCSTR Str1, int Count1, LPCSTR Str2, int Count2, int CodePage)
{
    auto const CompareStringW = (PNTCRT_COMPARE_STRING_W)_NtCRT_GetKernel32Function("CompareStringW");
    PWSTR Wide1, Wide2;
    int WideCount1, WideCount2, Ret;
    CPINFO CpInfo;

    if (Count1 > 0)
    {
        Count1 = _NtCRT_StrNCount(Str1, Count1);
    } else if (Count1 < -1)
    {
        return 0;
    }
    if (Count2 > 0)
    {
        Count2 = _NtCRT_StrNCount(Str2, Count2);
    } else if (Count2 < -1)
    {
        return 0;
    }
    if (CodePage == 0)
    {
        CodePage = (int)_NtCRT_lc_codepage;
    }

    /* An empty string, a lone lead byte compares equal to it */
    if (Count1 == 0 || Count2 == 0)
    {
        if (Count1 == Count2)
        {
            return CSTR_EQUAL;
        }
        if (Count2 > 1)
        {
            return CSTR_LESS_THAN;
        }
        if (Count1 > 1)
        {
            return CSTR_GREATER_THAN;
        }
        if (!_CRT_GetCPInfo(CodePage, &CpInfo))
        {
            return 0;
        }
        if (Count1 > 0)
        {
            return CpInfo.MaxCharSize >= 2 && _NtCRT_IsLeadByteOf(&CpInfo, (unsigned char)*Str1) ? CSTR_EQUAL
                                                                                                 : CSTR_GREATER_THAN;
        }
        if (Count2 > 0)
        {
            return CpInfo.MaxCharSize >= 2 && _NtCRT_IsLeadByteOf(&CpInfo, (unsigned char)*Str2) ? CSTR_EQUAL
                                                                                                 : CSTR_LESS_THAN;
        }
    }

    if (CompareStringW == NULL)
    {
        return 0;
    }
    Wide1 = _NtCRT_ToWide(CodePage, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, Str1, Count1, &WideCount1);
    if (Wide1 == NULL)
    {
        return 0;
    }
    Wide2 = _NtCRT_ToWide(CodePage, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, Str2, Count2, &WideCount2);
    if (Wide2 == NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Wide1);
        return 0;
    }
    Ret = CompareStringW(Locale, Flags, Wide1, WideCount1, Wide2, WideCount2);
    RtlFreeHeap(NTCRT_HEAP, 0, Wide2);
    RtlFreeHeap(NTCRT_HEAP, 0, Wide1);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT___crtCompareStringW(
    LCID Locale, DWORD Flags, LPCWSTR Str1, int Count1, LPCWSTR Str2, int Count2, int CodePage)
{
    auto const CompareStringW = (PNTCRT_COMPARE_STRING_W)_NtCRT_GetKernel32Function("CompareStringW");

    UNREFERENCED_PARAMETER(CodePage);
    if (Count1 > 0)
    {
        Count1 = _NtCRT_WcsNCount(Str1, Count1);
    }
    if (Count2 > 0)
    {
        Count2 = _NtCRT_WcsNCount(Str2, Count2);
    }
    if (Count1 == 0 || Count2 == 0)
    {
        return Count1 == Count2 ? CSTR_EQUAL : (Count1 < Count2 ? CSTR_LESS_THAN : CSTR_GREATER_THAN);
    }
    return CompareStringW != NULL ? CompareStringW(Locale, Flags, Str1, Count1, Str2, Count2) : 0;
}

/* LCMapStringA by LCMapStringW, a sort key is written as bytes */
EXTERN_C int __cdecl _NtCRT___crtLCMapStringA(
    LCID Locale, DWORD Flags, LPCSTR Src, int SrcCount, LPSTR Dst, int DstCount, int CodePage, BOOL Error)
{
    auto const LCMapStringW = (PNTCRT_LCMAP_STRING_W)_NtCRT_GetKernel32Function("LCMapStringW");
    PWSTR Wide, Mapped;
    int WideCount, Ret;

    if (SrcCount > 0)
    {
        int n = _NtCRT_StrNCount(Src, SrcCount);

        /* The terminator within the count is included */
        SrcCount = n < SrcCount ? n + 1 : n;
    }
    if (CodePage == 0)
    {
        CodePage = (int)_NtCRT_lc_codepage;
    }
    if (LCMapStringW == NULL)
    {
        return 0;
    }
    Wide = _NtCRT_ToWide(
        CodePage, Error ? MB_PRECOMPOSED | MB_ERR_INVALID_CHARS : MB_PRECOMPOSED, Src, SrcCount, &WideCount);
    if (Wide == NULL)
    {
        return 0;
    }
    Ret = LCMapStringW(Locale, Flags, Wide, WideCount, NULL, 0);
    if (Ret == 0)
    {
        goto Exit;
    }
    if (Flags & LCMAP_SORTKEY)
    {
        if (DstCount != 0)
        {
            Ret = Ret > DstCount ? 0 : LCMapStringW(Locale, Flags, Wide, WideCount, (LPWSTR)Dst, DstCount);
        }
        goto Exit;
    }
    Mapped = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, Ret * sizeof(WCHAR));
    if (Mapped == NULL)
    {
        Ret = 0;
        goto Exit;
    }
    Ret = LCMapStringW(Locale, Flags, Wide, WideCount, Mapped, Ret);
    if (Ret != 0)
    {
        Ret = _NtCRT_WideCharToMultiByte(CodePage, 0, Mapped, Ret, DstCount != 0 ? Dst : NULL, DstCount, NULL, NULL);
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Mapped);
Exit:
    RtlFreeHeap(NTCRT_HEAP, 0, Wide);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT___crtLCMapStringW(
    LCID Locale, DWORD Flags, LPCWSTR Src, int SrcCount, LPWSTR Dst, int DstCount, int CodePage)
{
    auto const LCMapStringW = (PNTCRT_LCMAP_STRING_W)_NtCRT_GetKernel32Function("LCMapStringW");

    UNREFERENCED_PARAMETER(CodePage);
    if (SrcCount > 0)
    {
        SrcCount = _NtCRT_WcsNCount(Src, SrcCount);
    }
    return LCMapStringW != NULL ? LCMapStringW(Locale, Flags, Src, SrcCount, Dst, DstCount) : 0;
}

EXTERN_C int __cdecl _NtCRT___crtGetLocaleInfoW(LCID Locale, LCTYPE Type, LPWSTR Data, int Count, int CodePage)
{
    auto const GetLocaleInfoW = (PNTCRT_GET_LOCALE_INFO_W)_NtCRT_GetKernel32Function("GetLocaleInfoW");

    UNREFERENCED_PARAMETER(CodePage);
    return GetLocaleInfoW != NULL ? GetLocaleInfoW(Locale, Type, Data, Count) : 0;
}

EXTERN_C BOOL __cdecl _NtCRT___crtGetStringTypeW(
    DWORD InfoType, LPCWSTR Src, int SrcCount, LPWORD Types, int CodePage, int Lcid)
{
    UNREFERENCED_PARAMETER(CodePage);
    UNREFERENCED_PARAMETER(Lcid);
    return _CRT_GetStringTypeW(InfoType, Src, SrcCount, Types);
}

#pragma endregion

NTCRT_IMPORT(_create_locale, _NtCRT__create_locale);
NTCRT_IMPORT(_get_current_locale, _NtCRT__get_current_locale);
NTCRT_IMPORT(_free_locale, _NtCRT__free_locale);
NTCRT_IMPORT(___lc_collate_cp_func, _NtCRT____lc_collate_cp_func);
NTCRT_IMPORT(___lc_handle_func, _NtCRT____lc_handle_func);
NTCRT_IMPORT(___setlc_active_func, _NtCRT____setlc_active_func);
NTCRT_IMPORT(___unguarded_readlc_active_add_func, _NtCRT____unguarded_readlc_active_add_func);
NTCRT_IMPORT(_get_output_format, _NtCRT__get_output_format);
NTCRT_IMPORT(_set_output_format, _NtCRT__set_output_format);
NTCRT_IMPORT(__crtCompareStringA, _NtCRT___crtCompareStringA);
NTCRT_IMPORT(__crtCompareStringW, _NtCRT___crtCompareStringW);
NTCRT_IMPORT(__crtLCMapStringA, _NtCRT___crtLCMapStringA);
NTCRT_IMPORT(__crtLCMapStringW, _NtCRT___crtLCMapStringW);
NTCRT_IMPORT(__crtGetLocaleInfoW, _NtCRT___crtGetLocaleInfoW);
NTCRT_IMPORT(__crtGetStringTypeW, _NtCRT___crtGetStringTypeW);
NTCRT_IMPORT_DATA(__lc_collate_cp, _NtCRT___lc_collate_cp);
NTCRT_IMPORT_DATA(__lc_handle, _NtCRT___lc_handle);
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
NTCRT_IMPORT(_wcsftime_l, _NtCRT__wcsftime_l);
NTCRT_IMPORT(_mblen_l, _NtCRT__mblen_l);
NTCRT_IMPORT(_mbtowc_l, _NtCRT__mbtowc_l);
NTCRT_IMPORT(setlocale, _NtCRT_setlocale);
NTCRT_IMPORT(_wsetlocale, _NtCRT__wsetlocale);
NTCRT_IMPORT(_mbstowcs_l, _NtCRT__mbstowcs_l);
NTCRT_IMPORT(_mbstowcs_s_l, _NtCRT__mbstowcs_s_l);
NTCRT_IMPORT(_wcstombs_l, _NtCRT__wcstombs_l);
NTCRT_IMPORT(_wcstombs_s_l, _NtCRT__wcstombs_s_l);
NTCRT_IMPORT(_wctomb_l, _NtCRT__wctomb_l);
NTCRT_IMPORT(_wctomb_s_l, _NtCRT__wctomb_s_l);
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
NTCRT_IMPORT(__ms_fprintf, _NtCRT_fprintf);
NTCRT_IMPORT(__ms_fscanf, _NtCRT_fscanf);
NTCRT_IMPORT(__ms_fwscanf, _NtCRT_fwscanf);
NTCRT_IMPORT(__ms_printf, _NtCRT_printf);
NTCRT_IMPORT(__ms_scanf, _NtCRT_scanf);
NTCRT_IMPORT(__ms_sprintf, _NtCRT_sprintf);
NTCRT_IMPORT(__ms_sscanf, _NtCRT_sscanf);
NTCRT_IMPORT(__ms_swprintf, _NtCRT_swprintf);
NTCRT_IMPORT(__ms_swscanf, _NtCRT_swscanf);
NTCRT_IMPORT(__ms_vfprintf, _NtCRT_vfprintf);
NTCRT_IMPORT(__ms_vprintf, _NtCRT_vprintf);
NTCRT_IMPORT(__ms_vsprintf, _NtCRT_vsprintf);
NTCRT_IMPORT(__ms_vswprintf, _NtCRT_vswprintf);
NTCRT_IMPORT(__ms_vwprintf, _NtCRT_vwprintf);
NTCRT_IMPORT(__ms_wprintf, _NtCRT_wprintf);
NTCRT_IMPORT(__ms_wscanf, _NtCRT_wscanf);
