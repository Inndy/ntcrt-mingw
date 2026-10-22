/*
 * x86: the math functions of mingw-w64 that return st(0) as the x87 computed it, at the 64-bit precision of long
 * double that NtCRT keeps, where C11 F.6 wants a value of the return type (float or double). The Makefile renames
 * them to _NtCRT_X87_<name> in libmsvcrt_static.a (the NTCRT_X87_ROUND lines), lgamma and lgammaf in copies of their
 * objects of libmingwex, which ld reads before libntcrt (--wrap). The functions here call them and round. Those not
 * listed return a value of their type: they round it themselves or their result is exact (fabs, floor, fmod, ...).
 */

#include "NtCRT.h"

#ifdef __i386__

/* st(0) taken as the long double it is, rounded by the conversion to Type */
#define NTCRT_X87_ROUND_AS(Function, Name, Type, Params, Args) \
    EXTERN_C long double __cdecl _NtCRT_X87_##Name Params;     \
    EXTERN_C Type __cdecl Function Params                      \
    {                                                          \
        return (Type)_NtCRT_X87_##Name Args;                   \
    }
#define NTCRT_X87_ROUND(Name, Type, Params, Args) NTCRT_X87_ROUND_AS(Name, Name, Type, Params, Args)

/* For a name of libmingwex: __wrap_Name for ld, Name for lld, which reads libntcrt first */
#define NTCRT_X87_ROUND_WRAP(Name, Type, Params, Args)              \
    NTCRT_X87_ROUND_AS(__wrap_##Name, Name, Type, Params, Args)     \
    EXTERN_C __typeof__(__wrap_##Name) Name __attribute__((alias("__wrap_" #Name)));

NTCRT_X87_ROUND(acosh, double, (double x), (x))
NTCRT_X87_ROUND(atan2, double, (double y, double x), (y, x))
NTCRT_X87_ROUND(cbrt, double, (double x), (x))
NTCRT_X87_ROUND(erf, double, (double x), (x))
NTCRT_X87_ROUND(erfc, double, (double x), (x))
NTCRT_X87_ROUND(exp2, double, (double x), (x))
NTCRT_X87_ROUND(expm1, double, (double x), (x))
NTCRT_X87_ROUND(fdim, double, (double x, double y), (x, y))
NTCRT_X87_ROUND(fma, double, (double x, double y, double z), (x, y, z))
NTCRT_X87_ROUND(log1p, double, (double x), (x))
NTCRT_X87_ROUND(log2, double, (double x), (x))
NTCRT_X87_ROUND(pow, double, (double x, double y), (x, y))
NTCRT_X87_ROUND(sqrt, double, (double x), (x))
NTCRT_X87_ROUND(tgamma, double, (double x), (x))
NTCRT_X87_ROUND(acosf, float, (float x), (x))
NTCRT_X87_ROUND(acoshf, float, (float x), (x))
NTCRT_X87_ROUND(asinf, float, (float x), (x))
NTCRT_X87_ROUND(atan2f, float, (float y, float x), (y, x))
NTCRT_X87_ROUND(atanf, float, (float x), (x))
NTCRT_X87_ROUND(erfcf, float, (float x), (x))
NTCRT_X87_ROUND(erff, float, (float x), (x))
NTCRT_X87_ROUND(exp2f, float, (float x), (x))
NTCRT_X87_ROUND(expm1f, float, (float x), (x))
NTCRT_X87_ROUND(fdimf, float, (float x, float y), (x, y))
NTCRT_X87_ROUND(fmaf, float, (float x, float y, float z), (x, y, z))
NTCRT_X87_ROUND(log1pf, float, (float x), (x))
NTCRT_X87_ROUND(log2f, float, (float x), (x))
NTCRT_X87_ROUND(sqrtf, float, (float x), (x))
NTCRT_X87_ROUND_WRAP(lgamma, double, (double x), (x))
NTCRT_X87_ROUND_WRAP(lgammaf, float, (float x), (x))

#endif
