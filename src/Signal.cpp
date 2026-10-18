/* raise, _XcptFilter and abort by the actions of the signals, which signal of Startup.cpp sets */

#include "NtCRT.h"

#include <errno.h>
#include <float.h>
#include <signal.h>

/* Call the handler of SIGFPE, SIGILL or SIGSEGV with the exception information of the thread */
static
void
_NtCRT_CallXcptHandler(
    _In_ _NtCRT_sighandler_t Handler,
    _In_ int Signal,
    _In_opt_ void* XcptInfoPtrs,
    _In_ int Fpecode)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();
    void* OldXcptInfoPtrs = Ptd->PxcptInfoPtrs;
    int OldFpecode = Ptd->Fpecode;

    Ptd->PxcptInfoPtrs = XcptInfoPtrs;
    if (Signal == SIGFPE)
    {
        Ptd->Fpecode = Fpecode;
        ((void(__cdecl*)(int, int))(void*)Handler)(SIGFPE, Fpecode);
        Ptd->Fpecode = OldFpecode;
    } else
    {
        Handler(Signal);
    }
    Ptd->PxcptInfoPtrs = OldXcptInfoPtrs;
}

EXTERN_C int __cdecl _NtCRT_raise(int sig)
{
    _NtCRT_sighandler_t Handler;

    if (sig <= 0 || sig >= NSIG)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (sig == SIGABRT_COMPAT)
    {
        sig = SIGABRT;
    }
    _NtCRT_lock(_SIGNAL_LOCK);
    Handler = _NtCRT_SignalHandlers[sig];
    if (Handler != SIG_IGN && Handler != SIG_DFL)
    {
        _NtCRT_SignalHandlers[sig] = SIG_DFL;
    }
    _NtCRT_unlock(_SIGNAL_LOCK);
    if (Handler == SIG_IGN)
    {
        return 0;
    }
    if (Handler == SIG_DFL)
    {
        _NtCRT__exit(3);
    }
    if (sig == SIGFPE || sig == SIGILL || sig == SIGSEGV)
    {
        _NtCRT_CallXcptHandler(Handler, sig, NULL, _FPE_EXPLICITGEN);
    } else
    {
        Handler(sig);
    }
    return 0;
}

/* The exceptions that _XcptFilter passes to the handlers of signals */
static const struct
{
    NTSTATUS Status;
    int Signal;
    int Fpecode;
} g_XcptSignals[] = {
    { STATUS_ACCESS_VIOLATION, SIGSEGV, 0 },
    { STATUS_ILLEGAL_INSTRUCTION, SIGILL, 0 },
    { STATUS_PRIVILEGED_INSTRUCTION, SIGILL, 0 },
    { STATUS_FLOAT_DENORMAL_OPERAND, SIGFPE, _FPE_DENORMAL },
    { STATUS_FLOAT_DIVIDE_BY_ZERO, SIGFPE, _FPE_ZERODIVIDE },
    { STATUS_FLOAT_INEXACT_RESULT, SIGFPE, _FPE_INEXACT },
    { STATUS_FLOAT_INVALID_OPERATION, SIGFPE, _FPE_INVALID },
    { STATUS_FLOAT_OVERFLOW, SIGFPE, _FPE_OVERFLOW },
    { STATUS_FLOAT_STACK_CHECK, SIGFPE, _FPE_STACKOVERFLOW },
    { STATUS_FLOAT_UNDERFLOW, SIGFPE, _FPE_UNDERFLOW },
};

/* Other exceptions and SIG_DFL are left to the other exception handlers */
EXTERN_C int __cdecl _NtCRT__XcptFilter(unsigned long xcptnum, PEXCEPTION_POINTERS pxcptinfoptrs)
{
    _NtCRT_sighandler_t Handler = SIG_DFL;
    int Signal = 0, Fpecode = 0;

    for (size_t i = 0; i < ARRAYSIZE(g_XcptSignals); i++)
    {
        if ((unsigned long)g_XcptSignals[i].Status == xcptnum)
        {
            Signal = g_XcptSignals[i].Signal;
            Fpecode = g_XcptSignals[i].Fpecode;
            break;
        }
    }
    if (Signal != 0)
    {
        _NtCRT_sighandler_t* Action = xcptnum == (unsigned long)STATUS_PRIVILEGED_INSTRUCTION ?
                                          &_NtCRT_PrivilegedInstructionHandler :
                                          &_NtCRT_SignalHandlers[Signal];

        _NtCRT_lock(_SIGNAL_LOCK);
        Handler = *Action;
        if (Handler != SIG_IGN && Handler != SIG_DFL)
        {
            *Action = SIG_DFL;
        }
        _NtCRT_unlock(_SIGNAL_LOCK);
    }
    if (Handler == SIG_DFL)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (Handler != SIG_IGN)
    {
        _NtCRT_CallXcptHandler(Handler, Signal, pxcptinfoptrs, Fpecode);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

EXTERN_C __attribute__((noreturn)) void __cdecl _NtCRT_abort(void)
{
    if (_NtCRT_SignalHandlers[SIGABRT] == SIG_DFL || _NtCRT_SignalHandlers[SIGABRT] == SIG_IGN)
    {
        _NtCRT_WriteStderrRaw("\r\nThis application has requested the Runtime to terminate it in an unusual way.\r\n");
    }
    _NtCRT_raise(SIGABRT);
    _NtCRT__exit(3);
}

NTCRT_IMPORT(_XcptFilter, _NtCRT__XcptFilter);
NTCRT_IMPORT(raise, _NtCRT_raise);
NTCRT_IMPORT(abort, _NtCRT_abort);
