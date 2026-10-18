/*
 * The multibyte characters of the locale one at a time: the UTF-8 codec, mbrtowc, wcrtomb, btowc and wctob, which
 * printf and the wide stdio use. See Multibyte.cpp for the strings and the other functions.
 */

#include "NtCRT.h"
#include "Multibyte.inl"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <wchar.h>

EXTERN_C
size_t
_NtCRT_EncodingError(
    _Inout_ mbstate_t* State)
{
    *State = 0;
    NTCRT_ERRNO = EILSEQ;
    return NTCRT_ERROR;
}

/*
 * Decode a UTF-8 character continuing the state, returns the bytes consumed, 0 for the null character,
 * NTCRT_INCOMPLETE or NTCRT_ERROR. If Split, a character beyond U+FFFF is returned as the high surrogate after
 * its 3rd byte and the low surrogate after its 4th byte.
 */
EXTERN_C
size_t
_NtCRT_Utf8Decode(
    _Out_ char32_t* Char,
    _In_reads_(Count) const unsigned char* Bytes,
    _In_ size_t Count,
    _Inout_ mbstate_t* State,
    _In_ BOOL Split)
{
    unsigned int s = (unsigned int)*State, Value, Need, Length, Flags = 0;
    size_t i = 0;

    if (s & (NTCRT_MBS_LOW_PENDING | NTCRT_MBS_HIGH_SAVED))
    {
        return _NtCRT_EncodingError(State);
    }
    if (NTCRT_MBS_NEED(s) == 0)
    {
        unsigned char Lead;

        if (Count == 0)
        {
            return NTCRT_INCOMPLETE;
        }
        Lead = Bytes[i++];
        if (Lead < 0x80)
        {
            *Char = Lead;
            *State = 0;
            return Lead != 0;
        } else if (Lead < 0xC2)
        {
            return _NtCRT_EncodingError(State);
        } else if (Lead < 0xE0)
        {
            Value = Lead & 0x1F;
            Length = 2;
        } else if (Lead < 0xF0)
        {
            Value = Lead & 0x0F;
            Length = 3;
        } else if (Lead < 0xF5)
        {
            Value = Lead & 0x07;
            Length = 4;
        } else
        {
            return _NtCRT_EncodingError(State);
        }
        Need = Length - 1;
    } else
    {
        Value = NTCRT_MBS_VALUE(s);
        Need = NTCRT_MBS_NEED(s);
        Length = NTCRT_MBS_LENGTH(s);
        Flags = s & NTCRT_MBS_HIGH_DONE;
    }
    while (Need > 0)
    {
        unsigned char Low = 0x80, High = 0xBF, Byte;

        if (i == Count)
        {
            *State = (mbstate_t)NTCRT_MBS(Value, Need, Length, Flags);
            return NTCRT_INCOMPLETE;
        }
        if (Need == Length - 1)
        {
            /* Overlong forms, surrogates and code points beyond U+10FFFF are excluded by the 2nd byte */
            if (Length == 3)
            {
                Low = Value == 0 ? 0xA0 : 0x80;
                High = Value == 0xD ? 0x9F : 0xBF;
            } else if (Length == 4)
            {
                Low = Value == 0 ? 0x90 : 0x80;
                High = Value == 4 ? 0x8F : 0xBF;
            }
        }
        Byte = Bytes[i];
        if (Byte < Low || Byte > High)
        {
            return _NtCRT_EncodingError(State);
        }
        Value = (Value << 6) | (Byte & 0x3F);
        Need--;
        i++;
        if (Split && Length == 4 && Need == 1 && !(Flags & NTCRT_MBS_HIGH_DONE))
        {
            *Char = 0xD800 + (((Value << 6) - 0x10000) >> 10);
            *State = (mbstate_t)NTCRT_MBS(Value, 1, 4, NTCRT_MBS_HIGH_DONE);
            return i;
        }
    }
    *Char = (Flags & NTCRT_MBS_HIGH_DONE) ? 0xDC00 | (Value & 0x3FF) : Value;
    *State = 0;
    return i;
}

/* Encode a code point of U+0000 - U+10FFFF but surrogates as UTF-8, returns the length */
EXTERN_C
size_t
_NtCRT_Utf8Encode(
    _Out_writes_(4) char* Bytes,
    _In_ char32_t Char)
{
    if (Char < 0x80)
    {
        Bytes[0] = (char)Char;
        return 1;
    } else if (Char < 0x800)
    {
        Bytes[0] = (char)(0xC0 | (Char >> 6));
        Bytes[1] = (char)(0x80 | (Char & 0x3F));
        return 2;
    } else if (Char < 0x10000)
    {
        Bytes[0] = (char)(0xE0 | (Char >> 12));
        Bytes[1] = (char)(0x80 | ((Char >> 6) & 0x3F));
        Bytes[2] = (char)(0x80 | (Char & 0x3F));
        return 3;
    }
    Bytes[0] = (char)(0xF0 | (Char >> 18));
    Bytes[1] = (char)(0x80 | ((Char >> 12) & 0x3F));
    Bytes[2] = (char)(0x80 | ((Char >> 6) & 0x3F));
    Bytes[3] = (char)(0x80 | (Char & 0x3F));
    return 4;
}

