/*
 * errno of C: the domain error (EDOM) of log2 and log2f below 0, the pole error (ERANGE) at 0 (members of libmsvcrt on
 * x86, libmingwex on x64, see MathX87.inl).
 */

#include "NtCRT.h"
#include "MathX87.inl"

#include <float.h>

#ifdef __i386__
NTCRT_X87_DECLARE(log2, (double x))
NTCRT_X87_DECLARE(log2f, (float x))
#else
NTCRT_X87_DECLARE_WRAP(log2, double, (double x))
NTCRT_X87_DECLARE_WRAP(log2f, float, (float x))
#endif

NTCRT_MATH_ERRNO_MSVCRT(log2, double, (double x), (x), _NtCRT_MathLogErrno(x, 0))
NTCRT_MATH_ERRNO_MSVCRT(log2f, float, (float x), (x), _NtCRT_MathLogErrno(x, 0))
