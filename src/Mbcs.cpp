/*
 * msvcrt multibyte character (MBCS) functions of the multibyte code page set by _setmbcp, the ANSI code page at
 * startup. The "_l" functions use the multibyte code page of their locale object, the current one if it is NULL.
 */

#include "NtCRT.h"

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <mbctype.h>
#include <mbstring.h>
#include <stdlib.h>
#include <string.h>

#include "SplitPath.inl"

EXTERN_C BOOL WINAPI _CRT_GetCPInfo(UINT CodePage, LPCPINFO lpCPInfo);
EXTERN_C UINT WINAPI _K32_GetACP(VOID);
EXTERN_C UINT WINAPI _K32_GetOEMCP(VOID);
EXTERN_C BOOL WINAPI _CRT_GetStringTypeW(DWORD dwInfoType, LPCWCH lpSrcStr, int cchSrc, LPWORD lpCharType);
EXTERN_C int __cdecl _NtCRT___crtCompareStringA(
    LCID Locale, DWORD Flags, LPCSTR Str1, int Count1, LPCSTR Str2, int Count2, int CodePage);
EXTERN_C int __cdecl _NtCRT_strcoll(const char* s1, const char* s2);
EXTERN_C int __cdecl _NtCRT__stricoll(const char* s1, const char* s2);
EXTERN_C int __cdecl _NtCRT__strncoll(const char* s1, const char* s2, size_t count);
EXTERN_C int __cdecl _NtCRT__strnicoll(const char* s1, const char* s2, size_t count);
EXTERN_C char* __cdecl _NtCRT__strrev(char* str);
EXTERN_C char* __cdecl _NtCRT__strset(char* str, int c);
EXTERN_C char* __cdecl _NtCRT__strnset(char* str, int c, size_t count);
EXTERN_C char* __cdecl _NtCRT__strdup(const char* str);
EXTERN_C char* __cdecl _NtCRT_strtok_s(char* str, const char* delim, char** context);
EXTERN_C errno_t __cdecl _NtCRT_strcpy_s(char* dst, size_t size, const char* src);
EXTERN_C errno_t __cdecl _NtCRT_strcat_s(char* dst, size_t size, const char* src);
EXTERN_C errno_t __cdecl _NtCRT_strncpy_s(char* dst, size_t size, const char* src, size_t count);
EXTERN_C errno_t __cdecl _NtCRT_strncat_s(char* dst, size_t size, const char* src, size_t count);
EXTERN_C errno_t __cdecl _NtCRT__strset_s(char* str, size_t size, int c);
EXTERN_C errno_t __cdecl _NtCRT__strnset_s(char* str, size_t size, int c, size_t count);
EXTERN_C errno_t __cdecl _NtCRT__strlwr_s(char* str, size_t size);
EXTERN_C errno_t __cdecl _NtCRT__strupr_s(char* str, size_t size);
extern "C" unsigned short* _NtCRT_pctype;
extern "C" unsigned int _NtCRT_lc_codepage;

typedef unsigned char UCHAR_T;

#define NTCRT_KANJI_CP 932

#define NTCRT_VALIDATE(Expr, Ret) \
    do \
    { \
        if (!(Expr)) \
        { \
            _NtCRT_InvalidParameter(EINVAL); \
            return Ret; \
        } \
    } while (0)

#pragma region Multibyte code page

extern "C"
{
    unsigned char _NtCRT_mbctype[257];
    unsigned char _NtCRT_mbcasemap[256];
}

static int g_MbCodePage;
static int g_IsMbCodePage;
static int g_MbLcid;
static unsigned short g_MbUlInfo[6];

/* The multibyte code page used by a function */
typedef struct _NTCRT_MBC
{
    int CodePage;
    int IsMb;
    LCID Lcid;
    const unsigned char* Type; /* Type[c + 1] */
    const unsigned char* CaseMap;
    const unsigned short* UlInfo; /* Ranges of double-byte upper case letters and their differences to lower case */
} NTCRT_MBC;

static
NTCRT_MBC
_NtCRT_Mbc(
    _In_opt_ _locale_t Locale)
{
    const NTCRT_MBCINFO* Info;

    if (Locale == NULL || Locale->mbcinfo == NULL)
    {
        return { g_MbCodePage, g_IsMbCodePage, (LCID)g_MbLcid, _NtCRT_mbctype, _NtCRT_mbcasemap, g_MbUlInfo };
    }
    Info = (const NTCRT_MBCINFO*)Locale->mbcinfo;
    return { Info->mbcodepage, Info->ismbcodepage, (LCID)Info->mblcid, Info->mbctype, Info->mbcasemap, Info->mbulinfo };
}

#define NTCRT_MBTYPE(M, c) ((M).Type[(UCHAR_T)(c) + 1])
#define NTCRT_ISLEAD(M, c) (NTCRT_MBTYPE(M, c) & _M1)
#define NTCRT_ISTRAIL(M, c) (NTCRT_MBTYPE(M, c) & _M2)

/* Case mapping of single bytes by the tables */
#define NTCRT_MBBTOLOWER(M, c) ((NTCRT_MBTYPE(M, c) & _SBUP) ? (M).CaseMap[(UCHAR_T)(c)] : (UCHAR_T)(c))
#define NTCRT_MBBTOUPPER(M, c) ((NTCRT_MBTYPE(M, c) & _SBLOW) ? (M).CaseMap[(UCHAR_T)(c)] : (UCHAR_T)(c))

/* Code pages of msvcrt with their case ranges of full-width letters and byte ranges */
typedef struct _NTCRT_MBCP
{
    USHORT CodePage;
    USHORT Lcid;
    USHORT UlInfo[6];
    UCHAR Ranges[4][6]; /* Lead bytes, trail bytes, single-byte kana letters and kana punctuation */
} NTCRT_MBCP;

static const NTCRT_MBCP g_MbCodePages[] = {
    { 932, 0x0411, { 0x8260, 0x8279, 0x8281 - 0x8260, 0, 0, 0 },
      { { 0x81, 0x9F, 0xE0, 0xFC }, { 0x40, 0x7E, 0x80, 0xFC }, { 0xA6, 0xDF }, { 0xA1, 0xA5 } } },
    { 936, 0x0804, { 0xA3C1, 0xA3DA, 0xA3E1 - 0xA3C1, 0, 0, 0 }, { { 0x81, 0xFE }, { 0x40, 0xFE } } },
    { 949, 0x0412, { 0xA3C1, 0xA3DA, 0xA3E1 - 0xA3C1, 0, 0, 0 }, { { 0x81, 0xFE }, { 0x41, 0xFE } } },
    { 950, 0x0404, { 0xA2CF, 0xA2E4, 0xA2E9 - 0xA2CF, 0xA2E5, 0xA2E8, 0xA340 - 0xA2E5 },
      { { 0x81, 0xFE }, { 0x40, 0x7E, 0xA1, 0xFE } } },
    { 1361, 0x0812, { 0xDA51, 0xDA5E, 0xDA71 - 0xDA51, 0xDA5F, 0xDA70, 0xDA91 - 0xDA5F },
      { { 0x81, 0xD3, 0xD8, 0xDE, 0xE0, 0xF9 }, { 0x31, 0x7E, 0x81, 0xFE } } },
};

static const UCHAR g_MbRangeTypes[4] = { _M1, _M2, _MS, _MP };

/* The "C" locale: single-byte, ASCII letters only */
static
void
_NtCRT_SetSbcs(
    _Out_ NTCRT_MBCINFO* Info)
{
    Info->mbcodepage = 0;
    Info->ismbcodepage = 0;
    Info->mblcid = 0;
    memset(Info->mbulinfo, 0, sizeof(Info->mbulinfo));
    memset(Info->mbctype, 0, sizeof(Info->mbctype));
    memset(Info->mbcasemap, 0, sizeof(Info->mbcasemap));
    for (int c = 'A'; c <= 'Z'; c++)
    {
        Info->mbctype[c + 1] = _SBUP;
        Info->mbcasemap[c] = (unsigned char)(c - 'A' + 'a');
        Info->mbctype[c - 'A' + 'a' + 1] = _SBLOW;
        Info->mbcasemap[c - 'A' + 'a'] = (unsigned char)c;
    }
}

/* A byte converted by the case mapping of Unicode, as LCMapStringA */
static
unsigned char
_NtCRT_MapByte(
    _In_ UINT CodePage,
    _In_ WCHAR Wide)
{
    char Bytes[2];

    return _NtCRT_WideCharToMultiByte(CodePage, 0, &Wide, 1, Bytes, sizeof(Bytes), NULL, NULL) >= 1
               ? (unsigned char)Bytes[0]
               : 0;
}

/* Single-byte letters of the code page, lead bytes are not */
static
void
_NtCRT_SetSbUpLow(
    _Inout_ NTCRT_MBCINFO* Info)
{
    CPINFO CpInfo;

    if (!_CRT_GetCPInfo(Info->mbcodepage, &CpInfo))
    {
        for (int c = 'A'; c <= 'Z'; c++)
        {
            Info->mbctype[c + 1] |= _SBUP;
            Info->mbcasemap[c] = (unsigned char)(c - 'A' + 'a');
            Info->mbctype[c - 'A' + 'a' + 1] |= _SBLOW;
            Info->mbcasemap[c - 'A' + 'a'] = (unsigned char)c;
        }
        return;
    }
    for (int c = 0; c < 256; c++)
    {
        char Byte = (char)c;
        WCHAR Wide;
        WORD Type = 0;

        Info->mbcasemap[c] = 0;
        for (int i = 0; i + 1 < MAX_LEADBYTES && CpInfo.LeadByte[i] != 0; i += 2)
        {
            if (c >= CpInfo.LeadByte[i] && c <= CpInfo.LeadByte[i + 1])
            {
                Byte = ' ';
            }
        }
        if (c == 0 || Byte == ' ' ||
            _NtCRT_MultiByteToWideChar(Info->mbcodepage, 0, &Byte, 1, &Wide, 1) != 1 ||
            !_CRT_GetStringTypeW(CT_CTYPE1, &Wide, 1, &Type))
        {
            continue;
        }
        if (Type & C1_UPPER)
        {
            Info->mbctype[c + 1] |= _SBUP;
            Info->mbcasemap[c] = _NtCRT_MapByte(Info->mbcodepage, RtlDowncaseUnicodeChar(Wide));
        } else if (Type & C1_LOWER)
        {
            Info->mbctype[c + 1] |= _SBLOW;
            Info->mbcasemap[c] = _NtCRT_MapByte(Info->mbcodepage, RtlUpcaseUnicodeChar(Wide));
        }
    }
}

static
int
_NtCRT_BuildMbcInfo(
    _In_ int CodePage,
    _Out_ NTCRT_MBCINFO* Info)
{
    CPINFO CpInfo;

    switch (CodePage)
    {
    case _MB_CP_ANSI:
        CodePage = (int)_K32_GetACP();
        break;
    case _MB_CP_OEM:
        CodePage = (int)_K32_GetOEMCP();
        break;
    case _MB_CP_LOCALE:
        CodePage = (int)_NtCRT_lc_codepage;
        break;
    }
    if (CodePage == _MB_CP_SBCS)
    {
        _NtCRT_SetSbcs(Info);
        return 0;
    }
    if (CodePage == CP_UTF7 || CodePage == CP_UTF8)
    {
        return -1;
    }
    memset(Info->mbctype, 0, sizeof(Info->mbctype));
    memset(Info->mbcasemap, 0, sizeof(Info->mbcasemap));
    memset(Info->mbulinfo, 0, sizeof(Info->mbulinfo));
    for (const NTCRT_MBCP& Cp : g_MbCodePages)
    {
        if (Cp.CodePage != CodePage)
        {
            continue;
        }
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 6 && Cp.Ranges[i][j] != 0; j += 2)
            {
                for (int c = Cp.Ranges[i][j]; c <= Cp.Ranges[i][j + 1]; c++)
                {
                    Info->mbctype[c + 1] |= g_MbRangeTypes[i];
                }
            }
        }
        Info->mbcodepage = CodePage;
        Info->ismbcodepage = 1;
        Info->mblcid = Cp.Lcid;
        memcpy(Info->mbulinfo, Cp.UlInfo, sizeof(Info->mbulinfo));
        _NtCRT_SetSbUpLow(Info);
        return 0;
    }
    if (CodePage <= 0 || !_CRT_GetCPInfo(CodePage, &CpInfo))
    {
        return -1;
    }
    Info->ismbcodepage = 0;
    if (CpInfo.MaxCharSize > 1)
    {
        for (int i = 0; i + 1 < MAX_LEADBYTES && CpInfo.LeadByte[i] != 0; i += 2)
        {
            for (int c = CpInfo.LeadByte[i]; c <= CpInfo.LeadByte[i + 1]; c++)
            {
                Info->mbctype[c + 1] |= _M1;
            }
        }
        /* Any byte may be a trail byte */
        for (int c = 1; c < 0xFF; c++)
        {
            Info->mbctype[c + 1] |= _M2;
        }
        Info->ismbcodepage = 1;
    }
    Info->mbcodepage = CodePage;
    Info->mblcid = 0;
    _NtCRT_SetSbUpLow(Info);
    return 0;
}

EXTERN_C int __cdecl _NtCRT__setmbcp(int codepage)
{
    NTCRT_MBCINFO Info;

    if (_NtCRT_BuildMbcInfo(codepage, &Info) != 0)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_lock(_MB_CP_LOCK);
    g_MbCodePage = Info.mbcodepage;
    g_IsMbCodePage = Info.ismbcodepage;
    g_MbLcid = Info.mblcid;
    memcpy(g_MbUlInfo, Info.mbulinfo, sizeof(g_MbUlInfo));
    memcpy(_NtCRT_mbctype, Info.mbctype, sizeof(_NtCRT_mbctype));
    memcpy(_NtCRT_mbcasemap, Info.mbcasemap, sizeof(_NtCRT_mbcasemap));
    _NtCRT_unlock(_MB_CP_LOCK);
    return 0;
}

/* Single-byte code pages are reported as 0 */
EXTERN_C int __cdecl _NtCRT__getmbcp(void)
{
    return g_IsMbCodePage ? g_MbCodePage : 0;
}

EXTERN_C void _NtCRT_GetMbcInfo(NTCRT_MBCINFO* Info, BOOL Current)
{
    Info->refcount = 1;
    if (!Current)
    {
        _NtCRT_SetSbcs(Info);
        return;
    }
    _NtCRT_lock(_MB_CP_LOCK);
    Info->mbcodepage = g_MbCodePage;
    Info->ismbcodepage = g_IsMbCodePage;
    Info->mblcid = g_MbLcid;
    memcpy(Info->mbulinfo, g_MbUlInfo, sizeof(g_MbUlInfo));
    memcpy(Info->mbctype, _NtCRT_mbctype, sizeof(Info->mbctype));
    memcpy(Info->mbcasemap, _NtCRT_mbcasemap, sizeof(Info->mbcasemap));
    _NtCRT_unlock(_MB_CP_LOCK);
}

/* msvcrt starts with the ANSI code page */
static int __cdecl _NtCRT_InitMbcs(void)
{
    _NtCRT__setmbcp(_MB_CP_ANSI);
    return 0;
}
NTCRT_INITIALIZER(".CRT$XIC", _NtCRT_InitMbcsPtr, _NtCRT_InitMbcs);

#pragma endregion

#pragma region Classification

static
int
_NtCRT_IsMbbType(
    _In_ const NTCRT_MBC& M,
    _In_ unsigned int c,
    _In_ unsigned short CtypeMask,
    _In_ unsigned char MbMask)
{
    c &= 0xFF;
    return (M.Type[c + 1] & MbMask) || (_NtCRT_pctype[c] & CtypeMask);
}

