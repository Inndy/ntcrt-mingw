/*
 * Conversion of decimal and hexadecimal floating point numbers to double and float, for strtod and the like.
 *
 * Ported from internal/strconv of Go 1.27 (uscale.go, atof.go, decimal.go): fast unrounded scaling by Russ Cox,
 * "Floating-Point Printing and Parsing Can Be Simple And Fast", https://research.swtch.com/fp. A number of up to
 * 19 significant digits is d * 10**p, scaled by a 128-bit power of 10 (FloatPow10.inl) to the 53 or 24 bits of
 * the result and two bits more for rounding. More digits are truncated to 19 when d + 1 rounds the same, the
 * multiprecision decimal of up to 800 digits is the fallback. The syntax is the one of strtod in C instead of Go.
 * The decimal also gives the exact digits for %g of printf (Format.cpp).
 *
 * Copyright 2009 The Go Authors.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *    * Neither the name of Google LLC nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "NtCRT.h"

#include <string.h>

typedef struct _NTCRT_POW10
{
    ULONG64 Hi;
    ULONG64 Lo;
} NTCRT_POW10;

#include "FloatPow10.inl"

/* The format of a binary floating point type */
typedef struct _NTCRT_FLOAT_INFO
{
    int MantBits;
    int ExpBits;
    int Bias;
} NTCRT_FLOAT_INFO;

static const NTCRT_FLOAT_INFO g_Float64Info = { 52, 11, -1023 };
static const NTCRT_FLOAT_INFO g_Float32Info = { 23, 8, -127 };

#pragma region Unrounded scaling (uscale.go)

/*
 * An unrounded value: the integer part shifted left by 2, bit 1 is the half and bit 0 is set if any lower bit
 * is set ("sticky").
 */
typedef ULONG64 NTCRT_UNROUNDED;

static inline ULONG64 _NtCRT_Round(NTCRT_UNROUNDED u)
{
    return (u + 1 + ((u >> 2) & 1)) >> 2;
}

/* The minimum unrounded that rounds to x */
static inline NTCRT_UNROUNDED _NtCRT_Unmin(ULONG64 x)
{
    return (x << 2) - 2;
}

static inline int _NtCRT_Len64(ULONG64 x)
{
    return x == 0 ? 0 : 64 - __builtin_clzll(x);
}

/* floor(log2(10**x)), log2 10 ~ 108853 / 2**15 */
static inline int _NtCRT_Log2Pow10(int x)
{
    return (x * 108853) >> 15;
}

typedef struct _NTCRT_SCALER
{
    ULONG64 PmHi;
    ULONG64 PmLo;
    int s;
} NTCRT_SCALER;

/* The scaling constants of e, p, lp must be _NtCRT_Log2Pow10(p) */
static inline void _NtCRT_Prescale(NTCRT_SCALER* Pre, int e, int p, int lp)
{
    Pre->PmHi = g_Pow10[p - NTCRT_POW10_MIN].Hi;
    Pre->PmLo = g_Pow10[p - NTCRT_POW10_MIN].Lo;
    Pre->s = -(e + lp + 3);
}

/* The unrounded x * 2**e * 10**p, x left-justified (high bit set), s < 64 */
static inline NTCRT_UNROUNDED _NtCRT_Uscale(ULONG64 x, const NTCRT_SCALER* c)
{
    unsigned __int128 Product = (unsigned __int128)x * c->PmHi;
    ULONG64 Hi = (ULONG64)(Product >> 64), Mid = (ULONG64)Product, Mid2;
    int s = c->s & 63;

    if (Hi >> s << s != Hi)
    {
        return Hi >> s | 1;
    }
    Mid2 = (ULONG64)(((unsigned __int128)x * c->PmLo) >> 64);
    Hi -= Mid < Mid2;
    return Hi >> s | (Mid - Mid2 > 1);
}

/* Bits of m * 2**e, m the 53-bit mantissa with the sign bit, TRUE if it overflows to infinity */
static BOOL _NtCRT_Pack64(ULONG64 m, int e, ULONG64* Bits)
{
    if ((m & (1ull << 52)) == 0)
    {
        *Bits = m;
        return FALSE;
    }
    if (e >= 0x7FF - 1075)
    {
        *Bits = (m & (1ull << 63)) | 0x7FFull << 52;
        return TRUE;
    }
    *Bits = (m & ~(1ull << 52)) | (ULONG64)(1075 + e) << 52;
    return FALSE;
}

static BOOL _NtCRT_Pack32(ULONG m, int e, ULONG* Bits)
{
    if ((m & (1u << 23)) == 0)
    {
        *Bits = m;
        return FALSE;
    }
    if (e >= 0xFF - 150)
    {
        *Bits = (m & (1u << 31)) | 0xFFu << 23;
        return TRUE;
    }
    *Bits = (m & ~(1u << 23)) | (ULONG)(150 + e) << 23;
    return FALSE;
}

