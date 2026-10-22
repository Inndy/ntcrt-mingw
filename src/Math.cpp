/* msvcrt math functions not provided by ntdll or mingw-w64, the floating point environment is in FloatEnv.cpp */

#include "NtCRT.h"

#include <float.h>
#include <math.h>

/* Elementary functions, from mingw-w64 exp/expm1/hypot */

EXTERN_C double __cdecl _NtCRT_log10(double x)
{
    double Result;

    if (x < 0)
    {
        NTCRT_ERRNO = EDOM;
        return NAN;
    }
    if (x == 0)
    {
        NTCRT_ERRNO = ERANGE;
        return -HUGE_VAL;
    }
    __asm__("fldlg2\n\t"
            "fxch\n\t"
            "fyl2x"
            : "=t"(Result)
            : "0"(x));
    return Result;
}

EXTERN_C double __cdecl _NtCRT_sinh(double x)
{
    double a = fabs(x), t;

    if (a > 710.5)
    {
        NTCRT_ERRNO = ERANGE;
        return copysign(HUGE_VAL, x);
    }
    if (a > 22)
    {
        t = exp(a * 0.5);
        return copysign(0.5 * t * t, x);
    }
    t = expm1(a);
    return copysign(0.5 * (t + t / (t + 1)), x);
}

EXTERN_C double __cdecl _NtCRT_cosh(double x)
{
    double a = fabs(x), t;

    if (isnan(x))
    {
        return x;
    }
    if (a > 710.5)
    {
        NTCRT_ERRNO = ERANGE;
        return HUGE_VAL;
    }
    if (a > 22)
    {
        t = exp(a * 0.5);
        return 0.5 * t * t;
    }
    t = exp(a);
    return 0.5 * (t + 1 / t);
}

EXTERN_C double __cdecl _NtCRT_tanh(double x)
{
    double a = fabs(x), t;

    if (isnan(x))
    {
        return x;
    }
    if (a > 22)
    {
        return copysign(1.0, x);
    }
    t = expm1(2 * a);
    return copysign(t / (t + 2), x);
}

EXTERN_C double __cdecl _NtCRT_asin(double x)
{
    if (fabs(x) > 1)
    {
        NTCRT_ERRNO = EDOM;
        return NAN;
    }
    return atan2(x, sqrt((1 - x) * (1 + x)));
}

EXTERN_C double __cdecl _NtCRT_acos(double x)
{
    if (fabs(x) > 1)
    {
        NTCRT_ERRNO = EDOM;
        return NAN;
    }
    return atan2(sqrt((1 - x) * (1 + x)), x);
}

/* mingw-w64 hypot/hypotf call _hypot, x87 extended range avoids overflow here */
EXTERN_C double __cdecl _NtCRT__hypot(double x, double y)
{
    long double Result;

    if (isinf(x) || isinf(y))
    {
        return HUGE_VAL;
    }
    /* fsqrt, not sqrtl of libmingwex: the long double of tcc is a double (NTCRT_TCC) */
    Result = (long double)x * x + (long double)y * y;
    __asm__("fsqrt" : "+t"(Result));
    if (Result > DBL_MAX)
    {
        NTCRT_ERRNO = ERANGE;
        return HUGE_VAL;
    }
    return (double)Result;
}

EXTERN_C float __cdecl _NtCRT__hypotf(float x, float y)
{
    double Result;

    if (isinf(x) || isinf(y))
    {
        return HUGE_VALF;
    }
    Result = sqrt((double)x * x + (double)y * y);
    if (Result > FLT_MAX)
    {
        NTCRT_ERRNO = ERANGE;
        return HUGE_VALF;
    }
    return (float)Result;
}

/* IEEE helpers */

EXTERN_C double __cdecl _NtCRT__copysign(double x, double y)
{
    return copysign(x, y);
}

EXTERN_C float __cdecl _NtCRT__copysignf(float x, float y)
{
    return copysignf(x, y);
}

EXTERN_C double __cdecl _NtCRT__chgsign(double x)
{
    return -x;
}

EXTERN_C float __cdecl _NtCRT__chgsignf(float x)
{
    return -x;
}

EXTERN_C int __cdecl _NtCRT__finite(double x)
{
    return isfinite(x);
}

EXTERN_C int __cdecl _NtCRT__finitef(float x)
{
    return isfinite(x);
}

EXTERN_C int __cdecl _NtCRT__isnan(double x)
{
    return isnan(x);
}

EXTERN_C int __cdecl _NtCRT__isnanf(float x)
{
    return isnan(x);
}