static int _NtCRT_Mbcs_ismbbalnum(const NTCRT_MBC& M, unsigned int c)
{
    return _NtCRT_IsMbbType(M, c, _ALPHA | _DIGIT, _MS);
}
static int _NtCRT_Mbcs_ismbbalpha(const NTCRT_MBC& M, unsigned int c) { return _NtCRT_IsMbbType(M, c, _ALPHA, _MS); }
static int _NtCRT_Mbcs_ismbbgraph(const NTCRT_MBC& M, unsigned int c)
{
    return _NtCRT_IsMbbType(M, c, _PUNCT | _ALPHA | _DIGIT, _MS | _MP);
}
static int _NtCRT_Mbcs_ismbbkalnum(const NTCRT_MBC& M, unsigned int c) { return _NtCRT_IsMbbType(M, c, 0, _MS); }
static int _NtCRT_Mbcs_ismbbkprint(const NTCRT_MBC& M, unsigned int c) { return _NtCRT_IsMbbType(M, c, 0, _MS | _MP); }
static int _NtCRT_Mbcs_ismbbkpunct(const NTCRT_MBC& M, unsigned int c) { return _NtCRT_IsMbbType(M, c, 0, _MP); }
static int _NtCRT_Mbcs_ismbblead(const NTCRT_MBC& M, unsigned int c) { return _NtCRT_IsMbbType(M, c, 0, _M1); }
static int _NtCRT_Mbcs_ismbbprint(const NTCRT_MBC& M, unsigned int c)
{
    return _NtCRT_IsMbbType(M, c, _BLANK | _PUNCT | _ALPHA | _DIGIT, _MS | _MP);
}
static int _NtCRT_Mbcs_ismbbpunct(const NTCRT_MBC& M, unsigned int c) { return _NtCRT_IsMbbType(M, c, _PUNCT, _MP); }
static int _NtCRT_Mbcs_ismbbtrail(const NTCRT_MBC& M, unsigned int c) { return _NtCRT_IsMbbType(M, c, 0, _M2); }

static int _NtCRT_Mbcs_ismbbkana(const NTCRT_MBC& M, unsigned int c)
{
    return M.CodePage == NTCRT_KANJI_CP && _NtCRT_IsMbbType(M, c, 0, _MS | _MP);
}

/* The character type (CT_CTYPE1) of c, 0 unless it is a single character of the code page, as GetStringTypeA */
static
WORD
_NtCRT_MbcType(
    _In_ const NTCRT_MBC& M,
    _In_ unsigned int c)
{
    char Bytes[2];
    WCHAR Wide[2];
    WORD Type[2] = { 0, 0 };
    int Length = 0, Count;

    if (c > 0xFF)
    {
        Bytes[Length++] = (char)(c >> 8);
    }
    Bytes[Length++] = (char)c;
    Count = _NtCRT_MultiByteToWideChar(M.CodePage, MB_ERR_INVALID_CHARS, Bytes, Length, Wide, ARRAYSIZE(Wide));
    if (Count <= 0 || !_CRT_GetStringTypeW(CT_CTYPE1, Wide, Count, Type) || Type[1] != 0)
    {
        return 0;
    }
    return Type[0];
}

/* Test is an expression of the character type "Type" of a double-byte character */
#define NTCRT_ISMBC(Name, Test, Single) \
    static int _NtCRT_Mbcs_##Name(const NTCRT_MBC& M, unsigned int c) \
    { \
        WORD Type; \
\
        if (c <= 0xFF) \
        { \
            return Single; \
        } \
        if (!M.IsMb) \
        { \
            return 0; \
        } \
        Type = _NtCRT_MbcType(M, c); \
        return (Test) != 0; \
    }

NTCRT_ISMBC(ismbcalnum, Type & (_ALPHA | _DIGIT), _NtCRT_Mbcs_ismbbalnum(M, c))
NTCRT_ISMBC(ismbcalpha, Type & _ALPHA, _NtCRT_Mbcs_ismbbalpha(M, c))
NTCRT_ISMBC(ismbcdigit, Type & _DIGIT, (_NtCRT_pctype[c] & _DIGIT) != 0)
NTCRT_ISMBC(ismbcgraph, Type & (_PUNCT | _ALPHA | _DIGIT), _NtCRT_Mbcs_ismbbgraph(M, c))
NTCRT_ISMBC(ismbclower, Type & _LOWER, (NTCRT_MBTYPE(M, c) & _SBLOW) != 0)
NTCRT_ISMBC(ismbcupper, Type & _UPPER, (NTCRT_MBTYPE(M, c) & _SBUP) != 0)
NTCRT_ISMBC(ismbcprint, Type != 0 && !(Type & _CONTROL), _NtCRT_Mbcs_ismbbprint(M, c))
NTCRT_ISMBC(ismbcpunct, Type & _PUNCT, _NtCRT_Mbcs_ismbbpunct(M, c))
NTCRT_ISMBC(ismbcspace, Type & _SPACE, (_NtCRT_pctype[c] & _SPACE) != 0)

/* The symbols of JIS X 0208 row 1 of code page 932 */
static int _NtCRT_Mbcs_ismbcsymbol(const NTCRT_MBC& M, unsigned int c)
{
    return M.CodePage == NTCRT_KANJI_CP && c >= 0x8141 && c <= 0x81AC && c != 0x817F;
}

static int _NtCRT_Mbcs_ismbclegal(const NTCRT_MBC& M, unsigned int c)
{
    return NTCRT_ISLEAD(M, c >> 8) && NTCRT_ISTRAIL(M, c);
}

/* JIS levels and kana of code page 932 */

static int _NtCRT_Mbcs_ismbchira(const NTCRT_MBC& M, unsigned int c)
{
    return M.CodePage == NTCRT_KANJI_CP && c >= 0x829F && c <= 0x82F1;
}

static int _NtCRT_Mbcs_ismbckata(const NTCRT_MBC& M, unsigned int c)
{
    return M.CodePage == NTCRT_KANJI_CP && c >= 0x8340 && c <= 0x8396 && c != 0x837F;
}

static int _NtCRT_Mbcs_ismbcl0(const NTCRT_MBC& M, unsigned int c)
{
    return M.CodePage == NTCRT_KANJI_CP && _NtCRT_Mbcs_ismbclegal(M, c) && c < 0x889F;
}

static int _NtCRT_Mbcs_ismbcl1(const NTCRT_MBC& M, unsigned int c)
{
    return M.CodePage == NTCRT_KANJI_CP && _NtCRT_Mbcs_ismbclegal(M, c) && c >= 0x889F && c <= 0x9872;
}

static int _NtCRT_Mbcs_ismbcl2(const NTCRT_MBC& M, unsigned int c)
{
    return M.CodePage == NTCRT_KANJI_CP && _NtCRT_Mbcs_ismbclegal(M, c) && c >= 0x989F && c <= 0xEAA4;
}

/* Whether current is at a lead byte (or a trail byte) of string */

static int _NtCRT_Mbcs_ismbslead(const NTCRT_MBC& M, const UCHAR_T* string, const UCHAR_T* current)
{
    NTCRT_VALIDATE(string != NULL && current != NULL, 0);
    if (!M.IsMb)
    {
        return 0;
    }
    while (string <= current && *string)
    {
        if (NTCRT_ISLEAD(M, *string))
        {
            if (string++ == current)
            {
                return -1;
            }
            if (!*string)
            {
                return 0;
            }
        }
        string++;
    }
    return 0;
}

static int _NtCRT_Mbcs_ismbstrail(const NTCRT_MBC& M, const UCHAR_T* string, const UCHAR_T* current)
{
    NTCRT_VALIDATE(string != NULL && current != NULL, 0);
    if (!M.IsMb)
    {
        return 0;
    }
    while (string <= current && *string)
    {
        if (NTCRT_ISLEAD(M, *string))
        {
            if (++string == current)
            {
                return -1;
            }
            if (!*string)
            {
                return 0;
            }
        }
        string++;
    }
    return 0;
}

/* Type of a byte after one of type ctype */
static int _NtCRT_Mbcs_mbbtype(const NTCRT_MBC& M, unsigned char c, int ctype)
{
    if (ctype == _MBC_LEAD)
    {
        return NTCRT_ISTRAIL(M, c) ? _MBC_TRAIL : _MBC_ILLEGAL;
    }
    if (NTCRT_ISLEAD(M, c))
    {
        return _MBC_LEAD;
    }
    return _NtCRT_Mbcs_ismbbprint(M, c) ? _MBC_SINGLE : _MBC_ILLEGAL;
}

static int _NtCRT_Mbcs_mbsbtype(const NTCRT_MBC& M, const UCHAR_T* string, size_t count)
{
    int Type = _MBC_ILLEGAL;

    NTCRT_VALIDATE(string != NULL, _MBC_ILLEGAL);
    if (!M.IsMb)
    {
        return _MBC_SINGLE;
    }
    do
    {
        if (*string == '\0')
        {
            return _MBC_ILLEGAL;
        }
        Type = _NtCRT_Mbcs_mbbtype(M, *string++, Type);
    } while (count--);
    return Type;
}

#pragma endregion

#pragma region Character conversion

/* _mbbtombc of code page 932 for 0x20 - 0x7E and 0xA1 - 0xDF */
static const unsigned short g_MbbToMbcAscii[] = {
    0x8140, 0x8149, 0x8168, 0x8194, 0x8190, 0x8193, 0x8195, 0x8166,
    0x8169, 0x816A, 0x8196, 0x817B, 0x8143, 0x817C, 0x8144, 0x815E,
    0x824F, 0x8250, 0x8251, 0x8252, 0x8253, 0x8254, 0x8255, 0x8256,
    0x8257, 0x8258, 0x8146, 0x8147, 0x8183, 0x8181, 0x8184, 0x8148,
    0x8197, 0x8260, 0x8261, 0x8262, 0x8263, 0x8264, 0x8265, 0x8266,
    0x8267, 0x8268, 0x8269, 0x826A, 0x826B, 0x826C, 0x826D, 0x826E,
    0x826F, 0x8270, 0x8271, 0x8272, 0x8273, 0x8274, 0x8275, 0x8276,
    0x8277, 0x8278, 0x8279, 0x816D, 0x818F, 0x816E, 0x814F, 0x8151,
    0x8165, 0x8281, 0x8282, 0x8283, 0x8284, 0x8285, 0x8286, 0x8287,
    0x8288, 0x8289, 0x828A, 0x828B, 0x828C, 0x828D, 0x828E, 0x828F,
    0x8290, 0x8291, 0x8292, 0x8293, 0x8294, 0x8295, 0x8296, 0x8297,
    0x8298, 0x8299, 0x829A, 0x816F, 0x8162, 0x8170, 0x8150,
};

static const unsigned short g_MbbToMbcKana[] = {
    0x8142, 0x8175, 0x8176, 0x8141, 0x8145, 0x8392, 0x8340, 0x8342,
    0x8344, 0x8346, 0x8348, 0x8383, 0x8385, 0x8387, 0x8362, 0x815B,
    0x8341, 0x8343, 0x8345, 0x8347, 0x8349, 0x834A, 0x834C, 0x834E,
    0x8350, 0x8352, 0x8354, 0x8356, 0x8358, 0x835A, 0x835C, 0x835E,
    0x8360, 0x8363, 0x8365, 0x8367, 0x8369, 0x836A, 0x836B, 0x836C,
    0x836D, 0x836E, 0x8371, 0x8374, 0x8377, 0x837A, 0x837D, 0x837E,
    0x8380, 0x8381, 0x8382, 0x8384, 0x8386, 0x8388, 0x8389, 0x838A,
    0x838B, 0x838C, 0x838D, 0x838F, 0x8393, 0x814A, 0x814B,
};

/* _mbctombb of code page 932 for 0x8140 - 0x8396, 0 if unchanged */
static const unsigned char g_MbcToMbb[] = {
    0x20, 0xA4, 0xA1, 0x2C, 0x2E, 0xA5, 0x3A, 0x3B, 0x3F, 0x21, 0xDE, 0xDF,
    0x00, 0x00, 0x00, 0x5E, 0x7E, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xB0, 0x00, 0x00, 0x2F, 0x00, 0x00, 0x00, 0x7C, 0x00,
    0x00, 0x60, 0x27, 0x00, 0x22, 0x28, 0x29, 0x00, 0x00, 0x5B, 0x5D, 0x7B,
    0x7D, 0x00, 0x00, 0x00, 0x00, 0xA2, 0xA3, 0x00, 0x00, 0x00, 0x00, 0x2B,
    0x2D, 0x00, 0x00, 0x00, 0x00, 0x3D, 0x00, 0x3C, 0x3E, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C, 0x24, 0x00, 0x00, 0x25,
    0x23, 0x26, 0x2A, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x31, 0x32, 0x33, 0x34,
    0x35, 0x36, 0x37, 0x38, 0x39, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C,
    0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x62, 0x63,
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x00,
    0x00, 0x00, 0x00, 0xA7, 0xB1, 0xA8, 0xB2, 0xA9, 0xB3, 0xAA, 0xB4, 0xAB,
    0xB5, 0xB6, 0xB6, 0xB7, 0xB7, 0xB8, 0xB8, 0xB9, 0xB9, 0xBA, 0xBA, 0xBB,
    0xBB, 0xBC, 0xBC, 0xBD, 0xBD, 0xBE, 0xBE, 0xBF, 0xBF, 0xC0, 0xC0, 0xC1,
    0xC1, 0xAF, 0xC2, 0xC2, 0xC3, 0xC3, 0xC4, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
    0xC9, 0xCA, 0xCA, 0xCA, 0xCB, 0xCB, 0xCB, 0xCC, 0xCC, 0xCC, 0xCD, 0xCD,
    0xCD, 0xCE, 0xCE, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xAC, 0xD4, 0xAD,
    0xD5, 0xAE, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDC, 0xB2, 0xB4,
    0xA6, 0xDD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA7, 0xB1, 0xA8, 0xB2,
    0xA9, 0xB3, 0xAA, 0xB4, 0xAB, 0xB5, 0xB6, 0xB6, 0xB7, 0xB7, 0xB8, 0xB8,
    0xB9, 0xB9, 0xBA, 0xBA, 0xBB, 0xBB, 0xBC, 0xBC, 0xBD, 0xBD, 0xBE, 0xBE,
    0xBF, 0xBF, 0xC0, 0xC0, 0xC1, 0xC1, 0xAF, 0xC2, 0xC2, 0xC3, 0xC3, 0xC4,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCA, 0xCA, 0xCB, 0xCB, 0xCB,
    0xCC, 0xCC, 0xCC, 0xCD, 0xCD, 0xCD, 0xCE, 0xCE, 0xCE, 0xCF, 0xD0, 0x00,
    0xD1, 0xD2, 0xD3, 0xAC, 0xD4, 0xAD, 0xD5, 0xAE, 0xD6, 0xD7, 0xD8, 0xD9,
    0xDA, 0xDB, 0xDC, 0xDC, 0xB2, 0xB4, 0xA6, 0xDD, 0xB3, 0xB6, 0xB9,
};

/* LCMapStringA of a double-byte character, returns the length or 0 */
static
int
_NtCRT_MapMbc(
    _In_ const NTCRT_MBC& M,
    _In_ BOOL Upper,
    _In_reads_(2) const UCHAR_T* Src,
    _Out_writes_(2) UCHAR_T* Dst)
{
    WCHAR Wide[2];
    int Count;

    Count = _NtCRT_MultiByteToWideChar(M.CodePage, MB_ERR_INVALID_CHARS, (LPCCH)Src, 2, Wide, ARRAYSIZE(Wide));
    if (Count <= 0)
    {
        return 0;
    }
    for (int i = 0; i < Count; i++)
    {
        Wide[i] = Upper ? RtlUpcaseUnicodeChar(Wide[i]) : RtlDowncaseUnicodeChar(Wide[i]);
    }
    return _NtCRT_WideCharToMultiByte(M.CodePage, 0, Wide, Count, (LPSTR)Dst, 2, NULL, NULL);
}