/* d * 10**p rounded to a double, d of at most 19 digits */
static BOOL _NtCRT_ParseFloat64(ULONG64 d, int p, ULONG64 Sign, ULONG64* Bits)
{
    int b = _NtCRT_Len64(d), lp = _NtCRT_Log2Pow10(p), e = 53 - b - lp, s;
    NTCRT_SCALER Pre;
    NTCRT_UNROUNDED u;

    e = e < 1074 ? e : 1074;
    _NtCRT_Prescale(&Pre, e - (64 - b), p, lp);
    if (Pre.s >= 64)
    {
        *Bits = Sign;
        return FALSE;
    }
    u = _NtCRT_Uscale(d << (64 - b), &Pre);
    s = u >= _NtCRT_Unmin(1ull << 53);
    u = u >> s | (u & 1);
    e -= s;
    return _NtCRT_Pack64(Sign | _NtCRT_Round(u), -e, Bits);
}

/* d * 10**p rounded to a float, d of at most 19 digits */
static BOOL _NtCRT_ParseFloat32(ULONG64 d, int p, ULONG Sign, ULONG* Bits)
{
    int b = _NtCRT_Len64(d), lp = _NtCRT_Log2Pow10(p), e = 24 - b - lp, s;
    NTCRT_SCALER Pre;
    NTCRT_UNROUNDED u;

    e = e < 149 ? e : 149;
    _NtCRT_Prescale(&Pre, e - (64 - b), p, lp);
    if (Pre.s >= 64)
    {
        *Bits = Sign;
        return FALSE;
    }
    u = _NtCRT_Uscale(d << (64 - b), &Pre);
    s = u >= _NtCRT_Unmin(1ull << 24);
    u = u >> s | (u & 1);
    e -= s;
    return _NtCRT_Pack32(Sign | (ULONG)_NtCRT_Round(u), -e, Bits);
}

#pragma endregion

#pragma region Multiprecision decimal (decimal.go)

typedef struct _NTCRT_DECIMAL
{
    char d[800]; /* Digits */
    int nd;      /* Number of digits */
    int dp;      /* Decimal point */
    BOOL Neg;
    BOOL Trunc; /* Nonzero digits discarded beyond d[nd - 1] */
} NTCRT_DECIMAL;

/* The largest shift without overflow of 9 << k in 64 bits */
#define NTCRT_MAX_SHIFT 60

static void _NtCRT_DecimalTrim(NTCRT_DECIMAL* a)
{
    while (a->nd > 0 && a->d[a->nd - 1] == '0')
    {
        a->nd--;
    }
    if (a->nd == 0)
    {
        a->dp = 0;
    }
}

/* Divide by 2**k */
static void _NtCRT_DecimalRightShift(NTCRT_DECIMAL* a, int k)
{
    int r = 0, w = 0;
    ULONG64 n = 0, Mask = (1ull << k) - 1;

    /* Pick up enough leading digits to cover the first shift */
    for (; n >> k == 0; r++)
    {
        if (r >= a->nd)
        {
            if (n == 0)
            {
                a->nd = 0;
                return;
            }
            while (n >> k == 0)
            {
                n *= 10;
                r++;
            }
            break;
        }
        n = n * 10 + (ULONG64)(a->d[r] - '0');
    }
    a->dp -= r - 1;

    /* Pick up a digit, put down a digit */
    for (; r < a->nd; r++)
    {
        ULONG64 Digit = n >> k;

        n &= Mask;
        a->d[w++] = (char)(Digit + '0');
        n = n * 10 + (ULONG64)(a->d[r] - '0');
    }

    /* Put down extra digits */
    while (n > 0)
    {
        ULONG64 Digit = n >> k;

        n &= Mask;
        if (w < (int)sizeof(a->d))
        {
            a->d[w++] = (char)(Digit + '0');
        } else if (Digit > 0)
        {
            a->Trunc = TRUE;
        }
        n *= 10;
    }
    a->nd = w;
    _NtCRT_DecimalTrim(a);
}

/* The digits a shift left by k adds, one fewer if the decimal is less than 5**k as a string (leading digits) */
typedef struct _NTCRT_LEFT_CHEAT
{
    int Delta;
    const char* Cutoff;
} NTCRT_LEFT_CHEAT;

static const NTCRT_LEFT_CHEAT g_LeftCheats[] = {
    { 0, "" },
    { 1, "5" },
    { 1, "25" },
    { 1, "125" },
    { 2, "625" },
    { 2, "3125" },
    { 2, "15625" },
    { 3, "78125" },
    { 3, "390625" },
    { 3, "1953125" },
    { 4, "9765625" },
    { 4, "48828125" },
    { 4, "244140625" },
    { 4, "1220703125" },
    { 5, "6103515625" },
    { 5, "30517578125" },
    { 5, "152587890625" },
    { 6, "762939453125" },
    { 6, "3814697265625" },
    { 6, "19073486328125" },
    { 7, "95367431640625" },
    { 7, "476837158203125" },
    { 7, "2384185791015625" },
    { 7, "11920928955078125" },
    { 8, "59604644775390625" },
    { 8, "298023223876953125" },
    { 8, "1490116119384765625" },
    { 9, "7450580596923828125" },
    { 9, "37252902984619140625" },
    { 9, "186264514923095703125" },
    { 10, "931322574615478515625" },
    { 10, "4656612873077392578125" },
    { 10, "23283064365386962890625" },
    { 10, "116415321826934814453125" },
    { 11, "582076609134674072265625" },
    { 11, "2910383045673370361328125" },
    { 11, "14551915228366851806640625" },
    { 12, "72759576141834259033203125" },
    { 12, "363797880709171295166015625" },
    { 12, "1818989403545856475830078125" },
    { 13, "9094947017729282379150390625" },
    { 13, "45474735088646411895751953125" },
    { 13, "227373675443232059478759765625" },
    { 13, "1136868377216160297393798828125" },
    { 14, "5684341886080801486968994140625" },
    { 14, "28421709430404007434844970703125" },
    { 14, "142108547152020037174224853515625" },
    { 15, "710542735760100185871124267578125" },
    { 15, "3552713678800500929355621337890625" },
    { 15, "17763568394002504646778106689453125" },
    { 16, "88817841970012523233890533447265625" },
    { 16, "444089209850062616169452667236328125" },
    { 16, "2220446049250313080847263336181640625" },
    { 16, "11102230246251565404236316680908203125" },
    { 17, "55511151231257827021181583404541015625" },
    { 17, "277555756156289135105907917022705078125" },
    { 17, "1387778780781445675529539585113525390625" },
    { 18, "6938893903907228377647697925567626953125" },
    { 18, "34694469519536141888238489627838134765625" },
    { 18, "173472347597680709441192448139190673828125" },
    { 19, "867361737988403547205962240695953369140625" },
};

