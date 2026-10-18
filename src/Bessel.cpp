/*
 * msvcrt Bessel functions: the rational approximations of msvcrt for |x| <= 7.5 and its asymptotic expansion
 * beyond, evaluated in the same order to give the same results
 */

#include "NtCRT.h"

#include <errno.h>
#include <math.h>

static
double
_NtCRT_BesselSqrt(
    _In_ double x)
{
    __asm__("sqrtsd %1, %0" : "=x"(x) : "x"(x));
    return x;
}

/* An invalid argument of y0, y1 and yn */
static
double
_NtCRT_BesselDomainError(void)
{
    NTCRT_ERRNO = EDOM;
    return -__builtin_nan("");
}

/* The asymptotic expansion sqrt(2 / (pi x)) * (P(z) cos(t) - Q(z) w sin(t)) of order 0 or 1, w = 8 / x, z = w^2 */
static
double
_NtCRT_BesselAsymptotic(
    _In_ double x,
    _In_ int Order,
    _In_ bool SecondKind)
{
    double t = x - (Order == 0 ? 0x1.921fb54442d18p-1 : 0x1.2d97c7f3321d2p+1);
    double w = 0x1.0000000000000p+3 / x;
    double s = 0x1.0000000000000p+1 / (x * 0x1.921fb54442d18p+1);
    double z = w * w;
    double p, pd, q, qd, c, Sin;

    if (Order == 0)
    {
        p = z * 0x1.c77bae45ab8abp-1;
        p += 0x1.3386275dbdac5p+7; p *= z; p += 0x1.b314c1b1be498p+11; p *= z; p += 0x1.4aca17f127492p+14; p *= z;
        p += 0x1.4302c5f59f857p+15; p *= z; p += 0x1.63ec5c5caea74p+14;
        pd = z + 0x1.3a392372fdb57p+7;
        pd *= z; pd += 0x1.b5dbf3d343198p+11; pd *= z; pd += 0x1.4b7d66f9b1895p+14; pd *= z;
        pd += 0x1.4334d3329c8e0p+15; pd *= z; pd += 0x1.63ec5c5caea74p+14;
        q = z * (-0x1.2077afe0ad7a1p-7);
        q -= 0x1.3e7d834c33d98p+0; q *= z; q -= 0x1.64cddf2d4a1e8p+4; q *= z; q -= 0x1.bf565287df3fep+6; q *= z;
        q -= 0x1.73d6cd7b10060p+7; q *= z; q += (-0x1.64e809e20ed05p+6);
        qd = z + 0x1.6a600522ff6b2p+6;
        qd *= z; qd += 0x1.742e47a6a6368p+10; qd *= z; qd += 0x1.c60472c1defdep+12; qd *= z;
        qd += 0x1.75790d66a4ed6p+13; qd *= z; qd += 0x1.64e809e20ed05p+12;
    } else
    {
        p = z * 0x1.41d6010a865e8p+0;
        p += 0x1.a64e4b4e35797p+7; p *= z; p += 0x1.3797bb3646e7fp+12; p *= z; p += 0x1.e9e7da39bf79dp+14; p *= z;
        p += 0x1.ea4db0c43c658p+15; p *= z; p += 0x1.1331546f86645p+15;
        pd = z + 0x1.9627b08f1c66bp+7;
        pd *= z; pd += 0x1.34265806168f6p+12; pd *= z; pd += 0x1.e821a02293ef4p+14; pd *= z;
        pd += 0x1.e9ccb1a4a81e9p+15; pd *= z; pd += 0x1.1331546f86645p+15;
        q = z * 0x1.2169069c535fbp-5;
        q += 0x1.245cec89dc00cp+2; q *= z; q += 0x1.4cc274093a456p+6; q *= z; q += 0x1.a9fcbfc4e8e2cp+8; q *= z;
        q += 0x1.688503ddc68e8p+9; q *= z; q += 0x1.5f2cd95877ea5p+8;
        qd = z + 0x1.9f4668b15933ap+6;
        qd *= z; qd += 0x1.c4cbf2e6df9fap+10; qd *= z; qd += 0x1.1e01da86532f5p+13; qd *= z;
        qd += 0x1.e1b16b30d93f2p+13; qd *= z; qd += 0x1.d43bcc75f5387p+12;
    }
    p /= pd;
    q /= qd;
    s = _NtCRT_BesselSqrt(s);
    c = cos(t);
    /* Not sincos, which does not set errno for an infinity or NaN as cos and sin */
    __asm__("" : "+x"(t));
    if (SecondKind)
    {
        c *= q * w;
        Sin = sin(t) * p;
        return s * (c + Sin);
    }
    c *= p;
    Sin = sin(t) * (q * w);
    return s * (c - Sin);
}

