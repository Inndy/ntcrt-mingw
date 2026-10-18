/* msvcrt math and floating point environment functions not provided by ntdll or mingw-w64 */

#include "NtCRT.h"

#include <errno.h>
#include <float.h>
#include <math.h>

/* MSVC float.h values missing from mingw-w64 */
#ifndef _DN_SAVE_OPERANDS_FLUSH_RESULTS
#define _DN_SAVE_OPERANDS_FLUSH_RESULTS 0x02000000
#endif
#ifndef _DN_FLUSH_OPERANDS_SAVE_RESULTS
#define _DN_FLUSH_OPERANDS_SAVE_RESULTS 0x03000000
#endif
#ifndef _SW_INEXACT
#define _SW_INEXACT 0x00000001
#define _SW_UNDERFLOW 0x00000002
#define _SW_OVERFLOW 0x00000004
#define _SW_ZERODIVIDE 0x00000008
#define _SW_INVALID 0x00000010
#define _SW_DENORMAL 0x00080000
#endif

/* Elementary functions, from mingw-w64 exp/expm1/hypot */

EXTERN_C double __cdecl _NtCRT_log10(double x)
{
    double Result;

    if (x < 0)
    {
        NTCRT_ERRNO = EDOM;
        return NAN;
    }
    if (x == 0)
    {
        NTCRT_ERRNO = ERANGE;
        return -HUGE_VAL;
    }
    __asm__("fldlg2\n\t"
            "fxch\n\t"
            "fyl2x"
            : "=t"(Result)
            : "0"(x));
    return Result;
}

EXTERN_C double __cdecl _NtCRT_sinh(double x)
{
    double a = fabs(x), t;

    if (a > 710.5)
    {
        NTCRT_ERRNO = ERANGE;
        return copysign(HUGE_VAL, x);
    }
    if (a > 22)
    {
        t = exp(a * 0.5);
        return copysign(0.5 * t * t, x);
    }
    t = expm1(a);
    return copysign(0.5 * (t + t / (t + 1)), x);
}

EXTERN_C double __cdecl _NtCRT_cosh(double x)
{
    double a = fabs(x), t;

    if (isnan(x))
    {
        return x;
    }
    if (a > 710.5)
    {
        NTCRT_ERRNO = ERANGE;
        return HUGE_VAL;
    }
    if (a > 22)
    {
        t = exp(a * 0.5);
        return 0.5 * t * t;
    }
    t = exp(a);
    return 0.5 * (t + 1 / t);
}

EXTERN_C double __cdecl _NtCRT_tanh(double x)
{
    double a = fabs(x), t;

    if (isnan(x))
    {
        return x;
    }
    if (a > 22)
    {
        return copysign(1.0, x);
    }
    t = expm1(2 * a);
    return copysign(t / (t + 2), x);
}

EXTERN_C double __cdecl _NtCRT_asin(double x)
{
    if (fabs(x) > 1)
    {
        NTCRT_ERRNO = EDOM;
        return NAN;
    }
    return atan2(x, sqrt((1 - x) * (1 + x)));
}

EXTERN_C double __cdecl _NtCRT_acos(double x)
{
    if (fabs(x) > 1)
    {
        NTCRT_ERRNO = EDOM;
        return NAN;
    }
    return atan2(sqrt((1 - x) * (1 + x)), x);
}

/* mingw-w64 hypot/hypotf call _hypot, x87 extended range avoids overflow here */
EXTERN_C double __cdecl _NtCRT__hypot(double x, double y)
{
    long double Result;

    if (isinf(x) || isinf(y))
    {
        return HUGE_VAL;
    }
    Result = sqrtl((long double)x * x + (long double)y * y);
    if (Result > DBL_MAX)
    {
        NTCRT_ERRNO = ERANGE;
        return HUGE_VAL;
    }
    return (double)Result;
}

EXTERN_C float __cdecl _NtCRT__hypotf(float x, float y)
{
    double Result;

    if (isinf(x) || isinf(y))
    {
        return HUGE_VALF;
    }
    Result = sqrt((double)x * x + (double)y * y);
    if (Result > FLT_MAX)
    {
        NTCRT_ERRNO = ERANGE;
        return HUGE_VALF;
    }
    return (float)Result;
}

/* IEEE helpers */

EXTERN_C double __cdecl _NtCRT__copysign(double x, double y)
{
    return copysign(x, y);
}

EXTERN_C float __cdecl _NtCRT__copysignf(float x, float y)
{
    return copysignf(x, y);
}

