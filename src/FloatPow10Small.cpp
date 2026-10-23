/*
 * NTCRT_POW10_SMALL (make POW10_SMALL=1): the 128-bit powers of 10 of the floating point conversions computed from
 * every 27th (FloatPow10Small.inl), 808 bytes in place of the 11136 of the table of FloatPow10.inl, for a conversion
 * slower by a product and a shift (README.md). In every library, linked by _NtCRT_Prescale of that build only and by
 * test/float/pow10.cpp, which checks it against the table. Ported from internal/strconv of Go 1.27, see Float.cpp for
 * its license.
 */

#include "NtCRT.h"
#include "Float.inl"

#include "FloatPow10Small.inl"

EXTERN_C NTCRT_POW10 _NtCRT_GetPow10(int p)
{
    unsigned i = (unsigned)(p - NTCRT_POW10_MIN), r = i % NTCRT_POW10_STEP;
    ULONG64 Hi = g_Pow10Base[i / NTCRT_POW10_STEP].Hi, Lo = g_Pow10Base[i / NTCRT_POW10_STEP].Lo;
    NTCRT_UINT128 m;

    if (r != 0)
    {
        ULONG64 P2, P1, P0;
        NTCRT_UINT128 Low, High;
        int n;

        /* The top 128 bits of the 192-bit product, 3 <= n <= 62 */
        Low = (NTCRT_UINT128)Lo * g_Pow5[r];
        High = (NTCRT_UINT128)Hi * g_Pow5[r] + (ULONG64)(Low >> 64);
        P2 = (ULONG64)(High >> 64);
        P1 = (ULONG64)High;
        P0 = (ULONG64)Low;
        n = __builtin_clzll(P2);
        Hi = P2 << n | P1 >> (64 - n);
        Lo = P1 << n | P0 >> (64 - n);
    }
    m = ((NTCRT_UINT128)Hi << 64 | Lo) + (unsigned)(g_Pow10Fix[i / 32] >> (i % 32 * 2) & 3) - 1u;
    Lo = (ULONG64)m;
    return { (ULONG64)(m >> 64) + (Lo != 0), 0 - Lo };
}