EXTERN_C double __cdecl _NtCRT__j0(double x)
{
    double ax = 0 > x ? -x : x, z, n, d;

    if (!(ax <= 0x1.e000000000000p+2))
    {
        return _NtCRT_BesselAsymptotic(ax, 0, false);
    }
    z = ax * ax;
    n = z * (-0x1.ff58a9769fb2dp-44);
    n += 0x1.18d099dc9f8a9p-32; n *= z; n -= 0x1.25ab6da87e6c3p-22; n *= z; n += 0x1.77e8386b261b7p-13; n *= z;
    n -= 0x1.3e91bc279a664p-4; n *= z; n += 0x1.6cf6df6618527p+4; n *= z;
    n -= 0x1.177f697a2c439p+12; n *= z; n += 0x1.13a49dcdc5777p+19; n *= z; n -= 0x1.46a93a1b94ce7p+25; n *= z;
    n += 0x1.9c5300da646f3p+30; n *= z; n -= 0x1.b88e0549c7babp+34; n *= z;
    n += 0x1.c21526c46a7f8p+36;
    d = z + 0x1.c399ede5e9452p+10;
    d *= z; d += 0x1.698002140c2c4p+20; d *= z; d += 0x1.30e42f545899ep+29; d *= z; d += 0x1.c21526c46a7f8p+36;
    return n / d;
}

EXTERN_C double __cdecl _NtCRT__j1(double x)
{
    double ax = 0 > x ? -x : x, Sign = 0 > x ? -1.0 : 1.0, z, n, d;

    if (!(ax <= 0x1.e000000000000p+2))
    {
        return Sign * _NtCRT_BesselAsymptotic(ax, 1, false);
    }
    z = ax * ax;
    n = z * 0x1.21faf590f6676p-39;
    n -= 0x1.1f34f1753c3bdp-28; n *= z; n += 0x1.0be3f536df823p-18; n *= z; n -= 0x1.2dc83d2582858p-9; n *= z;
    n += 0x1.baabffc5ac862p-1; n *= z; n -= 0x1.ad7783d56647ep+7; n *= z;
    n += 0x1.0e74e692efbecp+15; n *= z; n -= 0x1.a48f06cfcf18fp+21; n *= z; n += 0x1.6fd2511e0da42p+27; n *= z;
    n -= 0x1.30137f1ea9e1cp+32; n *= z; n += 0x1.3e9e9f69657cap+35;
    d = z + 0x1.98d959d9fa099p+10;
    d *= z; d += 0x1.2b5311ca151cep+20; d *= z; d += 0x1.d1640957735afp+28; d *= z; d += 0x1.3e9e9f69657cap+36;
    return Sign * ax * (n / d);
}

EXTERN_C double __cdecl _NtCRT__y0(double x)
{
    double z, n, d;

    if (0 > x)
    {
        return _NtCRT_BesselDomainError();
    }
    if (!(x <= 0x1.e000000000000p+2))
    {
        return _NtCRT_BesselAsymptotic(x, 0, true);
    }
    z = x * x;
    n = z * (-0x1.4334b5bfd9d70p+15);
    n += 0x1.c34b83541447bp+25; n *= z; n -= 0x1.0007945cb94b0p+35; n *= z; n += 0x1.2a774218affe3p+43; n *= z;
    n -= 0x1.76d97d06e9706p+50; n *= z; n += 0x1.e8b84d805655cp+56; n *= z;
    n -= 0x1.234549622d9ebp+62; n *= z; n += 0x1.c91908363136fp+65; n *= z; n += (-0x1.7dadcfa02dc52p+64);
    d = z + 0x1.409cfa396b73bp+10;
    d *= z; d += 0x1.e91cd485704ffp+19; d *= z; d += 0x1.14553e408f827p+29; d *= z; d += 0x1.e6b8af5480dd7p+37; d *= z;
    d += 0x1.4d5020e5c83f7p+46; d *= z; d += 0x1.540c93f7dfd8fp+54;
    d *= z; d += 0x1.d173a8b5765f5p+61; d *= z; d += 0x1.43380210b1480p+68;
    return _NtCRT__j0(x) * 0x1.45f306dc9c883p-1 * log(x) + n / d;
}