static BOOL _NtCRT_PrefixIsLessThan(const char* b, int n, const char* s)
{
    for (int i = 0; s[i] != '\0'; i++)
    {
        if (i >= n)
        {
            return TRUE;
        }
        if (b[i] != s[i])
        {
            return b[i] < s[i];
        }
    }
    return FALSE;
}

/* Multiply by 2**k */
static void _NtCRT_DecimalLeftShift(NTCRT_DECIMAL* a, int k)
{
    int Delta = g_LeftCheats[k].Delta - _NtCRT_PrefixIsLessThan(a->d, a->nd, g_LeftCheats[k].Cutoff);
    int r = a->nd, w = a->nd + Delta;
    ULONG64 n = 0;

    /* Pick up a digit, put down a digit */
    for (r--; r >= 0; r--)
    {
        ULONG64 Quo, Rem;

        n += (ULONG64)(a->d[r] - '0') << k;
        Quo = n / 10;
        Rem = n - 10 * Quo;
        w--;
        if (w < (int)sizeof(a->d))
        {
            a->d[w] = (char)(Rem + '0');
        } else if (Rem != 0)
        {
            a->Trunc = TRUE;
        }
        n = Quo;
    }

    /* Put down extra digits */
    while (n > 0)
    {
        ULONG64 Quo = n / 10, Rem = n - 10 * Quo;

        w--;
        if (w < (int)sizeof(a->d))
        {
            a->d[w] = (char)(Rem + '0');
        } else if (Rem != 0)
        {
            a->Trunc = TRUE;
        }
        n = Quo;
    }
    a->nd += Delta;
    if (a->nd >= (int)sizeof(a->d))
    {
        a->nd = (int)sizeof(a->d);
    }
    a->dp += Delta;
    _NtCRT_DecimalTrim(a);
}

/* Shift left by k bits, right if k is negative */
static void _NtCRT_DecimalShift(NTCRT_DECIMAL* a, int k)
{
    if (a->nd == 0)
    {
        return;
    }
    if (k > 0)
    {
        for (; k > NTCRT_MAX_SHIFT; k -= NTCRT_MAX_SHIFT)
        {
            _NtCRT_DecimalLeftShift(a, NTCRT_MAX_SHIFT);
        }
        _NtCRT_DecimalLeftShift(a, k);
    } else if (k < 0)
    {
        for (; k < -NTCRT_MAX_SHIFT; k += NTCRT_MAX_SHIFT)
        {
            _NtCRT_DecimalRightShift(a, NTCRT_MAX_SHIFT);
        }
        _NtCRT_DecimalRightShift(a, -k);
    }
}

