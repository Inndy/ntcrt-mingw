/*
 * Conversion of decimal and hexadecimal floating point numbers to double, float and long double, for strtod and the
 * like.
 *
 * Ported from internal/strconv of Go 1.27 (uscale.go, atof.go), see Float.inl. A number of up to 19 significant
 * digits is d * 10**p, scaled by a 128-bit power of 10 to the 53 or 24 bits of the result and two bits more for
 * rounding. More digits are truncated to 19 when d + 1 rounds the same, the multiprecision decimal of up to 800
 * digits is the fallback. The syntax is the one of strtod in C instead of Go.
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
#include "Float.inl"

#include <string.h>

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

#pragma region Multiprecision decimal to float (atof.go)

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
        if (b->nd < b->Cap)
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
            char Digits[NTCRT_DECIMAL_DIGITS];
            NTCRT_DECIMAL Dec = {};

            Dec.d = Digits;
            Dec.Cap = sizeof(Digits);

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
            char Digits[NTCRT_DECIMAL_DIGITS];
            NTCRT_DECIMAL Dec = {};

            Dec.d = Digits;
            Dec.Cap = sizeof(Digits);

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

#pragma region long double

/*
 * The 80-bit long double of x87: a 64-bit significand with the integer bit, a 15-bit exponent of bias 16383, the
 * denormals of exponent 0 are Mant * 2**-16445. The digits of a decimal are collected by the multiprecision decimal
 * (NTCRT_FLOAT_DIGITS_LONG hold every halfway point) and converted as a big integer, hex digits are read up to 120
 * bits. Both are rounded by _NtCRT_HexLongDoubleBits.
 */
#define NTCRT_LDBL_BIAS 16383
#define NTCRT_LDBL_MAX_EXP 0x7FFF
#define NTCRT_LDBL_MIN_SCALE (-16445)

/* The bits of the nearest long double to m * 2**e, Sticky for nonzero bits below m, TRUE if it overflows */
static BOOL _NtCRT_HexLongDoubleBits(unsigned __int128 m, int e, BOOL Sticky, ULONG64* Mant, int* BiasedExp)
{
    int Len = 128 - (int)(m >> 64 != 0 ? __builtin_clzll((ULONG64)(m >> 64)) : 64 + __builtin_clzll((ULONG64)m));
    /* The exponent of the lowest bit of the significand, denormal at most */
    int q = e + Len - 64 > NTCRT_LDBL_MIN_SCALE ? e + Len - 64 : NTCRT_LDBL_MIN_SCALE, k = q - e;
    unsigned __int128 r;

    if (k <= 0)
    {
        r = m << -k;
    } else if (k >= 128)
    {
        /* Below half of the lowest bit: Len < 128 <= k */
        r = 0;
    } else
    {
        unsigned __int128 Half = (unsigned __int128)1 << (k - 1), Low = m & ((Half << 1) - 1);

        r = m >> k;
        if (Low > Half || (Low == Half && (Sticky || (r & 1) != 0)))
        {
            r++;
        }
    }
    if (r >> 64 != 0)
    {
        r >>= 1;
        q++;
    }
    *Mant = (ULONG64)r;
    *BiasedExp = (*Mant >> 63) != 0 ? q + 63 + NTCRT_LDBL_BIAS : 0;
    if (*BiasedExp >= NTCRT_LDBL_MAX_EXP)
    {
        *Mant = 1ull << 63;
        *BiasedExp = NTCRT_LDBL_MAX_EXP;
        return TRUE;
    }
    return FALSE;
}

/*
 * A big integer of 64-bit words, low first: the 12000 digits of a decimal (39864 bits) or 5**16952 (39363 bits) and
 * the bits of a quotient
 */
#define NTCRT_BIG_WORDS 640

typedef struct _NTCRT_BIG
{
    int n; /* Words used, the highest is not 0 */
    ULONG64 w[NTCRT_BIG_WORDS];
} NTCRT_BIG;

/* a = a * m + Add */
static void _NtCRT_BigMulAdd(NTCRT_BIG* a, ULONG64 m, ULONG64 Add)
{
    unsigned __int128 Carry = Add;

    for (int i = 0; i < a->n; i++)
    {
        Carry += (unsigned __int128)a->w[i] * m;
        a->w[i] = (ULONG64)Carry;
        Carry >>= 64;
    }
    if (Carry != 0)
    {
        a->w[a->n++] = (ULONG64)Carry;
    }
}

