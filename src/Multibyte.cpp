/*
 * Multibyte characters of the locale, see NtCRT.h. The UTF-8 codec, mbrtowc, wcrtomb, btowc and wctob are in
 * MultibyteChar.cpp.
 *
 * In the "C" locale a byte is the character of the same code point (U+0000 - U+00FF), as msvcrt. In "C.UTF-8"
 * the characters are UTF-8, a character beyond U+FFFF is a surrogate pair of wchar_t: mbrtowc and mbtowc return
 * the high surrogate for the first 3 bytes of it and the low surrogate for the 4th byte, wcrtomb and wctomb keep
 * a high surrogate in the state and write the character with the low surrogate. mbrtoc16 and c16rtomb work in
 * the same way as C11 defines. Invalid and overlong sequences, surrogates and code points beyond U+10FFFF are
 * encoding errors.
 */

#include "NtCRT.h"
#include "Multibyte.inl"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma region C89 functions

static mbstate_t g_MbtowcState;
static mbstate_t g_WctombState;

EXTERN_C int __cdecl _NtCRT_mblen(const char* s, size_t n)
{
    mbstate_t State = 0;
    char32_t Char;
    size_t Ret;

    if (s == NULL)
    {
        return 0;
    }
    if (_NtCRT_CurrentCodePage() != CP_UTF8)
    {
        return n == 0 ? -1 : *s != '\0';
    }
    Ret = _NtCRT_Utf8Decode(&Char, (const unsigned char*)s, n, &State, FALSE);
    if (Ret == NTCRT_INCOMPLETE)
    {
        NTCRT_ERRNO = EILSEQ;
        return -1;
    }
    return (int)Ret;
}

EXTERN_C int __cdecl _NtCRT_mbtowc(wchar_t* pwc, const char* s, size_t n)
{
    size_t Ret;

    if (s == NULL)
    {
        g_MbtowcState = 0;
        return 0;
    }
    Ret = _NtCRT_MbrToWc(_NtCRT_CurrentCodePage(), pwc, s, n, &g_MbtowcState);
    if (Ret == NTCRT_INCOMPLETE)
    {
        g_MbtowcState = 0;
        NTCRT_ERRNO = EILSEQ;
        return -1;
    }
    return (int)Ret;
}

EXTERN_C int __cdecl _NtCRT_wctomb(char* s, wchar_t wc)
{
    if (s == NULL)
    {
        g_WctombState = 0;
        return 0;
    }
    return (int)_NtCRT_WcrToMb(_NtCRT_CurrentCodePage(), s, wc, &g_WctombState);
}

