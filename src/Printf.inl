/*
 * The formatting of the printf family, narrow and wide, for the functions of msvcrt and for the __mingw_*printf
 * of the headers of mingw-w64 (wrapped by the specs, FormatNarrow.cpp and FormatWide.cpp).
 *
 * C: flags, width and precision (also by * and *m$), positional arguments (%n$, POSIX), the length modifiers hh,
 * h, l, ll, j, z, t and L, %ls/%lc converted by the locale. Floating point numbers are exact and round ties to
 * even, by the digits of Float.cpp (the ftoa of Go 1.27). The length modifiers of msvcrt I, I32, I64 and w, %S and
 * %C (the other width than the format), %Z (ANSI_STRING, %wZ UNICODE_STRING) and, with
 * NTCRT_PRINTF_LEGACY_WIDE, the %s and %c of the wide functions of msvcrt, which are wide (%hs/%hc narrow).
 *
 * The long double of %Lf is that of the caller: a double for the functions of msvcrt (MSVC, Windows), the 80-bit
 * long double of GCC and clang for __mingw_*printf (NTCRT_PRINTF_LONG_DOUBLE), as mingw-w64 does.
 *
 * An argument takes a slot of 8 bytes in the va_list of x64 (a long double is passed by address), which makes the
 * positional arguments an index. On x86 it takes the size of its type, the positional arguments are found by the
 * types the format gives them (NTCRT_PRINTF_ARGS). A conversion the engine does not know is copied to the output as
 * it is.
 *
 * The engine is a template of the character type, instantiated for char by Printf.cpp and for wchar_t by
 * PrintfWide.cpp: ld links whole objects, a program with printf only does not link the wide engine.
 */

#pragma once

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <type_traits>

static_assert(sizeof(va_list) == sizeof(char*), "va_list is a pointer to the arguments");

/* Precision of %f beyond which every digit of a long double is 0 (2**-16445 has 16445 digits after the point) */
#define NTCRT_PRINTF_MAX_FRACTION 17000

/* The output: the first Count characters go to Buffer and all are counted, or Buffer grows to take them all */
template<typename TChar>
struct NTCRT_PRINTF_OUT
{
    TChar* Buffer;
    size_t Count;
    size_t Length;
    BOOL Grow;
    BOOL Heap;   /* Buffer is from the heap */
    BOOL Failed; /* errno is set */
};

template<typename TChar>
static
BOOL
_NtCRT_OutReserve(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_ size_t n)
{
    size_t Size;
    TChar* New;

    if (!o->Grow || n <= o->Count - o->Length)
    {
        return TRUE;
    }
    /* A growing buffer holds what printf can return */
    if (n > INT_MAX - o->Length)
    {
        NTCRT_ERRNO = EOVERFLOW;
        o->Failed = TRUE;
        return FALSE;
    }
    Size = o->Length + n > o->Count * 2 ? o->Length + n : o->Count * 2;
    New = (TChar*)(o->Heap ? RtlReAllocateHeap(NTCRT_HEAP, 0, o->Buffer, Size * sizeof(TChar))
                           : RtlAllocateHeap(NTCRT_HEAP, 0, Size * sizeof(TChar)));
    if (New == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        o->Failed = TRUE;
        return FALSE;
    }
    if (!o->Heap)
    {
        memcpy(New, o->Buffer, o->Length * sizeof(TChar));
    }
    o->Buffer = New;
    o->Count = Size;
    o->Heap = TRUE;
    return TRUE;
}

template<typename TChar>
static
void
_NtCRT_OutChars(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_reads_(n) const TChar* s,
    _In_ size_t n)
{
    if (o->Failed || !_NtCRT_OutReserve(o, n))
    {
        return;
    }
    if (o->Length < o->Count)
    {
        memcpy(o->Buffer + o->Length, s, (n < o->Count - o->Length ? n : o->Count - o->Length) * sizeof(TChar));
    }
    o->Length += n;
}

/* ASCII characters */
template<typename TChar>
static
void
_NtCRT_OutAscii(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_reads_(n) const char* s,
    _In_ size_t n)
{
    if (sizeof(TChar) == sizeof(char))
    {
        _NtCRT_OutChars(o, (const TChar*)s, n);
        return;
    }
    size_t i = 0;

    if (o->Failed || !_NtCRT_OutReserve(o, n))
    {
        return;
    }
    for (; i < n && o->Length < o->Count; i++)
    {
        o->Buffer[o->Length++] = (TChar)(unsigned char)s[i];
    }
    o->Length += n - i;
}

template<typename TChar>
static
void
_NtCRT_OutRepeat(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_ TChar c,
    _In_ size_t n)
{
    size_t i = 0;

    if (o->Failed || !_NtCRT_OutReserve(o, n))
    {
        return;
    }
    for (; i < n && o->Length < o->Count; i++)
    {
        o->Buffer[o->Length++] = c;
    }
    o->Length += n - i;
}

typedef enum _NTCRT_PRINTF_LENGTH
{
    NtCRTLengthNone,
    NtCRTLengthChar,     /* hh */
    NtCRTLengthShort,    /* h */
    NtCRTLengthLong,     /* l, 32 bits */
    NtCRTLengthLongLong, /* ll, j, q, I64: 64 bits */
    NtCRTLengthInt32,    /* I32 */
    NtCRTLengthDouble    /* L of __mingw_*printf: long double, or 64 bits for an integer */
} NTCRT_PRINTF_LENGTH;

/* z, t and I: the size of a pointer */
#define NTCRT_PRINTF_LENGTH_PTR (sizeof(void*) == 8 ? NtCRTLengthLongLong : NtCRTLengthLong)

/* The position of an argument: -1 the next one, NTCRT_PRINTF_NO_ARG none (a width or precision of the format) */
#define NTCRT_PRINTF_NO_ARG (-2)