/* a = a * 5**p */
static void _NtCRT_BigMulPow5(NTCRT_BIG* a, int p)
{
    static const ULONG64 Pow5_27 = 7450580596923828125ull;
    ULONG64 m = 1;

    for (; p >= 27; p -= 27)
    {
        _NtCRT_BigMulAdd(a, Pow5_27, 0);
    }
    while (p-- > 0)
    {
        m *= 5;
    }
    _NtCRT_BigMulAdd(a, m, 0);
}

static int _NtCRT_BigBitLength(const NTCRT_BIG* a)
{
    return a->n == 0 ? 0 : a->n * 64 - __builtin_clzll(a->w[a->n - 1]);
}

/* The bit i of a */
static inline unsigned _NtCRT_BigBit(const NTCRT_BIG* a, int i)
{
    return i >= 0 && (i >> 6) < a->n ? (unsigned)(a->w[i >> 6] >> (i & 63)) & 1 : 0;
}

/* a >>= k, TRUE if a bit shifted out is 1 */
static BOOL _NtCRT_BigShiftRight(NTCRT_BIG* a, int k)
{
    int Words = k >> 6, s = k & 63, n = a->n - Words;
    BOOL Sticky = FALSE;

    if (n <= 0)
    {
        Sticky = a->n != 0;
        a->n = 0;
        return Sticky;
    }
    for (int i = 0; i < Words; i++)
    {
        Sticky |= a->w[i] != 0;
    }
    if (s != 0)
    {
        Sticky |= (a->w[Words] << (64 - s)) != 0;
    }
    for (int i = 0; i < n; i++)
    {
        ULONG64 Hi = i + Words + 1 < a->n ? a->w[i + Words + 1] : 0;

        a->w[i] = s == 0 ? a->w[i + Words] : a->w[i + Words] >> s | Hi << (64 - s);
    }
    a->n = n;
    while (a->n > 0 && a->w[a->n - 1] == 0)
    {
        a->n--;
    }
    return Sticky;
}

/* a <<= k, the result fits */
static void _NtCRT_BigShiftLeft(NTCRT_BIG* a, int k)
{
    int Words = k >> 6, s = k & 63;

    if (a->n == 0)
    {
        return;
    }
    a->w[a->n + Words] = 0;
    for (int i = a->n - 1; i >= 0; i--)
    {
        a->w[i + Words + 1] |= s == 0 ? 0 : a->w[i] >> (64 - s);
        a->w[i + Words] = a->w[i] << s;
    }
    for (int i = 0; i < Words; i++)
    {
        a->w[i] = 0;
    }
    a->n += Words + 1;
    while (a->n > 0 && a->w[a->n - 1] == 0)
    {
        a->n--;
    }
}

/* r = 2r + Bit, then subtract v if r >= v: returns the quotient bit */
static unsigned _NtCRT_BigDivStep(NTCRT_BIG* r, const NTCRT_BIG* v, unsigned Bit)
{
    ULONG64 Carry = Bit, Borrow = 0;
    int Cmp = 0;

    for (int i = 0; i < r->n; i++)
    {
        ULONG64 x = r->w[i];

        r->w[i] = x << 1 | Carry;
        Carry = x >> 63;
    }
    if (Carry != 0)
    {
        r->w[r->n++] = Carry;
    }
    if (r->n != v->n)
    {
        Cmp = r->n > v->n ? 1 : -1;
    }
    for (int i = r->n - 1; Cmp == 0 && i >= 0; i--)
    {
        Cmp = r->w[i] > v->w[i] ? 1 : r->w[i] < v->w[i] ? -1 : 0;
    }
    if (Cmp < 0)
    {
        return 0;
    }
    for (int i = 0; i < r->n; i++)
    {
        ULONG64 y = i < v->n ? v->w[i] : 0, d = r->w[i] - y - Borrow;

        Borrow = r->w[i] < y || (r->w[i] == y && Borrow != 0);
        r->w[i] = d;
    }
    while (r->n > 0 && r->w[r->n - 1] == 0)
    {
        r->n--;
    }
    return 1;
}

/*
 * The bits of the nearest long double to the digits of d, without the sign, TRUE if it overflows to infinity: the
 * digits are an integer D * 10**p, exact as a big integer. For p >= 0 the bits of D * 5**p * 2**p, else 67 or 68
 * bits of a quotient D * 2**s / 5**-p and its remainder: the long division takes one step per bit, from a remainder
 * below 5**-p.
 */
