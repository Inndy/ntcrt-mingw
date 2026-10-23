/*
 * errno of C: the overflow (ERANGE) of tgamma and tgammaf, an infinity or the largest finite value of a directed
 * rounding (members of libmsvcrt on x86, libmingwex on x64, see MathX87.inl).
 */

#include "NtCRT.h"
#include "MathX87.inl"

#include <float.h>

#ifdef __i386__
NTCRT_X87_DECLARE(tgamma, (double x))
NTCRT_X87_DECLARE(tgammaf, (float x))
#else
NTCRT_X87_DECLARE_WRAP(tgamma, double, (double x))
NTCRT_X87_DECLARE_WRAP(tgammaf, float, (float x))
#endif

NTCRT_MATH_ERRNO_MSVCRT(tgamma, double, (double x), (x), _NtCRT_MathOverflowErrno(Result, x, DBL_MAX))
NTCRT_MATH_ERRNO_MSVCRT(tgammaf, float, (float x), (x), _NtCRT_MathOverflowErrno(Result, x, FLT_MAX))