static void _NtCRT_DecimalAssign(NTCRT_DECIMAL* a, ULONG64 v)
{
    char Buf[20];
    int n = 0;

    while (v > 0)
    {
        Buf[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    a->nd = 0;
    while (n > 0)
    {
        a->d[a->nd++] = Buf[--n];
    }
    a->dp = a->nd;
    a->Neg = FALSE;
    a->Trunc = FALSE;
    _NtCRT_DecimalTrim(a);
}

/* Whether a chopped at nd digits rounds up, a tie to even */
static BOOL _NtCRT_DecimalShouldRoundUp(const NTCRT_DECIMAL* a, int nd)
{
    if (nd < 0 || nd >= a->nd)
    {
        return FALSE;
    }
    if (a->d[nd] == '5' && nd + 1 == a->nd)
    {
        /* Truncated digits make it a little more than halfway */
        if (a->Trunc)
        {
            return TRUE;
        }
        return nd > 0 && (a->d[nd - 1] - '0') % 2 != 0;
    }
    return a->d[nd] >= '5';
}

/* Round a to nd digits */
static void _NtCRT_DecimalRound(NTCRT_DECIMAL* a, int nd)
{
    int i;

    if (nd < 0 || nd >= a->nd)
    {
        return;
    }
    if (!_NtCRT_DecimalShouldRoundUp(a, nd))
    {
        a->nd = nd;
        _NtCRT_DecimalTrim(a);
        return;
    }
    for (i = nd - 1; i >= 0 && a->d[i] == '9'; i--)
    {
    }
    if (i < 0)
    {
        /* All 9s: 1 and one more digit before the point */
        a->d[0] = '1';
        a->nd = 1;
        a->dp++;
        return;
    }
    a->d[i]++;
    a->nd = i + 1;
}

/* The integer part, rounded */
static ULONG64 _NtCRT_DecimalRoundedInteger(const NTCRT_DECIMAL* a)
{
    ULONG64 n = 0;
    int i;

    if (a->dp > 20)
    {
        return ~0ull;
    }
    for (i = 0; i < a->dp && i < a->nd; i++)
    {
        n = n * 10 + (ULONG64)(a->d[i] - '0');
    }
    for (; i < a->dp; i++)
    {
        n *= 10;
    }
    if (_NtCRT_DecimalShouldRoundUp(a, a->dp))
    {
        n++;
    }
    return n;
}

/* The bits of the nearest floating point number, TRUE if it overflows to infinity */
static BOOL _NtCRT_DecimalFloatBits(NTCRT_DECIMAL* d, const NTCRT_FLOAT_INFO* Flt, ULONG64* Bits)
{
    /* Decimal powers of ten to binary powers of two */
    static const int PowTab[] = { 1, 3, 6, 9, 13, 16, 19, 23, 26 };
    int Exp = 0;
    ULONG64 Mant = 0;
    BOOL Overflow = FALSE;

    if (d->nd == 0 || d->dp < -330)
    {
        /* Zero */
        Exp = Flt->Bias;
        goto Out;
    }
    if (d->dp > 310)
    {
        goto Overflow;
    }

    /* Scale by powers of two to [0.5, 1) */
    while (d->dp > 0)
    {
        int n = d->dp >= (int)ARRAYSIZE(PowTab) ? 27 : PowTab[d->dp];

        _NtCRT_DecimalShift(d, -n);
        Exp += n;
    }
    while (d->dp < 0 || (d->dp == 0 && d->d[0] < '5'))
    {
        int n = -d->dp >= (int)ARRAYSIZE(PowTab) ? 27 : PowTab[-d->dp];

        _NtCRT_DecimalShift(d, n);
        Exp -= n;
    }

    /* [0.5, 1) to the [1, 2) of floating point */
    Exp--;

    /* Below the minimum exponent the number is denormalized */
    if (Exp < Flt->Bias + 1)
    {
        int n = Flt->Bias + 1 - Exp;

        _NtCRT_DecimalShift(d, -n);
        Exp += n;
    }
    if (Exp - Flt->Bias >= (1 << Flt->ExpBits) - 1)
    {
        goto Overflow;
    }

    /* 1 + MantBits bits, rounding may add one */
    _NtCRT_DecimalShift(d, 1 + Flt->MantBits);
    Mant = _NtCRT_DecimalRoundedInteger(d);
    if (Mant == 2ull << Flt->MantBits)
    {
        Mant >>= 1;
        Exp++;
        if (Exp - Flt->Bias >= (1 << Flt->ExpBits) - 1)
        {
            goto Overflow;
        }
    }
    if ((Mant & (1ull << Flt->MantBits)) == 0)
    {
        /* Denormal */
        Exp = Flt->Bias;
    }
    goto Out;

Overflow:
    Mant = 0;
    Exp = (1 << Flt->ExpBits) - 1 + Flt->Bias;
    Overflow = TRUE;

Out:
    *Bits = Mant & ((1ull << Flt->MantBits) - 1);
    *Bits |= (ULONG64)((Exp - Flt->Bias) & ((1 << Flt->ExpBits) - 1)) << Flt->MantBits;
    if (d->Neg)
    {
        *Bits |= 1ull << Flt->MantBits << Flt->ExpBits;
    }
    return Overflow;
}

#pragma endregion

#pragma region Parsing (atof.go)

/* A number scanned by the syntax of strtod */
typedef struct _NTCRT_FLOAT_TEXT
{
    ULONG64 Mantissa; /* The first 19 decimal or 16 hex significant digits */
    int Exp;          /* Of 10, or of 2 for hex */
    BOOL Neg;
    BOOL Trunc; /* Nonzero digits after those of Mantissa */
    BOOL Hex;
    int Special;     /* 1 infinity, 2 NaN */
    size_t Digits;   /* Offset of the digits and the point */
    size_t DigitEnd; /* Offset after them */
    int Exp10;       /* The exponent written after the digits, for the decimal fallback */
} NTCRT_FLOAT_TEXT;

template<typename TChar>
static inline int _NtCRT_Lower(TChar c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : (int)c;
}

/* The length of the longest prefix of s matching the lowercase Word, ignoring case */
template<typename TChar>
static size_t _NtCRT_MatchWord(const TChar* s, const char* Word)
{
    size_t n = 0;

    while (Word[n] != '\0' && _NtCRT_Lower(s[n]) == Word[n])
    {
        n++;
    }
    return n;
}

template<typename TChar>
static inline int _NtCRT_HexDigit(TChar c)
{
    int l = _NtCRT_Lower(c);

    return c >= '0' && c <= '9' ? (int)(c - '0') : l >= 'a' && l <= 'f' ? l - 'a' + 10 : -1;
}

/*
 * An optional exponent of the character ExpChar, the length taken, 0 if there is none. It saturates at 2**28: far
 * past any result once the position of the digits is added, which cannot overflow then. Go 1.27 saturates at 10000,
 * before that position is added, so the zeros of a long number made 0 or infinity of 1.
 */
template<typename TChar>
static size_t _NtCRT_ScanExponent(const TChar* s, char ExpChar, int* Exp)
{
    size_t i = 1;
    int Sign = 1, e = 0;

    if (_NtCRT_Lower(s[0]) != ExpChar)
    {
        return 0;
    }
    if (s[i] == '+' || s[i] == '-')
    {
        Sign = s[i++] == '-' ? -1 : 1;
    }
    if (s[i] < '0' || s[i] > '9')
    {
        return 0;
    }
    for (; s[i] >= '0' && s[i] <= '9'; i++)
    {
        e = e < (1 << 28) / 10 ? e * 10 + (int)(s[i] - '0') : 1 << 28;
    }
    *Exp = e * Sign;
    return i;
}

/*
 * Scan the subject sequence of strtod: an optional sign, then inf, infinity, nan, nan(n-char-sequence), a hex
 * number 0x... with an optional binary exponent p..., or a decimal number with an optional exponent e....
 * Returns its length, 0 if there is none.
 */
template<typename TChar>
static size_t _NtCRT_ScanFloat(const TChar* s, NTCRT_FLOAT_TEXT* t)
{
    size_t i = 0, n;
    int Base = 10, MaxMantDigits = 19, nd = 0, ndMant = 0, dp = 0, e;
    BOOL SawDot = FALSE, SawDigits = FALSE;

    memset(t, 0, sizeof(*t));
    if (s[i] == '+' || s[i] == '-')
    {
        t->Neg = s[i++] == '-';
    }
    if (_NtCRT_Lower(s[i]) == 'i')
    {
        n = _NtCRT_MatchWord(s + i, "infinity");
        if (n < 3)
        {
            return 0;
        }
        t->Special = 1;
        return i + (n == 8 ? 8 : 3);
    }
    if (_NtCRT_Lower(s[i]) == 'n')
    {
        if (_NtCRT_MatchWord(s + i, "nan") != 3)
        {
            return 0;
        }
        t->Special = 2;
        i += 3;
        if (s[i] == '(')
        {
            n = i + 1;
            while ((s[n] >= '0' && s[n] <= '9') || (_NtCRT_Lower(s[n]) >= 'a' && _NtCRT_Lower(s[n]) <= 'z') ||
                   s[n] == '_')
            {
                n++;
            }
            if (s[n] == ')')
            {
                i = n + 1;
            }
        }
        return i;
    }

    /* "0x" needs a hex digit, "0x" alone is the number 0 */
    if (s[i] == '0' && _NtCRT_Lower(s[i + 1]) == 'x' &&
        (_NtCRT_HexDigit(s[i + 2]) >= 0 || (s[i + 2] == '.' && _NtCRT_HexDigit(s[i + 3]) >= 0)))
    {
        Base = 16;
        MaxMantDigits = 16;
        t->Hex = TRUE;
        i += 2;
    }
    t->Digits = i;
    for (;; i++)
    {
        TChar c = s[i];
        int Digit = Base == 16 ? _NtCRT_HexDigit(c) : c >= '0' && c <= '9' ? (int)(c - '0') : -1;

        if (c == '.')
        {
            if (SawDot)
            {
                break;
            }
            SawDot = TRUE;
            dp = nd;
            continue;
        }
        if (Digit < 0)
        {
            break;
        }
        SawDigits = TRUE;
        if (Digit == 0 && nd == 0)
        {
            /* Leading zeros */
            dp--;
            continue;
        }
        nd++;
        if (ndMant < MaxMantDigits)
        {
            t->Mantissa = t->Mantissa * (ULONG64)Base + (ULONG64)Digit;
            ndMant++;
        } else if (Digit != 0)
        {
            t->Trunc = TRUE;
        }
    }
    if (!SawDigits)
    {
        return 0;
    }
    t->DigitEnd = i;
    if (!SawDot)
    {
        dp = nd;
    }
    if (Base == 16)
    {
        dp *= 4;
        ndMant *= 4;
    }
    e = 0;
    n = _NtCRT_ScanExponent(s + i, Base == 16 ? 'p' : 'e', &e);
    i += n;
    dp += e;
    t->Exp10 = e;
    if (t->Mantissa != 0)
    {
        t->Exp = dp - ndMant;
    }
    return i;
}

/*
 * The decimal of the digits of t at s. The point counts all the significant digits before it, also those past
 * the 800 the decimal holds (decimal.set of Go 1.27 counts only the ones it holds).
 */
template<typename TChar>
static void _NtCRT_DecimalSet(NTCRT_DECIMAL* b, const TChar* s, const NTCRT_FLOAT_TEXT* t)
{
    BOOL SawDot = FALSE;
    int nd = 0;

    b->nd = b->dp = 0;
    b->Neg = t->Neg;
    b->Trunc = FALSE;
    for (size_t i = t->Digits; i < t->DigitEnd; i++)
    {
        if (s[i] == '.')
        {
            SawDot = TRUE;
            b->dp = nd;
            continue;
        }
        if (s[i] == '0' && nd == 0)
        {
            b->dp--;
            continue;
        }
        nd++;
        if (b->nd < (int)sizeof(b->d))
        {
            b->d[b->nd++] = (char)s[i];
        } else if (s[i] != '0')
        {
            b->Trunc = TRUE;
        }
    }
    if (!SawDot)
    {
        b->dp = nd;
    }
    b->dp += t->Exp10;
}

/*
 * A hex mantissa * 2**Exp rounded to the type of Flt, two more bits for rounding, the low one sticky. TRUE if it
 * overflows to infinity.
 */
static BOOL _NtCRT_AtofHex(const NTCRT_FLOAT_INFO* Flt, ULONG64 Mantissa, int Exp, BOOL Neg, BOOL Trunc, ULONG64* Bits)
{
    int MaxExp = (1 << Flt->ExpBits) + Flt->Bias - 2, MinExp = Flt->Bias + 1;
    ULONG64 Round;
    BOOL Overflow = FALSE;

    /* The mantissa is divided by 2**MantBits */
    Exp += Flt->MantBits;
    while (Mantissa != 0 && Mantissa >> (Flt->MantBits + 2) == 0)
    {
        Mantissa <<= 1;
        Exp--;
    }
    if (Trunc)
    {
        Mantissa |= 1;
    }
    while (Mantissa >> (1 + Flt->MantBits + 2) != 0)
    {
        Mantissa = Mantissa >> 1 | (Mantissa & 1);
        Exp++;
    }

    /* Denormalize a too small exponent */
    while (Mantissa > 1 && Exp < MinExp - 2)
    {
        Mantissa = Mantissa >> 1 | (Mantissa & 1);
        Exp++;
    }

    /* Round with the two low bits, a tie to even */
    Round = Mantissa & 3;
    Mantissa >>= 2;
    Round |= Mantissa & 1;
    Exp += 2;
    if (Round == 3)
    {
        Mantissa++;
        if (Mantissa == 1ull << (1 + Flt->MantBits))
        {
            Mantissa >>= 1;
            Exp++;
        }
    }
    if (Mantissa >> Flt->MantBits == 0)
    {
        /* Denormal or zero */
        Exp = Flt->Bias;
    }
    if (Exp > MaxExp)
    {
        Mantissa = 1ull << Flt->MantBits;
        Exp = MaxExp + 1;
        Overflow = TRUE;
    }
    *Bits = Mantissa & ((1ull << Flt->MantBits) - 1);
    *Bits |= (ULONG64)((Exp - Flt->Bias) & ((1 << Flt->ExpBits) - 1)) << Flt->MantBits;
    if (Neg)
    {
        *Bits |= 1ull << Flt->MantBits << Flt->ExpBits;
    }
    return Overflow;
}

/* Exact powers of ten */
static const double g_Float64Pow10[] = { 1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                         1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
static const float g_Float32Pow10[] = { 1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f, 1e10f };

/*
 * Mantissa * 10**Exp by one multiplication or division if all are exact: an exact integer times or divided by an
 * exact power of ten rounds correctly.
 */
static BOOL _NtCRT_Atof64Exact(ULONG64 Mantissa, int Exp, BOOL Neg, double* f)
{
    if (Mantissa >> 52 != 0)
    {
        return FALSE;
    }
    *f = (double)Mantissa;
    if (Neg)
    {
        *f = -*f;
    }
    if (Exp == 0)
    {
        return TRUE;
    }
    if (Exp > 0 && Exp <= 15 + 22)
    {
        /* Move zeros of a big exponent into the integer */
        if (Exp > 22)
        {
            *f *= g_Float64Pow10[Exp - 22];
            Exp = 22;
        }
        if (*f > 1e15 || *f < -1e15)
        {
            return FALSE;
        }
        *f *= g_Float64Pow10[Exp];
        return TRUE;
    }
    if (Exp < 0 && Exp >= -22)
    {
        *f /= g_Float64Pow10[-Exp];
        return TRUE;
    }
    return FALSE;
}

static BOOL _NtCRT_Atof32Exact(ULONG64 Mantissa, int Exp, BOOL Neg, float* f)
{
    if (Mantissa >> 23 != 0)
    {
        return FALSE;
    }
    *f = (float)Mantissa;
    if (Neg)
    {
        *f = -*f;
    }
    if (Exp == 0)
    {
        return TRUE;
    }
    if (Exp > 0 && Exp <= 7 + 10)
    {
        if (Exp > 10)
        {
            *f *= g_Float32Pow10[Exp - 10];
            Exp = 10;
        }
        if (*f > 1e7f || *f < -1e7f)
        {
            return FALSE;
        }
        *f *= g_Float32Pow10[Exp];
        return TRUE;
    }
    if (Exp < 0 && Exp >= -10)
    {
        *f /= g_Float32Pow10[-Exp];
        return TRUE;
    }
    return FALSE;
}

/* A range error is an overflow to infinity or an underflow to zero or a denormal */
template<typename TChar>
static size_t _NtCRT_Atof64(const TChar* s, double* Value, BOOL* Range)
{
    NTCRT_FLOAT_TEXT t;
    size_t n = _NtCRT_ScanFloat(s, &t);
    ULONG64 Sign = (ULONG64)t.Neg << 63, Bits = Sign;
    BOOL Overflow = FALSE;

    *Range = FALSE;
    if (n == 0)
    {
        *Value = 0;
        return 0;
    }
    if (t.Special != 0)
    {
        Bits = Sign | (t.Special == 1 ? 0x7FF0000000000000ull : 0x7FF8000000000000ull);
        memcpy(Value, &Bits, sizeof(Bits));
        return n;
    }
    if (t.Hex)
    {
        Overflow = _NtCRT_AtofHex(&g_Float64Info, t.Mantissa, t.Exp, t.Neg, t.Trunc, &Bits);
    } else if (t.Mantissa == 0)
    {
        Bits = Sign;
    } else if (t.Exp > 310)
    {
        Bits = Sign | 0x7FF0000000000000ull;
        Overflow = TRUE;
    } else if (t.Exp < -345)
    {
        Bits = Sign;
    } else if (!t.Trunc && _NtCRT_Atof64Exact(t.Mantissa, t.Exp, t.Neg, Value))
    {
        return n;
    } else
    {
        ULONG64 Bits1;

        Overflow = _NtCRT_ParseFloat64(t.Mantissa, t.Exp, Sign, &Bits);
        /* The truncated digits do not matter if d + 1 rounds the same */
        if (t.Trunc && (_NtCRT_ParseFloat64(t.Mantissa + 1, t.Exp, Sign, &Bits1), Bits1 != Bits))
        {
            NTCRT_DECIMAL Dec;

            _NtCRT_DecimalSet(&Dec, s, &t);
            Overflow = _NtCRT_DecimalFloatBits(&Dec, &g_Float64Info, &Bits);
        }
    }
    memcpy(Value, &Bits, sizeof(Bits));
    *Range = Overflow || (t.Mantissa != 0 && (Bits & 0x7FF0000000000000ull) == 0);
    return n;
}

template<typename TChar>
static size_t _NtCRT_Atof32(const TChar* s, float* Value, BOOL* Range)
{
    NTCRT_FLOAT_TEXT t;
    size_t n = _NtCRT_ScanFloat(s, &t);
    ULONG Sign = (ULONG)t.Neg << 31, Bits = Sign;
    ULONG64 Bits64;
    BOOL Overflow = FALSE;

    *Range = FALSE;
    if (n == 0)
    {
        *Value = 0;
        return 0;
    }
    if (t.Special != 0)
    {
        Bits = Sign | (t.Special == 1 ? 0x7F800000u : 0x7FC00000u);
        memcpy(Value, &Bits, sizeof(Bits));
        return n;
    }
    if (t.Hex)
    {
        Overflow = _NtCRT_AtofHex(&g_Float32Info, t.Mantissa, t.Exp, t.Neg, t.Trunc, &Bits64);
        Bits = (ULONG)Bits64;
    } else if (t.Mantissa == 0)
    {
        Bits = Sign;
    } else if (t.Exp > 40)
    {
        Bits = Sign | 0x7F800000u;
        Overflow = TRUE;
    } else if (t.Exp < -70)
    {
        Bits = Sign;
    } else if (!t.Trunc && _NtCRT_Atof32Exact(t.Mantissa, t.Exp, t.Neg, Value))
    {
        return n;
    } else
    {
        ULONG Bits1;

        Overflow = _NtCRT_ParseFloat32(t.Mantissa, t.Exp, Sign, &Bits);
        if (t.Trunc && (_NtCRT_ParseFloat32(t.Mantissa + 1, t.Exp, Sign, &Bits1), Bits1 != Bits))
        {
            NTCRT_DECIMAL Dec;

            _NtCRT_DecimalSet(&Dec, s, &t);
            Overflow = _NtCRT_DecimalFloatBits(&Dec, &g_Float32Info, &Bits64);
            Bits = (ULONG)Bits64;
        }
    }
    memcpy(Value, &Bits, sizeof(Bits));
    *Range = Overflow || (t.Mantissa != 0 && (Bits & 0x7F800000u) == 0);
    return n;
}

#pragma endregion

#pragma region Formatting (ftoa.go)

/*
 * The exponent of %e of a finite double with Digits significant digits, ties to even, the digits after the last one
 * that is not 0 in *Zeros. Exact, by the multiprecision decimal: the slow path of ftoa.
 */
EXTERN_C int _NtCRT_DecimalExponent(double Value, int Digits, int* Zeros)
{
    const NTCRT_FLOAT_INFO* Flt = &g_Float64Info;
    NTCRT_DECIMAL d;
    ULONG64 Bits, Mant;
    int Exp;

    memcpy(&Bits, &Value, sizeof(Bits));
    Mant = Bits & ((1ull << Flt->MantBits) - 1);
    Exp = (int)(Bits >> Flt->MantBits) & ((1 << Flt->ExpBits) - 1);
    if (Exp == 0)
    {
        Exp++;
    } else
    {
        Mant |= 1ull << Flt->MantBits;
    }
    Exp += Flt->Bias;
    _NtCRT_DecimalAssign(&d, Mant);
    _NtCRT_DecimalShift(&d, Exp - Flt->MantBits);
    _NtCRT_DecimalRound(&d, Digits);
    *Zeros = Digits - d.nd;
    return d.nd == 0 ? 0 : d.dp - 1;
}

#pragma endregion

#pragma region strtod

/*
 * The headers of mingw-w64 call __mingw_strtod and the like for strtod, the specs wrap them (--wrap). No function here
 * refers to __mingw_strtod and the like: lld loads every object of a __wrap_ function, and the member of libmingwex for
 * such a reference with it. long double is left to gdtoa of mingw-w64, which returns -0 when it converts nothing from a
 * string with a minus sign, C requires 0, and takes any characters in the parentheses of "nan(...)", C allows letters,
 * digits and underscores only: the end is after "nan" then.
 */

static inline BOOL _NtCRT_IsFloatSpace(char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

static inline BOOL _NtCRT_IsFloatSpace(wchar_t c)
{
    return iswspace(c);
}

static size_t _NtCRT_Parse(const char* s, double* Value, BOOL* Range)
{
    return _NtCRT_Atof64(s, Value, Range);
}

static size_t _NtCRT_Parse(const wchar_t* s, double* Value, BOOL* Range)
{
    return _NtCRT_Atof64(s, Value, Range);
}

static size_t _NtCRT_Parse(const char* s, float* Value, BOOL* Range)
{
    return _NtCRT_Atof32(s, Value, Range);
}

static size_t _NtCRT_Parse(const wchar_t* s, float* Value, BOOL* Range)
{
    return _NtCRT_Atof32(s, Value, Range);
}

/* strtod and strtof: ERANGE on overflow and on underflow to zero or a denormal */
template<typename TChar, typename TFloat>
static
TFloat
_NtCRT_StrToFloat(
    _In_z_ const TChar* Str,
    _Out_opt_ TChar** End)
{
    const TChar* p = Str;
    TFloat Value;
    BOOL Range;
    size_t n;

    while (_NtCRT_IsFloatSpace(*p))
    {
        p++;
    }
    n = _NtCRT_Parse(p, &Value, &Range);
    if (n == 0)
    {
        p = Str;
    }
    if (Range)
    {
        NTCRT_ERRNO = ERANGE;
    }
    if (End != NULL)
    {
        *End = (TChar*)p + n;
    }
    return Value;
}

EXTERN_C float __cdecl __wrap___mingw_strtof(const char* str, char** end)
{
    return _NtCRT_StrToFloat<char, float>(str, end);
}

EXTERN_C double __cdecl __wrap___mingw_strtod(const char* str, char** end)
{
    return _NtCRT_StrToFloat<char, double>(str, end);
}

EXTERN_C float __cdecl __wrap___mingw_wcstof(const wchar_t* str, wchar_t** end)
{
    return _NtCRT_StrToFloat<wchar_t, float>(str, end);
}

EXTERN_C double __cdecl __wrap___mingw_wcstod(const wchar_t* str, wchar_t** end)
{
    return _NtCRT_StrToFloat<wchar_t, double>(str, end);
}

EXTERN_C long double __cdecl __real___mingw_strtold(const char* str, char** end);
EXTERN_C long double __cdecl __real___mingw_wcstold(const wchar_t* str, wchar_t** end);

template<typename TChar, typename TFloat>
static
TFloat
_NtCRT_FloatResult(
    _In_z_ const TChar* Str,
    _Out_opt_ TChar** End,
    _In_ TChar* Stop,
    _In_ TFloat Value)
{
    if (Stop == Str)
    {
        Value = 0;
    } else if (Value != Value && Stop[-1] == ')')
    {
        /* The first '(' is the one after "nan" */
        TChar* Open;
        TChar* p;

        for (p = (TChar*)Str; *p != '('; p++)
        {
        }
        Open = p;
        for (p = Open + 1; p < Stop - 1; p++)
        {
            if (!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_'))
            {
                Stop = Open;
                break;
            }
        }
    }
    if (End != NULL)
    {
        *End = Stop;
    }
    return Value;
}

#define NTCRT_WRAP_STRTOF(Name, TChar, TFloat)                                                                 \
    EXTERN_C TFloat __cdecl __wrap_##Name(const TChar* str, TChar** end)                                         \
    {                                                                                                          \
        TChar* Stop;                                                                                           \
        TFloat Value = __real_##Name(str, &Stop);                                                              \
                                                                                                               \
        return _NtCRT_FloatResult(str, end, Stop, Value);                                                      \
    }

NTCRT_WRAP_STRTOF(__mingw_strtold, char, long double)
NTCRT_WRAP_STRTOF(__mingw_wcstold, wchar_t, long double)

EXTERN_C double __cdecl _NtCRT_strtod(const char* str, char** end)
{
    return _NtCRT_StrToFloat<char, double>(str, end);
}

EXTERN_C double __cdecl _NtCRT_wcstod(const wchar_t* str, wchar_t** end)
{
    return _NtCRT_StrToFloat<wchar_t, double>(str, end);
}

EXTERN_C double __cdecl _NtCRT_atof(const char* str)
{
    return _NtCRT_strtod(str, NULL);
}

EXTERN_C double __cdecl _NtCRT__wtof(const wchar_t* str)
{
    return _NtCRT_wcstod(str, NULL);
}

/* Integer arithmetic */

NTCRT_IMPORT(strtod, _NtCRT_strtod);
NTCRT_IMPORT(wcstod, _NtCRT_wcstod);
NTCRT_IMPORT(atof, _NtCRT_atof);
NTCRT_IMPORT(_wtof, _NtCRT__wtof);

#pragma endregion