EXTERN_C double __cdecl _NtCRT__chgsign(double x)
{
    return -x;
}

EXTERN_C float __cdecl _NtCRT__chgsignf(float x)
{
    return -x;
}

EXTERN_C int __cdecl _NtCRT__finite(double x)
{
    return isfinite(x);
}

EXTERN_C int __cdecl _NtCRT__finitef(float x)
{
    return isfinite(x);
}

EXTERN_C int __cdecl _NtCRT__isnan(double x)
{
    return isnan(x);
}

EXTERN_C int __cdecl _NtCRT__isnanf(float x)
{
    return isnan(x);
}

/* mingw-w64 has nextafterf only */
EXTERN_C double __cdecl _NtCRT__nextafter(double x, double y)
{
    unsigned long long Bits;

    if (isnan(x) || isnan(y))
    {
        return x + y;
    }
    if (x == y)
    {
        return y;
    }
    if (x == 0)
    {
        return copysign(DBL_TRUE_MIN, y);
    }
    memcpy(&Bits, &x, sizeof(Bits));
    if ((x < y) == (x > 0))
    {
        Bits++;
    } else
    {
        Bits--;
    }
    memcpy(&x, &Bits, sizeof(x));
    if (isinf(x))
    {
        NTCRT_ERRNO = ERANGE;
    }
    return x;
}

EXTERN_C float __cdecl _NtCRT__nextafterf(float x, float y)
{
    return nextafterf(x, y);
}

EXTERN_C double __cdecl _NtCRT__logb(double x)
{
    return logb(x);
}

EXTERN_C float __cdecl _NtCRT__logbf(float x)
{
    return logbf(x);
}

EXTERN_C double __cdecl _NtCRT__scalb(double x, long exp)
{
    return scalbln(x, exp);
}

EXTERN_C float __cdecl _NtCRT__scalbf(float x, long exp)
{
    return scalblnf(x, exp);
}

template<typename T>
static
int
_NtCRT_FpClass(
    _In_ T x)
{
    bool Negative = signbit(x);

    switch (fpclassify(x))
    {
        case FP_NAN:
        {
            /* The quiet bit is the most significant bit of the mantissa */
            if constexpr (sizeof(T) == sizeof(double))
            {
                unsigned long long Bits;
                memcpy(&Bits, &x, sizeof(Bits));
                return (Bits & 0x0008000000000000ULL) ? _FPCLASS_QNAN : _FPCLASS_SNAN;
            } else
            {
                unsigned int Bits;
                memcpy(&Bits, &x, sizeof(Bits));
                return (Bits & 0x00400000U) ? _FPCLASS_QNAN : _FPCLASS_SNAN;
            }
        }
        case FP_INFINITE:
            return Negative ? _FPCLASS_NINF : _FPCLASS_PINF;
        case FP_ZERO:
            return Negative ? _FPCLASS_NZ : _FPCLASS_PZ;
        case FP_SUBNORMAL:
            return Negative ? _FPCLASS_ND : _FPCLASS_PD;
        default:
            return Negative ? _FPCLASS_NN : _FPCLASS_PN;
    }
}

EXTERN_C int __cdecl _NtCRT__fpclass(double x)
{
    return _NtCRT_FpClass(x);
}

EXTERN_C int __cdecl _NtCRT__fpclassf(float x)
{
    return _NtCRT_FpClass(x);
}

/* Floating point environment, SSE MXCSR and x87 control word */

static
unsigned int
_NtCRT_GetMxcsr(void)
{
    unsigned int Mxcsr;
    __asm__ __volatile__("stmxcsr %0" : "=m"(Mxcsr));
    return Mxcsr;
}

static
void
_NtCRT_SetMxcsr(
    _In_ unsigned int Mxcsr)
{
    __asm__ __volatile__("ldmxcsr %0" : : "m"(Mxcsr));
}