typedef struct _NTCRT_PRINTF_SPEC
{
    BOOL Left;
    BOOL Plus;
    BOOL Space;
    BOOL Alt;
    BOOL Zero;
    unsigned Width; /* Beyond INT_MAX if no output fits the int that printf returns */
    int Precision;  /* -1 if none */
    NTCRT_PRINTF_LENGTH Length;
    int Text; /* Width of a string or character: 1 narrow (h), 2 wide (l, w), 0 by the conversion */
    int Position;     /* Of the argument (n$), -1 the next one */
    int WidthArg;     /* Of the width (*, *m$) */
    int PrecisionArg; /* Of the precision (.*, .*m$) */
} NTCRT_PRINTF_SPEC;

/* The field: Prefix (sign, "0x"), the padding to Width (zeros after Prefix if ZeroPad) and the body */
template<typename TChar, typename TBody>
static
void
_NtCRT_OutField(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_ const NTCRT_PRINTF_SPEC* Spec,
    _In_z_ const char* Prefix,
    _In_ size_t BodyLength,
    _In_ BOOL ZeroPad,
    _In_ TBody Body)
{
    size_t PrefixLength = strlen(Prefix), Total = PrefixLength + BodyLength;
    size_t Pad = (size_t)Spec->Width > Total ? (size_t)Spec->Width - Total : 0;

    if (!Spec->Left && !ZeroPad)
    {
        _NtCRT_OutRepeat(o, (TChar)' ', Pad);
    }
    _NtCRT_OutAscii(o, Prefix, PrefixLength);
    if (!Spec->Left && ZeroPad)
    {
        _NtCRT_OutRepeat(o, (TChar)'0', Pad);
    }
    Body();
    if (Spec->Left)
    {
        _NtCRT_OutRepeat(o, (TChar)' ', Pad);
    }
}

#pragma region Integers

template<typename TChar>
static
void
_NtCRT_FormatInteger(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_ const NTCRT_PRINTF_SPEC* Spec,
    _In_ TChar Conv,
    _In_ ULONG64 Raw)
{
    BOOL Signed = Conv == 'd' || Conv == 'i';
    const char* DigitChars = Conv == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned Base = Conv == 'o' ? 8 : Conv == 'x' || Conv == 'X' ? 16 : 10;
    char Digits[24], Prefix[4] = "";
    int nd = 0, Zeros;
    ULONG64 v;

    switch (Spec->Length)
    {
    case NtCRTLengthChar:
        v = Signed ? (ULONG64)(LONG64)(signed char)Raw : (UCHAR)Raw;
        break;
    case NtCRTLengthShort:
        v = Signed ? (ULONG64)(LONG64)(SHORT)Raw : (USHORT)Raw;
        break;
    case NtCRTLengthLongLong:
    case NtCRTLengthDouble:
        v = Raw;
        break;
    default:
        v = Signed ? (ULONG64)(LONG64)(LONG)Raw : (ULONG)Raw;
        break;
    }
    if (Signed && (LONG64)v < 0)
    {
        Prefix[0] = '-';
        v = 0 - v;
    } else if (Signed && (Spec->Plus || Spec->Space))
    {
        Prefix[0] = Spec->Plus ? '+' : ' ';
    }
    if (Spec->Alt && Base == 16 && v != 0)
    {
        strcat(Prefix, Conv == 'X' ? "0X" : "0x");
    }

    /* The digits, in reverse */
    for (ULONG64 x = v; x != 0; x /= Base)
    {
        Digits[nd++] = DigitChars[x % Base];
    }
    Zeros = Spec->Precision > nd ? Spec->Precision - nd : Spec->Precision < 0 && nd == 0 ? 1 : 0;
    if (Spec->Alt && Base == 8 && Zeros == 0 && (nd == 0 || Digits[nd - 1] != '0'))
    {
        Zeros = 1;
    }
    _NtCRT_OutField(o, Spec, Prefix, (size_t)Zeros + (size_t)nd, Spec->Zero && Spec->Precision < 0, [&]() {
        _NtCRT_OutRepeat(o, (TChar)'0', (size_t)Zeros);
        while (nd > 0)
        {
            _NtCRT_OutAscii(o, &Digits[--nd], 1);
        }
    });
}

#pragma endregion

#pragma region Floating point

/* A double or an 80-bit long double: Mant * 2**Exp */
typedef struct _NTCRT_PRINTF_FLOAT
{
    BOOL Neg;
    BOOL Inf;
    BOOL NaN;
    BOOL Long;
    ULONG64 Mant;
    int Exp;
} NTCRT_PRINTF_FLOAT;

static
void
_NtCRT_UnpackDouble(
    _In_ ULONG64 Bits,
    _Out_ NTCRT_PRINTF_FLOAT* f)
{
    int e = (int)(Bits >> 52) & 0x7FF;

    f->Neg = Bits >> 63;
    f->Long = FALSE;
    f->Mant = Bits & ((1ull << 52) - 1);
    f->Inf = e == 0x7FF && f->Mant == 0;
    f->NaN = e == 0x7FF && f->Mant != 0;
    if (e == 0)
    {
        f->Exp = -1074;
    } else
    {
        f->Mant |= 1ull << 52;
        f->Exp = e - 1075;
    }
}

static
void
_NtCRT_UnpackLongDouble(
    _In_ const void* p,
    _Out_ NTCRT_PRINTF_FLOAT* f)
{
    USHORT SignExp;
    int e;

    memcpy(&f->Mant, p, sizeof(f->Mant));
    memcpy(&SignExp, (const char*)p + 8, sizeof(SignExp));
    e = SignExp & 0x7FFF;
    f->Neg = SignExp >> 15;
    f->Long = TRUE;
    f->Inf = e == 0x7FFF && (f->Mant << 1) == 0;
    f->NaN = e == 0x7FFF && (f->Mant << 1) != 0;
    f->Exp = (e == 0 ? 1 : e) - 16383 - 63;
}