static
unsigned int
_NtCRT_MbcCase(
    _In_ const NTCRT_MBC& M,
    _In_ unsigned int c,
    _In_ BOOL Upper)
{
    UCHAR_T Bytes[2];

    if (c <= 0xFF)
    {
        return Upper ? NTCRT_MBBTOUPPER(M, c) : NTCRT_MBBTOLOWER(M, c);
    }
    Bytes[0] = (UCHAR_T)(c >> 8);
    Bytes[1] = (UCHAR_T)c;
    /* Mapped in place, a single byte result keeps the trail byte */
    if (!NTCRT_ISLEAD(M, Bytes[0]) || _NtCRT_MapMbc(M, Upper, Bytes, Bytes) == 0)
    {
        return c;
    }
    return (Bytes[0] << 8) | Bytes[1];
}

static unsigned int _NtCRT_Mbcs_mbctolower(const NTCRT_MBC& M, unsigned int c) { return _NtCRT_MbcCase(M, c, FALSE); }
static unsigned int _NtCRT_Mbcs_mbctoupper(const NTCRT_MBC& M, unsigned int c) { return _NtCRT_MbcCase(M, c, TRUE); }

static unsigned int _NtCRT_Mbcs_mbctohira(const NTCRT_MBC& M, unsigned int c)
{
    if (_NtCRT_Mbcs_ismbckata(M, c) && c <= 0x8393)
    {
        c -= c < 0x837F ? 0xA1 : 0xA2;
    }
    return c;
}

static unsigned int _NtCRT_Mbcs_mbctokata(const NTCRT_MBC& M, unsigned int c)
{
    if (_NtCRT_Mbcs_ismbchira(M, c))
    {
        c += 0xA1;
        if (c >= 0x837F)
        {
            c++;
        }
    }
    return c;
}

static unsigned int _NtCRT_Mbcs_mbbtombc(const NTCRT_MBC& M, unsigned int c)
{
    if (M.CodePage != NTCRT_KANJI_CP)
    {
        return c;
    }
    if (c >= 0x20 && c <= 0x7E)
    {
        return g_MbbToMbcAscii[c - 0x20];
    }
    if (c >= 0xA1 && c <= 0xDF)
    {
        return g_MbbToMbcKana[c - 0xA1];
    }
    return c;
}

static unsigned int _NtCRT_Mbcs_mbctombb(const NTCRT_MBC& M, unsigned int c)
{
    if (M.CodePage == NTCRT_KANJI_CP && c >= 0x8140 && c < 0x8140 + sizeof(g_MbcToMbb) && g_MbcToMbb[c - 0x8140] != 0)
    {
        return g_MbcToMbb[c - 0x8140];
    }
    return c;
}

/* JIS X 0208 and Shift_JIS */

static unsigned int _NtCRT_Mbcs_mbcjistojms(const NTCRT_MBC& M, unsigned int c)
{
    unsigned int h, l;

    if (M.CodePage != NTCRT_KANJI_CP)
    {
        return c;
    }
    h = (c >> 8) & 0xFF;
    l = c & 0xFF;
    if (h < 0x21 || h > 0x7E || l < 0x21 || l > 0x7E)
    {
        NTCRT_ERRNO = EILSEQ;
        return 0;
    }
    if (h & 1)
    {
        l += l <= 0x5F ? 0x1F : 0x20;
    } else
    {
        l += 0x7E;
    }
    h = ((h - 0x21) >> 1) + 0x81;
    if (h > 0x9F)
    {
        h += 0x40;
    }
    return (h << 8) | l;
}

static unsigned int _NtCRT_Mbcs_mbcjmstojis(const NTCRT_MBC& M, unsigned int c)
{
    unsigned int h, l;

    if (M.CodePage != NTCRT_KANJI_CP)
    {
        return c;
    }
    h = (c >> 8) & 0xFF;
    l = c & 0xFF;
    if (!NTCRT_ISLEAD(M, h) || !NTCRT_ISTRAIL(M, l))
    {
        NTCRT_ERRNO = EILSEQ;
        return 0;
    }
    h -= h >= 0xA0 ? 0xC1 : 0x81;
    if (l >= 0x9F)
    {
        c = (h << 9) + 0x2200;
        c |= l - 0x7E;
    } else
    {
        c = (h << 9) + 0x2100;
        c |= l - (l <= 0x7E ? 0x1F : 0x20);
    }
    if (c > 0x7E7E || c < 0x2121 || (c & 0xFF) > 0x7E || (c & 0xFF) < 0x21)
    {
        NTCRT_ERRNO = EILSEQ;
        return 0;
    }
    return c;
}

#pragma endregion

#pragma region Characters of strings

static size_t _NtCRT_Mbcs_mbclen(const NTCRT_MBC& M, const UCHAR_T* c)
{
    return NTCRT_ISLEAD(M, *c) && c[1] != '\0' ? 2 : 1;
}

static UCHAR_T* _NtCRT_Mbcs_mbsinc(const NTCRT_MBC& M, const UCHAR_T* current)
{
    NTCRT_VALIDATE(current != NULL, NULL);
    if (NTCRT_ISLEAD(M, *current++) && *current != '\0')
    {
        current++;
    }
    return (UCHAR_T*)current;
}

static size_t _NtCRT_Mbcs_mbsnbcnt(const NTCRT_MBC& M, const UCHAR_T* string, size_t ccnt)
{
    const UCHAR_T* p;

    NTCRT_VALIDATE(string != NULL || ccnt == 0, 0);
    for (p = string; ccnt-- && *p; p++)
    {
        if (NTCRT_ISLEAD(M, *p) && *++p == '\0')
        {
            p--;
            break;
        }
    }
    return (size_t)(p - string);
}

static size_t _NtCRT_Mbcs_mbsnccnt(const NTCRT_MBC& M, const UCHAR_T* string, size_t bcnt)
{
    size_t n;

    NTCRT_VALIDATE(string != NULL || bcnt == 0, 0);
    for (n = 0; bcnt-- && *string; n++, string++)
    {
        if (NTCRT_ISLEAD(M, *string) && (!bcnt-- || *++string == '\0'))
        {
            break;
        }
    }
    return n;
}

static UCHAR_T* _NtCRT_Mbcs_mbsninc(const NTCRT_MBC& M, const UCHAR_T* string, size_t ccnt)
{
    if (string == NULL)
    {
        return NULL;
    }
    return (UCHAR_T*)string + _NtCRT_Mbcs_mbsnbcnt(M, string, ccnt);
}

static UCHAR_T* _NtCRT_Mbcs_mbsdec(const NTCRT_MBC& M, const UCHAR_T* string, const UCHAR_T* current)
{
    const UCHAR_T* p;

    NTCRT_VALIDATE(string != NULL && current != NULL, NULL);
    if (string >= current)
    {
        return NULL;
    }
    if (!M.IsMb)
    {
        return (UCHAR_T*)current - 1;
    }
    /* Lead bytes before current pair up from the last byte that is not one */
    p = current - 1;
    while (string <= --p && NTCRT_ISLEAD(M, *p))
    {
    }
    return (UCHAR_T*)current - 1 - ((current - p) & 1);
}

static unsigned int _NtCRT_Mbcs_mbsnextc(const NTCRT_MBC& M, const UCHAR_T* s)
{
    unsigned int Next = 0;

    NTCRT_VALIDATE(s != NULL, 0);
    if (NTCRT_ISLEAD(M, *s) && s[1] != '\0')
    {
        Next = (unsigned int)*s++ << 8;
    }
    return Next + *s;
}

static size_t _NtCRT_Mbcs_mbslen(const NTCRT_MBC& M, const UCHAR_T* s)
{
    size_t n;

    NTCRT_VALIDATE(s != NULL, 0);
    for (n = 0; *s; n++, s++)
    {
        if (NTCRT_ISLEAD(M, *s) && *++s == '\0')
        {
            break;
        }
    }
    return n;
}

static size_t _NtCRT_Mbcs_mbsnlen(const NTCRT_MBC& M, const UCHAR_T* s, size_t maxsize)
{
    size_t n, size;

    if (!M.IsMb)
    {
        return strnlen((const char*)s, maxsize);
    }
    for (n = 0, size = 0; size < maxsize && *s; n++, s++, size++)
    {
        if (NTCRT_ISLEAD(M, *s))
        {
            size++;
            if (size >= maxsize || *++s == '\0')
            {
                break;
            }
        }
    }
    return size >= maxsize ? maxsize : n;
}

/* The multibyte characters of the locale (LC_CTYPE) instead of the multibyte code page */

static size_t _NtCRT_Mbcs_mbstrlen(const NTCRT_MBC& M, const char* s)
{
    (void)M;
    NTCRT_VALIDATE(s != NULL, (size_t)-1);
    return _NtCRT_MbsLength(s, SIZE_MAX);
}

static size_t _NtCRT_Mbcs_mbstrnlen(const NTCRT_MBC& M, const char* s, size_t maxsize)
{
    (void)M;
    NTCRT_VALIDATE(s != NULL && maxsize <= INT_MAX, (size_t)-1);
    return _NtCRT_MbsLength(s, maxsize);
}

static void _NtCRT_Mbcs_mbccpy(const NTCRT_MBC& M, UCHAR_T* dst, const UCHAR_T* src)
{
    NTCRT_VALIDATE(dst != NULL && src != NULL, );
    *dst = *src;
    if (NTCRT_ISLEAD(M, *src))
    {
        *++dst = *++src;
    }
}

static errno_t _NtCRT_Mbcs_mbccpy_s(const NTCRT_MBC& M, UCHAR_T* dst, size_t size, int* copied, const UCHAR_T* src)
{
    if (copied != NULL)
    {
        *copied = 0;
    }
    if (dst == NULL || size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *dst = '\0';
    if (src == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (NTCRT_ISLEAD(M, *src))
    {
        if (src[1] == '\0')
        {
            /* A lead byte without its trail byte is copied as the end of the string */
            if (copied != NULL)
            {
                *copied = 1;
            }
            NTCRT_ERRNO = EILSEQ;
            return EILSEQ;
        }
        if (size < 2)
        {
            return _NtCRT_InvalidParameter(ERANGE);
        }
        *dst++ = *src++;
        *dst = *src;
        if (copied != NULL)
        {
            *copied = 2;
        }
    } else
    {
        *dst = *src;
        if (copied != NULL)
        {
            *copied = 1;
        }
    }
    return 0;
}

#pragma endregion

#pragma region Searching

static UCHAR_T* _NtCRT_Mbcs_mbschr(const NTCRT_MBC& M, const UCHAR_T* string, unsigned int c)
{
    UCHAR_T cc;

    NTCRT_VALIDATE(string != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)strchr((const char*)string, (int)c);
    }
    for (; (cc = *string) != '\0'; string++)
    {
        if (NTCRT_ISLEAD(M, cc))
        {
            if (*++string == '\0')
            {
                return NULL;
            }
            if (c == (((unsigned int)cc << 8) | *string))
            {
                return (UCHAR_T*)string - 1;
            }
        } else if (c == cc)
        {
            break;
        }
    }
    return c == cc ? (UCHAR_T*)string : NULL;
}

static UCHAR_T* _NtCRT_Mbcs_mbsrchr(const NTCRT_MBC& M, const UCHAR_T* string, unsigned int c)
{
    const UCHAR_T* r = NULL;
    UCHAR_T cc;

    NTCRT_VALIDATE(string != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)strrchr((const char*)string, (int)c);
    }
    do
    {
        cc = *string;
        if (NTCRT_ISLEAD(M, cc))
        {
            if (*++string)
            {
                if (c == (((unsigned int)cc << 8) | *string))
                {
                    r = string - 1;
                }
            } else if (r == NULL)
            {
                /* The terminator after a lone lead byte */
                r = string;
            }
        } else if (c == cc)
        {
            r = string;
        }
    } while (*string++);
    return (UCHAR_T*)r;
}

static UCHAR_T* _NtCRT_Mbcs_mbsstr(const NTCRT_MBC& M, const UCHAR_T* str1, const UCHAR_T* str2)
{
    size_t Length;
    const UCHAR_T* End;

    NTCRT_VALIDATE(str1 != NULL && str2 != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)strstr((const char*)str1, (const char*)str2);
    }
    Length = strlen((const char*)str2);
    if (Length == 0)
    {
        return (UCHAR_T*)str1;
    }
    End = str1 + strlen((const char*)str1) - Length;
    for (; str1 <= End; str1++)
    {
        if (*str1 == *str2 && strncmp((const char*)str1, (const char*)str2, Length) == 0)
        {
            return (UCHAR_T*)str1;
        }
        if (NTCRT_ISLEAD(M, *str1) && *++str1 == '\0')
        {
            return NULL;
        }
    }
    return NULL;
}

/*
 * Position of the first character of string that is (Match) or is not in charset, double-byte characters compare
 * whole
 */
static
const UCHAR_T*
_NtCRT_MbsSpan(
    _In_ const NTCRT_MBC& M,
    _In_z_ const UCHAR_T* string,
    _In_z_ const UCHAR_T* charset,
    _In_ bool Match)
{
    const UCHAR_T *p, *q;

    for (p = string; *p; p++)
    {
        for (q = charset; *q; q++)
        {
            if (NTCRT_ISLEAD(M, *q))
            {
                /* A lead byte at the end of string is in charset of _mbsspn */
                if ((*p == *q && p[1] == q[1]) || (!Match && p[1] == '\0'))
                {
                    break;
                }
                if (*++q == '\0')
                {
                    break;
                }
            } else if (*p == *q)
            {
                break;
            }
        }
        if ((*q != '\0') == Match)
        {
            break;
        }
        if (NTCRT_ISLEAD(M, *p) && *++p == '\0')
        {
            break;
        }
    }
    return p;
}

static size_t _NtCRT_Mbcs_mbsspn(const NTCRT_MBC& M, const UCHAR_T* string, const UCHAR_T* charset)
{
    NTCRT_VALIDATE(string != NULL && charset != NULL, 0);
    if (!M.IsMb)
    {
        return strspn((const char*)string, (const char*)charset);
    }
    return (size_t)(_NtCRT_MbsSpan(M, string, charset, false) - string);
}

static UCHAR_T* _NtCRT_Mbcs_mbsspnp(const NTCRT_MBC& M, const UCHAR_T* string, const UCHAR_T* charset)
{
    const UCHAR_T* p;

    NTCRT_VALIDATE(string != NULL && charset != NULL, NULL);
    if (!M.IsMb)
    {
        p = string + strspn((const char*)string, (const char*)charset);
    } else
    {
        p = _NtCRT_MbsSpan(M, string, charset, false);
    }
    return *p ? (UCHAR_T*)p : NULL;
}

static size_t _NtCRT_Mbcs_mbscspn(const NTCRT_MBC& M, const UCHAR_T* string, const UCHAR_T* charset)
{
    NTCRT_VALIDATE(string != NULL && charset != NULL, 0);
    if (!M.IsMb)
    {
        return strcspn((const char*)string, (const char*)charset);
    }
    return (size_t)(_NtCRT_MbsSpan(M, string, charset, true) - string);
}

static UCHAR_T* _NtCRT_Mbcs_mbspbrk(const NTCRT_MBC& M, const UCHAR_T* string, const UCHAR_T* charset)
{
    const UCHAR_T* p;

    NTCRT_VALIDATE(string != NULL && charset != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)strpbrk((const char*)string, (const char*)charset);
    }
    p = _NtCRT_MbsSpan(M, string, charset, true);
    return *p ? (UCHAR_T*)p : NULL;
}

