/*
 * errno of C: the domain error (EDOM) of log1p and log1pf below -1, the pole error (ERANGE) at -1 (members of libmsvcrt
 * on x86, libmingwex on x64, see MathX87.inl).
 */

#include "NtCRT.h"
#include "MathX87.inl"

#include <float.h>

#ifdef __i386__
NTCRT_X87_DECLARE(log1p, (double x))
NTCRT_X87_DECLARE(log1pf, (float x))
#else
NTCRT_X87_DECLARE_WRAP(log1p, double, (double x))
NTCRT_X87_DECLARE_WRAP(log1pf, float, (float x))
#endif

NTCRT_MATH_ERRNO_MSVCRT(log1p, double, (double x), (x), _NtCRT_MathLogErrno(x, -1))
NTCRT_MATH_ERRNO_MSVCRT(log1pf, float, (float x), (x), _NtCRT_MathLogErrno(x, -1))