template<typename TChar>
static
void
_NtCRT_OutDigits(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_reads_(nd) const char* Digits,
    _In_ LONG64 nd,
    _In_ LONG64 From,
    _In_ LONG64 To)
{
    /* Digits From..To of the digit string, 0 outside it */
    if (From < 0)
    {
        _NtCRT_OutRepeat(o, (TChar)'0', (size_t)((To < 0 ? To : 0) - From));
        From = 0;
    }
    if (From < To && From < nd)
    {
        _NtCRT_OutAscii(o, Digits + From, (size_t)((To < nd ? To : nd) - From));
    }
    if (To > nd && To > From)
    {
        _NtCRT_OutRepeat(o, (TChar)'0', (size_t)(To - (From > nd ? From : nd)));
    }
}

/* %e of the digits 0.d1d2... * 10**Point with Precision digits after the point */
template<typename TChar>
static
void
_NtCRT_FormatExponent(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_ const NTCRT_PRINTF_SPEC* Spec,
    _In_z_ const char* Prefix,
    _In_reads_(nd) const char* Digits,
    _In_ int nd,
    _In_ int Point,
    _In_ int Precision,
    _In_ BOOL Upper)
{
    int X = nd == 0 ? 0 : Point - 1;
    char Exp[8];
    int n = 0;
    unsigned Abs = X < 0 ? (unsigned)-X : (unsigned)X;
    BOOL Dot = Precision > 0 || Spec->Alt;

    do
    {
        Exp[n++] = (char)('0' + Abs % 10);
        Abs /= 10;
    } while (Abs != 0 || n < 2);
    _NtCRT_OutField(o, Spec, Prefix, 1 + (size_t)Dot + (size_t)Precision + 2 + (size_t)n, Spec->Zero, [&]() {
        char Head[2] = { nd == 0 ? '0' : Digits[0], '.' };
        char Tail[2] = { Upper ? 'E' : 'e', X < 0 ? '-' : '+' };

        _NtCRT_OutAscii(o, Head, 1 + (size_t)Dot);
        _NtCRT_OutDigits(o, Digits, nd, 1, 1 + (LONG64)Precision);
        _NtCRT_OutAscii(o, Tail, 2);
        while (n > 0)
        {
            _NtCRT_OutAscii(o, &Exp[--n], 1);
        }
    });
}

/* %f of the digits 0.d1d2... * 10**Point with Precision digits after the point */
template<typename TChar>
static
void
_NtCRT_FormatFixed(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_ const NTCRT_PRINTF_SPEC* Spec,
    _In_z_ const char* Prefix,
    _In_reads_(nd) const char* Digits,
    _In_ int nd,
    _In_ int Point,
    _In_ int Precision)
{
    int Integer = nd == 0 || Point <= 0 ? 1 : Point;
    BOOL Dot = Precision > 0 || Spec->Alt;

    _NtCRT_OutField(o, Spec, Prefix, (size_t)Integer + (size_t)Dot + (size_t)Precision, Spec->Zero, [&]() {
        if (nd == 0 || Point <= 0)
        {
            _NtCRT_OutAscii(o, "0", 1);
        } else
        {
            _NtCRT_OutDigits(o, Digits, nd, 0, Point);
        }
        if (Dot)
        {
            _NtCRT_OutAscii(o, ".", 1);
        }
        if (nd == 0)
        {
            _NtCRT_OutRepeat(o, (TChar)'0', (size_t)Precision);
        } else
        {
            _NtCRT_OutDigits(o, Digits, nd, Point, (LONG64)Point + Precision);
        }
    });
}

/* %a: 1.xxx or 0.xxx (denormal) * 2**e for a double, the 4 high bits x.xxx * 2**e for a long double */
template<typename TChar>
static
void
_NtCRT_FormatHex(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_ const NTCRT_PRINTF_SPEC* Spec,
    _In_z_ const char* Sign,
    _In_ const NTCRT_PRINTF_FLOAT* f,
    _In_ BOOL Upper)
{
    const char* Hex = Upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int FracDigits = f->Long ? 15 : 13, Precision = Spec->Precision, nd, Exp = 0, n = 0;
    ULONG64 Lead = 0, Frac = 0;
    char Prefix[8], Exponent[8], Body[24];
    unsigned Abs;

    if (f->Mant != 0)
    {
        if (f->Long)
        {
            Lead = f->Mant >> 60;
            Frac = f->Mant & ((1ull << 60) - 1);
            Exp = f->Exp + 60;
        } else
        {
            Lead = f->Mant >> 52;
            Frac = f->Mant & ((1ull << 52) - 1);
            Exp = f->Exp + 52;
        }
    }
    if (Precision >= 0 && Precision < FracDigits)
    {
        /* Round to Precision digits, ties to even */
        int Drop = (FracDigits - Precision) * 4;
        ULONG64 Rem = Frac & ((1ull << Drop) - 1), Half = 1ull << (Drop - 1);

        Frac >>= Drop;
        if (Rem > Half || (Rem == Half && ((Precision == 0 ? Lead : Frac) & 1)))
        {
            Frac++;
            if (Frac >> (Precision * 4) != 0)
            {
                Frac = 0;
                Lead++;
            }
        }
        nd = Precision;
        if (Lead == 16)
        {
            Lead = 1;
            Exp += 4;
        }
    } else
    {
        nd = FracDigits;
        if (Precision < 0)
        {
            for (; nd > 0 && (Frac & 0xF) == 0; nd--)
            {
                Frac >>= 4;
            }
        }
    }
    for (int i = nd - 1; i >= 0; i--, Frac >>= 4)
    {
        Body[i] = Hex[Frac & 0xF];
    }
    Abs = Exp < 0 ? (unsigned)-Exp : (unsigned)Exp;
    do
    {
        Exponent[n++] = (char)('0' + Abs % 10);
        Abs /= 10;
    } while (Abs != 0);
    strcpy(Prefix, Sign);
    strcat(Prefix, Upper ? "0X" : "0x");
    {
        int Zeros = Precision > nd ? Precision - nd : 0;
        BOOL Dot = nd > 0 || Zeros > 0 || Spec->Alt;

        _NtCRT_OutField(o, Spec, Prefix, 1 + (size_t)Dot + (size_t)nd + (size_t)Zeros + 2 + (size_t)n, Spec->Zero, [&]() {
            char Head[2] = { Hex[Lead], '.' };
            char Tail[2] = { Upper ? 'P' : 'p', Exp < 0 ? '-' : '+' };

            _NtCRT_OutAscii(o, Head, 1 + (size_t)Dot);
            _NtCRT_OutAscii(o, Body, (size_t)nd);
            _NtCRT_OutRepeat(o, (TChar)'0', (size_t)Zeros);
            _NtCRT_OutAscii(o, Tail, 2);
            while (n > 0)
            {
                _NtCRT_OutAscii(o, &Exponent[--n], 1);
            }
        });
    }
}

