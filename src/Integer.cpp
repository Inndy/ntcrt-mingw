/*
 * Integer conversion of msvcrt, and wcstoll and the like of libmingwex wrapped by the specs (--wrap). lld loads
 * every object of a __wrap_ function: this one refers to nothing that loads more.
 */

#include "NtCRT.h"

#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>
#include <wchar.h>

template<typename TChar>
static
bool
_NtCRT_IsSpace(
    _In_ TChar c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

template<typename TChar>
static
int
_NtCRT_DigitValue(
    _In_ TChar c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    } else if (c >= 'a' && c <= 'z')
    {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z')
    {
        return c - 'A' + 10;
    }
    return 99;
}

/* Parse the magnitude, returns the limit with ERANGE on overflow */
template<typename TChar>
static
unsigned long long
_NtCRT_ParseInteger(
    _In_ const TChar* Str,
    _Out_opt_ TChar** End,
    _In_ int Base,
    _In_ unsigned long long PositiveLimit,
    _In_ unsigned long long NegativeLimit,
    _Out_ bool* Negative)
{
    const TChar* p = Str;
    unsigned long long Value = 0, Limit;
    bool Any = false, Overflow = false;

    *Negative = false;
    if (End != NULL)
    {
        *End = (TChar*)Str;
    }
    if (Str == NULL || Base < 0 || Base == 1 || Base > 36)
    {
        NTCRT_ERRNO = EINVAL;
        return 0;
    }
    while (_NtCRT_IsSpace(*p))
    {
        p++;
    }
    if (*p == '-')
    {
        *Negative = true;
        p++;
    } else if (*p == '+')
    {
        p++;
    }
    if ((Base == 0 || Base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && _NtCRT_DigitValue(p[2]) < 16)
    {
        p += 2;
        Base = 16;
    } else if (Base == 0)
    {
        Base = p[0] == '0' ? 8 : 10;
    }
    Limit = *Negative ? NegativeLimit : PositiveLimit;
    for (;; p++)
    {
        int Digit = _NtCRT_DigitValue(*p);
        if (Digit >= Base)
        {
            break;
        }
        Any = true;
        if (!Overflow)
        {
            if (Value > (Limit - Digit) / Base)
            {
                Overflow = true;
            } else
            {
                Value = Value * Base + Digit;
            }
        }
    }
    if (!Any)
    {
        *Negative = false;
        return 0;
    }
    if (End != NULL)
    {
        *End = (TChar*)p;
    }
    if (Overflow)
    {
        NTCRT_ERRNO = ERANGE;
        return Limit;
    }
    return Value;
}

template<typename TChar, typename TSigned>
static
TSigned
_NtCRT_StrToSigned(
    _In_ const TChar* Str,
    _Out_opt_ TChar** End,
    _In_ int Base,
    _In_ TSigned Max)
{
    bool Negative;
    unsigned long long Value = _NtCRT_ParseInteger(Str,
                                                   End,
                                                   Base,
                                                   (unsigned long long)Max,
                                                   (unsigned long long)Max + 1,
                                                   &Negative);
    return Negative ? (TSigned)(0 - Value) : (TSigned)Value;
}

template<typename TChar, typename TUnsigned>
static
TUnsigned
_NtCRT_StrToUnsigned(
    _In_ const TChar* Str,
    _Out_opt_ TChar** End,
    _In_ int Base,
    _In_ TUnsigned Max)
{
    bool Negative;
    int OldErrno = NTCRT_ERRNO;
    unsigned long long Value;

    NTCRT_ERRNO = 0;
    Value = _NtCRT_ParseInteger(Str, End, Base, Max, Max, &Negative);
    if (NTCRT_ERRNO == ERANGE)
    {
        return Max;
    }
    if (NTCRT_ERRNO == 0)
    {
        NTCRT_ERRNO = OldErrno;
    }
    return Negative ? (TUnsigned)(0 - Value) : (TUnsigned)Value;
}

EXTERN_C long __cdecl _NtCRT_strtol(const char* str, char** end, int base)
{
    return _NtCRT_StrToSigned<char, long>(str, end, base, LONG_MAX);
}

EXTERN_C unsigned long __cdecl _NtCRT_strtoul(const char* str, char** end, int base)
{
    return _NtCRT_StrToUnsigned<char, unsigned long>(str, end, base, ULONG_MAX);
}

EXTERN_C long long __cdecl _NtCRT_strtoll(const char* str, char** end, int base)
{
    return _NtCRT_StrToSigned<char, long long>(str, end, base, LLONG_MAX);
}

EXTERN_C unsigned long long __cdecl _NtCRT_strtoull(const char* str, char** end, int base)
{
    return _NtCRT_StrToUnsigned<char, unsigned long long>(str, end, base, ULLONG_MAX);
}

EXTERN_C long __cdecl _NtCRT_wcstol(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_StrToSigned<wchar_t, long>(str, end, base, LONG_MAX);
}

EXTERN_C unsigned long __cdecl _NtCRT_wcstoul(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_StrToUnsigned<wchar_t, unsigned long>(str, end, base, ULONG_MAX);
}

EXTERN_C long long __cdecl _NtCRT_wcstoll(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_StrToSigned<wchar_t, long long>(str, end, base, LLONG_MAX);
}

EXTERN_C unsigned long long __cdecl _NtCRT_wcstoull(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_StrToUnsigned<wchar_t, unsigned long long>(str, end, base, ULLONG_MAX);
}

/*
 * wcstoll, wcstoull, wcstoimax and wcstoumax of libmingwex convert nothing from "0x" without a hex digit, where C
 * converts the 0, and fail an invalid base with EDOM. The specs wrap them to these (--wrap).
 */
EXTERN_C long long __cdecl __wrap_wcstoll(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_wcstoll(str, end, base);
}

EXTERN_C unsigned long long __cdecl __wrap_wcstoull(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_wcstoull(str, end, base);
}

EXTERN_C intmax_t __cdecl __wrap_wcstoimax(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_wcstoll(str, end, base);
}

EXTERN_C uintmax_t __cdecl __wrap_wcstoumax(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_wcstoull(str, end, base);
}

EXTERN_C long long __cdecl _NtCRT_atoll(const char* str)
{
    return _NtCRT_strtoll(str, NULL, 10);
}

EXTERN_C long long __cdecl _NtCRT__wtoll(const wchar_t* str)
{
    return _NtCRT_wcstoll(str, NULL, 10);
}

NTCRT_IMPORT(strtol, _NtCRT_strtol);
NTCRT_IMPORT(strtoul, _NtCRT_strtoul);
NTCRT_IMPORT(strtoll, _NtCRT_strtoll);
NTCRT_IMPORT(_strtoi64, _NtCRT_strtoll);
NTCRT_IMPORT(strtoull, _NtCRT_strtoull);
NTCRT_IMPORT(_strtoui64, _NtCRT_strtoull);
NTCRT_IMPORT(wcstol, _NtCRT_wcstol);
NTCRT_IMPORT(wcstoul, _NtCRT_wcstoul);
NTCRT_IMPORT_ONLY(wcstoll, _NtCRT_wcstoll);
NTCRT_IMPORT(_wcstoi64, _NtCRT_wcstoll);
NTCRT_IMPORT_ONLY(wcstoull, _NtCRT_wcstoull);
NTCRT_IMPORT(_wcstoui64, _NtCRT_wcstoull);
NTCRT_IMPORT(atoll, _NtCRT_atoll);
NTCRT_IMPORT(_wtoll, _NtCRT__wtoll);

/* Under their names too, as the printf of FormatNarrow.cpp: lld takes them from libntcrt without --wrap */
EXTERN_C long long __cdecl wcstoll(const wchar_t*, wchar_t**, int) __attribute__((alias("__wrap_wcstoll")));
EXTERN_C unsigned long long __cdecl wcstoull(const wchar_t*, wchar_t**, int) __attribute__((alias("__wrap_wcstoull")));
EXTERN_C intmax_t __cdecl wcstoimax(const wchar_t*, wchar_t**, int) __attribute__((alias("__wrap_wcstoimax")));
EXTERN_C uintmax_t __cdecl wcstoumax(const wchar_t*, wchar_t**, int) __attribute__((alias("__wrap_wcstoumax")));
