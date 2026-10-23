/*
 * errno of C: the overflow (ERANGE) of tgamma and tgammaf, of a large x and of an x near 0, and the pole error at 0
 * (members of libmsvcrt on x86, libmingwex on x64, see MathX87.inl).
 */

#include "NtCRT.h"
#include "MathX87.inl"

#ifdef __i386__
NTCRT_X87_DECLARE(tgamma, (double x))
NTCRT_X87_DECLARE(tgammaf, (float x))
#else
NTCRT_X87_DECLARE_WRAP(tgamma, double, (double x))
NTCRT_X87_DECLARE_WRAP(tgammaf, float, (float x))
#endif

/* Beyond the maximum from Big on, and from Tiny to -Tiny (about 1 / x), 0 included (test/math/thresholds.py) */
static inline void _NtCRT_TgammaErrno(double x, double Big, double Tiny)
{
    if (_NtCRT_MathAbove(x, Big) || __builtin_islessequal(__builtin_fabs(x), Tiny))
    {
        NTCRT_ERRNO = ERANGE;
    }
}

NTCRT_MATH_ERRNO_MSVCRT(tgamma, double, (double x), (x), _NtCRT_TgammaErrno(x, 0x1.573fae561f648p+7, 0x1p-1024))
NTCRT_MATH_ERRNO_MSVCRT(tgammaf, float, (float x), (x), _NtCRT_TgammaErrno(x, 0x1.18522p+5, 0x1p-128))