/* Not inlined in the loop of the format, where it takes more space */
template<typename TChar>
static __attribute__((noinline))
void
_NtCRT_FormatFloat(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_ const NTCRT_PRINTF_SPEC* Spec,
    _In_ TChar Conv,
    _In_ ULONG64 Arg)
{
    NTCRT_PRINTF_FLOAT f;
    char Sign[2] = "";
    char Stack[NTCRT_FLOAT_DIGITS];
    char* Digits = Stack;
    int Size = NTCRT_FLOAT_DIGITS, Precision = Spec->Precision, nd = 0, Point = 0;
    BOOL Upper = Conv == 'E' || Conv == 'F' || Conv == 'G' || Conv == 'A';
    char Lower = (char)(Conv | 0x20);

    if (Spec->Length == NtCRTLengthDouble)
    {
        _NtCRT_UnpackLongDouble((const void*)(ULONG_PTR)Arg, &f);
    } else
    {
        _NtCRT_UnpackDouble(Arg, &f);
    }
    if (f.Neg)
    {
        Sign[0] = '-';
    } else if (Spec->Plus || Spec->Space)
    {
        Sign[0] = Spec->Plus ? '+' : ' ';
    }
    if (f.Inf || f.NaN)
    {
        const char* Text = f.Inf ? (Upper ? "INF" : "inf") : (Upper ? "NAN" : "nan");

        _NtCRT_OutField(o, Spec, Sign, 3, FALSE, [&]() { _NtCRT_OutAscii(o, Text, 3); });
        return;
    }
    if (Lower == 'a')
    {
        _NtCRT_FormatHex(o, Spec, Sign, &f, Upper);
        return;
    }
    if (f.Long && f.Mant != 0)
    {
        Size = NTCRT_FLOAT_DIGITS_LONG;
        Digits = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, (size_t)Size);
        if (Digits == NULL)
        {
            NTCRT_ERRNO = ENOMEM;
            o->Failed = TRUE;
            return;
        }
    }
    if (Precision < 0)
    {
        Precision = 6;
    }
    if (Lower == 'e')
    {
        if (f.Mant != 0)
        {
            nd = _NtCRT_FloatDigits(f.Mant, f.Exp, 'e', Precision < Size ? Precision : Size, Digits, Size, &Point);
        }
        _NtCRT_FormatExponent(o, Spec, Sign, Digits, nd, Point, Precision, Upper);
    } else if (Lower == 'f')
    {
        if (f.Mant != 0)
        {
            int p = Precision < NTCRT_PRINTF_MAX_FRACTION ? Precision : NTCRT_PRINTF_MAX_FRACTION;

            nd = _NtCRT_FloatDigits(f.Mant, f.Exp, 'f', p, Digits, Size, &Point);
        }
        _NtCRT_FormatFixed(o, Spec, Sign, Digits, nd, Point, Precision);
    } else
    {
        /*
         * %g: P significant digits, X the exponent of %e with them, %f with P - 1 - X digits after the point if
         * P > X >= -4 and %e with P - 1 otherwise, the trailing zeros removed without the # flag
         */
        int P = Precision == 0 ? 1 : Precision, X = 0;

        if (f.Mant != 0)
        {
            nd = _NtCRT_FloatDigits(f.Mant, f.Exp, 'e', (P < Size ? P : Size) - 1, Digits, Size, &Point);
            X = nd == 0 ? 0 : Point - 1;
        }
        if (P > X && X >= -4)
        {
            LONG64 Fraction = (LONG64)P - 1 - X;

            if (!Spec->Alt)
            {
                int Kept = nd - (nd == 0 ? 0 : Point);
                Fraction = Fraction < Kept ? Fraction : Kept > 0 ? Kept : 0;
            }
            _NtCRT_FormatFixed(o, Spec, Sign, Digits, nd, nd == 0 ? 1 : Point, Fraction < INT_MAX ? (int)Fraction : INT_MAX);
        } else
        {
            int Fraction = P - 1;

            if (!Spec->Alt)
            {
                Fraction = Fraction < nd - 1 ? Fraction : nd > 1 ? nd - 1 : 0;
            }
            _NtCRT_FormatExponent(o, Spec, Sign, Digits, nd, Point, Fraction, Upper);
        }
    }
    if (Digits != Stack)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Digits);
    }
}

#pragma endregion

#pragma region Strings and characters

/*
 * A string of the other width than the output, converted by the locale, Count characters or up to the null
 * character if Count is SIZE_MAX. Narrow output: the precision counts bytes and never cuts a character. Wide output:
 * it counts wchar_t and never splits a surrogate pair. FALSE with EILSEQ if a character does not convert. The
 * overloads of the other character type are unused in Printf.cpp or PrintfWide.cpp.
 */