/* mingw-w64 has nextafterf only */
EXTERN_C double __cdecl _NtCRT__nextafter(double x, double y)
{
    unsigned long long Bits;

    if (isnan(x) || isnan(y))
    {
        return x + y;
    }
    if (x == y)
    {
        return y;
    }
    if (x == 0)
    {
        return copysign(DBL_TRUE_MIN, y);
    }
    memcpy(&Bits, &x, sizeof(Bits));
    if ((x < y) == (x > 0))
    {
        Bits++;
    } else
    {
        Bits--;
    }
    memcpy(&x, &Bits, sizeof(x));
    if (isinf(x))
    {
        NTCRT_ERRNO = ERANGE;
    }
    return x;
}

EXTERN_C float __cdecl _NtCRT__nextafterf(float x, float y)
{
    return nextafterf(x, y);
}

EXTERN_C double __cdecl _NtCRT__logb(double x)
{
    return logb(x);
}

EXTERN_C float __cdecl _NtCRT__logbf(float x)
{
    return logbf(x);
}

EXTERN_C double __cdecl _NtCRT__scalb(double x, long exp)
{
    return scalbln(x, exp);
}

EXTERN_C float __cdecl _NtCRT__scalbf(float x, long exp)
{
    return scalblnf(x, exp);
}

template<typename T>
static
int
_NtCRT_FpClass(
    _In_ T x)
{
    bool Negative = signbit(x);

    switch (fpclassify(x))
    {
        case FP_NAN:
        {
            /* The quiet bit is the most significant bit of the mantissa */
            if constexpr (sizeof(T) == sizeof(double))
            {
                unsigned long long Bits;
                memcpy(&Bits, &x, sizeof(Bits));
                return (Bits & 0x0008000000000000ULL) ? _FPCLASS_QNAN : _FPCLASS_SNAN;
            } else
            {
                unsigned int Bits;
                memcpy(&Bits, &x, sizeof(Bits));
                return (Bits & 0x00400000U) ? _FPCLASS_QNAN : _FPCLASS_SNAN;
            }
        }
        case FP_INFINITE:
            return Negative ? _FPCLASS_NINF : _FPCLASS_PINF;
        case FP_ZERO:
            return Negative ? _FPCLASS_NZ : _FPCLASS_PZ;
        case FP_SUBNORMAL:
            return Negative ? _FPCLASS_ND : _FPCLASS_PD;
        default:
            return Negative ? _FPCLASS_NN : _FPCLASS_PN;
    }
}

EXTERN_C int __cdecl _NtCRT__fpclass(double x)
{
    return _NtCRT_FpClass(x);
}

EXTERN_C int __cdecl _NtCRT__fpclassf(float x)
{
    return _NtCRT_FpClass(x);
}

NTCRT_IMPORT(log10, _NtCRT_log10);
NTCRT_IMPORT(sinh, _NtCRT_sinh);
NTCRT_IMPORT(cosh, _NtCRT_cosh);
NTCRT_IMPORT(tanh, _NtCRT_tanh);
NTCRT_IMPORT(asin, _NtCRT_asin);
NTCRT_IMPORT(acos, _NtCRT_acos);
NTCRT_IMPORT(_hypot, _NtCRT__hypot);
NTCRT_IMPORT(_hypotf, _NtCRT__hypotf);
NTCRT_IMPORT(_copysign, _NtCRT__copysign);
NTCRT_IMPORT(_copysignf, _NtCRT__copysignf);
NTCRT_IMPORT(_chgsign, _NtCRT__chgsign);
NTCRT_IMPORT(chgsign, _NtCRT__chgsign);
NTCRT_IMPORT(_chgsignf, _NtCRT__chgsignf);
NTCRT_IMPORT(_finite, _NtCRT__finite);
NTCRT_IMPORT(finite, _NtCRT__finite);
NTCRT_IMPORT(_finitef, _NtCRT__finitef);
NTCRT_IMPORT(_isnan, _NtCRT__isnan);
NTCRT_IMPORT(_isnanf, _NtCRT__isnanf);
NTCRT_IMPORT(_nextafter, _NtCRT__nextafter);
NTCRT_IMPORT(nextafter, _NtCRT__nextafter);
NTCRT_IMPORT(_nextafterf, _NtCRT__nextafterf);
NTCRT_IMPORT(_logb, _NtCRT__logb);
NTCRT_IMPORT(_logbf, _NtCRT__logbf);
NTCRT_IMPORT(_scalb, _NtCRT__scalb);
NTCRT_IMPORT(_scalbf, _NtCRT__scalbf);
NTCRT_IMPORT(_fpclass, _NtCRT__fpclass);
NTCRT_IMPORT(fpclass, _NtCRT__fpclass);
NTCRT_IMPORT(_fpclassf, _NtCRT__fpclassf);