static BOOL _NtCRT_BigLongDoubleBits(NTCRT_DECIMAL* d, ULONG64* Mant, int* BiasedExp)
{
    NTCRT_BIG a, v;
    unsigned __int128 m = 0;
    BOOL Sticky = d->Trunc;
    int p, e, i;

    *Mant = 0;
    *BiasedExp = 0;
    _NtCRT_DecimalTrim(d);
    /* 0.d * 10**dp below 10**-4952, half of the smallest denormal is 1.8e-4951 */
    if (d->nd == 0 || d->dp < -4951)
    {
        return FALSE;
    }
    if (d->dp > 4933)
    {
        *Mant = 1ull << 63;
        *BiasedExp = NTCRT_LDBL_MAX_EXP;
        return TRUE;
    }
    a.n = 0;
    for (i = 0; i < d->nd;)
    {
        ULONG64 Chunk = 0, Scale = 1;

        for (int k = 0; k < 19 && i < d->nd; k++, i++)
        {
            Chunk = Chunk * 10 + (ULONG64)(d->d[i] - '0');
            Scale *= 10;
        }
        _NtCRT_BigMulAdd(&a, Scale, Chunk);
    }
    p = d->dp - d->nd;
    if (p >= 0)
    {
        int Len;

        _NtCRT_BigMulPow5(&a, p);
        Len = _NtCRT_BigBitLength(&a);
        e = p;
        if (Len > 128)
        {
            Sticky |= _NtCRT_BigShiftRight(&a, Len - 128);
            e += Len - 128;
        }
        for (i = a.n - 1; i >= 0; i--)
        {
            m = m << 64 | a.w[i];
        }
    } else
    {
        int s;

        v.n = 1;
        v.w[0] = 1;
        _NtCRT_BigMulPow5(&v, -p);
        s = _NtCRT_BigBitLength(&v) + 67 - _NtCRT_BigBitLength(&a);
        if (s >= 0)
        {
            _NtCRT_BigShiftLeft(&a, s);
        } else
        {
            Sticky |= _NtCRT_BigShiftRight(&a, -s);
        }
        /* The low 68 bits are taken one by one into a remainder below v */
        for (i = 0; i < 68; i++)
        {
            m |= (unsigned __int128)_NtCRT_BigBit(&a, i) << i;
        }
        _NtCRT_BigShiftRight(&a, 68);
        {
            unsigned __int128 Low = m;

            m = 0;
            for (i = 67; i >= 0; i--)
            {
                m = m << 1 | _NtCRT_BigDivStep(&a, &v, (unsigned)(Low >> i) & 1);
            }
        }
        Sticky |= a.n != 0;
        e = p - s;
    }
    return _NtCRT_HexLongDoubleBits(m, e, Sticky, Mant, BiasedExp);
}

/* The digits of the hex number t at s: up to 30 significant digits in *m, *Sticky if more are not 0, returns Exp */
template<typename TChar>
static int _NtCRT_HexMantissa(const TChar* s, const NTCRT_FLOAT_TEXT* t, unsigned __int128* m, BOOL* Sticky)
{
    int nd = 0, ndMant = 0;

    *m = 0;
    *Sticky = FALSE;
    for (size_t i = t->Digits; i < t->DigitEnd; i++)
    {
        int Digit = _NtCRT_HexDigit(s[i]);

        if (Digit < 0 || (Digit == 0 && nd == 0))
        {
            continue;
        }
        nd++;
        if (ndMant < 30)
        {
            *m = *m << 4 | (unsigned)Digit;
            ndMant++;
        } else if (Digit != 0)
        {
            *Sticky = TRUE;
        }
    }
    /* t->Exp is of the first 16 digits */
    return t->Exp + 4 * ((nd < 16 ? nd : 16) - ndMant);
}