[[maybe_unused]]
static
BOOL
_NtCRT_ConvertedLength(
    _In_ const wchar_t* s,
    _In_ size_t Count,
    _In_ int Precision,
    _Out_ size_t* Length,
    _Out_ size_t* Used)
{
    mbstate_t State = 0;
    char Bytes[MB_LEN_MAX];
    size_t i, n = 0, Max = Precision >= 0 ? (size_t)Precision : SIZE_MAX;

    /* A precision that is reached reads no further, the array may end there */
    for (i = 0; i < Count && n < Max; i++)
    {
        size_t k = wcrtomb(Bytes, s[i], &State);

        /* The null character ends a string without a count, after a high surrogate it is an error */
        if (k == (size_t)-1)
        {
            return FALSE;
        }
        if (Count == SIZE_MAX && s[i] == L'\0')
        {
            break;
        }
        if (n + k > Max)
        {
            break;
        }
        n += k;
    }
    if (i == Count && wcrtomb(Bytes, L'\0', &State) == (size_t)-1)
    {
        /* A high surrogate at the end */
        return FALSE;
    }
    *Length = n;
    *Used = i;
    return TRUE;
}

[[maybe_unused]]
static
BOOL
_NtCRT_ConvertedLength(
    _In_ const char* s,
    _In_ size_t Count,
    _In_ int Precision,
    _Out_ size_t* Length,
    _Out_ size_t* Used)
{
    mbstate_t State = 0;
    size_t i = 0, n = 0, Max = Precision >= 0 ? (size_t)Precision : SIZE_MAX;

    /*
     * mbrtowc returns a character above U+FFFF in two calls: the high surrogate, then the low one. A precision that
     * is reached reads no further, the array may end there.
     */
    while (i < Count && n < Max)
    {
        wchar_t Char;
        size_t k = mbrtowc(&Char, s + i, Count == SIZE_MAX ? MB_LEN_MAX : Count - i, &State);

        if (k == (size_t)-1 || k == (size_t)-2)
        {
            return FALSE;
        }
        if (k == 0 || n + (Char >= 0xD800 && Char <= 0xDBFF ? 2 : 1) > Max)
        {
            break;
        }
        n++;
        i += k;
    }
    *Length = n;
    *Used = i;
    return TRUE;
}

[[maybe_unused]]
static
void
_NtCRT_OutConverted(
    _Inout_ NTCRT_PRINTF_OUT<char>* o,
    _In_ const wchar_t* s,
    _In_ size_t Used)
{
    mbstate_t State = 0;
    char Bytes[MB_LEN_MAX];

    for (size_t i = 0; i < Used; i++)
    {
        size_t k = wcrtomb(Bytes, s[i], &State);

        _NtCRT_OutChars(o, Bytes, k);
    }
}

[[maybe_unused]]
static
void
_NtCRT_OutConverted(
    _Inout_ NTCRT_PRINTF_OUT<wchar_t>* o,
    _In_ const char* s,
    _In_ size_t Used)
{
    mbstate_t State = 0;

    for (size_t i = 0; i < Used;)
    {
        wchar_t Char;

        i += mbrtowc(&Char, s + i, Used - i, &State);
        _NtCRT_OutChars(o, &Char, 1);
    }
}

/* A string: Str of the width of the output or of the other one (Other), Count characters or null-terminated */
template<typename TChar>
static
void
_NtCRT_FormatString(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_ const NTCRT_PRINTF_SPEC* Spec,
    _In_opt_ const void* Str,
    _In_ BOOL Other,
    _In_ size_t Count)
{
    if (Str == NULL)
    {
        static const char Null[] = "(null)";
        size_t n = Spec->Precision >= 0 && (size_t)Spec->Precision < 6 ? (size_t)Spec->Precision : 6;

        _NtCRT_OutField(o, Spec, "", n, FALSE, [&]() { _NtCRT_OutAscii(o, Null, n); });
        return;
    }
    if (!Other)
    {
        const TChar* s = (const TChar*)Str;
        size_t n = 0;

        while (n < Count && (Spec->Precision < 0 || n < (size_t)Spec->Precision) && !(Count == SIZE_MAX && s[n] == 0))
        {
            n++;
        }
        _NtCRT_OutField(o, Spec, "", n, FALSE, [&]() { _NtCRT_OutChars(o, s, n); });
    } else
    {
        typedef typename std::conditional<sizeof(TChar) == sizeof(char), wchar_t, char>::type TOther;
        const TOther* s = (const TOther*)Str;
        size_t Length, Used;

        if (!_NtCRT_ConvertedLength(s, Count, Spec->Precision, &Length, &Used))
        {
            NTCRT_ERRNO = EILSEQ;
            o->Failed = TRUE;
            return;
        }
        _NtCRT_OutField(o, Spec, "", Length, FALSE, [&]() { _NtCRT_OutConverted(o, s, Used); });
    }
}

/* A character of the width of the output or of the other one */
[[maybe_unused]]
static
void
_NtCRT_FormatChar(
    _Inout_ NTCRT_PRINTF_OUT<char>* o,
    _In_ const NTCRT_PRINTF_SPEC* Spec,
    _In_ ULONG64 Arg,
    _In_ BOOL Other)
{
    char Bytes[MB_LEN_MAX];
    size_t n = 1;

    Bytes[0] = (char)Arg;
    if (Other && (wchar_t)Arg != L'\0')
    {
        mbstate_t State = 0;

        n = wcrtomb(Bytes, (wchar_t)Arg, &State);
        if (n == (size_t)-1 || n == 0)
        {
            /* A lone surrogate */
            NTCRT_ERRNO = EILSEQ;
            o->Failed = TRUE;
            return;
        }
    }
    _NtCRT_OutField(o, Spec, "", n, FALSE, [&]() { _NtCRT_OutChars(o, Bytes, n); });
}

[[maybe_unused]]
static
void
_NtCRT_FormatChar(
    _Inout_ NTCRT_PRINTF_OUT<wchar_t>* o,
    _In_ const NTCRT_PRINTF_SPEC* Spec,
    _In_ ULONG64 Arg,
    _In_ BOOL Other)
{
    wchar_t Char = (wchar_t)Arg;

    if (Other)
    {
        wint_t w = btowc((unsigned char)Arg);

        if (w == WEOF)
        {
            NTCRT_ERRNO = EILSEQ;
            o->Failed = TRUE;
            return;
        }
        Char = (wchar_t)w;
    }
    _NtCRT_OutField(o, Spec, "", 1, FALSE, [&]() { _NtCRT_OutChars(o, &Char, 1); });
}