EXTERN_C double __cdecl _NtCRT__y1(double x)
{
    double z, n, d;

    if (0 > x)
    {
        return _NtCRT_BesselDomainError();
    }
    if (!(x <= 0x1.e000000000000p+2))
    {
        return _NtCRT_BesselAsymptotic(x, 1, true);
    }
    z = x * x;
    n = z * 0x1.5b5b19a9b4d25p+18;
    n -= 0x1.99136fce2b1c2p+28; n *= z; n += 0x1.7dc927597487ap+37; n *= z; n -= 0x1.61da564da83e0p+45; n *= z;
    n += 0x1.503ceafeb1857p+52; n *= z; n -= 0x1.31a05fe2611e1p+58; n *= z;
    n += 0x1.ae211a07320e6p+62; n *= z; n += (-0x1.95c305d45494ap+64);
    d = z + 0x1.0c2c8d9ea2f32p+10;
    d *= z; d += 0x1.62f057620eacfp+19; d *= z; d += 0x1.6272c9fdcb8afp+28; d *= z; d += 0x1.17702529dd5e0p+37; d *= z;
    d += 0x1.59fc029bdf8b0p+45; d *= z; d += 0x1.41a19a1191997p+53;
    d *= z; d += 0x1.93d30380618bep+60; d *= z; d += 0x1.02b388fa5044dp+67;
    return (_NtCRT__j1(x) * log(x) - 0x1.0000000000000p+0 / x) * 0x1.45f306dc9c883p-1 + n / d * x;
}

/* Forward recurrence from j0 and j1 beyond the order, backward recurrence by a continued fraction below it */
EXTERN_C double __cdecl _NtCRT__jn(int n, double x)
{
    unsigned int m = n < 0 ? 0U - (unsigned int)n : (unsigned int)n;
    int TwoM, k;
    double Prev, Cur, Next, Ratio, r;

    if (n < 0)
    {
        x = -x;
    }
    if (m == 0)
    {
        return _NtCRT__j0(x);
    }
    if (m == 1)
    {
        return _NtCRT__j1(x);
    }
    if (x == 0)
    {
        return 0.0;
    }
    TwoM = (int)(m * 2);
    if (fabs(x) > (double)(int)m)
    {
        Prev = _NtCRT__j0(x);
        Cur = _NtCRT__j1(x);
        Next = (Cur + Cur) / x - Prev;
        for (k = 4; k != TwoM; k += 2)
        {
            Prev = Cur;
            Cur = Next;
            Next = Cur * (double)k / x - Prev;
        }
        return Next;
    }
    r = 0;
    for (k = TwoM + 36; k > TwoM; k -= 2)
    {
        r = x * x / ((double)k - r);
    }
    Ratio = x / ((double)TwoM - r);
    k = TwoM - 2;
    Cur = (double)k / x - Ratio;
    Prev = 1.0;
    for (k -= 2; k != 0; k -= 2)
    {
        Next = Cur * (double)k / x - Prev;
        Prev = Cur;
        Cur = Next;
    }
    return _NtCRT__j0(x) * Ratio / Cur;
}

/* Forward recurrence from y0 and y1 */
EXTERN_C double __cdecl _NtCRT__yn(int n, double x)
{
    double Sign = 1.0, Prev, Cur, Next, c;

    if (0 > x)
    {
        return _NtCRT_BesselDomainError();
    }
    if (n < 0)
    {
        n = (int)(0U - (unsigned int)n);
        if (n & 1)
        {
            Sign = -1.0;
        }
    }
    if (n == 0)
    {
        return _NtCRT__y0(x) * Sign;
    }
    if (n == 1)
    {
        return _NtCRT__y1(x) * Sign;
    }
    Prev = _NtCRT__y0(x);
    Cur = _NtCRT__y1(x);
    c = 0x1.0000000000000p+1 / x;
    for (int k = 1; k < n; k++)
    {
        Next = (double)k * c * Cur - Prev;
        Prev = Cur;
        Cur = Next;
    }
    return Sign * Cur;
}

NTCRT_IMPORT(_j0, _NtCRT__j0);
NTCRT_IMPORT(_j1, _NtCRT__j1);
NTCRT_IMPORT(_jn, _NtCRT__jn);
NTCRT_IMPORT(_y0, _NtCRT__y0);
NTCRT_IMPORT(_y1, _NtCRT__y1);
NTCRT_IMPORT(_yn, _NtCRT__yn);
NTCRT_IMPORT(j0, _NtCRT__j0);
NTCRT_IMPORT(j1, _NtCRT__j1);
NTCRT_IMPORT(jn, _NtCRT__jn);
NTCRT_IMPORT(y0, _NtCRT__y0);
NTCRT_IMPORT(y1, _NtCRT__y1);
NTCRT_IMPORT(yn, _NtCRT__yn);
