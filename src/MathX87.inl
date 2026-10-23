/*
 * The x87 functions of mingw-w64 renamed _NtCRT_X87_<name> by the Makefile (src/MathX87.awk reads the lines of
 * MathX87.cpp and MathErrno*.cpp that declare or define them) and the functions of C that call them, see MathX87.cpp.
 */

#pragma once

/* The result of a function of the x87: st(0) as the long double it is on x86, a double in xmm0 on x64 */
#ifdef __i386__
typedef long double NTCRT_X87_RESULT;
#else
typedef double NTCRT_X87_RESULT;
#endif

/* The control word before, the precision control (bits 8-9) set to 53 bits; the x87 state of the thread */
static inline unsigned short _NtCRT_X87_Precision53(void)
{
    unsigned short Cw, Pc53;

    __asm__ __volatile__("fnstcw %0" : "=m"(Cw));
    Pc53 = (unsigned short)((Cw & ~0x300) | 0x200);
    __asm__ __volatile__("fldcw %0" : : "m"(Pc53) : "memory");
    return Cw;
}

static inline void _NtCRT_X87_Restore(unsigned short Cw)
{
    __asm__ __volatile__("fldcw %0" : : "m"(Cw) : "memory");
}

/* Rounded by the conversion to Type */
#define NTCRT_X87_ROUND_AS(Function, Name, Type, Params, Args)   \
    EXTERN_C NTCRT_X87_RESULT __cdecl _NtCRT_X87_##Name Params;  \
    EXTERN_C Type __cdecl Function Params                        \
    {                                                            \
        return (Type)_NtCRT_X87_##Name Args;                     \
    }
#define NTCRT_X87_ROUND(Name, Type, Params, Args) NTCRT_X87_ROUND_AS(Name, Name, Type, Params, Args)

/* _NtCRT_X87_<Name>53: Name at the precision control of 53 bits, for the function of C below */
#define NTCRT_X87_ROUND53(Name, Params, Args)                    \
    EXTERN_C NTCRT_X87_RESULT __cdecl _NtCRT_X87_##Name Params;  \
    static inline double _NtCRT_X87_##Name##53 Params            \
    {                                                            \
        unsigned short Cw = _NtCRT_X87_Precision53();            \
        NTCRT_X87_RESULT Result = _NtCRT_X87_##Name Args;        \
        _NtCRT_X87_Restore(Cw);                                  \
        return (double)Result;                                   \
    }
#define NTCRT_X87_ROUND53_WRAP NTCRT_X87_ROUND53

/* For a name of libmingwex: __wrap_Name for ld, Name for lld, which reads libntcrt first */
#define NTCRT_X87_WRAP_ALIAS(Name) \
    EXTERN_C __typeof__(__wrap_##Name) Name __attribute__((alias("__wrap_" #Name)));
#define NTCRT_X87_ROUND_WRAP(Name, Type, Params, Args)          \
    NTCRT_X87_ROUND_AS(__wrap_##Name, Name, Type, Params, Args) \
    NTCRT_X87_WRAP_ALIAS(Name)

/* _NtCRT_X87_<Name>, a function of libmingwex that is not of the x87, for the function of C below */
#define NTCRT_X87_DECLARE_WRAP(Name, Type, Params) EXTERN_C Type __cdecl _NtCRT_X87_##Name Params;

/* _NtCRT_X87_<Name> of the x87 (x86), of libmsvcrt or of libmingwex (_WRAP), for the function of C below */
#define NTCRT_X87_DECLARE(Name, Params) EXTERN_C NTCRT_X87_RESULT __cdecl _NtCRT_X87_##Name Params;
#define NTCRT_X87_DECLARE_X87_WRAP NTCRT_X87_DECLARE

/*
 * errno: the errors of C (math_errhandling has MATH_ERRNO) that the functions of mingw-w64 leave out (MathErrno*.cpp,
 * one object for a function and that of float, linked only by a program that calls one). The function of C calls that
 * of mingw-w64, rounds its result to its type (x86) and sets errno. The comparisons are quiet: a NaN raises nothing.
 */

/* A domain error (EDOM) below Pole, a pole error (ERANGE) at it */
static inline void _NtCRT_MathLogErrno(double x, double Pole)
{
    if (x == Pole)
    {
        NTCRT_ERRNO = ERANGE;
    } else if (__builtin_isless(x, Pole))
    {
        NTCRT_ERRNO = EDOM;
    }
}

/* An infinity of a finite argument: an overflow or a pole, ERANGE either (nexttoward, whose DBL_MAX is exact) */
static inline void _NtCRT_MathRangeErrno(double Result, double x)
{
    if (__builtin_isinf(Result) && __builtin_isfinite(x))
    {
        NTCRT_ERRNO = ERANGE;
    }
}

/*
 * An overflow known by the argument, in every rounding mode: a finite x at or above Limit, the first value of its
 * type whose mathematical result is beyond DBL_MAX (FLT_MAX), test/math/thresholds.py
 */
static inline BOOL _NtCRT_MathAbove(double x, double Limit)
{
    return __builtin_isgreaterequal(x, Limit) && __builtin_isfinite(x);
}

#define NTCRT_MATH_ERRNO_AS(Function, Name, Type, Params, Args, Check) \
    EXTERN_C Type __cdecl Function Params                               \
    {                                                                   \
        Type Result = (Type)_NtCRT_X87_##Name Args;                     \
                                                                        \
        Check;                                                          \
        return Result;                                                  \
    }
#define NTCRT_MATH_ERRNO(Name, ...) NTCRT_MATH_ERRNO_AS(Name, Name, __VA_ARGS__)
#define NTCRT_MATH_ERRNO_WRAP(Name, ...) \
    NTCRT_MATH_ERRNO_AS(__wrap_##Name, Name, __VA_ARGS__) NTCRT_X87_WRAP_ALIAS(Name)

/* Members of libmsvcrt on x86, of libmingwex on x64 */
#ifdef __i386__
#define NTCRT_MATH_ERRNO_MSVCRT NTCRT_MATH_ERRNO
#else
#define NTCRT_MATH_ERRNO_MSVCRT NTCRT_MATH_ERRNO_WRAP
#endif