#pragma endregion

/* Parse "n$", returns n - 1 or -1 */
template<typename TChar>
static
int
_NtCRT_ParseArgPosition(
    _Inout_ const TChar** p)
{
    const TChar* q = *p;
    int n = 0;

    while (*q >= '0' && *q <= '9' && n < 100000)
    {
        n = n * 10 + (*q++ - '0');
    }
    if (q == *p || *q != '$' || n == 0)
    {
        return -1;
    }
    *p = q + 1;
    return n - 1;
}

/* A number that saturates beyond INT_MAX */
template<typename TChar>
static
unsigned
_NtCRT_ParseNumber(
    _Inout_ const TChar** p)
{
    unsigned n = 0;

    for (; **p >= '0' && **p <= '9'; (*p)++)
    {
        n = n <= INT_MAX / 10 ? n * 10 + (unsigned)(**p - '0') : (unsigned)INT_MAX + 1;
    }
    return n;
}

/*
 * The conversion after a "%" at *p: the flags, width, precision, length and the positions of the arguments in Spec,
 * returns the conversion character, '\0' at the end of the format. The long double of %Lf is a double for the
 * functions of msvcrt, as for MSVC, and L of the other conversions is ignored as msvcrt does. For __mingw_*printf
 * (NTCRT_PRINTF_LONG_DOUBLE) it is the long double of GCC and an integer of 64 bits.
 */
template<typename TChar>
static
TChar
_NtCRT_ParseSpec(
    _Inout_ const TChar** p,
    _Out_ NTCRT_PRINTF_SPEC* Spec,
    _In_ ULONG Options)
{
    const TChar* f = *p;
    TChar Conv;

    *Spec = {};
    Spec->Position = _NtCRT_ParseArgPosition(&f);
    for (;; f++)
    {
        if (*f == '-')
        {
            Spec->Left = TRUE;
        } else if (*f == '+')
        {
            Spec->Plus = TRUE;
        } else if (*f == ' ')
        {
            Spec->Space = TRUE;
        } else if (*f == '#')
        {
            Spec->Alt = TRUE;
        } else if (*f == '0')
        {
            Spec->Zero = TRUE;
        } else if (*f != '\'')
        {
            break;
        }
    }
    Spec->WidthArg = NTCRT_PRINTF_NO_ARG;
    if (*f == '*')
    {
        f++;
        Spec->WidthArg = _NtCRT_ParseArgPosition(&f);
    } else
    {
        Spec->Width = _NtCRT_ParseNumber(&f);
    }
    Spec->Precision = -1;
    Spec->PrecisionArg = NTCRT_PRINTF_NO_ARG;
    if (*f == '.')
    {
        f++;
        if (*f == '*')
        {
            f++;
            Spec->PrecisionArg = _NtCRT_ParseArgPosition(&f);
        } else
        {
            unsigned Precision = _NtCRT_ParseNumber(&f);

            Spec->Precision = Precision > INT_MAX ? INT_MAX : (int)Precision;
        }
    }
    switch (*f)
    {
    case 'h':
        f++;
        Spec->Length = *f == 'h' ? (f++, NtCRTLengthChar) : NtCRTLengthShort;
        Spec->Text = 1;
        break;
    case 'l':
        f++;
        Spec->Length = *f == 'l' ? (f++, NtCRTLengthLongLong) : NtCRTLengthLong;
        Spec->Text = 2;
        break;
    case 'w':
        f++;
        Spec->Length = NtCRTLengthLong;
        Spec->Text = 2;
        break;
    case 'L':
        f++;
        Spec->Length = Options & NTCRT_PRINTF_LONG_DOUBLE ? NtCRTLengthDouble : NtCRTLengthNone;
        break;
    case 'q':
    case 'j':
        f++;
        Spec->Length = NtCRTLengthLongLong;
        break;
    case 'z':
    case 't':
        f++;
        Spec->Length = NTCRT_PRINTF_LENGTH_PTR;
        break;
    case 'I':
        f++;
        if (f[0] == '3' && f[1] == '2')
        {
            f += 2;
            Spec->Length = NtCRTLengthInt32;
        } else if (f[0] == '6' && f[1] == '4')
        {
            f += 2;
            Spec->Length = NtCRTLengthLongLong;
        } else
        {
            Spec->Length = NTCRT_PRINTF_LENGTH_PTR;
        }
        break;
    default:
        break;
    }
    Conv = *f;
    if (Conv != '\0')
    {
        f++;
    }
    *p = f;
    return Conv;
}

#pragma region Arguments

/*
 * The variadic arguments, which a va_list of x64 and of x86 points to. On x64 an argument takes a slot of 8 bytes
 * (a long double is passed by address), a position is an index. On x86 an int or a pointer takes 4 bytes, a long
 * long or a double 8 and the long double of GCC 12: the offsets of the positional arguments follow from the types
 * that the whole format gives them, a position that no conversion takes is an int. NTCRT_PRINTF_POSITIONS
 * positions at most on both (_ARGMAX of msvcrt).
 */
typedef enum _NTCRT_PRINTF_ARG
{
    NtCRTArgNone,
    NtCRTArgInt,
    NtCRTArgLongLong,
    NtCRTArgPointer,
    NtCRTArgDouble,
    NtCRTArgLongDouble
} NTCRT_PRINTF_ARG;

#define NTCRT_PRINTF_POSITIONS 100

struct NTCRT_PRINTF_ARGS
{
    const char* Base;
    size_t Next; /* Offset of the next argument */
#ifndef _WIN64
    BOOL Positioned; /* Offsets is set */
    USHORT Offsets[NTCRT_PRINTF_POSITIONS];
#endif
};