static UCHAR_T* _NtCRT_Mbcs_mbstok_s(const NTCRT_MBC& M, UCHAR_T* string, const UCHAR_T* delim, UCHAR_T** context)
{
    UCHAR_T* End;

    NTCRT_VALIDATE(context != NULL && delim != NULL && (string != NULL || *context != NULL), NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)_NtCRT_strtok_s((char*)string, (const char*)delim, (char**)context);
    }
    if (string == NULL)
    {
        string = *context;
    }
    /* Skip the leading delimiters */
    string = (UCHAR_T*)_NtCRT_MbsSpan(M, string, delim, false);
    if (*string == '\0')
    {
        *context = string;
        return NULL;
    }
    End = (UCHAR_T*)_NtCRT_MbsSpan(M, string, delim, true);
    if (*End != '\0')
    {
        if (NTCRT_ISLEAD(M, *End))
        {
            *End++ = '\0';
        }
        *End++ = '\0';
    }
    *context = End;
    return string;
}

static UCHAR_T* _NtCRT_Mbcs_mbstok(const NTCRT_MBC& M, UCHAR_T* string, const UCHAR_T* delim)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();

    if (string == NULL && Ptd->MbstokContext == NULL)
    {
        return NULL;
    }
    return _NtCRT_Mbcs_mbstok_s(M, string, delim, &Ptd->MbstokContext);
}

#pragma endregion

#pragma region Comparison

/* The next character of a string, 0 for a lead byte at the end */
static
unsigned int
_NtCRT_MbsNextChar(
    _In_ const NTCRT_MBC& M,
    _Inout_ const UCHAR_T** String)
{
    const UCHAR_T* s = *String;
    unsigned int c = *s++;

    if (NTCRT_ISLEAD(M, c))
    {
        c = *s == '\0' ? 0 : (c << 8) | *s++;
    }
    *String = s;
    return c;
}

/* Double-byte letters of the ranges of mbulinfo in lower case */
static
unsigned int
_NtCRT_MbcFold(
    _In_ const NTCRT_MBC& M,
    _In_ unsigned int c)
{
    if (c >= M.UlInfo[0] && c <= M.UlInfo[1])
    {
        c += M.UlInfo[2];
    } else if (c >= M.UlInfo[3] && c <= M.UlInfo[4])
    {
        c += M.UlInfo[5];
    }
    return c & 0xFFFF;
}

/* The next character of a string in lower case by mbulinfo */
static
unsigned int
_NtCRT_MbsNextFold(
    _In_ const NTCRT_MBC& M,
    _Inout_ const UCHAR_T** String)
{
    unsigned int c = **String;

    if (NTCRT_ISLEAD(M, c))
    {
        c = _NtCRT_MbsNextChar(M, String);
        return c == 0 ? 0 : _NtCRT_MbcFold(M, c);
    }
    (*String)++;
    return NTCRT_MBBTOLOWER(M, c);
}

/* The next character of a string, double-byte characters in upper case by LCMapStringA */
static
bool
_NtCRT_MbsNextUpper(
    _In_ const NTCRT_MBC& M,
    _Inout_ const UCHAR_T** String,
    _Out_ unsigned int* Char)
{
    const UCHAR_T* s = *String;
    unsigned int c = *s++;
    UCHAR_T Result[2];

    if (!NTCRT_ISLEAD(M, c))
    {
        c = NTCRT_MBBTOLOWER(M, c);
    } else if (*s == '\0')
    {
        c = 0;
    } else
    {
        switch (_NtCRT_MapMbc(M, TRUE, s - 1, Result))
        {
        case 1:
            c = Result[0];
            break;
        case 2:
            c = (Result[0] << 8) | Result[1];
            break;
        default:
            NTCRT_ERRNO = EINVAL;
            return false;
        }
        s++;
    }
    *String = s;
    *Char = c;
    return true;
}

static int _NtCRT_Mbcs_mbscmp(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2)
{
    unsigned int c1, c2;

    NTCRT_VALIDATE(s1 != NULL && s2 != NULL, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return strcmp((const char*)s1, (const char*)s2);
    }
    for (;;)
    {
        c1 = _NtCRT_MbsNextChar(M, &s1);
        c2 = _NtCRT_MbsNextChar(M, &s2);
        if (c1 != c2)
        {
            return c1 > c2 ? 1 : -1;
        }
        if (c1 == 0)
        {
            return 0;
        }
    }
}

static int _NtCRT_Mbcs_mbsicmp(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2)
{
    unsigned int c1, c2;

    NTCRT_VALIDATE(s1 != NULL && s2 != NULL, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return _stricmp((const char*)s1, (const char*)s2);
    }
    for (;;)
    {
        if (!_NtCRT_MbsNextUpper(M, &s1, &c1) || !_NtCRT_MbsNextUpper(M, &s2, &c2))
        {
            return _NLSCMPERROR;
        }
        if (c1 != c2)
        {
            return c1 > c2 ? 1 : -1;
        }
        if (c1 == 0)
        {
            return 0;
        }
    }
}

static int _NtCRT_Mbcs_mbsncmp(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    unsigned int c1, c2;

    if (count == 0)
    {
        return 0;
    }
    NTCRT_VALIDATE(s1 != NULL && s2 != NULL, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return strncmp((const char*)s1, (const char*)s2, count);
    }
    while (count--)
    {
        c1 = _NtCRT_MbsNextChar(M, &s1);
        c2 = _NtCRT_MbsNextChar(M, &s2);
        if (c1 != c2)
        {
            return c1 > c2 ? 1 : -1;
        }
        if (c1 == 0)
        {
            return 0;
        }
    }
    return 0;
}

static int _NtCRT_Mbcs_mbsnicmp(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    unsigned int c1, c2;

    if (count == 0)
    {
        return 0;
    }
    NTCRT_VALIDATE(s1 != NULL && s2 != NULL, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return _strnicmp((const char*)s1, (const char*)s2, count);
    }
    while (count--)
    {
        c1 = _NtCRT_MbsNextFold(M, &s1);
        c2 = _NtCRT_MbsNextFold(M, &s2);
        if (c1 != c2)
        {
            return c1 > c2 ? 1 : -1;
        }
        if (c1 == 0)
        {
            return 0;
        }
    }
    return 0;
}

/* Count bytes, a double-byte character of s2 counts two, a lead byte at the end of the count ends the string */
static
int
_NtCRT_MbsNbCompare(
    _In_ const NTCRT_MBC& M,
    _In_z_ const UCHAR_T* s1,
    _In_z_ const UCHAR_T* s2,
    _In_ size_t count,
    _In_ bool Fold)
{
    unsigned int c1, c2;

    while (count--)
    {
        c1 = *s1;
        if (NTCRT_ISLEAD(M, c1))
        {
            if (count == 0)
            {
                c1 = 0;
                c2 = NTCRT_ISLEAD(M, *s2) ? 0 : (Fold ? NTCRT_MBBTOLOWER(M, *s2) : *s2);
                goto Test;
            }
        }
        c1 = Fold ? _NtCRT_MbsNextFold(M, &s1) : _NtCRT_MbsNextChar(M, &s1);
        c2 = *s2;
        if (NTCRT_ISLEAD(M, c2))
        {
            if (count == 0)
            {
                c2 = 0;
                goto Test;
            }
            count--;
        }
        c2 = Fold ? _NtCRT_MbsNextFold(M, &s2) : _NtCRT_MbsNextChar(M, &s2);
Test:
        if (c1 != c2)
        {
            return c1 > c2 ? 1 : -1;
        }
        if (c1 == 0)
        {
            return 0;
        }
    }
    return 0;
}

static int _NtCRT_Mbcs_mbsnbcmp(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    if (count == 0)
    {
        return 0;
    }
    NTCRT_VALIDATE(s1 != NULL && s2 != NULL, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return strncmp((const char*)s1, (const char*)s2, count);
    }
    return _NtCRT_MbsNbCompare(M, s1, s2, count, false);
}

static int _NtCRT_Mbcs_mbsnbicmp(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    if (count == 0)
    {
        return 0;
    }
    NTCRT_VALIDATE(s1 != NULL && s2 != NULL, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return _strnicmp((const char*)s1, (const char*)s2, count);
    }
    return _NtCRT_MbsNbCompare(M, s1, s2, count, true);
}

/* Collation by CompareStringA of the locale of the multibyte code page */
static
int
_NtCRT_MbsCollate(
    _In_ const NTCRT_MBC& M,
    _In_ DWORD Flags,
    _In_ const UCHAR_T* s1,
    _In_ int Count1,
    _In_ const UCHAR_T* s2,
    _In_ int Count2)
{
    int Ret =
        _NtCRT___crtCompareStringA(M.Lcid, Flags | SORT_STRINGSORT, (LPCSTR)s1, Count1, (LPCSTR)s2, Count2, M.CodePage);

    if (Ret == 0)
    {
        NTCRT_ERRNO = EINVAL;
        return _NLSCMPERROR;
    }
    return Ret - 2;
}

static int _NtCRT_Mbcs_mbscoll(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2)
{
    NTCRT_VALIDATE(s1 != NULL && s2 != NULL, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return _NtCRT_strcoll((const char*)s1, (const char*)s2);
    }
    return _NtCRT_MbsCollate(M, 0, s1, -1, s2, -1);
}

static int _NtCRT_Mbcs_mbsicoll(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2)
{
    NTCRT_VALIDATE(s1 != NULL && s2 != NULL, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return _NtCRT__stricoll((const char*)s1, (const char*)s2);
    }
    return _NtCRT_MbsCollate(M, NORM_IGNORECASE, s1, -1, s2, -1);
}

static int _NtCRT_Mbcs_mbsncoll(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    if (count == 0)
    {
        return 0;
    }
    NTCRT_VALIDATE(s1 != NULL && s2 != NULL && count <= INT_MAX, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return _NtCRT__strncoll((const char*)s1, (const char*)s2, count);
    }
    return _NtCRT_MbsCollate(M,
                             0,
                             s1,
                             (int)_NtCRT_Mbcs_mbsnbcnt(M, s1, count),
                             s2,
                             (int)_NtCRT_Mbcs_mbsnbcnt(M, s2, count));
}

static int _NtCRT_Mbcs_mbsnicoll(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    if (count == 0)
    {
        return 0;
    }
    NTCRT_VALIDATE(s1 != NULL && s2 != NULL && count <= INT_MAX, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return _NtCRT__strnicoll((const char*)s1, (const char*)s2, count);
    }
    return _NtCRT_MbsCollate(M,
                             NORM_IGNORECASE,
                             s1,
                             (int)_NtCRT_Mbcs_mbsnbcnt(M, s1, count),
                             s2,
                             (int)_NtCRT_Mbcs_mbsnbcnt(M, s2, count));
}

static int _NtCRT_Mbcs_mbsnbcoll(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    if (count == 0)
    {
        return 0;
    }
    NTCRT_VALIDATE(s1 != NULL && s2 != NULL && count <= INT_MAX, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return _NtCRT__strncoll((const char*)s1, (const char*)s2, count);
    }
    return _NtCRT_MbsCollate(M, 0, s1, (int)count, s2, (int)count);
}

static int _NtCRT_Mbcs_mbsnbicoll(const NTCRT_MBC& M, const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    if (count == 0)
    {
        return 0;
    }
    NTCRT_VALIDATE(s1 != NULL && s2 != NULL && count <= INT_MAX, _NLSCMPERROR);
    if (!M.IsMb)
    {
        return _NtCRT__strnicoll((const char*)s1, (const char*)s2, count);
    }
    return _NtCRT_MbsCollate(M, NORM_IGNORECASE, s1, (int)count, s2, (int)count);
}

#pragma endregion

#pragma region Modification

/* Odd count of lead bytes ending at p: p is a lead byte */
static
bool
_NtCRT_IsLeadPrefix(
    _In_ const NTCRT_MBC& M,
    _In_ const UCHAR_T* Start,
    _In_ const UCHAR_T* p)
{
    const UCHAR_T* q = p;

    while (q >= Start && NTCRT_ISLEAD(M, *q))
    {
        q--;
    }
    return ((p - q) & 1) != 0;
}

static errno_t _NtCRT_MbsCaseS(const NTCRT_MBC& M, UCHAR_T* string, size_t size, BOOL Upper)
{
    UCHAR_T *cp, *dst;

    if (string == NULL && size == 0)
    {
        return 0;
    }
    if (string == NULL || size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (strnlen((const char*)string, size) >= size)
    {
        string[0] = '\0';
        return _NtCRT_InvalidParameter(EINVAL);
    }
    for (cp = string, dst = string; *cp; cp++)
    {
        if (NTCRT_ISLEAD(M, *cp))
        {
            UCHAR_T Result[2];
            int Length = _NtCRT_MapMbc(M, Upper, cp, Result);

            if (Length == 0)
            {
                string[0] = '\0';
                NTCRT_ERRNO = EILSEQ;
                return EILSEQ;
            }
            *dst++ = Result[0];
            cp++;
            if (Length > 1)
            {
                *dst++ = Result[1];
            }
        } else
        {
            *dst++ = Upper ? NTCRT_MBBTOUPPER(M, *cp) : NTCRT_MBBTOLOWER(M, *cp);
        }
    }
    *dst = '\0';
    return 0;
}

static errno_t _NtCRT_Mbcs_mbslwr_s(const NTCRT_MBC& M, UCHAR_T* string, size_t size)
{
    return _NtCRT_MbsCaseS(M, string, size, FALSE);
}

static errno_t _NtCRT_Mbcs_mbsupr_s(const NTCRT_MBC& M, UCHAR_T* string, size_t size)
{
    return _NtCRT_MbsCaseS(M, string, size, TRUE);
}

static UCHAR_T* _NtCRT_Mbcs_mbslwr(const NTCRT_MBC& M, UCHAR_T* string)
{
    return _NtCRT_MbsCaseS(M, string, string == NULL ? 0 : (size_t)-1, FALSE) == 0 ? string : NULL;
}

static UCHAR_T* _NtCRT_Mbcs_mbsupr(const NTCRT_MBC& M, UCHAR_T* string)
{
    return _NtCRT_MbsCaseS(M, string, string == NULL ? 0 : (size_t)-1, TRUE) == 0 ? string : NULL;
}

static UCHAR_T* _NtCRT_Mbcs_mbsrev(const NTCRT_MBC& M, UCHAR_T* string)
{
    UCHAR_T *Start = string, *Left, *Right, c;

    NTCRT_VALIDATE(string != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)_NtCRT__strrev((char*)string);
    }
    /* Swap the bytes of double-byte characters first */
    while (*string)
    {
        if (NTCRT_ISLEAD(M, *string++))
        {
            if (*string)
            {
                c = *string;
                *string = string[-1];
                string[-1] = c;
                string++;
            } else
            {
                /* A lead byte at the end is removed */
                NTCRT_ERRNO = EINVAL;
                *--string = '\0';
                break;
            }
        }
    }
    for (Left = Start, Right = string - 1; Left < Right; Left++, Right--)
    {
        c = *Left;
        *Left = *Right;
        *Right = c;
    }
    return Start;
}

static UCHAR_T* _NtCRT_Mbcs_mbsset(const NTCRT_MBC& M, UCHAR_T* string, unsigned int c)
{
    UCHAR_T *Start = string, High = (UCHAR_T)(c >> 8), Low = (UCHAR_T)c;

    NTCRT_VALIDATE(string != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)_NtCRT__strset((char*)string, (int)c);
    }
    if (High != 0)
    {
        while (*string)
        {
            *string++ = High;
            if (*string)
            {
                *string++ = Low;
            } else
            {
                /* No lead byte without its trail byte */
                string[-1] = ' ';
            }
        }
    } else
    {
        while (*string)
        {
            *string++ = Low;
        }
    }
    return Start;
}

