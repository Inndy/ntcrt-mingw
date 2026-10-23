/*
 * errno of C: the overflow (ERANGE) of nexttoward and nexttowardf, as nextafter (Math.cpp): its result DBL_MAX is exact
 * (members of libmsvcrt on x86, libmingwex on x64, see MathX87.inl).
 */

#include "NtCRT.h"
#include "MathX87.inl"

#include <float.h>

#ifdef __i386__
NTCRT_X87_DECLARE(nexttoward, (double x, long double y))
NTCRT_X87_DECLARE(nexttowardf, (float x, long double y))
#else
NTCRT_X87_DECLARE_WRAP(nexttoward, double, (double x, long double y))
NTCRT_X87_DECLARE_WRAP(nexttowardf, float, (float x, long double y))
#endif

NTCRT_MATH_ERRNO_MSVCRT(nexttoward, double, (double x, long double y), (x, y), _NtCRT_MathRangeErrno(Result, x))
NTCRT_MATH_ERRNO_MSVCRT(nexttowardf, float, (float x, long double y), (x, y), _NtCRT_MathRangeErrno(Result, x))