static inline size_t _NtCRT_ArgSize(NTCRT_PRINTF_ARG Kind)
{
#ifdef _WIN64
    UNREFERENCED_PARAMETER(Kind);
    return 8;
#else
    return Kind == NtCRTArgLongLong || Kind == NtCRTArgDouble ? 8 : Kind == NtCRTArgLongDouble ? 12 : 4;
#endif
}

/* The argument a conversion takes */
static inline NTCRT_PRINTF_ARG _NtCRT_ArgKind(const NTCRT_PRINTF_SPEC* Spec, int Conv)
{
    switch (Conv)
    {
    case 'd':
    case 'i':
    case 'u':
    case 'o':
    case 'x':
    case 'X':
        return Spec->Length == NtCRTLengthLongLong || Spec->Length == NtCRTLengthDouble ? NtCRTArgLongLong
                                                                                        : NtCRTArgInt;
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case 'a':
    case 'A':
        return Spec->Length == NtCRTLengthDouble ? NtCRTArgLongDouble : NtCRTArgDouble;
    case 'c':
    case 'C':
        return NtCRTArgInt;
    case 'p':
    case 's':
    case 'S':
    case 'Z':
    case 'n':
        return NtCRTArgPointer;
    default:
        return NtCRTArgNone;
    }
}

#ifndef _WIN64
/* The offsets of the positional arguments of Format, FALSE if one is beyond NTCRT_PRINTF_POSITIONS */
template<typename TChar>
static
BOOL
_NtCRT_ArgOffsets(
    _Inout_ NTCRT_PRINTF_ARGS* Args,
    _In_z_ const TChar* f,
    _In_ ULONG Options)
{
    NTCRT_PRINTF_ARG Kinds[NTCRT_PRINTF_POSITIONS] = {};
    int Count = 0;
    size_t Offset = 0;

    while (*f != '\0')
    {
        NTCRT_PRINTF_SPEC Spec;
        TChar Conv;
        NTCRT_PRINTF_ARG Kind;

        if (*f++ != '%')
        {
            continue;
        }
        Conv = _NtCRT_ParseSpec(&f, &Spec, Options);
        if (Conv == '\0')
        {
            break;
        }
        Kind = _NtCRT_ArgKind(&Spec, Conv);
        const int Positions[3] = {Spec.WidthArg, Spec.PrecisionArg, Kind != NtCRTArgNone ? Spec.Position : -1};
        const NTCRT_PRINTF_ARG Types[3] = {NtCRTArgInt, NtCRTArgInt, Kind};

        for (int i = 0; i < 3; i++)
        {
            if (Positions[i] >= NTCRT_PRINTF_POSITIONS)
            {
                return FALSE;
            }
            if (Positions[i] >= 0)
            {
                Kinds[Positions[i]] = Types[i];
                Count = Positions[i] >= Count ? Positions[i] + 1 : Count;
            }
        }
    }
    for (int i = 0; i < Count; i++)
    {
        Args->Offsets[i] = (USHORT)Offset;
        Offset += _NtCRT_ArgSize(Kinds[i] != NtCRTArgNone ? Kinds[i] : NtCRTArgInt);
    }
    Args->Positioned = TRUE;
    return TRUE;
}
#endif

/*
 * The argument at Position, or the next one for -1: its bits, or the address of a long double. A position beyond
 * NTCRT_PRINTF_POSITIONS fails the output with EINVAL.
 */
template<typename TChar>
static
ULONG64
_NtCRT_Arg(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _Inout_ NTCRT_PRINTF_ARGS* Args,
    _In_z_ const TChar* Format,
    _In_ int Position,
    _In_ NTCRT_PRINTF_ARG Kind,
    _In_ ULONG Options)
{
    const char* p;
    ULONG64 v64;
    ULONG v32;

    if (Position < 0)
    {
        p = Args->Base + Args->Next;
        Args->Next += _NtCRT_ArgSize(Kind);
    } else
    {
#ifdef _WIN64
        BOOL Ok = Position < NTCRT_PRINTF_POSITIONS;

        UNREFERENCED_PARAMETER(Format);
        UNREFERENCED_PARAMETER(Options);
#else
        BOOL Ok = Args->Positioned || _NtCRT_ArgOffsets(Args, Format, Options);
#endif
        if (!Ok)
        {
            NTCRT_ERRNO = EINVAL;
            o->Failed = TRUE;
            return 0;
        }
#ifdef _WIN64
        p = Args->Base + (size_t)Position * 8;
#else
        p = Args->Base + Args->Offsets[Position];
#endif
    }
#ifndef _WIN64
    if (Kind == NtCRTArgLongDouble)
    {
        return (ULONG_PTR)p;
    }
#endif
    if (_NtCRT_ArgSize(Kind) == 8)
    {
        memcpy(&v64, p, sizeof(v64));
        return v64;
    }
    memcpy(&v32, p, sizeof(v32));
    return v32;
}

#pragma endregion

