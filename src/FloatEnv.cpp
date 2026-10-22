/*
 * The floating point environment of msvcrt, the SSE MXCSR and the x87 control word. Apart from Math.cpp: the
 * startup of tcc calls _controlfp, and tcc links whole objects.
 */

#include "NtCRT.h"

#include <errno.h>
#include <float.h>

/* MSVC float.h values missing from mingw-w64 */
#ifndef _DN_FLUSH_OPERANDS_SAVE_RESULTS
#define _DN_FLUSH_OPERANDS_SAVE_RESULTS 0x02000000
#endif
#ifndef _DN_SAVE_OPERANDS_FLUSH_RESULTS
#define _DN_SAVE_OPERANDS_FLUSH_RESULTS 0x03000000
#endif
#ifndef _SW_INEXACT
#define _SW_INEXACT 0x00000001
#define _SW_UNDERFLOW 0x00000002
#define _SW_OVERFLOW 0x00000004
#define _SW_ZERODIVIDE 0x00000008
#define _SW_INVALID 0x00000010
#define _SW_DENORMAL 0x00080000
#endif

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
        Control |= _DN_SAVE_OPERANDS_FLUSH_RESULTS;
    } else if (Mxcsr & 0x0040)
    {
        Control |= _DN_FLUSH_OPERANDS_SAVE_RESULTS;
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
    /* DAZ (0x0040) flushes denormal operands, FTZ (0x8000) denormal results */
    switch (Control & _MCW_DN)
    {
        case _DN_FLUSH: Mxcsr |= 0x8040; break;
        case _DN_FLUSH_OPERANDS_SAVE_RESULTS: Mxcsr |= 0x0040; break;
        case _DN_SAVE_OPERANDS_FLUSH_RESULTS: Mxcsr |= 0x8000; break;
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
    unsigned short Word = _NtCRT_GetX87Control() & ~0x1F3FU;

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
    if (Control & _IC_AFFINE) Word |= 0x1000;
    __asm__ __volatile__("fldcw %0" : : "m"(Word));
}

/*
 * The control of x86 is that of the x87 or that of SSE, _EM_AMBIGUOUS when the exception masks or the rounding of
 * the two differ, as the x86 msvcrt. As the x64 msvcrt, x64 has no infinity control and the precision
 * is always _PC_64.
 */
static
unsigned int
_NtCRT_GetControl(void)
{
    unsigned int Sse = _NtCRT_MxcsrToControl(_NtCRT_GetMxcsr());
#ifdef _WIN64
    return Sse;
#else
    unsigned short Word = _NtCRT_GetX87Control();
    unsigned int Control = 0;

    if (Word & 0x01) Control |= _EM_INVALID;
    if (Word & 0x02) Control |= _EM_DENORMAL;
    if (Word & 0x04) Control |= _EM_ZERODIVIDE;
    if (Word & 0x08) Control |= _EM_OVERFLOW;
    if (Word & 0x10) Control |= _EM_UNDERFLOW;
    if (Word & 0x20) Control |= _EM_INEXACT;
    switch (Word & 0xC00)
    {
        case 0x400: Control |= _RC_DOWN; break;
        case 0x800: Control |= _RC_UP; break;
        case 0xC00: Control |= _RC_CHOP; break;
    }
    switch (Word & 0x300)
    {
        case 0x000: Control |= _PC_24; break;
        case 0x200: Control |= _PC_53; break;
    }
    if (Word & 0x1000) Control |= _IC_AFFINE;
    if ((Control ^ Sse) & (_MCW_EM | _MCW_RC))
    {
        Control |= _EM_AMBIGUOUS;
    }
    return Control | Sse;
#endif
}

EXTERN_C unsigned int __cdecl _NtCRT__control87(unsigned int newval, unsigned int mask)
{
    unsigned int Control = _NtCRT_GetControl();

#ifdef _WIN64
    mask &= ~(_MCW_IC | _MCW_PC);
#endif
    if (mask != 0)
    {
        Control = (Control & ~mask) | (newval & mask);
        _NtCRT_SetMxcsr(_NtCRT_ControlToMxcsr(Control, _NtCRT_GetMxcsr()));
        _NtCRT_SetX87Control(Control);
        Control = _NtCRT_GetControl();
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

NTCRT_IMPORT(_control87, _NtCRT__control87);
NTCRT_IMPORT(_controlfp, _NtCRT__controlfp);
NTCRT_IMPORT(_controlfp_s, _NtCRT__controlfp_s);
NTCRT_IMPORT(_set_controlfp, _NtCRT__set_controlfp);
NTCRT_IMPORT(_statusfp, _NtCRT__statusfp);
NTCRT_IMPORT(_clearfp, _NtCRT__clearfp);
