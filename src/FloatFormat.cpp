/*
 * The decimal digits of %e, %f and %g of printf (Printf.cpp), ties to even.
 *
 * Ported from internal/strconv of Go 1.27 (ftoa.go), see Float.inl: up to 18 digits of a double by unrounded
 * scaling, more and those of a long double by the multiprecision decimal.
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

#pragma region Formatting (ftoa.go)

/* floor(log10(2**x)), log10 2 ~ 78913 / 2**18 */
static inline int _NtCRT_Log10Pow2(int x)
{
    return (x * 78913) >> 18;
}

static inline NTCRT_UNROUNDED _NtCRT_UnroundedDiv(NTCRT_UNROUNDED u, ULONG64 d)
{
    return u / d | (u & 1) | (u % d != 0);
}

static const ULONG64 g_Uint64Pow10[] = { 1ull,
                                         10ull,
                                         100ull,
                                         1000ull,
                                         10000ull,
                                         100000ull,
                                         1000000ull,
                                         10000000ull,
                                         100000000ull,
                                         1000000000ull,
                                         10000000000ull,
                                         100000000000ull,
                                         1000000000000ull,
                                         10000000000000ull,
                                         100000000000000ull,
                                         1000000000000000ull,
                                         10000000000000000ull,
                                         100000000000000000ull,
                                         1000000000000000000ull,
                                         10000000000000000000ull };

/*
 * The n-digit decimal of m * 2**e, m left-justified, as d * 10**-p (fixedWidthFloat). For 'f' n is an upper
 * bound and digits after the Precision-th after the point are dropped. FALSE if 10**p is not in the table.
 */
static BOOL _NtCRT_FixedWidth(ULONG64 m, int e, int n, int Precision, char Mode, ULONG64* d, int* p)
{
    NTCRT_SCALER Pre;
    NTCRT_UNROUNDED u;

    *p = n - 1 - _NtCRT_Log10Pow2(e + 63);
    if (*p < NTCRT_POW10_MIN || *p > NTCRT_POW10_MAX)
    {
        return FALSE;
    }
    _NtCRT_Prescale(&Pre, e, *p, _NtCRT_Log2Pow10(*p));
    u = _NtCRT_Uscale(m, &Pre);
    if (u >= _NtCRT_Unmin(g_Uint64Pow10[n]))
    {
        u = _NtCRT_UnroundedDiv(u, 10);
        (*p)--;
    }
    if (Mode == 'f')
    {
        for (; *p > Precision; (*p)--)
        {
            u = _NtCRT_UnroundedDiv(u, 10);
        }
    }
    *d = _NtCRT_Round(u);
    return TRUE;
}

/*
 * The decimal digits of Mant * 2**Exp, Mant not 0, for printf, ties to even: Mode 'e' rounds to Precision + 1
 * significant digits, 'f' to Precision digits after the point. The digits d1 d2 ... dn in Digits, without the
 * trailing zeros, are 0.d1d2...dn * 10**Point; returns n, 0 if they are all 0 (%f of a small number). Size is
 * NTCRT_FLOAT_DIGITS for a double and NTCRT_FLOAT_DIGITS_LONG for a long double; the long double needs the
 * multiprecision decimal and a mantissa of up to 53 bits takes the fast path of ftoa when it has at most 18
 * digits to print.
 */
EXTERN_C int _NtCRT_FloatDigits(ULONG64 Mant, int Exp, char Mode, int Precision, char* Digits, int Size, int* Point)
{
    int Len = _NtCRT_Len64(Mant), Log2 = Exp + Len - 1, n;
    NTCRT_DECIMAL d = {};

    d.d = Digits;
    d.Cap = Size;

    /* Digits to compute: Precision + 1 for %e, an upper bound for %f */
    if (Mode == 'f')
    {
        n = Log2 >= 0 ? 1 + _NtCRT_Log10Pow2(1 + Log2) + Precision : 1 + Precision - _NtCRT_Log10Pow2(-Log2);
    } else
    {
        n = Precision + 1;
    }
    if (n <= 0)
    {
        return 0;
    }
    if (n <= 18 && Len <= 53)
    {
        ULONG64 v;
        int p, nd;

        if (_NtCRT_FixedWidth(Mant << (64 - Len), Exp - (64 - Len), n, Precision, Mode, &v, &p))
        {
            if (v == 0)
            {
                return 0;
            }
            for (nd = 1; nd < 20 && v >= g_Uint64Pow10[nd]; nd++)
            {
            }
            *Point = nd - p;
            for (int i = nd - 1; i >= 0; i--)
            {
                Digits[i] = (char)('0' + v % 10);
                v /= 10;
            }
            while (Digits[nd - 1] == '0')
            {
                nd--;
            }
            return nd;
        }
    }

    /* The slow path */
    _NtCRT_DecimalAssign(&d, Mant);
    _NtCRT_DecimalShift(&d, Exp);
    if (Mode == 'f' && d.dp + Precision < 0)
    {
        return 0;
    }
    _NtCRT_DecimalRound(&d, Mode == 'f' ? d.dp + Precision : Precision + 1);
    *Point = d.dp;
    return d.nd;
}

#pragma endregion