static
unsigned int
_NtCRT_MxcsrToControl(
    _In_ unsigned int Mxcsr)
{
    unsigned int Control = 0;

    if (Mxcsr & 0x0080) Control |= _EM_INVALID;
    if (Mxcsr & 0x0100) Control |= _EM_DENORMAL;
    if (Mxcsr & 0x0200) Control |= _EM_ZERODIVIDE;
    if (Mxcsr & 0x0400) Control |= _EM_OVERFLOW;
    if (Mxcsr & 0x0800) Control |= _EM_UNDERFLOW;
    if (Mxcsr & 0x1000) Control |= _EM_INEXACT;
    switch (Mxcsr & 0x6000)
    {
        case 0x2000: Control |= _RC_DOWN; break;
        case 0x4000: Control |= _RC_UP; break;
        case 0x6000: Control |= _RC_CHOP; break;
    }
    if ((Mxcsr & 0x8040) == 0x8040)
    {
        Control |= _DN_FLUSH;
    } else if (Mxcsr & 0x8000)
    {
        Control |= _DN_FLUSH_OPERANDS_SAVE_RESULTS;
    } else if (Mxcsr & 0x0040)
    {
        Control |= _DN_SAVE_OPERANDS_FLUSH_RESULTS;
    }
    return Control;
}

static
unsigned int
_NtCRT_ControlToMxcsr(
    _In_ unsigned int Control,
    _In_ unsigned int Mxcsr)
{
    Mxcsr &= ~0xFFC0U;
    if (Control & _EM_INVALID) Mxcsr |= 0x0080;
    if (Control & _EM_DENORMAL) Mxcsr |= 0x0100;
    if (Control & _EM_ZERODIVIDE) Mxcsr |= 0x0200;
    if (Control & _EM_OVERFLOW) Mxcsr |= 0x0400;
    if (Control & _EM_UNDERFLOW) Mxcsr |= 0x0800;
    if (Control & _EM_INEXACT) Mxcsr |= 0x1000;
    switch (Control & _MCW_RC)
    {
        case _RC_DOWN: Mxcsr |= 0x2000; break;
        case _RC_UP: Mxcsr |= 0x4000; break;
        case _RC_CHOP: Mxcsr |= 0x6000; break;
    }
    switch (Control & _MCW_DN)
    {
        case _DN_FLUSH: Mxcsr |= 0x8040; break;
        case _DN_FLUSH_OPERANDS_SAVE_RESULTS: Mxcsr |= 0x8000; break;
        case _DN_SAVE_OPERANDS_FLUSH_RESULTS: Mxcsr |= 0x0040; break;
    }
    return Mxcsr;
}

static
unsigned short
_NtCRT_GetX87Control(void)
{
    unsigned short Control;
    __asm__ __volatile__("fnstcw %0" : "=m"(Control));
    return Control;
}

static
void
_NtCRT_SetX87Control(
    _In_ unsigned int Control)
{
    unsigned short Word = _NtCRT_GetX87Control() & ~0x0F3FU;

    if (Control & _EM_INVALID) Word |= 0x01;
    if (Control & _EM_DENORMAL) Word |= 0x02;
    if (Control & _EM_ZERODIVIDE) Word |= 0x04;
    if (Control & _EM_OVERFLOW) Word |= 0x08;
    if (Control & _EM_UNDERFLOW) Word |= 0x10;
    if (Control & _EM_INEXACT) Word |= 0x20;
    switch (Control & _MCW_RC)
    {
        case _RC_DOWN: Word |= 0x400; break;
        case _RC_UP: Word |= 0x800; break;
        case _RC_CHOP: Word |= 0xC00; break;
    }
    switch (Control & _MCW_PC)
    {
        case _PC_24: break;
        case _PC_53: Word |= 0x200; break;
        default: Word |= 0x300; break;
    }
    __asm__ __volatile__("fldcw %0" : : "m"(Word));
}

/* As the x64 msvcrt, there is no infinity control and the precision is always _PC_64 */
EXTERN_C unsigned int __cdecl _NtCRT__control87(unsigned int newval, unsigned int mask)
{
    unsigned int Mxcsr = _NtCRT_GetMxcsr();
    unsigned int Control = _NtCRT_MxcsrToControl(Mxcsr);

    mask &= ~(_MCW_IC | _MCW_PC);
    if (mask != 0)
    {
        Control = (Control & ~mask) | (newval & mask);
        _NtCRT_SetMxcsr(_NtCRT_ControlToMxcsr(Control, Mxcsr));
        _NtCRT_SetX87Control(Control);
    }
    return Control;
}

/* The x64 _controlfp of msvcrt ignores the denormal exception mask */
EXTERN_C unsigned int __cdecl _NtCRT__controlfp(unsigned int newval, unsigned int mask)
{
    return _NtCRT__control87(newval, mask & ~_EM_DENORMAL);
}

