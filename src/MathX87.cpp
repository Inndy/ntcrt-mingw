/*
 * The math functions of mingw-w64 that compute in the x87, at the 64-bit precision of long double that NtCRT keeps
 * (control word 0x37F), and return a result that is not that of C:
 * - x86: they return st(0) as it is, where C11 F.6 wants a value of the return type (float or double).
 * - x86 and x64: sqrt and fdim, one operation of the x87 (fsqrt, fsub) that C wants correctly rounded (F.3), round to
 *   64 bits and then to 53: the result may round twice. They run at the precision control of double (53 bits), which
 *   changes neither their exceptions nor their errno. Those of float need not: from 64 bits, more than 2 * 24 + 2,
 *   the second rounding gives the result of one. fdim of x64 is of SSE.
 * The Makefile renames them to _NtCRT_X87_<name> (src/MathX87.awk reads the lines here): those of libmsvcrt in
 * libmsvcrt_static.a, those of libmingwex (_WRAP), which ld reads before libntcrt (--wrap), in copies of their
 * members, and in the members that the library for tcc takes. The functions here call them and round. Those not
 * listed return a value of their type: they round it themselves or their result is exact (fabs, floor, fmod, ...).
 * The functions of mingw-w64 that leave out an errno of C are taken the same way (src/MathErrno*.cpp).
 */

#include "NtCRT.h"
#include "MathX87.inl"

/*
 * fdim: an x - y beyond double is finite in the x87 (x86), which mingw-w64 takes as no overflow, and the infinity of
 * an infinite x or y, which is exact, it takes as one (x86 and x64). The range error is that of an overflow: ERANGE
 * (math_errhandling has MATH_ERRNO), errno as it was otherwise. The conversion to double raises the overflow.
 */
static inline double _NtCRT_X87_FdimRange(double Result, double x, double y, int Errno)
{
    if (__builtin_isinf(Result))
    {
        NTCRT_ERRNO = __builtin_isfinite(x) && __builtin_isfinite(y) ? ERANGE : Errno;
    }
    return Result;
}

/*
 * sqrt of a number below 0: mingw-w64 returns a NaN and sets EDOM, but raises no invalid operation, which F.10.4.5
 * wants. One fsqrt (x86) or sqrtsd (x64) of the number raises it, where fetestexcept looks. The comparison is quiet: a
 * NaN raises nothing. A NaN, which mingw-w64 takes as a domain error too, is none (7.12.7.5): errno as it was.
 */
static inline double _NtCRT_X87_SqrtDomain(double Result, double x, int Errno)
{
    if (__builtin_isless(x, 0.0))
    {
#ifdef __i386__
        __asm__ __volatile__("fsqrt" : "+t"(x));
#else
        __asm__ __volatile__("sqrtsd %0, %0" : "+x"(x));
#endif
    }
    else if (__builtin_isnan(x))
    {
        NTCRT_ERRNO = Errno;
    }
    return Result;
}

#ifdef __i386__
NTCRT_X87_ROUND(acosh, double, (double x), (x))
NTCRT_X87_ROUND(atan2, double, (double y, double x), (y, x))
NTCRT_X87_ROUND(cbrt, double, (double x), (x))
NTCRT_X87_ROUND(erf, double, (double x), (x))
NTCRT_X87_ROUND(erfc, double, (double x), (x))
NTCRT_X87_ROUND(expm1, double, (double x), (x))
NTCRT_X87_ROUND53(fdim, (double x, double y), (x, y))
NTCRT_X87_ROUND(fma, double, (double x, double y, double z), (x, y, z))
NTCRT_X87_ROUND(pow, double, (double x, double y), (x, y))
NTCRT_X87_ROUND53(sqrt, (double x), (x))
NTCRT_X87_ROUND(acosf, float, (float x), (x))
NTCRT_X87_ROUND(acoshf, float, (float x), (x))
NTCRT_X87_ROUND(asinf, float, (float x), (x))
NTCRT_X87_ROUND(atan2f, float, (float y, float x), (y, x))
NTCRT_X87_ROUND(atanf, float, (float x), (x))
NTCRT_X87_ROUND(erfcf, float, (float x), (x))
NTCRT_X87_ROUND(erff, float, (float x), (x))
NTCRT_X87_ROUND(expm1f, float, (float x), (x))
NTCRT_X87_ROUND(fdimf, float, (float x, float y), (x, y))
NTCRT_X87_ROUND(fmaf, float, (float x, float y, float z), (x, y, z))
NTCRT_X87_ROUND(sqrtf, float, (float x), (x))

EXTERN_C double __cdecl fdim(double x, double y)
{
    int Errno = NTCRT_ERRNO;

    return _NtCRT_X87_FdimRange(_NtCRT_X87_fdim53(x, y), x, y, Errno);
}

EXTERN_C double __cdecl sqrt(double x)
{
    int Errno = NTCRT_ERRNO;

    return _NtCRT_X87_SqrtDomain(_NtCRT_X87_sqrt53(x), x, Errno);
}
#else
NTCRT_X87_DECLARE_WRAP(fdim, double, (double x, double y))
NTCRT_X87_ROUND53_WRAP(sqrt, (double x), (x))

EXTERN_C double __cdecl __wrap_fdim(double x, double y)
{
    int Errno = NTCRT_ERRNO;

    return _NtCRT_X87_FdimRange(_NtCRT_X87_fdim(x, y), x, y, Errno);
}
NTCRT_X87_WRAP_ALIAS(fdim)

EXTERN_C double __cdecl __wrap_sqrt(double x)
{
    int Errno = NTCRT_ERRNO;

    return _NtCRT_X87_SqrtDomain(_NtCRT_X87_sqrt53(x), x, Errno);
}
NTCRT_X87_WRAP_ALIAS(sqrt)
#endif

