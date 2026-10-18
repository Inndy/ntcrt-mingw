/* pthread_once */

#include "NtCRT.h"

#include <pthread.h>

/* pthread_once_t: 0 (PTHREAD_ONCE_INIT), running, running with waiters, done */
#define ONCE_RUNNING 1
#define ONCE_WAITERS 2
#define ONCE_DONE    3

/*
 * A routine that does not return resets the once for another call: an exception (std::call_once) unwinds through
 * _NtCRT_OnceCall, whose unwind handler takes the call from the home of its second argument, and a cancelled thread
 * runs the cleanup handler of the call.
 */
typedef struct _NTCRT_ONCE_CALL
{
    pthread_once_t* Once;
    _pthread_cleanup** List; /* Of the thread, NULL if it cannot be cancelled */
    _pthread_cleanup Cleanup;
} NTCRT_ONCE_CALL;

EXTERN_C void _NtCRT_OnceCall(void (*Func)(void), NTCRT_ONCE_CALL* Call);

__asm__(".section .text$_NtCRT_OnceCall,\"xr\"\n\t"
        ".linkonce discard\n\t"
        ".globl _NtCRT_OnceCall\n\t"
        ".def _NtCRT_OnceCall; .scl 2; .type 32; .endef\n\t"
        ".seh_proc _NtCRT_OnceCall\n"
        "_NtCRT_OnceCall:\n\t"
        "subq $40, %rsp\n\t"
        ".seh_stackalloc 40\n\t"
        ".seh_endprologue\n\t"
        ".seh_handler _NtCRT_OnceUnwind, @unwind\n\t"
        "movq %rdx, 56(%rsp)\n\t"
        "callq *%rcx\n\t"
        "nop\n\t"
        "addq $40, %rsp\n\t"
        "retq\n\t"
        ".seh_endproc\n\t"
        ".text");

static void OnceEnd(pthread_once_t* Once, LONG State)
{
    if (InterlockedExchange((volatile LONG*)Once, State) == ONCE_WAITERS)
    {
        _NtCRT_Wake(Once, TRUE);
    }
}

static void OnceCancel(void* Once)
{
    OnceEnd((pthread_once_t*)Once, 0);
}

EXTERN_C
EXCEPTION_DISPOSITION
_NtCRT_OnceUnwind(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _In_ PVOID DispatcherContext)
{
    UNREFERENCED_PARAMETER(ContextRecord);
    UNREFERENCED_PARAMETER(DispatcherContext);
    if ((ExceptionRecord->ExceptionFlags & EXCEPTION_UNWIND) != 0)
    {
        NTCRT_ONCE_CALL* Call;

        memcpy(&Call, (char*)EstablisherFrame + 56, sizeof(Call));
        if (Call->List != NULL)
        {
            *Call->List = Call->Cleanup.next;
        }
        OnceEnd(Call->Once, 0);
    }
    return ExceptionContinueSearch;
}

NTCRT_API int pthread_once(pthread_once_t* o, void (*func)(void))
{
    for (;;)
    {
        LONG State = __atomic_load_n(o, __ATOMIC_ACQUIRE);

        if (State == ONCE_DONE)
        {
            return 0;
        }
        if (State == 0)
        {
            if (InterlockedCompareExchange((volatile LONG*)o, ONCE_RUNNING, 0) == 0)
            {
                NTCRT_ONCE_CALL Call = { o, _NtCRT_CleanupList != NULL ? _NtCRT_CleanupList() : NULL,
                                         { OnceCancel, o, NULL } };

                if (Call.List != NULL)
                {
                    Call.Cleanup.next = *Call.List;
                    *Call.List = &Call.Cleanup;
                }
                _NtCRT_OnceCall(func, &Call);
                if (Call.List != NULL)
                {
                    *Call.List = Call.Cleanup.next;
                }
                OnceEnd(o, ONCE_DONE);
                return 0;
            }
            continue;
        }
        if (State == ONCE_RUNNING && InterlockedCompareExchange((volatile LONG*)o, ONCE_WAITERS, ONCE_RUNNING) !=
                                         ONCE_RUNNING)
        {
            continue;
        }
        _NtCRT_Wait(o, ONCE_WAITERS, sizeof(*o), NULL, FALSE);
    }
}
