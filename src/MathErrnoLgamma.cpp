/*
 * errno of C: the pole error (ERANGE) of lgamma and lgammaf at 0 and the negative integers, which mingw-w64 takes as a
 * domain error (EDOM), and the overflow of a large x (members of libmingwex, see MathX87.inl).
 */

#include "NtCRT.h"
#include "MathX87.inl"

#ifdef __i386__
NTCRT_X87_DECLARE_X87_WRAP(lgamma, (double x))
NTCRT_X87_DECLARE_X87_WRAP(lgammaf, (float x))
#else
NTCRT_X87_DECLARE_WRAP(lgamma, double, (double x))
NTCRT_X87_DECLARE_WRAP(lgammaf, float, (float x))
#endif

/* A pole (an integer up to 0, every double from -2**52 down), or beyond the maximum from Big on (thresholds.py) */
static inline void _NtCRT_LgammaErrno(double x, double Big)
{
    if (_NtCRT_MathAbove(x, Big) || (__builtin_islessequal(x, 0) && __builtin_isfinite(x) &&
                                     (x <= -0x1p52 || x == (double)(long long)x)))
    {
        NTCRT_ERRNO = ERANGE;
    }
}

NTCRT_MATH_ERRNO_WRAP(lgamma, double, (double x), (x), _NtCRT_LgammaErrno(x, 0x1.754d9278b51a8p+1014))
NTCRT_MATH_ERRNO_WRAP(lgammaf, float, (float x), (x), _NtCRT_LgammaErrno(x, 0x1.895f1cp+121))