template<typename TChar>
static size_t _NtCRT_AtofLong(const TChar* s, long double* Value, BOOL* Range)
{
    NTCRT_FLOAT_TEXT t;
    size_t n = _NtCRT_ScanFloat(s, &t);
    ULONG64 Mant = 0;
    int BiasedExp = 0;
    BOOL Overflow = FALSE;
    struct
    {
        ULONG64 Mant;
        USHORT SignExp;
    } Bits;

    *Range = FALSE;
    if (n == 0)
    {
        *Value = 0;
        return 0;
    }
    if (t.Special != 0)
    {
        Mant = t.Special == 1 ? 1ull << 63 : 3ull << 62;
        BiasedExp = NTCRT_LDBL_MAX_EXP;
    } else if (t.Mantissa == 0)
    {
        /* Zero */
    } else if (t.Hex)
    {
        unsigned __int128 m;
        BOOL Sticky;
        int e = _NtCRT_HexMantissa(s, &t, &m, &Sticky);

        Overflow = _NtCRT_HexLongDoubleBits(m, e, Sticky, &Mant, &BiasedExp);
    } else if (t.Exp > 4933)
    {
        Mant = 1ull << 63;
        BiasedExp = NTCRT_LDBL_MAX_EXP;
        Overflow = TRUE;
    } else if (t.Exp >= -4972)
    {
        char Digits[NTCRT_FLOAT_DIGITS_LONG];
        NTCRT_DECIMAL Dec = {};

        Dec.d = Digits;
        Dec.Cap = sizeof(Digits);
        _NtCRT_DecimalSet(&Dec, s, &t);
        Overflow = _NtCRT_BigLongDoubleBits(&Dec, &Mant, &BiasedExp);
    }
    Bits.Mant = Mant;
    Bits.SignExp = (USHORT)((t.Neg ? 0x8000 : 0) | BiasedExp);
    *Value = 0;
    memcpy(Value, &Bits, 10);
    *Range = t.Special == 0 && (Overflow || (t.Mantissa != 0 && BiasedExp == 0));
    return n;
}

#pragma endregion

#pragma region strtod

/*
 * The headers of mingw-w64 call __mingw_strtod, __mingw_strtof, __mingw_wcstod, __mingw_wcstof and strtold, wcstold
 * of libmingwex, the specs wrap them and __mingw_strtold, __mingw_wcstold (--wrap). The scanf of
 * mingw-w64, _atoldbl (LocaleConv.cpp) and libstdc++ call these too: the gdtoa of libmingwex is not linked.
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

static size_t _NtCRT_Parse(const char* s, long double* Value, BOOL* Range)
{
    return _NtCRT_AtofLong(s, Value, Range);
}

static size_t _NtCRT_Parse(const wchar_t* s, long double* Value, BOOL* Range)
{
    return _NtCRT_AtofLong(s, Value, Range);
}

/* strtod, strtof and strtold: ERANGE on overflow and on underflow to zero or a denormal */
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

EXTERN_C long double __cdecl __wrap___mingw_strtold(const char* str, char** end)
{
    return _NtCRT_StrToFloat<char, long double>(str, end);
}

EXTERN_C long double __cdecl __wrap___mingw_wcstold(const wchar_t* str, wchar_t** end)
{
    return _NtCRT_StrToFloat<wchar_t, long double>(str, end);
}

EXTERN_C long double __cdecl __wrap_strtold(const char* str, char** end)
    __attribute__((alias("__wrap___mingw_strtold")));
EXTERN_C long double __cdecl __wrap_wcstold(const wchar_t* str, wchar_t** end)
    __attribute__((alias("__wrap___mingw_wcstold")));

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

NTCRT_IMPORT(strtod, _NtCRT_strtod);
NTCRT_IMPORT(wcstod, _NtCRT_wcstod);
NTCRT_IMPORT(atof, _NtCRT_atof);
NTCRT_IMPORT(_wtof, _NtCRT__wtof);

/* Under their names too, as the printf of FormatNarrow.cpp: lld takes them from libntcrt without --wrap */
EXTERN_C float __cdecl __mingw_strtof(const char*, char**) __attribute__((alias("__wrap___mingw_strtof")));
EXTERN_C double __cdecl __mingw_strtod(const char*, char**) __attribute__((alias("__wrap___mingw_strtod")));
EXTERN_C long double __cdecl __mingw_strtold(const char*, char**) __attribute__((alias("__wrap___mingw_strtold")));
EXTERN_C float __cdecl __mingw_wcstof(const wchar_t*, wchar_t**) __attribute__((alias("__wrap___mingw_wcstof")));
EXTERN_C double __cdecl __mingw_wcstod(const wchar_t*, wchar_t**) __attribute__((alias("__wrap___mingw_wcstod")));
EXTERN_C long double __cdecl __mingw_wcstold(const wchar_t*, wchar_t**) __attribute__((alias("__wrap___mingw_wcstold")));
EXTERN_C long double __cdecl strtold(const char*, char**) __attribute__((alias("__wrap___mingw_strtold")));
EXTERN_C long double __cdecl wcstold(const wchar_t*, wchar_t**) __attribute__((alias("__wrap___mingw_wcstold")));

#pragma endregion
