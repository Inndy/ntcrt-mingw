/*
 * errno of C: the pole error (ERANGE) of lgamma and lgammaf at 0 and the negative integers, which mingw-w64 takes as a
 * domain error (EDOM), and the overflow (members of libmingwex, see MathX87.inl).
 */

#include "NtCRT.h"
#include "MathX87.inl"

#include <float.h>

#ifdef __i386__
NTCRT_X87_DECLARE_X87_WRAP(lgamma, (double x))
NTCRT_X87_DECLARE_X87_WRAP(lgammaf, (float x))
#else
NTCRT_X87_DECLARE_WRAP(lgamma, double, (double x))
NTCRT_X87_DECLARE_WRAP(lgammaf, float, (float x))
#endif

NTCRT_MATH_ERRNO_WRAP(lgamma, double, (double x), (x), _NtCRT_MathOverflowErrno(Result, x, DBL_MAX))
NTCRT_MATH_ERRNO_WRAP(lgammaf, float, (float x), (x), _NtCRT_MathOverflowErrno(Result, x, FLT_MAX))