static UCHAR_T* _NtCRT_Mbcs_mbsnset(const NTCRT_MBC& M, UCHAR_T* string, unsigned int c, size_t count)
{
    UCHAR_T *Start = string, High = (UCHAR_T)(c >> 8), Low = (UCHAR_T)c;

    if (count == 0)
    {
        return string;
    }
    NTCRT_VALIDATE(string != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)_NtCRT__strnset((char*)string, (int)c, count);
    }
    bool Lead = false;

    /* Count characters of c, Lead tracks whether the last byte replaced is a lead byte of the string */
    if (High != 0)
    {
        while (count-- && *string)
        {
            if (string[1] == '\0')
            {
                *string = ' ';
                Lead = false;
                break;
            }
            Lead = !Lead && NTCRT_ISLEAD(M, *string);
            *string++ = High;
            Lead = !Lead && NTCRT_ISLEAD(M, *string);
            *string++ = Low;
        }
    } else
    {
        while (count-- && *string)
        {
            Lead = !Lead && NTCRT_ISLEAD(M, *string);
            *string++ = Low;
        }
    }

    /* The trail byte of the last character replaced partially */
    if (Lead && *string)
    {
        *string = ' ';
    }
    return Start;
}

static UCHAR_T* _NtCRT_Mbcs_mbsnbset(const NTCRT_MBC& M, UCHAR_T* string, unsigned int c, size_t count)
{
    UCHAR_T *Start = string, High = (UCHAR_T)(c >> 8), Low = (UCHAR_T)c;

    if (count == 0)
    {
        return string;
    }
    NTCRT_VALIDATE(string != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)_NtCRT__strnset((char*)string, (int)c, count);
    }
    if (High != 0)
    {
        while (count-- && *string)
        {
            if (!count-- || string[1] == '\0')
            {
                *string = ' ';
                break;
            }
            *string++ = High;
            *string++ = Low;
        }
    } else
    {
        while (count-- && *string)
        {
            *string++ = Low;
        }
    }
    return Start;
}

static UCHAR_T* _NtCRT_Mbcs_mbsncpy(const NTCRT_MBC& M, UCHAR_T* dst, const UCHAR_T* src, size_t count)
{
    UCHAR_T* Start = dst;

    if (count == 0)
    {
        return dst;
    }
    NTCRT_VALIDATE(dst != NULL && src != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)strncpy((char*)dst, (const char*)src, count);
    }
    while (count)
    {
        count--;
        if (NTCRT_ISLEAD(M, *src))
        {
            *dst++ = *src++;
            if ((*dst++ = *src++) == '\0')
            {
                dst[-2] = '\0';
                break;
            }
        } else if ((*dst++ = *src++) == '\0')
        {
            break;
        }
    }
    while (count--)
    {
        *dst++ = '\0';
    }
    return Start;
}

static UCHAR_T* _NtCRT_Mbcs_mbsnbcpy(const NTCRT_MBC& M, UCHAR_T* dst, const UCHAR_T* src, size_t count)
{
    UCHAR_T* Start = dst;

    if (count == 0)
    {
        return dst;
    }
    NTCRT_VALIDATE(dst != NULL && src != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)strncpy((char*)dst, (const char*)src, count);
    }
    while (count)
    {
        count--;
        if (NTCRT_ISLEAD(M, *src))
        {
            *dst++ = *src++;
            if (count == 0)
            {
                dst[-1] = '\0';
                break;
            }
            count--;
            if ((*dst++ = *src++) == '\0')
            {
                dst[-2] = '\0';
                break;
            }
        } else if ((*dst++ = *src++) == '\0')
        {
            break;
        }
    }
    while (count--)
    {
        *dst++ = '\0';
    }
    return Start;
}

/* The end of dst for concatenation, before a lead byte without its trail byte */
static
UCHAR_T*
_NtCRT_MbsCatEnd(
    _In_ const NTCRT_MBC& M,
    _In_z_ UCHAR_T* dst)
{
    UCHAR_T* End = dst + strlen((const char*)dst);

    if (End != dst && _NtCRT_Mbcs_mbsbtype(M, dst, (size_t)(End - dst) - 1) == _MBC_LEAD)
    {
        End--;
    }
    return End;
}

static void _NtCRT_MbsCatFinish(const NTCRT_MBC& M, UCHAR_T* Start, UCHAR_T* dst)
{
    if (dst != Start && _NtCRT_Mbcs_mbsbtype(M, Start, (size_t)(dst - Start) - 1) == _MBC_LEAD)
    {
        dst[-1] = '\0';
    } else
    {
        *dst = '\0';
    }
}

static UCHAR_T* _NtCRT_Mbcs_mbsncat(const NTCRT_MBC& M, UCHAR_T* dst, const UCHAR_T* src, size_t count)
{
    UCHAR_T *Start = dst;

    if (count == 0)
    {
        return dst;
    }
    NTCRT_VALIDATE(dst != NULL && src != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)strncat((char*)dst, (const char*)src, count);
    }
    /* msvcrt tests the terminator for a lead byte, a lead byte at the end of dst is kept */
    dst += strlen((const char*)dst);
    while (count--)
    {
        if (NTCRT_ISLEAD(M, *src))
        {
            *dst++ = *src++;
            if ((*dst++ = *src++) == '\0')
            {
                dst[-2] = '\0';
                return Start;
            }
        } else if ((*dst++ = *src++) == '\0')
        {
            return Start;
        }
    }
    _NtCRT_MbsCatFinish(M, Start, dst);
    return Start;
}

static UCHAR_T* _NtCRT_Mbcs_mbsnbcat(const NTCRT_MBC& M, UCHAR_T* dst, const UCHAR_T* src, size_t count)
{
    UCHAR_T *Start = dst;

    if (count == 0)
    {
        return dst;
    }
    NTCRT_VALIDATE(dst != NULL && src != NULL, NULL);
    if (!M.IsMb)
    {
        return (UCHAR_T*)strncat((char*)dst, (const char*)src, count);
    }
    dst = _NtCRT_MbsCatEnd(M, dst);
    while (count--)
    {
        if (NTCRT_ISLEAD(M, *src))
        {
            *dst++ = *src++;
            if (count == 0)
            {
                dst[-1] = '\0';
                break;
            }
            count--;
            if ((*dst++ = *src++) == '\0')
            {
                dst[-2] = '\0';
                return Start;
            }
        } else if ((*dst++ = *src++) == '\0')
        {
            return Start;
        }
    }
    _NtCRT_MbsCatFinish(M, Start, dst);
    return Start;
}

#pragma endregion

#pragma region Secure functions

