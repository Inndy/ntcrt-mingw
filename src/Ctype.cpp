/*
 * Character class tables: _ctype, _pctype, _pwctype and _wctype of the "C" locale, see Locale.cpp for "C.UTF-8".
 */

#include "NtCRT.h"

#include <ctype.h>

/* Character class tables of the "C" locale */

static
constexpr
unsigned short
_NtCRT_CtypeOf(
    unsigned int c)
{
    if (c >= '\t' && c <= '\r')
    {
        return _SPACE | _CONTROL;
    } else if (c < 0x20 || c == 0x7F)
    {
        return _CONTROL;
    } else if (c == ' ')
    {
        return _SPACE | _BLANK;
    } else if (c >= '0' && c <= '9')
    {
        return _DIGIT | _HEX;
    } else if (c >= 'A' && c <= 'Z')
    {
        return c <= 'F' ? _UPPER | _HEX : _UPPER;
    } else if (c >= 'a' && c <= 'z')
    {
        return c <= 'f' ? _LOWER | _HEX : _LOWER;
    } else if (c < 0x7F)
    {
        return _PUNCT;
    }
    return 0;
}

/* Latin-1 classes of the wide table, a tab is blank in the wide table only. _ALPHA includes _UPPER and _LOWER. */
static
constexpr
unsigned short
_NtCRT_WctypeOf(
    unsigned int c)
{
    if (c == '\t')
    {
        return _SPACE | _CONTROL | _BLANK;
    } else if (c < 0x80)
    {
        return _NtCRT_CtypeOf(c);
    } else if (c < 0xA0)
    {
        return _CONTROL;
    } else if (c == 0xA0)
    {
        return _SPACE | _BLANK;
    } else if (c == 0xAA || c == 0xB5 || c == 0xBA || c == 0xDF || (c >= 0xE0 && c != 0xF7))
    {
        return _LOWER | C1_ALPHA;
    } else if (c >= 0xC0 && c != 0xD7 && c <= 0xDE)
    {
        return _UPPER | C1_ALPHA;
    } else if (c == 0xB2 || c == 0xB3 || c == 0xB9)
    {
        return _DIGIT | _PUNCT;
    }
    return _PUNCT;
}

static
constexpr
NTCRT_CTYPE_TABLE
_NtCRT_MakeCtype(
    bool Wide)
{
    NTCRT_CTYPE_TABLE t {};
    t.Table[0] = 0;
    for (unsigned int c = 0; c < 256; c++)
    {
        t.Table[c + 1] = Wide ? _NtCRT_WctypeOf(c) : _NtCRT_CtypeOf(c);
    }
    return t;
}

static constexpr NTCRT_CTYPE_TABLE g_Ctype = _NtCRT_MakeCtype(false);
static constexpr NTCRT_CTYPE_TABLE g_Wctype = _NtCRT_MakeCtype(true);

/* _ctype is writable data in msvcrt */
extern "C"
{
    NTCRT_CTYPE_TABLE _NtCRT_ctype = g_Ctype;
    unsigned short* _NtCRT_pctype = _NtCRT_ctype.Table + 1;
    extern const NTCRT_CTYPE_TABLE _NtCRT_wctype;
    const NTCRT_CTYPE_TABLE _NtCRT_wctype = g_Wctype;
    unsigned short* _NtCRT_pwctype = (unsigned short*)_NtCRT_wctype.Table + 1;
}

EXTERN_C unsigned short* __cdecl _NtCRT___pctype_func(void)
{
    return _NtCRT_pctype;
}

EXTERN_C unsigned short* __cdecl _NtCRT___pwctype_func(void)
{
    return _NtCRT_pwctype;
}

EXTERN_C unsigned short** __cdecl _NtCRT___p__pctype(void)
{
    return &_NtCRT_pctype;
}

EXTERN_C unsigned short** __cdecl _NtCRT___p__pwctype(void)
{
    return &_NtCRT_pwctype;
}

NTCRT_IMPORT_DATA(_ctype, _NtCRT_ctype);
NTCRT_IMPORT_DATA(_pctype, _NtCRT_pctype);
NTCRT_IMPORT_DATA(_pwctype, _NtCRT_pwctype);
NTCRT_IMPORT_DATA(_wctype, _NtCRT_wctype);
NTCRT_IMPORT(__pctype_func, _NtCRT___pctype_func);
NTCRT_IMPORT(__pwctype_func, _NtCRT___pwctype_func);
NTCRT_IMPORT(__p__pctype, _NtCRT___p__pctype);
NTCRT_IMPORT(__p__pwctype, _NtCRT___p__pwctype);