/* Undefined bits are rejected, the control word is still returned */
EXTERN_C errno_t __cdecl _NtCRT__controlfp_s(unsigned int* currentControl, unsigned int newval, unsigned int mask)
{
    const unsigned int Valid = _MCW_DN | _MCW_EM | _MCW_IC | _MCW_RC | _MCW_PC;
    unsigned int Control;

    if (newval & mask & ~Valid)
    {
        if (currentControl != NULL)
        {
            *currentControl = _NtCRT__controlfp(0, 0);
        }
        return _NtCRT_InvalidParameter(EINVAL);
    }
    Control = _NtCRT__controlfp(newval, mask & Valid);
    if (currentControl != NULL)
    {
        *currentControl = Control;
    }
    return 0;
}

EXTERN_C void __cdecl _NtCRT__set_controlfp(unsigned int newval, unsigned int mask)
{
    _NtCRT__controlfp(newval, mask);
}

static
unsigned int
_NtCRT_StatusFromFlags(
    _In_ unsigned int Flags)
{
    unsigned int Status = 0;

    if (Flags & 0x01) Status |= _SW_INVALID;
    if (Flags & 0x02) Status |= _SW_DENORMAL;
    if (Flags & 0x04) Status |= _SW_ZERODIVIDE;
    if (Flags & 0x08) Status |= _SW_OVERFLOW;
    if (Flags & 0x10) Status |= _SW_UNDERFLOW;
    if (Flags & 0x20) Status |= _SW_INEXACT;
    return Status;
}

EXTERN_C unsigned int __cdecl _NtCRT__statusfp(void)
{
    unsigned short X87Status;

    __asm__ __volatile__("fnstsw %0" : "=m"(X87Status));
    return _NtCRT_StatusFromFlags((_NtCRT_GetMxcsr() | X87Status) & 0x3F);
}

EXTERN_C unsigned int __cdecl _NtCRT__clearfp(void)
{
    unsigned int Status = _NtCRT__statusfp();

    _NtCRT_SetMxcsr(_NtCRT_GetMxcsr() & ~0x3FU);
    __asm__ __volatile__("fnclex");
    return Status;
}

NTCRT_IMPORT(log10, _NtCRT_log10);
NTCRT_IMPORT(sinh, _NtCRT_sinh);
NTCRT_IMPORT(cosh, _NtCRT_cosh);
NTCRT_IMPORT(tanh, _NtCRT_tanh);
NTCRT_IMPORT(asin, _NtCRT_asin);
NTCRT_IMPORT(acos, _NtCRT_acos);
NTCRT_IMPORT(_hypot, _NtCRT__hypot);
NTCRT_IMPORT(_hypotf, _NtCRT__hypotf);
NTCRT_IMPORT(_copysign, _NtCRT__copysign);
NTCRT_IMPORT(_copysignf, _NtCRT__copysignf);
NTCRT_IMPORT(_chgsign, _NtCRT__chgsign);
NTCRT_IMPORT(chgsign, _NtCRT__chgsign);
NTCRT_IMPORT(_chgsignf, _NtCRT__chgsignf);
NTCRT_IMPORT(_finite, _NtCRT__finite);
NTCRT_IMPORT(finite, _NtCRT__finite);
NTCRT_IMPORT(_finitef, _NtCRT__finitef);
NTCRT_IMPORT(_isnan, _NtCRT__isnan);
NTCRT_IMPORT(_isnanf, _NtCRT__isnanf);
NTCRT_IMPORT(_nextafter, _NtCRT__nextafter);
NTCRT_IMPORT(nextafter, _NtCRT__nextafter);
NTCRT_IMPORT(_nextafterf, _NtCRT__nextafterf);
NTCRT_IMPORT(_logb, _NtCRT__logb);
NTCRT_IMPORT(_logbf, _NtCRT__logbf);
NTCRT_IMPORT(_scalb, _NtCRT__scalb);
NTCRT_IMPORT(_scalbf, _NtCRT__scalbf);
NTCRT_IMPORT(_fpclass, _NtCRT__fpclass);
NTCRT_IMPORT(fpclass, _NtCRT__fpclass);
NTCRT_IMPORT(_fpclassf, _NtCRT__fpclassf);
NTCRT_IMPORT(_control87, _NtCRT__control87);
NTCRT_IMPORT(_controlfp, _NtCRT__controlfp);
NTCRT_IMPORT(_controlfp_s, _NtCRT__controlfp_s);
NTCRT_IMPORT(_set_controlfp, _NtCRT__set_controlfp);
NTCRT_IMPORT(_statusfp, _NtCRT__statusfp);
NTCRT_IMPORT(_clearfp, _NtCRT__clearfp);