static errno_t _NtCRT_Mbcs_mbscpy_s(const NTCRT_MBC& M, UCHAR_T* dst, size_t size, const UCHAR_T* src)
{
    UCHAR_T* p;
    size_t Available;

    if (dst == NULL || size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (src == NULL)
    {
        dst[0] = '\0';
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (!M.IsMb)
    {
        return _NtCRT_strcpy_s((char*)dst, size, (const char*)src);
    }
    p = dst;
    Available = size;
    while ((*p++ = *src++) != 0 && --Available > 0)
    {
    }
    if (Available == 0)
    {
        if (*src == '\0' && _NtCRT_IsLeadPrefix(M, dst, p - 1))
        {
            p[-1] = '\0';
            NTCRT_ERRNO = EILSEQ;
            return EILSEQ;
        }
        dst[0] = '\0';
        return _NtCRT_InvalidParameter(ERANGE);
    }
    if (p - 2 >= dst && _NtCRT_IsLeadPrefix(M, dst, p - 2))
    {
        p[-2] = '\0';
        NTCRT_ERRNO = EILSEQ;
        return EILSEQ;
    }
    return 0;
}

static errno_t _NtCRT_Mbcs_mbscat_s(const NTCRT_MBC& M, UCHAR_T* dst, size_t size, const UCHAR_T* src)
{
    UCHAR_T* p;
    size_t Available;

    if (dst == NULL || size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (src == NULL)
    {
        dst[0] = '\0';
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (!M.IsMb)
    {
        return _NtCRT_strcat_s((char*)dst, size, (const char*)src);
    }
    p = dst;
    Available = size;
    while (Available > 0 && *p != '\0')
    {
        p++;
        Available--;
    }
    if (Available == 0)
    {
        dst[0] = '\0';
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (p > dst && _NtCRT_IsLeadPrefix(M, dst, p - 1))
    {
        /* The lead byte at the end of dst is replaced */
        p--;
        Available++;
    }
    while ((*p++ = *src++) != 0 && --Available > 0)
    {
    }
    if (Available == 0)
    {
        if (*src == '\0' && _NtCRT_IsLeadPrefix(M, dst, p - 1))
        {
            p[-1] = '\0';
            NTCRT_ERRNO = EILSEQ;
            return EILSEQ;
        }
        dst[0] = '\0';
        return _NtCRT_InvalidParameter(ERANGE);
    }
    if (p - 2 >= dst && _NtCRT_IsLeadPrefix(M, dst, p - 2))
    {
        p[-2] = '\0';
        NTCRT_ERRNO = EILSEQ;
        return EILSEQ;
    }
    return 0;
}

/* The result of a string with a lead byte without its trail byte */
static
errno_t
_NtCRT_MbcsError(void)
{
    NTCRT_ERRNO = EILSEQ;
    return EILSEQ;
}

/*
 * The end of copying count characters (or bytes if !ByChars, or all if _TRUNCATE) of src to p of dst, as msvcrt
 * copies them before the size is checked
 */
static
errno_t
_NtCRT_MbsCopyN(
    _In_ const NTCRT_MBC& M,
    _Inout_updates_z_(size) UCHAR_T* dst,
    _In_ size_t size,
    _Inout_ UCHAR_T* p,
    _In_ size_t Available,
    _In_z_ const UCHAR_T* src,
    _In_ size_t count,
    _In_ bool ByChars,
    _In_ bool Invalid)
{
    if (count == _TRUNCATE)
    {
        while ((*p++ = *src++) != 0 && --Available > 0)
        {
        }
    } else
    {
        if (!ByChars)
        {
            while ((*p++ = *src++) != 0 && --Available > 0 && --count > 0)
            {
            }
        } else
        {
            do
            {
                if (NTCRT_ISLEAD(M, *src))
                {
                    if (src[1] == '\0')
                    {
                        /* Only the terminator is copied */
                        *p++ = '\0';
                        Invalid = true;
                        break;
                    }
                    if (Available <= 2)
                    {
                        Available = 0;
                        break;
                    }
                    *p++ = *src++;
                    *p++ = *src++;
                    Available -= 2;
                } else if ((*p++ = *src++) == '\0' || --Available == 0)
                {
                    break;
                }
            } while (--count > 0);
        }
        if (count == 0)
        {
            *p++ = '\0';
        }
    }

    if (Available == 0)
    {
        /* A lead byte at the end, never copied by characters but by _TRUNCATE */
        if (!ByChars && (*src == '\0' || count == 1) && _NtCRT_IsLeadPrefix(M, dst, p - 1))
        {
            p[-1] = '\0';
            return _NtCRT_MbcsError();
        }
        if (count == _TRUNCATE)
        {
            if (Invalid)
            {
                NTCRT_ERRNO = EILSEQ;
            }
            if (size > 1 && _NtCRT_IsLeadPrefix(M, dst, dst + size - 2))
            {
                dst[size - 2] = '\0';
                return STRUNCATE;
            }
            dst[size - 1] = '\0';
            return STRUNCATE;
        }
        dst[0] = '\0';
        return _NtCRT_InvalidParameter(ERANGE);
    }
    if (!ByChars && p - 2 >= dst && _NtCRT_IsLeadPrefix(M, dst, p - 2))
    {
        p[-2] = '\0';
        return _NtCRT_MbcsError();
    }
    return Invalid ? _NtCRT_MbcsError() : 0;
}

static
errno_t
_NtCRT_MbsNCopyS(
    _In_ const NTCRT_MBC& M,
    _Out_writes_z_(size) UCHAR_T* dst,
    _In_ size_t size,
    _In_reads_opt_z_(count) const UCHAR_T* src,
    _In_ size_t count,
    _In_ bool ByChars)
{
    if (count == 0 && dst == NULL && size == 0)
    {
        return 0;
    }
    if (dst == NULL || size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (count == 0)
    {
        dst[0] = '\0';
        return 0;
    }
    if (src == NULL)
    {
        dst[0] = '\0';
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (!M.IsMb)
    {
        return _NtCRT_strncpy_s((char*)dst, size, (const char*)src, count);
    }
    return _NtCRT_MbsCopyN(M, dst, size, dst, size, src, count, ByChars, false);
}

static errno_t _NtCRT_Mbcs_mbsncpy_s(const NTCRT_MBC& M, UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count)
{
    return _NtCRT_MbsNCopyS(M, dst, size, src, count, true);
}

static errno_t _NtCRT_Mbcs_mbsnbcpy_s(const NTCRT_MBC& M, UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count)
{
    return _NtCRT_MbsNCopyS(M, dst, size, src, count, false);
}

static
errno_t
_NtCRT_MbsNCatS(
    _In_ const NTCRT_MBC& M,
    _Inout_updates_z_(size) UCHAR_T* dst,
    _In_ size_t size,
    _In_reads_opt_z_(count) const UCHAR_T* src,
    _In_ size_t count,
    _In_ bool ByChars)
{
    UCHAR_T* p;
    size_t Available;

    if (count == 0 && dst == NULL && size == 0)
    {
        return 0;
    }
    if (dst == NULL || size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (count != 0 && src == NULL)
    {
        dst[0] = '\0';
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (!M.IsMb)
    {
        return _NtCRT_strncat_s((char*)dst, size, (const char*)src, count);
    }
    p = dst;
    Available = size;
    while (Available > 0 && *p != '\0')
    {
        p++;
        Available--;
    }
    if (Available == 0)
    {
        dst[0] = '\0';
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (Available < size && _NtCRT_IsLeadPrefix(M, dst, p - 1))
    {
        /* The lead byte at the end of dst is replaced */
        p--;
        Available++;
    }
    if (count == 0)
    {
        *p = '\0';
        return 0;
    }
    return _NtCRT_MbsCopyN(M, dst, size, p, Available, src, count, ByChars, false);
}

static errno_t _NtCRT_Mbcs_mbsncat_s(const NTCRT_MBC& M, UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count)
{
    return _NtCRT_MbsNCatS(M, dst, size, src, count, true);
}

static errno_t _NtCRT_Mbcs_mbsnbcat_s(const NTCRT_MBC& M, UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count)
{
    return _NtCRT_MbsNCatS(M, dst, size, src, count, false);
}

/* Fill count characters (or bytes if !ByChars, or all if (size_t)-1) of string, written before it is checked */
static
errno_t
_NtCRT_MbsSetS(
    _In_ const NTCRT_MBC& M,
    _Inout_updates_z_(size) UCHAR_T* string,
    _In_ size_t size,
    _In_ unsigned int c,
    _In_ size_t count,
    _In_ bool ByChars)
{
    UCHAR_T *p, High = (UCHAR_T)(c >> 8), Low = (UCHAR_T)c;
    size_t Available;
    bool Lead = false, Invalid = false, All = count == (size_t)-1;

    if (count == 0 && string == NULL && size == 0 && !All)
    {
        return 0;
    }
    if (string == NULL || size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (!M.IsMb)
    {
        return All ? _NtCRT__strset_s((char*)string, size, (int)c)
                   : _NtCRT__strnset_s((char*)string, size, (int)c, count);
    }
    if ((High != 0 && (Low == 0 || !NTCRT_ISLEAD(M, High))) || (High == 0 && NTCRT_ISLEAD(M, Low)))
    {
        string[0] = '\0';
        return _NtCRT_MbcsError();
    }
    p = string;
    Available = size;
    if (All)
    {
        if (High != 0)
        {
            while (*p != '\0' && --Available > 0)
            {
                if (p[1] == '\0')
                {
                    *p = ' ';
                    break;
                }
                *p++ = High;
                if (--Available == 0)
                {
                    break;
                }
                *p++ = Low;
            }
        } else
        {
            while (*p != '\0' && --Available > 0)
            {
                *p++ = Low;
            }
        }
    } else
    {
        /* Lead tracks whether the last byte replaced is a lead byte of the string */
        if (High != 0)
        {
            if (!ByChars)
            {
                Invalid = (count & 1) != 0;
                count /= 2;
            }
            while (*p != '\0' && count > 0 && --Available > 0)
            {
                if (p[1] == '\0')
                {
                    /* No lead byte without its trail byte */
                    *p = '\0';
                    Available++;
                    Invalid = true;
                    break;
                }
                Lead = !Lead && NTCRT_ISLEAD(M, *p);
                *p++ = High;
                Lead = !Lead && NTCRT_ISLEAD(M, *p);
                *p++ = Low;
                Available--;
                count--;
            }
        } else
        {
            while (*p != '\0' && count > 0 && --Available > 0)
            {
                Lead = !Lead && NTCRT_ISLEAD(M, *p);
                *p++ = Low;
                count--;
            }
        }
        if (*p != '\0' && Lead && Available > 0)
        {
            *p = ' ';
        }
        if (count == 0)
        {
            while (*p != '\0' && --Available > 0)
            {
                p++;
            }
        }
    }
    if (Available == 0)
    {
        string[0] = '\0';
        return _NtCRT_InvalidParameter(EINVAL);
    }
    return Invalid ? _NtCRT_MbcsError() : 0;
}

static errno_t _NtCRT_Mbcs_mbsset_s(const NTCRT_MBC& M, UCHAR_T* string, size_t size, unsigned int c)
{
    return _NtCRT_MbsSetS(M, string, size, c, (size_t)-1, true);
}

static errno_t _NtCRT_Mbcs_mbsnset_s(const NTCRT_MBC& M, UCHAR_T* string, size_t size, unsigned int c, size_t count)
{
    return _NtCRT_MbsSetS(M, string, size, c, count, true);
}

static errno_t _NtCRT_Mbcs_mbsnbset_s(const NTCRT_MBC& M, UCHAR_T* string, size_t size, unsigned int c, size_t count)
{
    return _NtCRT_MbsSetS(M, string, size, c, count, false);
}

#pragma endregion

#pragma region Exports

/* Byte string functions of msvcrt under the multibyte names */

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbscat(UCHAR_T* dst, const UCHAR_T* src)
{
    return (UCHAR_T*)strcat((char*)dst, (const char*)src);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbscpy(UCHAR_T* dst, const UCHAR_T* src)
{
    return (UCHAR_T*)strcpy((char*)dst, (const char*)src);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsdup(const UCHAR_T* str)
{
    return (UCHAR_T*)_NtCRT__strdup((const char*)str);
}

EXTERN_C int __cdecl _NtCRT__ismbbalnum(unsigned int c)
{
    return _NtCRT_Mbcs_ismbbalnum(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbalnum_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbbalnum(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbalpha(unsigned int c)
{
    return _NtCRT_Mbcs_ismbbalpha(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbalpha_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbbalpha(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbgraph(unsigned int c)
{
    return _NtCRT_Mbcs_ismbbgraph(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbgraph_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbbgraph(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbkalnum(unsigned int c)
{
    return _NtCRT_Mbcs_ismbbkalnum(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbkalnum_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbbkalnum(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbkana(unsigned int c)
{
    return _NtCRT_Mbcs_ismbbkana(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbkana_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbbkana(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbkprint(unsigned int c)
{
    return _NtCRT_Mbcs_ismbbkprint(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbkprint_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbbkprint(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbkpunct(unsigned int c)
{
    return _NtCRT_Mbcs_ismbbkpunct(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbkpunct_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbbkpunct(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbblead(unsigned int c)
{
    return _NtCRT_Mbcs_ismbblead(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbblead_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbblead(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbprint(unsigned int c)
{
    return _NtCRT_Mbcs_ismbbprint(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbprint_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbbprint(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbpunct(unsigned int c)
{
    return _NtCRT_Mbcs_ismbbpunct(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbpunct_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbbpunct(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbtrail(unsigned int c)
{
    return _NtCRT_Mbcs_ismbbtrail(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbbtrail_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbbtrail(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcalnum(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcalnum(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcalnum_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcalnum(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcalpha(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcalpha(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcalpha_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcalpha(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcdigit(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcdigit(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcdigit_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcdigit(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcgraph(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcgraph(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcgraph_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcgraph(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbchira(unsigned int c)
{
    return _NtCRT_Mbcs_ismbchira(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbchira_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbchira(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbckata(unsigned int c)
{
    return _NtCRT_Mbcs_ismbckata(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbckata_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbckata(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcl0(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcl0(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcl0_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcl0(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcl1(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcl1(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcl1_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcl1(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcl2(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcl2(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcl2_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcl2(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbclegal(unsigned int c)
{
    return _NtCRT_Mbcs_ismbclegal(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbclegal_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbclegal(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbclower(unsigned int c)
{
    return _NtCRT_Mbcs_ismbclower(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbclower_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbclower(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcprint(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcprint(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcprint_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcprint(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcpunct(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcpunct(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcpunct_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcpunct(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcspace(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcspace(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcspace_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcspace(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcsymbol(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcsymbol(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcsymbol_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcsymbol(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcupper(unsigned int c)
{
    return _NtCRT_Mbcs_ismbcupper(_NtCRT_Mbc(NULL), c);
}

EXTERN_C int __cdecl _NtCRT__ismbcupper_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbcupper(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__ismbslead(const UCHAR_T* string, const UCHAR_T* current)
{
    return _NtCRT_Mbcs_ismbslead(_NtCRT_Mbc(NULL), string, current);
}

EXTERN_C int __cdecl _NtCRT__ismbslead_l(const UCHAR_T* string, const UCHAR_T* current, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbslead(_NtCRT_Mbc(locale), string, current);
}

EXTERN_C int __cdecl _NtCRT__ismbstrail(const UCHAR_T* string, const UCHAR_T* current)
{
    return _NtCRT_Mbcs_ismbstrail(_NtCRT_Mbc(NULL), string, current);
}

EXTERN_C int __cdecl _NtCRT__ismbstrail_l(const UCHAR_T* string, const UCHAR_T* current, _locale_t locale)
{
    return _NtCRT_Mbcs_ismbstrail(_NtCRT_Mbc(locale), string, current);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbbtombc(unsigned int c)
{
    return _NtCRT_Mbcs_mbbtombc(_NtCRT_Mbc(NULL), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbbtombc_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbbtombc(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__mbbtype(unsigned char c, int ctype)
{
    return _NtCRT_Mbcs_mbbtype(_NtCRT_Mbc(NULL), c, ctype);
}

EXTERN_C void __cdecl _NtCRT__mbccpy(UCHAR_T* dst, const UCHAR_T* src)
{
    _NtCRT_Mbcs_mbccpy(_NtCRT_Mbc(NULL), dst, src);
}

EXTERN_C void __cdecl _NtCRT__mbccpy_l(UCHAR_T* dst, const UCHAR_T* src, _locale_t locale)
{
    _NtCRT_Mbcs_mbccpy(_NtCRT_Mbc(locale), dst, src);
}

EXTERN_C errno_t __cdecl _NtCRT__mbccpy_s(UCHAR_T* dst, size_t size, int* copied, const UCHAR_T* src)
{
    return _NtCRT_Mbcs_mbccpy_s(_NtCRT_Mbc(NULL), dst, size, copied, src);
}

EXTERN_C errno_t __cdecl _NtCRT__mbccpy_s_l(
    UCHAR_T* dst, size_t size, int* copied, const UCHAR_T* src, _locale_t locale)
{
    return _NtCRT_Mbcs_mbccpy_s(_NtCRT_Mbc(locale), dst, size, copied, src);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbcjistojms(unsigned int c)
{
    return _NtCRT_Mbcs_mbcjistojms(_NtCRT_Mbc(NULL), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbcjistojms_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbcjistojms(_NtCRT_Mbc(locale), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbcjmstojis(unsigned int c)
{
    return _NtCRT_Mbcs_mbcjmstojis(_NtCRT_Mbc(NULL), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbcjmstojis_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbcjmstojis(_NtCRT_Mbc(locale), c);
}

EXTERN_C size_t __cdecl _NtCRT__mbclen(const UCHAR_T* c)
{
    return _NtCRT_Mbcs_mbclen(_NtCRT_Mbc(NULL), c);
}

EXTERN_C size_t __cdecl _NtCRT__mbclen_l(const UCHAR_T* c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbclen(_NtCRT_Mbc(locale), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbctohira(unsigned int c)
{
    return _NtCRT_Mbcs_mbctohira(_NtCRT_Mbc(NULL), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbctohira_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbctohira(_NtCRT_Mbc(locale), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbctokata(unsigned int c)
{
    return _NtCRT_Mbcs_mbctokata(_NtCRT_Mbc(NULL), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbctokata_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbctokata(_NtCRT_Mbc(locale), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbctolower(unsigned int c)
{
    return _NtCRT_Mbcs_mbctolower(_NtCRT_Mbc(NULL), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbctolower_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbctolower(_NtCRT_Mbc(locale), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbctombb(unsigned int c)
{
    return _NtCRT_Mbcs_mbctombb(_NtCRT_Mbc(NULL), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbctombb_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbctombb(_NtCRT_Mbc(locale), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbctoupper(unsigned int c)
{
    return _NtCRT_Mbcs_mbctoupper(_NtCRT_Mbc(NULL), c);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbctoupper_l(unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbctoupper(_NtCRT_Mbc(locale), c);
}

EXTERN_C int __cdecl _NtCRT__mbsbtype(const UCHAR_T* string, size_t count)
{
    return _NtCRT_Mbcs_mbsbtype(_NtCRT_Mbc(NULL), string, count);
}

EXTERN_C int __cdecl _NtCRT__mbsbtype_l(const UCHAR_T* string, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsbtype(_NtCRT_Mbc(locale), string, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbscat_s(UCHAR_T* dst, size_t size, const UCHAR_T* src)
{
    return _NtCRT_Mbcs_mbscat_s(_NtCRT_Mbc(NULL), dst, size, src);
}

EXTERN_C errno_t __cdecl _NtCRT__mbscat_s_l(UCHAR_T* dst, size_t size, const UCHAR_T* src, _locale_t locale)
{
    return _NtCRT_Mbcs_mbscat_s(_NtCRT_Mbc(locale), dst, size, src);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbschr(const UCHAR_T* string, unsigned int c)
{
    return _NtCRT_Mbcs_mbschr(_NtCRT_Mbc(NULL), string, c);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbschr_l(const UCHAR_T* string, unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbschr(_NtCRT_Mbc(locale), string, c);
}

EXTERN_C int __cdecl _NtCRT__mbscmp(const UCHAR_T* s1, const UCHAR_T* s2)
{
    return _NtCRT_Mbcs_mbscmp(_NtCRT_Mbc(NULL), s1, s2);
}

EXTERN_C int __cdecl _NtCRT__mbscmp_l(const UCHAR_T* s1, const UCHAR_T* s2, _locale_t locale)
{
    return _NtCRT_Mbcs_mbscmp(_NtCRT_Mbc(locale), s1, s2);
}

EXTERN_C int __cdecl _NtCRT__mbscoll(const UCHAR_T* s1, const UCHAR_T* s2)
{
    return _NtCRT_Mbcs_mbscoll(_NtCRT_Mbc(NULL), s1, s2);
}

EXTERN_C int __cdecl _NtCRT__mbscoll_l(const UCHAR_T* s1, const UCHAR_T* s2, _locale_t locale)
{
    return _NtCRT_Mbcs_mbscoll(_NtCRT_Mbc(locale), s1, s2);
}

EXTERN_C errno_t __cdecl _NtCRT__mbscpy_s(UCHAR_T* dst, size_t size, const UCHAR_T* src)
{
    return _NtCRT_Mbcs_mbscpy_s(_NtCRT_Mbc(NULL), dst, size, src);
}

EXTERN_C errno_t __cdecl _NtCRT__mbscpy_s_l(UCHAR_T* dst, size_t size, const UCHAR_T* src, _locale_t locale)
{
    return _NtCRT_Mbcs_mbscpy_s(_NtCRT_Mbc(locale), dst, size, src);
}

EXTERN_C size_t __cdecl _NtCRT__mbscspn(const UCHAR_T* string, const UCHAR_T* charset)
{
    return _NtCRT_Mbcs_mbscspn(_NtCRT_Mbc(NULL), string, charset);
}

EXTERN_C size_t __cdecl _NtCRT__mbscspn_l(const UCHAR_T* string, const UCHAR_T* charset, _locale_t locale)
{
    return _NtCRT_Mbcs_mbscspn(_NtCRT_Mbc(locale), string, charset);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsdec(const UCHAR_T* string, const UCHAR_T* current)
{
    return _NtCRT_Mbcs_mbsdec(_NtCRT_Mbc(NULL), string, current);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsdec_l(const UCHAR_T* string, const UCHAR_T* current, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsdec(_NtCRT_Mbc(locale), string, current);
}

EXTERN_C int __cdecl _NtCRT__mbsicmp(const UCHAR_T* s1, const UCHAR_T* s2)
{
    return _NtCRT_Mbcs_mbsicmp(_NtCRT_Mbc(NULL), s1, s2);
}

EXTERN_C int __cdecl _NtCRT__mbsicmp_l(const UCHAR_T* s1, const UCHAR_T* s2, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsicmp(_NtCRT_Mbc(locale), s1, s2);
}

EXTERN_C int __cdecl _NtCRT__mbsicoll(const UCHAR_T* s1, const UCHAR_T* s2)
{
    return _NtCRT_Mbcs_mbsicoll(_NtCRT_Mbc(NULL), s1, s2);
}

EXTERN_C int __cdecl _NtCRT__mbsicoll_l(const UCHAR_T* s1, const UCHAR_T* s2, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsicoll(_NtCRT_Mbc(locale), s1, s2);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsinc(const UCHAR_T* current)
{
    return _NtCRT_Mbcs_mbsinc(_NtCRT_Mbc(NULL), current);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsinc_l(const UCHAR_T* current, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsinc(_NtCRT_Mbc(locale), current);
}

EXTERN_C size_t __cdecl _NtCRT__mbslen(const UCHAR_T* s)
{
    return _NtCRT_Mbcs_mbslen(_NtCRT_Mbc(NULL), s);
}

EXTERN_C size_t __cdecl _NtCRT__mbslen_l(const UCHAR_T* s, _locale_t locale)
{
    return _NtCRT_Mbcs_mbslen(_NtCRT_Mbc(locale), s);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbslwr(UCHAR_T* string)
{
    return _NtCRT_Mbcs_mbslwr(_NtCRT_Mbc(NULL), string);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbslwr_l(UCHAR_T* string, _locale_t locale)
{
    return _NtCRT_Mbcs_mbslwr(_NtCRT_Mbc(locale), string);
}

EXTERN_C errno_t __cdecl _NtCRT__mbslwr_s(UCHAR_T* string, size_t size)
{
    return _NtCRT_Mbcs_mbslwr_s(_NtCRT_Mbc(NULL), string, size);
}

EXTERN_C errno_t __cdecl _NtCRT__mbslwr_s_l(UCHAR_T* string, size_t size, _locale_t locale)
{
    return _NtCRT_Mbcs_mbslwr_s(_NtCRT_Mbc(locale), string, size);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsnbcat(UCHAR_T* dst, const UCHAR_T* src, size_t count)
{
    return _NtCRT_Mbcs_mbsnbcat(_NtCRT_Mbc(NULL), dst, src, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsnbcat_l(UCHAR_T* dst, const UCHAR_T* src, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnbcat(_NtCRT_Mbc(locale), dst, src, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsnbcat_s(UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count)
{
    return _NtCRT_Mbcs_mbsnbcat_s(_NtCRT_Mbc(NULL), dst, size, src, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsnbcat_s_l(
    UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnbcat_s(_NtCRT_Mbc(locale), dst, size, src, count);
}

EXTERN_C int __cdecl _NtCRT__mbsnbcmp(const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    return _NtCRT_Mbcs_mbsnbcmp(_NtCRT_Mbc(NULL), s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__mbsnbcmp_l(const UCHAR_T* s1, const UCHAR_T* s2, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnbcmp(_NtCRT_Mbc(locale), s1, s2, count);
}

EXTERN_C size_t __cdecl _NtCRT__mbsnbcnt(const UCHAR_T* string, size_t ccnt)
{
    return _NtCRT_Mbcs_mbsnbcnt(_NtCRT_Mbc(NULL), string, ccnt);
}

EXTERN_C size_t __cdecl _NtCRT__mbsnbcnt_l(const UCHAR_T* string, size_t ccnt, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnbcnt(_NtCRT_Mbc(locale), string, ccnt);
}

EXTERN_C int __cdecl _NtCRT__mbsnbcoll(const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    return _NtCRT_Mbcs_mbsnbcoll(_NtCRT_Mbc(NULL), s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__mbsnbcoll_l(const UCHAR_T* s1, const UCHAR_T* s2, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnbcoll(_NtCRT_Mbc(locale), s1, s2, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsnbcpy(UCHAR_T* dst, const UCHAR_T* src, size_t count)
{
    return _NtCRT_Mbcs_mbsnbcpy(_NtCRT_Mbc(NULL), dst, src, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsnbcpy_l(UCHAR_T* dst, const UCHAR_T* src, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnbcpy(_NtCRT_Mbc(locale), dst, src, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsnbcpy_s(UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count)
{
    return _NtCRT_Mbcs_mbsnbcpy_s(_NtCRT_Mbc(NULL), dst, size, src, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsnbcpy_s_l(
    UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnbcpy_s(_NtCRT_Mbc(locale), dst, size, src, count);
}

EXTERN_C int __cdecl _NtCRT__mbsnbicmp(const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    return _NtCRT_Mbcs_mbsnbicmp(_NtCRT_Mbc(NULL), s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__mbsnbicmp_l(const UCHAR_T* s1, const UCHAR_T* s2, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnbicmp(_NtCRT_Mbc(locale), s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__mbsnbicoll(const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    return _NtCRT_Mbcs_mbsnbicoll(_NtCRT_Mbc(NULL), s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__mbsnbicoll_l(const UCHAR_T* s1, const UCHAR_T* s2, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnbicoll(_NtCRT_Mbc(locale), s1, s2, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsnbset(UCHAR_T* string, unsigned int c, size_t count)
{
    return _NtCRT_Mbcs_mbsnbset(_NtCRT_Mbc(NULL), string, c, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsnbset_l(UCHAR_T* string, unsigned int c, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnbset(_NtCRT_Mbc(locale), string, c, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsnbset_s(UCHAR_T* string, size_t size, unsigned int c, size_t count)
{
    return _NtCRT_Mbcs_mbsnbset_s(_NtCRT_Mbc(NULL), string, size, c, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsnbset_s_l(
    UCHAR_T* string, size_t size, unsigned int c, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnbset_s(_NtCRT_Mbc(locale), string, size, c, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsncat(UCHAR_T* dst, const UCHAR_T* src, size_t count)
{
    return _NtCRT_Mbcs_mbsncat(_NtCRT_Mbc(NULL), dst, src, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsncat_l(UCHAR_T* dst, const UCHAR_T* src, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsncat(_NtCRT_Mbc(locale), dst, src, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsncat_s(UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count)
{
    return _NtCRT_Mbcs_mbsncat_s(_NtCRT_Mbc(NULL), dst, size, src, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsncat_s_l(
    UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsncat_s(_NtCRT_Mbc(locale), dst, size, src, count);
}

EXTERN_C size_t __cdecl _NtCRT__mbsnccnt(const UCHAR_T* string, size_t bcnt)
{
    return _NtCRT_Mbcs_mbsnccnt(_NtCRT_Mbc(NULL), string, bcnt);
}

EXTERN_C size_t __cdecl _NtCRT__mbsnccnt_l(const UCHAR_T* string, size_t bcnt, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnccnt(_NtCRT_Mbc(locale), string, bcnt);
}

EXTERN_C int __cdecl _NtCRT__mbsncmp(const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    return _NtCRT_Mbcs_mbsncmp(_NtCRT_Mbc(NULL), s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__mbsncmp_l(const UCHAR_T* s1, const UCHAR_T* s2, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsncmp(_NtCRT_Mbc(locale), s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__mbsncoll(const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    return _NtCRT_Mbcs_mbsncoll(_NtCRT_Mbc(NULL), s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__mbsncoll_l(const UCHAR_T* s1, const UCHAR_T* s2, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsncoll(_NtCRT_Mbc(locale), s1, s2, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsncpy(UCHAR_T* dst, const UCHAR_T* src, size_t count)
{
    return _NtCRT_Mbcs_mbsncpy(_NtCRT_Mbc(NULL), dst, src, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsncpy_l(UCHAR_T* dst, const UCHAR_T* src, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsncpy(_NtCRT_Mbc(locale), dst, src, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsncpy_s(UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count)
{
    return _NtCRT_Mbcs_mbsncpy_s(_NtCRT_Mbc(NULL), dst, size, src, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsncpy_s_l(
    UCHAR_T* dst, size_t size, const UCHAR_T* src, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsncpy_s(_NtCRT_Mbc(locale), dst, size, src, count);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbsnextc(const UCHAR_T* s)
{
    return _NtCRT_Mbcs_mbsnextc(_NtCRT_Mbc(NULL), s);
}

EXTERN_C unsigned int __cdecl _NtCRT__mbsnextc_l(const UCHAR_T* s, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnextc(_NtCRT_Mbc(locale), s);
}

EXTERN_C int __cdecl _NtCRT__mbsnicmp(const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    return _NtCRT_Mbcs_mbsnicmp(_NtCRT_Mbc(NULL), s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__mbsnicmp_l(const UCHAR_T* s1, const UCHAR_T* s2, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnicmp(_NtCRT_Mbc(locale), s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__mbsnicoll(const UCHAR_T* s1, const UCHAR_T* s2, size_t count)
{
    return _NtCRT_Mbcs_mbsnicoll(_NtCRT_Mbc(NULL), s1, s2, count);
}

EXTERN_C int __cdecl _NtCRT__mbsnicoll_l(const UCHAR_T* s1, const UCHAR_T* s2, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnicoll(_NtCRT_Mbc(locale), s1, s2, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsninc(const UCHAR_T* string, size_t ccnt)
{
    return _NtCRT_Mbcs_mbsninc(_NtCRT_Mbc(NULL), string, ccnt);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsninc_l(const UCHAR_T* string, size_t ccnt, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsninc(_NtCRT_Mbc(locale), string, ccnt);
}

EXTERN_C size_t __cdecl _NtCRT__mbsnlen(const UCHAR_T* s, size_t maxsize)
{
    return _NtCRT_Mbcs_mbsnlen(_NtCRT_Mbc(NULL), s, maxsize);
}

EXTERN_C size_t __cdecl _NtCRT__mbsnlen_l(const UCHAR_T* s, size_t maxsize, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnlen(_NtCRT_Mbc(locale), s, maxsize);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsnset(UCHAR_T* string, unsigned int c, size_t count)
{
    return _NtCRT_Mbcs_mbsnset(_NtCRT_Mbc(NULL), string, c, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsnset_l(UCHAR_T* string, unsigned int c, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnset(_NtCRT_Mbc(locale), string, c, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsnset_s(UCHAR_T* string, size_t size, unsigned int c, size_t count)
{
    return _NtCRT_Mbcs_mbsnset_s(_NtCRT_Mbc(NULL), string, size, c, count);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsnset_s_l(
    UCHAR_T* string, size_t size, unsigned int c, size_t count, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsnset_s(_NtCRT_Mbc(locale), string, size, c, count);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbspbrk(const UCHAR_T* string, const UCHAR_T* charset)
{
    return _NtCRT_Mbcs_mbspbrk(_NtCRT_Mbc(NULL), string, charset);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbspbrk_l(const UCHAR_T* string, const UCHAR_T* charset, _locale_t locale)
{
    return _NtCRT_Mbcs_mbspbrk(_NtCRT_Mbc(locale), string, charset);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsrchr(const UCHAR_T* string, unsigned int c)
{
    return _NtCRT_Mbcs_mbsrchr(_NtCRT_Mbc(NULL), string, c);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsrchr_l(const UCHAR_T* string, unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsrchr(_NtCRT_Mbc(locale), string, c);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsrev(UCHAR_T* string)
{
    return _NtCRT_Mbcs_mbsrev(_NtCRT_Mbc(NULL), string);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsrev_l(UCHAR_T* string, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsrev(_NtCRT_Mbc(locale), string);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsset(UCHAR_T* string, unsigned int c)
{
    return _NtCRT_Mbcs_mbsset(_NtCRT_Mbc(NULL), string, c);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsset_l(UCHAR_T* string, unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsset(_NtCRT_Mbc(locale), string, c);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsset_s(UCHAR_T* string, size_t size, unsigned int c)
{
    return _NtCRT_Mbcs_mbsset_s(_NtCRT_Mbc(NULL), string, size, c);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsset_s_l(UCHAR_T* string, size_t size, unsigned int c, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsset_s(_NtCRT_Mbc(locale), string, size, c);
}

EXTERN_C size_t __cdecl _NtCRT__mbsspn(const UCHAR_T* string, const UCHAR_T* charset)
{
    return _NtCRT_Mbcs_mbsspn(_NtCRT_Mbc(NULL), string, charset);
}

EXTERN_C size_t __cdecl _NtCRT__mbsspn_l(const UCHAR_T* string, const UCHAR_T* charset, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsspn(_NtCRT_Mbc(locale), string, charset);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsspnp(const UCHAR_T* string, const UCHAR_T* charset)
{
    return _NtCRT_Mbcs_mbsspnp(_NtCRT_Mbc(NULL), string, charset);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsspnp_l(const UCHAR_T* string, const UCHAR_T* charset, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsspnp(_NtCRT_Mbc(locale), string, charset);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsstr(const UCHAR_T* str1, const UCHAR_T* str2)
{
    return _NtCRT_Mbcs_mbsstr(_NtCRT_Mbc(NULL), str1, str2);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsstr_l(const UCHAR_T* str1, const UCHAR_T* str2, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsstr(_NtCRT_Mbc(locale), str1, str2);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbstok(UCHAR_T* string, const UCHAR_T* delim)
{
    return _NtCRT_Mbcs_mbstok(_NtCRT_Mbc(NULL), string, delim);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbstok_l(UCHAR_T* string, const UCHAR_T* delim, _locale_t locale)
{
    return _NtCRT_Mbcs_mbstok(_NtCRT_Mbc(locale), string, delim);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbstok_s(UCHAR_T* string, const UCHAR_T* delim, UCHAR_T** context)
{
    return _NtCRT_Mbcs_mbstok_s(_NtCRT_Mbc(NULL), string, delim, context);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbstok_s_l(UCHAR_T* string, const UCHAR_T* delim, UCHAR_T** context, _locale_t locale)
{
    return _NtCRT_Mbcs_mbstok_s(_NtCRT_Mbc(locale), string, delim, context);
}

EXTERN_C size_t __cdecl _NtCRT__mbstrlen(const char* s)
{
    return _NtCRT_Mbcs_mbstrlen(_NtCRT_Mbc(NULL), s);
}

EXTERN_C size_t __cdecl _NtCRT__mbstrlen_l(const char* s, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);

    return _NtCRT_Mbcs_mbstrlen(_NtCRT_Mbc(locale), s);
}

EXTERN_C size_t __cdecl _NtCRT__mbstrnlen(const char* s, size_t maxsize)
{
    return _NtCRT_Mbcs_mbstrnlen(_NtCRT_Mbc(NULL), s, maxsize);
}

EXTERN_C size_t __cdecl _NtCRT__mbstrnlen_l(const char* s, size_t maxsize, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);

    return _NtCRT_Mbcs_mbstrnlen(_NtCRT_Mbc(locale), s, maxsize);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsupr(UCHAR_T* string)
{
    return _NtCRT_Mbcs_mbsupr(_NtCRT_Mbc(NULL), string);
}

EXTERN_C UCHAR_T* __cdecl _NtCRT__mbsupr_l(UCHAR_T* string, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsupr(_NtCRT_Mbc(locale), string);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsupr_s(UCHAR_T* string, size_t size)
{
    return _NtCRT_Mbcs_mbsupr_s(_NtCRT_Mbc(NULL), string, size);
}

EXTERN_C errno_t __cdecl _NtCRT__mbsupr_s_l(UCHAR_T* string, size_t size, _locale_t locale)
{
    return _NtCRT_Mbcs_mbsupr_s(_NtCRT_Mbc(locale), string, size);
}

/* The narrow _splitpath goes by the multibyte code page as in msvcrt: a trail byte 0x5C is not a separator */
EXTERN_C void __cdecl _NtCRT__splitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
{
    NTCRT_MBC M = _NtCRT_Mbc(NULL);

    _NtCRT_SplitPath(path, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT, true,
                     M.IsMb ? M.Type : NULL);
}

EXTERN_C errno_t __cdecl _NtCRT__splitpath_s(const char* path,
                                             char* drive,
                                             size_t driveSize,
                                             char* dir,
                                             size_t dirSize,
                                             char* fname,
                                             size_t fnameSize,
                                             char* ext,
                                             size_t extSize)
{
    NTCRT_MBC M = _NtCRT_Mbc(NULL);

    return _NtCRT_SplitPathS(path, drive, driveSize, dir, dirSize, fname, fnameSize, ext, extSize,
                             M.IsMb ? M.Type : NULL);
}

#pragma endregion

NTCRT_IMPORT(_splitpath, _NtCRT__splitpath);
NTCRT_IMPORT(_splitpath_s, _NtCRT__splitpath_s);
NTCRT_IMPORT(_setmbcp, _NtCRT__setmbcp);
NTCRT_IMPORT(_getmbcp, _NtCRT__getmbcp);
NTCRT_IMPORT(_mbscat, _NtCRT__mbscat);
NTCRT_IMPORT(_mbscpy, _NtCRT__mbscpy);
NTCRT_IMPORT(_mbsdup, _NtCRT__mbsdup);
NTCRT_IMPORT_DATA(_mbctype, _NtCRT_mbctype);
NTCRT_IMPORT_DATA(_mbcasemap, _NtCRT_mbcasemap);
NTCRT_IMPORT(_ismbbalnum, _NtCRT__ismbbalnum);
NTCRT_IMPORT(_ismbbalnum_l, _NtCRT__ismbbalnum_l);
NTCRT_IMPORT(_ismbbalpha, _NtCRT__ismbbalpha);
NTCRT_IMPORT(_ismbbalpha_l, _NtCRT__ismbbalpha_l);
NTCRT_IMPORT(_ismbbgraph, _NtCRT__ismbbgraph);
NTCRT_IMPORT(_ismbbgraph_l, _NtCRT__ismbbgraph_l);
NTCRT_IMPORT(_ismbbkalnum, _NtCRT__ismbbkalnum);
NTCRT_IMPORT(_ismbbkalnum_l, _NtCRT__ismbbkalnum_l);
NTCRT_IMPORT(_ismbbkana, _NtCRT__ismbbkana);
NTCRT_IMPORT(_ismbbkana_l, _NtCRT__ismbbkana_l);
NTCRT_IMPORT(_ismbbkprint, _NtCRT__ismbbkprint);
NTCRT_IMPORT(_ismbbkprint_l, _NtCRT__ismbbkprint_l);
NTCRT_IMPORT(_ismbbkpunct, _NtCRT__ismbbkpunct);
NTCRT_IMPORT(_ismbbkpunct_l, _NtCRT__ismbbkpunct_l);
NTCRT_IMPORT(_ismbblead, _NtCRT__ismbblead);
NTCRT_IMPORT(_ismbblead_l, _NtCRT__ismbblead_l);
NTCRT_IMPORT(_ismbbprint, _NtCRT__ismbbprint);
NTCRT_IMPORT(_ismbbprint_l, _NtCRT__ismbbprint_l);
NTCRT_IMPORT(_ismbbpunct, _NtCRT__ismbbpunct);
NTCRT_IMPORT(_ismbbpunct_l, _NtCRT__ismbbpunct_l);
NTCRT_IMPORT(_ismbbtrail, _NtCRT__ismbbtrail);
NTCRT_IMPORT(_ismbbtrail_l, _NtCRT__ismbbtrail_l);
NTCRT_IMPORT(_ismbcalnum, _NtCRT__ismbcalnum);
NTCRT_IMPORT(_ismbcalnum_l, _NtCRT__ismbcalnum_l);
NTCRT_IMPORT(_ismbcalpha, _NtCRT__ismbcalpha);
NTCRT_IMPORT(_ismbcalpha_l, _NtCRT__ismbcalpha_l);
NTCRT_IMPORT(_ismbcdigit, _NtCRT__ismbcdigit);
NTCRT_IMPORT(_ismbcdigit_l, _NtCRT__ismbcdigit_l);
NTCRT_IMPORT(_ismbcgraph, _NtCRT__ismbcgraph);
NTCRT_IMPORT(_ismbcgraph_l, _NtCRT__ismbcgraph_l);
NTCRT_IMPORT(_ismbchira, _NtCRT__ismbchira);
NTCRT_IMPORT(_ismbchira_l, _NtCRT__ismbchira_l);
NTCRT_IMPORT(_ismbckata, _NtCRT__ismbckata);
NTCRT_IMPORT(_ismbckata_l, _NtCRT__ismbckata_l);
NTCRT_IMPORT(_ismbcl0, _NtCRT__ismbcl0);
NTCRT_IMPORT(_ismbcl0_l, _NtCRT__ismbcl0_l);
NTCRT_IMPORT(_ismbcl1, _NtCRT__ismbcl1);
NTCRT_IMPORT(_ismbcl1_l, _NtCRT__ismbcl1_l);
NTCRT_IMPORT(_ismbcl2, _NtCRT__ismbcl2);
NTCRT_IMPORT(_ismbcl2_l, _NtCRT__ismbcl2_l);
NTCRT_IMPORT(_ismbclegal, _NtCRT__ismbclegal);
NTCRT_IMPORT(_ismbclegal_l, _NtCRT__ismbclegal_l);
NTCRT_IMPORT(_ismbclower, _NtCRT__ismbclower);
NTCRT_IMPORT(_ismbclower_l, _NtCRT__ismbclower_l);
NTCRT_IMPORT(_ismbcprint, _NtCRT__ismbcprint);
NTCRT_IMPORT(_ismbcprint_l, _NtCRT__ismbcprint_l);
NTCRT_IMPORT(_ismbcpunct, _NtCRT__ismbcpunct);
NTCRT_IMPORT(_ismbcpunct_l, _NtCRT__ismbcpunct_l);
NTCRT_IMPORT(_ismbcspace, _NtCRT__ismbcspace);
NTCRT_IMPORT(_ismbcspace_l, _NtCRT__ismbcspace_l);
NTCRT_IMPORT(_ismbcsymbol, _NtCRT__ismbcsymbol);
NTCRT_IMPORT(_ismbcsymbol_l, _NtCRT__ismbcsymbol_l);
NTCRT_IMPORT(_ismbcupper, _NtCRT__ismbcupper);
NTCRT_IMPORT(_ismbcupper_l, _NtCRT__ismbcupper_l);
NTCRT_IMPORT(_ismbslead, _NtCRT__ismbslead);
NTCRT_IMPORT(_ismbslead_l, _NtCRT__ismbslead_l);
NTCRT_IMPORT(_ismbstrail, _NtCRT__ismbstrail);
NTCRT_IMPORT(_ismbstrail_l, _NtCRT__ismbstrail_l);
NTCRT_IMPORT(_mbbtombc, _NtCRT__mbbtombc);
NTCRT_IMPORT(_mbbtombc_l, _NtCRT__mbbtombc_l);
NTCRT_IMPORT(_mbbtype, _NtCRT__mbbtype);
NTCRT_IMPORT(_mbccpy, _NtCRT__mbccpy);
NTCRT_IMPORT(_mbccpy_l, _NtCRT__mbccpy_l);
NTCRT_IMPORT(_mbccpy_s, _NtCRT__mbccpy_s);
NTCRT_IMPORT(_mbccpy_s_l, _NtCRT__mbccpy_s_l);
NTCRT_IMPORT(_mbcjistojms, _NtCRT__mbcjistojms);
NTCRT_IMPORT(_mbcjistojms_l, _NtCRT__mbcjistojms_l);
NTCRT_IMPORT(_mbcjmstojis, _NtCRT__mbcjmstojis);
NTCRT_IMPORT(_mbcjmstojis_l, _NtCRT__mbcjmstojis_l);
NTCRT_IMPORT(_mbclen, _NtCRT__mbclen);
NTCRT_IMPORT(_mbclen_l, _NtCRT__mbclen_l);
NTCRT_IMPORT(_mbctohira, _NtCRT__mbctohira);
NTCRT_IMPORT(_mbctohira_l, _NtCRT__mbctohira_l);
NTCRT_IMPORT(_mbctokata, _NtCRT__mbctokata);
NTCRT_IMPORT(_mbctokata_l, _NtCRT__mbctokata_l);
NTCRT_IMPORT(_mbctolower, _NtCRT__mbctolower);
NTCRT_IMPORT(_mbctolower_l, _NtCRT__mbctolower_l);
NTCRT_IMPORT(_mbctombb, _NtCRT__mbctombb);
NTCRT_IMPORT(_mbctombb_l, _NtCRT__mbctombb_l);
NTCRT_IMPORT(_mbctoupper, _NtCRT__mbctoupper);
NTCRT_IMPORT(_mbctoupper_l, _NtCRT__mbctoupper_l);
NTCRT_IMPORT(_mbsbtype, _NtCRT__mbsbtype);
NTCRT_IMPORT(_mbsbtype_l, _NtCRT__mbsbtype_l);
NTCRT_IMPORT(_mbscat_s, _NtCRT__mbscat_s);
NTCRT_IMPORT(_mbscat_s_l, _NtCRT__mbscat_s_l);
NTCRT_IMPORT(_mbschr, _NtCRT__mbschr);
NTCRT_IMPORT(_mbschr_l, _NtCRT__mbschr_l);
NTCRT_IMPORT(_mbscmp, _NtCRT__mbscmp);
NTCRT_IMPORT(_mbscmp_l, _NtCRT__mbscmp_l);
NTCRT_IMPORT(_mbscoll, _NtCRT__mbscoll);
NTCRT_IMPORT(_mbscoll_l, _NtCRT__mbscoll_l);
NTCRT_IMPORT(_mbscpy_s, _NtCRT__mbscpy_s);
NTCRT_IMPORT(_mbscpy_s_l, _NtCRT__mbscpy_s_l);
NTCRT_IMPORT(_mbscspn, _NtCRT__mbscspn);
NTCRT_IMPORT(_mbscspn_l, _NtCRT__mbscspn_l);
NTCRT_IMPORT(_mbsdec, _NtCRT__mbsdec);
NTCRT_IMPORT(_mbsdec_l, _NtCRT__mbsdec_l);
NTCRT_IMPORT(_mbsicmp, _NtCRT__mbsicmp);
NTCRT_IMPORT(_mbsicmp_l, _NtCRT__mbsicmp_l);
NTCRT_IMPORT(_mbsicoll, _NtCRT__mbsicoll);
NTCRT_IMPORT(_mbsicoll_l, _NtCRT__mbsicoll_l);
NTCRT_IMPORT(_mbsinc, _NtCRT__mbsinc);
NTCRT_IMPORT(_mbsinc_l, _NtCRT__mbsinc_l);
NTCRT_IMPORT(_mbslen, _NtCRT__mbslen);
NTCRT_IMPORT(_mbslen_l, _NtCRT__mbslen_l);
NTCRT_IMPORT(_mbslwr, _NtCRT__mbslwr);
NTCRT_IMPORT(_mbslwr_l, _NtCRT__mbslwr_l);
NTCRT_IMPORT(_mbslwr_s, _NtCRT__mbslwr_s);
NTCRT_IMPORT(_mbslwr_s_l, _NtCRT__mbslwr_s_l);
NTCRT_IMPORT(_mbsnbcat, _NtCRT__mbsnbcat);
NTCRT_IMPORT(_mbsnbcat_l, _NtCRT__mbsnbcat_l);
NTCRT_IMPORT(_mbsnbcat_s, _NtCRT__mbsnbcat_s);
NTCRT_IMPORT(_mbsnbcat_s_l, _NtCRT__mbsnbcat_s_l);
NTCRT_IMPORT(_mbsnbcmp, _NtCRT__mbsnbcmp);
NTCRT_IMPORT(_mbsnbcmp_l, _NtCRT__mbsnbcmp_l);
NTCRT_IMPORT(_mbsnbcnt, _NtCRT__mbsnbcnt);
NTCRT_IMPORT(_mbsnbcnt_l, _NtCRT__mbsnbcnt_l);
NTCRT_IMPORT(_mbsnbcoll, _NtCRT__mbsnbcoll);
NTCRT_IMPORT(_mbsnbcoll_l, _NtCRT__mbsnbcoll_l);
NTCRT_IMPORT(_mbsnbcpy, _NtCRT__mbsnbcpy);
NTCRT_IMPORT(_mbsnbcpy_l, _NtCRT__mbsnbcpy_l);
NTCRT_IMPORT(_mbsnbcpy_s, _NtCRT__mbsnbcpy_s);
NTCRT_IMPORT(_mbsnbcpy_s_l, _NtCRT__mbsnbcpy_s_l);
NTCRT_IMPORT(_mbsnbicmp, _NtCRT__mbsnbicmp);
NTCRT_IMPORT(_mbsnbicmp_l, _NtCRT__mbsnbicmp_l);
NTCRT_IMPORT(_mbsnbicoll, _NtCRT__mbsnbicoll);
NTCRT_IMPORT(_mbsnbicoll_l, _NtCRT__mbsnbicoll_l);
NTCRT_IMPORT(_mbsnbset, _NtCRT__mbsnbset);
NTCRT_IMPORT(_mbsnbset_l, _NtCRT__mbsnbset_l);
NTCRT_IMPORT(_mbsnbset_s, _NtCRT__mbsnbset_s);
NTCRT_IMPORT(_mbsnbset_s_l, _NtCRT__mbsnbset_s_l);
NTCRT_IMPORT(_mbsncat, _NtCRT__mbsncat);
NTCRT_IMPORT(_mbsncat_l, _NtCRT__mbsncat_l);
NTCRT_IMPORT(_mbsncat_s, _NtCRT__mbsncat_s);
NTCRT_IMPORT(_mbsncat_s_l, _NtCRT__mbsncat_s_l);
NTCRT_IMPORT(_mbsnccnt, _NtCRT__mbsnccnt);
NTCRT_IMPORT(_mbsnccnt_l, _NtCRT__mbsnccnt_l);
NTCRT_IMPORT(_mbsncmp, _NtCRT__mbsncmp);
NTCRT_IMPORT(_mbsncmp_l, _NtCRT__mbsncmp_l);
NTCRT_IMPORT(_mbsncoll, _NtCRT__mbsncoll);
NTCRT_IMPORT(_mbsncoll_l, _NtCRT__mbsncoll_l);
NTCRT_IMPORT(_mbsncpy, _NtCRT__mbsncpy);
NTCRT_IMPORT(_mbsncpy_l, _NtCRT__mbsncpy_l);
NTCRT_IMPORT(_mbsncpy_s, _NtCRT__mbsncpy_s);
NTCRT_IMPORT(_mbsncpy_s_l, _NtCRT__mbsncpy_s_l);
NTCRT_IMPORT(_mbsnextc, _NtCRT__mbsnextc);
NTCRT_IMPORT(_mbsnextc_l, _NtCRT__mbsnextc_l);
NTCRT_IMPORT(_mbsnicmp, _NtCRT__mbsnicmp);
NTCRT_IMPORT(_mbsnicmp_l, _NtCRT__mbsnicmp_l);
NTCRT_IMPORT(_mbsnicoll, _NtCRT__mbsnicoll);
NTCRT_IMPORT(_mbsnicoll_l, _NtCRT__mbsnicoll_l);
NTCRT_IMPORT(_mbsninc, _NtCRT__mbsninc);
NTCRT_IMPORT(_mbsninc_l, _NtCRT__mbsninc_l);
NTCRT_IMPORT(_mbsnlen, _NtCRT__mbsnlen);
NTCRT_IMPORT(_mbsnlen_l, _NtCRT__mbsnlen_l);
NTCRT_IMPORT(_mbsnset, _NtCRT__mbsnset);
NTCRT_IMPORT(_mbsnset_l, _NtCRT__mbsnset_l);
NTCRT_IMPORT(_mbsnset_s, _NtCRT__mbsnset_s);
NTCRT_IMPORT(_mbsnset_s_l, _NtCRT__mbsnset_s_l);
NTCRT_IMPORT(_mbspbrk, _NtCRT__mbspbrk);
NTCRT_IMPORT(_mbspbrk_l, _NtCRT__mbspbrk_l);
NTCRT_IMPORT(_mbsrchr, _NtCRT__mbsrchr);
NTCRT_IMPORT(_mbsrchr_l, _NtCRT__mbsrchr_l);
NTCRT_IMPORT(_mbsrev, _NtCRT__mbsrev);
NTCRT_IMPORT(_mbsrev_l, _NtCRT__mbsrev_l);
NTCRT_IMPORT(_mbsset, _NtCRT__mbsset);
NTCRT_IMPORT(_mbsset_l, _NtCRT__mbsset_l);
NTCRT_IMPORT(_mbsset_s, _NtCRT__mbsset_s);
NTCRT_IMPORT(_mbsset_s_l, _NtCRT__mbsset_s_l);
NTCRT_IMPORT(_mbsspn, _NtCRT__mbsspn);
NTCRT_IMPORT(_mbsspn_l, _NtCRT__mbsspn_l);
NTCRT_IMPORT(_mbsspnp, _NtCRT__mbsspnp);
NTCRT_IMPORT(_mbsspnp_l, _NtCRT__mbsspnp_l);
NTCRT_IMPORT(_mbsstr, _NtCRT__mbsstr);
NTCRT_IMPORT(_mbsstr_l, _NtCRT__mbsstr_l);
NTCRT_IMPORT(_mbstok, _NtCRT__mbstok);
NTCRT_IMPORT(_mbstok_l, _NtCRT__mbstok_l);
NTCRT_IMPORT(_mbstok_s, _NtCRT__mbstok_s);
NTCRT_IMPORT(_mbstok_s_l, _NtCRT__mbstok_s_l);
NTCRT_IMPORT(_mbstrlen, _NtCRT__mbstrlen);
NTCRT_IMPORT(_mbstrlen_l, _NtCRT__mbstrlen_l);
NTCRT_IMPORT(_mbstrnlen, _NtCRT__mbstrnlen);
NTCRT_IMPORT(_mbstrnlen_l, _NtCRT__mbstrnlen_l);
NTCRT_IMPORT(_mbsupr, _NtCRT__mbsupr);
NTCRT_IMPORT(_mbsupr_l, _NtCRT__mbsupr_l);
NTCRT_IMPORT(_mbsupr_s, _NtCRT__mbsupr_s);
NTCRT_IMPORT(_mbsupr_s_l, _NtCRT__mbsupr_s_l);
