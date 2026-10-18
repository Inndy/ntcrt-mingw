/*
 * The default _matherr of mingw-w64 (merr.c), installed by the startup code when the program sets
 * _MINGW_INSTALL_DEBUG_MATHERR to 1. libmingw32 prints the arguments by fprintf, which links the printf family
 * into every program; this one prints the error and the function name without the numbers.
 *
 * A program defines its own _matherr to replace it, libntcrt_start.a precedes libmingw32.a in the link.
 */

#include "../NtCRT.h"

#include <io.h>
#include <math.h>
#include <string.h>

static
void
_NtCRT_MathErrWrite(
    _In_z_ const char* Text)
{
    _write(2, Text, (unsigned int)strlen(Text));
}

EXTERN_C int __cdecl _matherr(struct _exception* pexcept)
{
    const char* Type;

    switch (pexcept->type)
    {
    case _DOMAIN:
        Type = "Argument domain error (DOMAIN)";
        break;
    case _SING:
        Type = "Argument singularity (SIGN)";
        break;
    case _OVERFLOW:
        Type = "Overflow range error (OVERFLOW)";
        break;
    case _PLOSS:
        Type = "Partial loss of significance (PLOSS)";
        break;
    case _TLOSS:
        Type = "Total loss of significance (TLOSS)";
        break;
    case _UNDERFLOW:
        Type = "The result is too small to be represented (UNDERFLOW)";
        break;
    default:
        Type = "Unknown error";
        break;
    }
    _NtCRT_MathErrWrite("_matherr(): ");
    _NtCRT_MathErrWrite(Type);
    _NtCRT_MathErrWrite(" in ");
    _NtCRT_MathErrWrite(pexcept->name != NULL ? pexcept->name : "?");
    _NtCRT_MathErrWrite("()\n");
    return 0;
}
