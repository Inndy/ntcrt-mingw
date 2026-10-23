/*
 * The powers of 10 of NTCRT_POW10_SMALL: _NtCRT_GetPow10 (src/FloatPow10Small.cpp), which computes them from every
 * 27th, against the table of src/FloatPow10.inl they are made from, the 696 mantissas bit for bit. Every library has
 * _NtCRT_GetPow10, this program takes the table under another name.
 */

#include <stdio.h>

typedef unsigned long long ULONG64;

typedef struct _NTCRT_POW10
{
    ULONG64 Hi;
    ULONG64 Lo;
} NTCRT_POW10;

extern "C" NTCRT_POW10 _NtCRT_GetPow10(int p);

#define _NtCRT_Pow10 g_Pow10Table
#include "../../src/FloatPow10.inl"

int main(void)
{
    int Failures = 0;

    for (int p = -348; p <= 347; p++)
    {
        NTCRT_POW10 Got = _NtCRT_GetPow10(p), Want = g_Pow10Table[p + 348];

        if (Got.Hi != Want.Hi || Got.Lo != Want.Lo)
        {
            if (++Failures <= 10)
            {
                printf("1e%d: %016llx %016llx, want %016llx %016llx\n", p, Got.Hi, Got.Lo, Want.Hi, Want.Lo);
            }
        }
    }
    if (Failures != 0)
    {
        printf("pow10: %d of %d differ\n", Failures, 696);
        return 1;
    }
    printf("pow10: ok\n");
    return 0;
}