EXTERN_C errno_t __cdecl _NtCRT_wctomb_s(int* pRetValue, char* mbchar, size_t sizeInBytes, wchar_t wchar)
{
    char Buffer[MB_LEN_MAX];
    size_t Length;

    if (pRetValue != NULL)
    {
        *pRetValue = -1;
    }
    if (mbchar == NULL && sizeInBytes > 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (mbchar == NULL)
    {
        g_WctombState = 0;
        if (pRetValue != NULL)
        {
            *pRetValue = 0;
        }
        return 0;
    }
    Length = _NtCRT_WcrToMb(_NtCRT_CurrentCodePage(), Buffer, wchar, &g_WctombState);
    if (Length == NTCRT_ERROR)
    {
        if (sizeInBytes > 0)
        {
            *mbchar = '\0';
        }
        return EILSEQ;
    }
    if (Length > sizeInBytes)
    {
        if (sizeInBytes > 0)
        {
            *mbchar = '\0';
        }
        return _NtCRT_InvalidParameter(ERANGE);
    }
    memcpy(mbchar, Buffer, Length);
    if (pRetValue != NULL)
    {
        *pRetValue = (int)Length;
    }
    return 0;
}

/*
 * Convert a multibyte string to at most Count wchar_t (any if Dest is NULL), a surrogate pair is not split.
 * *Src is set as mbsrtowcs does.
 */
static
size_t
_NtCRT_MbsToWcs(
    _Out_writes_opt_(Count) wchar_t* Dest,
    _Inout_ const char** Src,
    _In_ size_t Count,
    _Inout_ mbstate_t* State)
{
    UINT CodePage = _NtCRT_CurrentCodePage();
    const char* p = *Src;
    size_t Done = 0;

    while (Dest == NULL || Done < Count)
    {
        char32_t Char;
        size_t Length, Units;

        if (CodePage != CP_UTF8)
        {
            Char = (unsigned char)*p;
            Length = Char != 0;
        } else
        {
            mbstate_t Saved = *State;

            Length = _NtCRT_Utf8Decode(&Char, (const unsigned char*)p, (size_t)-1, State, FALSE);
            if (Length == NTCRT_ERROR)
            {
                if (Dest != NULL)
                {
                    *Src = p;
                }
                return NTCRT_ERROR;
            }
            if (Dest != NULL && Char >= 0x10000 && Done + 2 > Count)
            {
                *State = Saved;
                break;
            }
        }
        if (Char == 0)
        {
            if (Dest != NULL)
            {
                Dest[Done] = L'\0';
                *Src = NULL;
            }
            return Done;
        }
        Units = Char >= 0x10000 ? 2 : 1;
        if (Dest != NULL)
        {
            if (Units == 2)
            {
                Dest[Done] = (wchar_t)(0xD800 + ((Char - 0x10000) >> 10));
                Dest[Done + 1] = (wchar_t)(0xDC00 | (Char & 0x3FF));
            } else
            {
                Dest[Done] = (wchar_t)Char;
            }
        }
        Done += Units;
        p += Length;
    }
    *Src = p;
    return Done;
}

/*
 * Convert a wide string to at most Count bytes (any if Dest is NULL), a multibyte character is not split.
 * *Src is set as wcsrtombs does.
 */
static
size_t
_NtCRT_WcsToMbs(
    _Out_writes_opt_(Count) char* Dest,
    _Inout_ const wchar_t** Src,
    _In_ size_t Count,
    _Inout_ mbstate_t* State)
{
    UINT CodePage = _NtCRT_CurrentCodePage();
    const wchar_t* p = *Src;
    size_t Done = 0;

    for (;; p++)
    {
        char Buffer[MB_LEN_MAX];
        mbstate_t Saved = *State;
        size_t Length;

        /* A full buffer ends the conversion before the next character, the low surrogate of a pair goes on */
        if (Dest != NULL && Done == Count && !((unsigned int)Saved & NTCRT_MBS_HIGH_SAVED))
        {
            break;
        }
        Length = _NtCRT_WcrToMb(CodePage, Buffer, *p, State);
        if (Length == NTCRT_ERROR)
        {
            /* At the high surrogate of an invalid pair */
            if (Dest != NULL)
            {
                *Src = ((unsigned int)Saved & NTCRT_MBS_HIGH_SAVED) && p > *Src ? p - 1 : p;
            }
            return NTCRT_ERROR;
        }
        if (*p == L'\0')
        {
            if (Dest != NULL)
            {
                /* The null byte does not fit, *Src is left at the null character */
                if (Done == Count)
                {
                    break;
                }
                Dest[Done] = '\0';
                *Src = NULL;
            }
            return Done;
        }
        if (Dest != NULL)
        {
            if (Done + Length > Count)
            {
                /* Back to the high surrogate of a pair */
                *State = Saved;
                if (Length == 4 && p > *Src)
                {
                    p--;
                    *State = 0;
                }
                break;
            }
            memcpy(Dest + Done, Buffer, Length);
        }
        Done += Length;
    }
    *Src = p;
    return Done;
}

EXTERN_C size_t __cdecl _NtCRT_mbstowcs(wchar_t* dest, const char* src, size_t count)
{
    mbstate_t State = 0;

    if (src == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NTCRT_ERROR;
    }
    return _NtCRT_MbsToWcs(dest, &src, count, &State);
}

EXTERN_C size_t __cdecl _NtCRT_wcstombs(char* dest, const wchar_t* src, size_t count)
{
    mbstate_t State = 0;

    if (src == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NTCRT_ERROR;
    }
    return _NtCRT_WcsToMbs(dest, &src, count, &State);
}

/* Count the multibyte characters in at most MaxBytes bytes, MaxBytes without a null byte, NTCRT_ERROR for an error */
EXTERN_C size_t _NtCRT_MbsLength(const char* Str, size_t MaxBytes)
{
    size_t Count = 0, i = 0;
    mbstate_t State = 0;

    if (_NtCRT_CurrentCodePage() != CP_UTF8)
    {
        return strnlen(Str, MaxBytes);
    }
    while (i < MaxBytes && Str[i] != '\0')
    {
        char32_t Char;
        size_t Length = _NtCRT_Utf8Decode(&Char, (const unsigned char*)Str + i, MaxBytes - i, &State, FALSE);

        if (Length == NTCRT_ERROR || Length == NTCRT_INCOMPLETE)
        {
            NTCRT_ERRNO = EILSEQ;
            return NTCRT_ERROR;
        }
        i += Length;
        Count++;
    }
    return i < MaxBytes ? Count : MaxBytes;
}

#pragma endregion

#pragma region Restartable functions

static mbstate_t g_MbrlenState;
static mbstate_t g_MbsrtowcsState;
static mbstate_t g_WcsrtombsState;
static mbstate_t g_Mbrtoc16State;
static mbstate_t g_C16rtombState;
static mbstate_t g_Mbrtoc32State;
static mbstate_t g_C32rtombState;

EXTERN_C int __cdecl _NtCRT_mbsinit(const mbstate_t* ps)
{
    return ps == NULL || *ps == 0;
}

EXTERN_C size_t __cdecl _NtCRT_mbrlen(const char* s, size_t n, mbstate_t* ps)
{
    return _NtCRT_MbrToWc(_NtCRT_CurrentCodePage(), NULL, s, n, ps != NULL ? ps : &g_MbrlenState);
}

EXTERN_C size_t __cdecl _NtCRT_mbsrtowcs(wchar_t* dst, const char** src, size_t len, mbstate_t* ps)
{
    if (src == NULL || *src == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NTCRT_ERROR;
    }
    return _NtCRT_MbsToWcs(dst, src, len, ps != NULL ? ps : &g_MbsrtowcsState);
}

EXTERN_C size_t __cdecl _NtCRT_wcsrtombs(char* dst, const wchar_t** src, size_t len, mbstate_t* ps)
{
    if (src == NULL || *src == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NTCRT_ERROR;
    }
    return _NtCRT_WcsToMbs(dst, src, len, ps != NULL ? ps : &g_WcsrtombsState);
}

EXTERN_C size_t __cdecl _NtCRT_mbrtoc16(char16_t* pc16, const char* s, size_t n, mbstate_t* ps)
{
    char32_t Char;
    size_t Ret;

    if (ps == NULL)
    {
        ps = &g_Mbrtoc16State;
    }
    if (s == NULL)
    {
        pc16 = NULL;
        s = "";
        n = 1;
    }
    if ((unsigned int)*ps & NTCRT_MBS_LOW_PENDING)
    {
        if (pc16 != NULL)
        {
            *pc16 = (char16_t)NTCRT_MBS_VALUE((unsigned int)*ps);
        }
        *ps = 0;
        return NTCRT_PENDING;
    }
    if (_NtCRT_CurrentCodePage() != CP_UTF8)
    {
        return _NtCRT_MbrToWc(0, (wchar_t*)pc16, s, n, ps);
    }
    Ret = _NtCRT_Utf8Decode(&Char, (const unsigned char*)s, n, ps, FALSE);
    if (Ret == NTCRT_ERROR || Ret == NTCRT_INCOMPLETE)
    {
        return Ret;
    }
    if (Char >= 0x10000)
    {
        *ps = (mbstate_t)(NTCRT_MBS_LOW_PENDING | 0xDC00 | (Char & 0x3FF));
        Char = 0xD800 + ((Char - 0x10000) >> 10);
    }
    if (pc16 != NULL)
    {
        *pc16 = (char16_t)Char;
    }
    return Ret;
}

EXTERN_C size_t __cdecl _NtCRT_c16rtomb(char* s, char16_t c16, mbstate_t* ps)
{
    return _NtCRT_WcrToMb(_NtCRT_CurrentCodePage(), s, c16, ps != NULL ? ps : &g_C16rtombState);
}

EXTERN_C size_t __cdecl _NtCRT_mbrtoc32(char32_t* pc32, const char* s, size_t n, mbstate_t* ps)
{
    char32_t Char;
    size_t Ret;

    if (ps == NULL)
    {
        ps = &g_Mbrtoc32State;
    }
    if (s == NULL)
    {
        pc32 = NULL;
        s = "";
        n = 1;
    }
    if (_NtCRT_CurrentCodePage() != CP_UTF8)
    {
        if (n == 0)
        {
            return NTCRT_INCOMPLETE;
        }
        Char = (unsigned char)*s;
        Ret = Char != 0;
    } else
    {
        Ret = _NtCRT_Utf8Decode(&Char, (const unsigned char*)s, n, ps, FALSE);
        if (Ret == NTCRT_ERROR || Ret == NTCRT_INCOMPLETE)
        {
            return Ret;
        }
    }
    if (pc32 != NULL)
    {
        *pc32 = Char;
    }
    return Ret;
}

EXTERN_C size_t __cdecl _NtCRT_c32rtomb(char* s, char32_t c32, mbstate_t* ps)
{
    char Buffer[MB_LEN_MAX];

    if (ps == NULL)
    {
        ps = &g_C32rtombState;
    }
    if (s == NULL)
    {
        s = Buffer;
        c32 = 0;
    }
    if (*ps != 0 || (c32 >= 0xD800 && c32 <= 0xDFFF) ||
        c32 >= (_NtCRT_CurrentCodePage() == CP_UTF8 ? 0x110000u : 0x100u))
    {
        return _NtCRT_EncodingError(ps);
    }
    if (_NtCRT_CurrentCodePage() != CP_UTF8)
    {
        *s = (char)c32;
        return 1;
    }
    return _NtCRT_Utf8Encode(s, c32);
}

#pragma endregion

#pragma region "_l" functions

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

EXTERN_C size_t __cdecl _NtCRT__wcstombs_l(char* dst, const wchar_t* src, size_t count, _locale_t locale)
{
    NTCRT_LOCALE_SCOPE Scope(locale);
    return _NtCRT_wcstombs(dst, src, count);
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

#pragma endregion

NTCRT_IMPORT(mblen, _NtCRT_mblen);
NTCRT_IMPORT(mbtowc, _NtCRT_mbtowc);
NTCRT_IMPORT(wctomb, _NtCRT_wctomb);
NTCRT_IMPORT(wctomb_s, _NtCRT_wctomb_s);
NTCRT_IMPORT(mbstowcs, _NtCRT_mbstowcs);
NTCRT_IMPORT(wcstombs, _NtCRT_wcstombs);
NTCRT_IMPORT(mbsinit, _NtCRT_mbsinit);
NTCRT_IMPORT(mbrlen, _NtCRT_mbrlen);
NTCRT_IMPORT(mbsrtowcs, _NtCRT_mbsrtowcs);
NTCRT_IMPORT(wcsrtombs, _NtCRT_wcsrtombs);
NTCRT_IMPORT(mbrtoc16, _NtCRT_mbrtoc16);
NTCRT_IMPORT(c16rtomb, _NtCRT_c16rtomb);
NTCRT_IMPORT(mbrtoc32, _NtCRT_mbrtoc32);
NTCRT_IMPORT(c32rtomb, _NtCRT_c32rtomb);
NTCRT_IMPORT(_mblen_l, _NtCRT__mblen_l);
NTCRT_IMPORT(_mbtowc_l, _NtCRT__mbtowc_l);
NTCRT_IMPORT(_mbstowcs_l, _NtCRT__mbstowcs_l);
NTCRT_IMPORT(_wcstombs_l, _NtCRT__wcstombs_l);
NTCRT_IMPORT(_wctomb_l, _NtCRT__wctomb_l);
NTCRT_IMPORT(_wctomb_s_l, _NtCRT__wctomb_s_l);
