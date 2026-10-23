/*
 * Floating point conversions, the parts that parsing (FloatParse.cpp) and formatting (FloatFormat.cpp) share: the
 * 128-bit powers of 10 (FloatPow10.inl) and the multiprecision decimal. GNU ld links a program with whole objects,
 * printf links the formatting and this, strtod the parsing and this.
 *
 * Ported from internal/strconv of Go 1.27 (uscale.go, atof.go, ftoa.go, decimal.go): fast unrounded scaling by Russ
 * Cox, "Floating-Point Printing and Parsing Can Be Simple And Fast", https://research.swtch.com/fp.
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

#ifndef NTCRT_POW10_SMALL
#include "FloatPow10.inl"
#endif

#pragma region Multiprecision decimal (decimal.go)

/* The largest shift without overflow of 9 << k in 64 bits */
#define NTCRT_MAX_SHIFT 60

EXTERN_C void _NtCRT_DecimalTrim(NTCRT_DECIMAL* a)
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
EXTERN_C void _NtCRT_DecimalRightShift(NTCRT_DECIMAL* a, int k)
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
        if (w < a->Cap)
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
EXTERN_C void _NtCRT_DecimalLeftShift(NTCRT_DECIMAL* a, int k)
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
        if (w < a->Cap)
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
        if (w < a->Cap)
        {
            a->d[w] = (char)(Rem + '0');
        } else if (Rem != 0)
        {
            a->Trunc = TRUE;
        }
        n = Quo;
    }
    a->nd += Delta;
    if (a->nd >= a->Cap)
    {
        a->nd = a->Cap;
    }
    a->dp += Delta;
    _NtCRT_DecimalTrim(a);
}

/* Shift left by k bits, right if k is negative */
EXTERN_C void _NtCRT_DecimalShift(NTCRT_DECIMAL* a, int k)
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

EXTERN_C void _NtCRT_DecimalAssign(NTCRT_DECIMAL* a, ULONG64 v)
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
EXTERN_C BOOL _NtCRT_DecimalShouldRoundUp(const NTCRT_DECIMAL* a, int nd)
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
EXTERN_C void _NtCRT_DecimalRound(NTCRT_DECIMAL* a, int nd)
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
EXTERN_C ULONG64 _NtCRT_DecimalRoundedInteger(const NTCRT_DECIMAL* a)
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

#pragma endregion
