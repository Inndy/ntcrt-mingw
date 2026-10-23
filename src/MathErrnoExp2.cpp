/*
 * errno of C: the overflow (ERANGE) of exp2 and exp2f, an infinity or the largest finite value of a directed rounding
 * (members of libmsvcrt on x86, libmingwex on x64, see MathX87.inl).
 */

#include "NtCRT.h"
#include "MathX87.inl"

#include <float.h>

#ifdef __i386__
NTCRT_X87_DECLARE(exp2, (double x))
NTCRT_X87_DECLARE(exp2f, (float x))
#else
NTCRT_X87_DECLARE_WRAP(exp2, double, (double x))
NTCRT_X87_DECLARE_WRAP(exp2f, float, (float x))
#endif

NTCRT_MATH_ERRNO_MSVCRT(exp2, double, (double x), (x), _NtCRT_MathOverflowErrno(Result, x, DBL_MAX))
NTCRT_MATH_ERRNO_MSVCRT(exp2f, float, (float x), (x), _NtCRT_MathOverflowErrno(Result, x, FLT_MAX))
