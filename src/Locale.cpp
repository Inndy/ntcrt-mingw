/*
 * setlocale and the locale services of the runtime. The locales are "C" and "C.UTF-8", see NtCRT.h. Locale
 * objects are in LocaleObject.cpp, the "_l" functions in Locale*.cpp and beside the functions they call.
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
EXTERN_C PVOID _NtCRT_GetKernel32Function(PCSTR Name);
extern "C" unsigned short* _NtCRT_pctype;
extern "C" unsigned int _NtCRT_lc_codepage;
extern "C"
{
    int _NtCRT___setlc_active = 0;
    int _NtCRT___unguarded_readlc_active = 0;
}

NTCRT_IMPORT_DATA(__setlc_active, _NtCRT___setlc_active);
NTCRT_IMPORT_DATA(__unguarded_readlc_active, _NtCRT___unguarded_readlc_active);
EXTERN_C int __cdecl _NtCRT_fprintf();
EXTERN_C int __cdecl _NtCRT_fscanf();
EXTERN_C int __cdecl _NtCRT_fwscanf();
EXTERN_C int __cdecl _NtCRT_printf();
EXTERN_C int __cdecl _NtCRT_scanf();
EXTERN_C int __cdecl _NtCRT_sprintf();
EXTERN_C int __cdecl _NtCRT_sscanf();
EXTERN_C int __cdecl _NtCRT_swprintf();
EXTERN_C int __cdecl _NtCRT_swscanf();
EXTERN_C int __cdecl _NtCRT_vfprintf(FILE*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vprintf(const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vsprintf(char*, const char*, va_list);
EXTERN_C int __cdecl _NtCRT_vswprintf();
EXTERN_C int __cdecl _NtCRT_vwprintf(const wchar_t*, va_list);
EXTERN_C int __cdecl _NtCRT_wprintf();
EXTERN_C int __cdecl _NtCRT_wscanf();

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

extern "C" int _NtCRT_mb_cur_max;

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
EXTERN_C
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
EXTERN_C
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
EXTERN_C
unsigned short*
_NtCRT_CtypeUtf8(void)
{
    if (g_CtypeUtf8['A' + 1] == 0)
    {
        g_CtypeUtf8[0] = 0;
        for (int i = 0; i < 256; i++)
        {
            g_CtypeUtf8[i + 1] = i < 0x80 ? _NtCRT_ctype.Table[i + 1] : (i >= 0xC2 && i <= 0xF4 ? _LEADBYTE : 0);
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
        _NtCRT_pctype = g_Utf8[LC_CTYPE] ? _NtCRT_CtypeUtf8() : _NtCRT_ctype.Table + 1;
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

/* The categories of the current locale that are "C.UTF-8" */
EXTERN_C void _NtCRT_GetLocaleUtf8(BOOL* Utf8)
{
    _NtCRT_lock(_SETLOCALE_LOCK);
    memcpy(Utf8, g_Utf8, (LC_MAX + 1) * sizeof(BOOL));
    _NtCRT_unlock(_SETLOCALE_LOCK);
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

/* The first argument of the msvcrt.dll export is unused, GetStringTypeW follows */
EXTERN_C BOOL __cdecl _NtCRT___crtGetStringTypeW(DWORD Unused, DWORD InfoType, LPCWSTR Src, int SrcCount, LPWORD Types)
{
    UNREFERENCED_PARAMETER(Unused);
    return _CRT_GetStringTypeW(InfoType, Src, SrcCount, Types);
}

#pragma endregion

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
NTCRT_IMPORT(setlocale, _NtCRT_setlocale);
NTCRT_IMPORT(_wsetlocale, _NtCRT__wsetlocale);
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