template<typename TChar>
static
void
_NtCRT_Format(
    _Inout_ NTCRT_PRINTF_OUT<TChar>* o,
    _In_z_ const TChar* Format,
    _In_ va_list ap,
    _In_ ULONG Options)
{
    const TChar* f = Format;
    NTCRT_PRINTF_ARGS Args;

    Args.Base = (const char*)ap;
    Args.Next = 0;
#ifndef _WIN64
    Args.Positioned = FALSE;
#endif

    auto Arg = [&](int Position, NTCRT_PRINTF_ARG Kind) {
        return _NtCRT_Arg(o, &Args, Format, Position, Kind, Options);
    };

    while (*f != '\0' && !o->Failed)
    {
        const TChar* Start = f;
        NTCRT_PRINTF_SPEC Spec;
        NTCRT_PRINTF_ARG Kind;
        ULONG64 Value;
        TChar Conv;
        BOOL Wide = sizeof(TChar) != sizeof(char);

        if (*f != '%')
        {
            while (*f != '\0' && *f != '%')
            {
                f++;
            }
            _NtCRT_OutChars(o, Start, (size_t)(f - Start));
            continue;
        }
        f++;
        Conv = _NtCRT_ParseSpec(&f, &Spec, Options);
        if (Conv == '\0')
        {
            _NtCRT_OutChars(o, Start, (size_t)(f - Start));
            break;
        }
        if (Spec.WidthArg != NTCRT_PRINTF_NO_ARG)
        {
            int Width = (int)Arg(Spec.WidthArg, NtCRTArgInt);

            /* A negative width is the flag - and its magnitude, that of INT_MIN too */
            if (Width < 0)
            {
                Spec.Left = TRUE;
            }
            Spec.Width = Width < 0 ? 0u - (unsigned)Width : (unsigned)Width;
        }
        if (Spec.PrecisionArg != NTCRT_PRINTF_NO_ARG)
        {
            Spec.Precision = (int)Arg(Spec.PrecisionArg, NtCRTArgInt);
            Spec.Precision = Spec.Precision < 0 ? -1 : Spec.Precision;
        }
        if (Spec.Left)
        {
            Spec.Zero = FALSE;
        }
        Kind = _NtCRT_ArgKind(&Spec, Conv);
        Value = Kind != NtCRTArgNone ? Arg(Spec.Position, Kind) : 0;
        if (o->Failed)
        {
            break;
        }
        switch (Conv)
        {
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            if (Spec.Precision >= 0)
            {
                Spec.Zero = FALSE;
            }
            if (Conv != 'd' && Conv != 'i')
            {
                Spec.Plus = Spec.Space = FALSE;
            }
            _NtCRT_FormatInteger(o, &Spec, Conv, Value);
            break;
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            _NtCRT_FormatFloat(o, &Spec, Conv, Value);
            break;
        case 'p':
        {
            /* The digits of a pointer, uppercase as msvcrt */
            const int Count = (int)sizeof(void*) * 2;
            ULONG64 v = Value;
            char Digits[16];

            for (int i = Count - 1; i >= 0; i--, v >>= 4)
            {
                Digits[i] = "0123456789ABCDEF"[v & 0xF];
            }
            _NtCRT_OutField(o, &Spec, "", Count, FALSE, [&]() { _NtCRT_OutAscii(o, Digits, Count); });
            break;
        }
        case 's':
        case 'S':
        case 'c':
        case 'C':
        {
            /* The argument is wide by l and w, narrow by h, else %S and %C are of the other width than %s and %c */
            BOOL Upper = Conv == 'S' || Conv == 'C';
            BOOL WideArg = Spec.Text != 0 ? Spec.Text == 2
                                          : (Wide && (Options & NTCRT_PRINTF_LEGACY_WIDE) ? !Upper : Upper);

            Spec.Zero = FALSE;
            if (Conv == 'c' || Conv == 'C')
            {
                Spec.Precision = -1;
                _NtCRT_FormatChar(o, &Spec, Value, WideArg != Wide);
            } else
            {
                _NtCRT_FormatString(o, &Spec, (const void*)(ULONG_PTR)Value, WideArg != Wide, SIZE_MAX);
            }
            break;
        }
        case 'Z':
        {
            /* An ANSI_STRING, a UNICODE_STRING with l or w */
            const void* s = (const void*)(ULONG_PTR)Value;
            BOOL WideArg = Spec.Text == 2;
            const void* Buffer = NULL;
            size_t Count = 0;

            if (s != NULL)
            {
                Buffer = WideArg ? (const void*)((PCUNICODE_STRING)s)->Buffer : (const void*)((const STRING*)s)->Buffer;
                Count = WideArg ? ((PCUNICODE_STRING)s)->Length / sizeof(WCHAR) : ((const STRING*)s)->Length;
            }
            Spec.Zero = FALSE;
            _NtCRT_FormatString(o, &Spec, Buffer, WideArg != Wide, Count);
            break;
        }
        case 'n':
        {
            void* p = (void*)(ULONG_PTR)Value;

            switch (Spec.Length)
            {
            case NtCRTLengthChar:
                *(signed char*)p = (signed char)o->Length;
                break;
            case NtCRTLengthShort:
                *(SHORT*)p = (SHORT)o->Length;
                break;
            case NtCRTLengthLongLong:
            case NtCRTLengthDouble:
                *(LONG64*)p = (LONG64)o->Length;
                break;
            default:
                *(int*)p = (int)o->Length;
                break;
            }
            break;
        }
        case '%':
            _NtCRT_OutChars(o, f - 1, 1);
            break;
        default:
            /* Not a conversion: copied as it is */
            _NtCRT_OutChars(o, Start, (size_t)(f - Start));
            break;
        }
    }
}

/* The length as the int of printf, -1 with errno */
template<typename TChar>
static
int
_NtCRT_FormatResult(
    _In_ const NTCRT_PRINTF_OUT<TChar>* o)
{
    if (o->Failed)
    {
        return -1;
    }
    if (o->Length > INT_MAX)
    {
        NTCRT_ERRNO = EOVERFLOW;
        return -1;
    }
    return (int)o->Length;
}

template<typename TChar>
static
int
_NtCRT_FormatBuffer(
    _Out_writes_opt_(Count) TChar* Buffer,
    _In_ size_t Count,
    _In_z_ const TChar* Format,
    _In_ va_list ap,
    _In_ ULONG Options,
    _In_ BOOL Legacy)
{
    NTCRT_PRINTF_OUT<TChar> o = {};
    int Ret;

    o.Buffer = Buffer;
    o.Count = Buffer == NULL ? 0 : Count;

    if (Format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_Format(&o, Format, ap, Options);
    Ret = _NtCRT_FormatResult(&o);
    if (Buffer == NULL || Count == 0)
    {
        return Legacy && Buffer != NULL && Ret > 0 ? -1 : Ret;
    }
    if (o.Length < Count)
    {
        Buffer[o.Length] = 0;
    } else if (!Legacy)
    {
        Buffer[Count - 1] = 0;
    }
    return Legacy && Ret >= 0 && o.Length > Count ? -1 : Ret;
}
