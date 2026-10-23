/*
 * errno of C: the overflow (ERANGE) of exp2 and exp2f, at 1024 and 128 (members of libmsvcrt on x86, libmingwex on
 * x64, see MathX87.inl).
 */

#include "NtCRT.h"
#include "MathX87.inl"

#ifdef __i386__
NTCRT_X87_DECLARE(exp2, (double x))
NTCRT_X87_DECLARE(exp2f, (float x))
#else
NTCRT_X87_DECLARE_WRAP(exp2, double, (double x))
NTCRT_X87_DECLARE_WRAP(exp2f, float, (float x))
#endif

static inline void _NtCRT_Exp2Errno(double x, double Limit)
{
    if (_NtCRT_MathAbove(x, Limit))
    {
        NTCRT_ERRNO = ERANGE;
    }
}

NTCRT_MATH_ERRNO_MSVCRT(exp2, double, (double x), (x), _NtCRT_Exp2Errno(x, 1024))
NTCRT_MATH_ERRNO_MSVCRT(exp2f, float, (float x), (x), _NtCRT_Exp2Errno(x, 128))