/* Encode a UTF-16 code unit continuing the state, returns the length, 0 for a high surrogate, or NTCRT_ERROR */
static
size_t
_NtCRT_Utf8EncodeUnit(
    _Out_writes_(4) char* Bytes,
    _In_ char16_t Unit,
    _Inout_ mbstate_t* State)
{
    unsigned int s = (unsigned int)*State;

    if (s & NTCRT_MBS_HIGH_SAVED)
    {
        if (Unit < 0xDC00 || Unit > 0xDFFF)
        {
            return _NtCRT_EncodingError(State);
        }
        *State = 0;
        return _NtCRT_Utf8Encode(Bytes, 0x10000 + ((NTCRT_MBS_VALUE(s) - 0xD800) << 10) + (Unit - 0xDC00));
    }
    if (s != 0 || (Unit >= 0xDC00 && Unit <= 0xDFFF))
    {
        return _NtCRT_EncodingError(State);
    }
    if (Unit >= 0xD800 && Unit <= 0xDBFF)
    {
        *State = (mbstate_t)(NTCRT_MBS_HIGH_SAVED | Unit);
        return 0;
    }
    return _NtCRT_Utf8Encode(Bytes, Unit);
}

/* mbrtowc by a code page, Split as _NtCRT_Utf8Decode */
EXTERN_C
size_t
_NtCRT_MbrToWc(
    _In_ UINT CodePage,
    _Out_opt_ wchar_t* pwc,
    _In_reads_bytes_opt_(n) const char* s,
    _In_ size_t n,
    _Inout_ mbstate_t* ps)
{
    char32_t Char;
    size_t Ret;

    if (s == NULL)
    {
        pwc = NULL;
        s = "";
        n = 1;
    }
    if (CodePage != CP_UTF8)
    {
        if (n == 0)
        {
            return NTCRT_INCOMPLETE;
        }
        if (pwc != NULL)
        {
            *pwc = (unsigned char)*s;
        }
        return *s != '\0';
    }
    Ret = _NtCRT_Utf8Decode(&Char, (const unsigned char*)s, n, ps, TRUE);
    if (Ret != NTCRT_ERROR && Ret != NTCRT_INCOMPLETE && pwc != NULL)
    {
        *pwc = (wchar_t)Char;
    }
    return Ret;
}

/* wcrtomb by a code page */
EXTERN_C
size_t
_NtCRT_WcrToMb(
    _In_ UINT CodePage,
    _Out_writes_opt_(MB_LEN_MAX) char* s,
    _In_ wchar_t wc,
    _Inout_ mbstate_t* ps)
{
    char Buffer[MB_LEN_MAX];

    if (s == NULL)
    {
        s = Buffer;
        wc = L'\0';
    }
    if (CodePage == CP_UTF8)
    {
        return _NtCRT_Utf8EncodeUnit(s, wc, ps);
    }
    if (wc > 0xFF)
    {
        return _NtCRT_EncodingError(ps);
    }
    *s = (char)wc;
    return 1;
}

static mbstate_t g_MbrtowcState;
static mbstate_t g_WcrtombState;

EXTERN_C size_t __cdecl _NtCRT_mbrtowc(wchar_t* pwc, const char* s, size_t n, mbstate_t* ps)
{
    return _NtCRT_MbrToWc(_NtCRT_CurrentCodePage(), pwc, s, n, ps != NULL ? ps : &g_MbrtowcState);
}

EXTERN_C size_t __cdecl _NtCRT_wcrtomb(char* s, wchar_t wc, mbstate_t* ps)
{
    return _NtCRT_WcrToMb(_NtCRT_CurrentCodePage(), s, wc, ps != NULL ? ps : &g_WcrtombState);
}

EXTERN_C wint_t __cdecl _NtCRT_btowc(int c)
{
    if (c == EOF || (c & ~0xFF) != 0)
    {
        return WEOF;
    }
    return _NtCRT_CurrentCodePage() == CP_UTF8 && c >= 0x80 ? WEOF : (wint_t)c;
}

EXTERN_C int __cdecl _NtCRT_wctob(wint_t c)
{
    return c < (_NtCRT_CurrentCodePage() == CP_UTF8 ? 0x80u : 0x100u) ? (int)c : EOF;
}

/* libmingwex defines btowc and wctob too and precedes libntcrt, the specs wrap them to these */
EXTERN_C wint_t __cdecl __wrap_btowc(int c)
{
    return _NtCRT_btowc(c);
}

EXTERN_C int __cdecl __wrap_wctob(wint_t c)
{
    return _NtCRT_wctob(c);
}

NTCRT_IMPORT(mbrtowc, _NtCRT_mbrtowc);
NTCRT_IMPORT(wcrtomb, _NtCRT_wcrtomb);
NTCRT_IMPORT(btowc, _NtCRT_btowc);
NTCRT_IMPORT(wctob, _NtCRT_wctob);
