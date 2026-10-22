/* pthread_once */

#include "NtCRT.h"

#include <pthread.h>
#ifdef __i386__
#include <unwind.h>
#endif

/* pthread_once_t: 0 (PTHREAD_ONCE_INIT), running, running with waiters, done */
#define ONCE_RUNNING 1
#define ONCE_WAITERS 2
#define ONCE_DONE    3

/*
 * A routine that does not return resets the once for another call: an exception (std::call_once) or a longjmp
 * unwinds through _NtCRT_OnceCall, whose unwind handler takes the call from its second argument, and a cancelled
 * thread runs the cleanup handler of the call. x64 unwinds both by SEH. On x86 an exception of GCC and clang unwinds by
 * DWARF, with the personality routine of _NtCRT_OnceCall, and a longjmp by SEH (RtlUnwind), with the handler that
 * _NtCRT_OnceCall registers.
 */
typedef struct _NTCRT_ONCE_CALL
{
    pthread_once_t* Once;
    _pthread_cleanup** List; /* Of the thread, NULL if it cannot be cancelled */
    _pthread_cleanup Cleanup;
} NTCRT_ONCE_CALL;

EXTERN_C void _NtCRT_OnceCall(void (*Func)(void), NTCRT_ONCE_CALL* Call);

#ifdef __x86_64__
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
#else
/* The SEH registration is at the frame - 8 (the stack pointer at the call), the arguments at the frame + 8 */
__asm__(".section .text$_NtCRT_OnceCall,\"xr\"\n\t"
        ".linkonce discard\n\t"
        ".globl __NtCRT_OnceCall\n\t"
        ".def __NtCRT_OnceCall; .scl 2; .type 32; .endef\n"
        "__NtCRT_OnceCall:\n\t"
        ".cfi_startproc\n\t"
        ".cfi_personality 0, __NtCRT_OncePersonality\n\t"
        "pushl %ebp\n\t"
        ".cfi_def_cfa_offset 8\n\t"
        ".cfi_offset %ebp, -8\n\t"
        "movl %esp, %ebp\n\t"
        ".cfi_def_cfa_register %ebp\n\t"
        "pushl $__NtCRT_OnceSeh\n\t"
        "pushl %fs:0\n\t"
        "movl %esp, %fs:0\n\t"
        "calll *8(%ebp)\n\t"
        "popl %fs:0\n\t"
        "leave\n\t"
        ".cfi_def_cfa %esp, 4\n\t"
        "retl\n\t"
        ".cfi_endproc\n\t"
        ".text");
#endif

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

/* The call that Offset bytes from Frame point to was unwound */
static void OnceUnwound(const void* Frame, size_t Offset)
{
    NTCRT_ONCE_CALL* Call;

    memcpy(&Call, (const char*)Frame + Offset, sizeof(Call));
    if (Call->List != NULL)
    {
        *Call->List = Call->Cleanup.next;
    }
    OnceEnd(Call->Once, 0);
}

/* The SEH handler of _NtCRT_OnceCall: its frame on x64, its registration on x86 */
EXTERN_C
EXCEPTION_DISPOSITION
NTCRT_X86_X64(_NtCRT_OnceSeh, _NtCRT_OnceUnwind)(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _In_ PVOID DispatcherContext)
{
    UNREFERENCED_PARAMETER(ContextRecord);
    UNREFERENCED_PARAMETER(DispatcherContext);
    if ((ExceptionRecord->ExceptionFlags & EXCEPTION_UNWIND) != 0)
    {
        OnceUnwound(EstablisherFrame, NTCRT_X86_X64(20, 56));
    }
    return ExceptionContinueSearch;
}

#ifdef __i386__
/*
 * The personality routine of _NtCRT_OnceCall for DWARF: the cleanup unwinds the call and the SEH registration, which
 * the frames of DWARF leave. _Unwind_GetCFA is the stack pointer of the frame at the call (libgcc and libunwind),
 * where the registration is. With clang it is in the section of _NtCRT_OnceCall, which is no COMDAT there: lld keeps
 * no section for the relocations of .eh_frame and would leave the personality of the CIE null. GNU as makes every
 * .text$ section a COMDAT, ld keeps what .eh_frame refers to.
 */
EXTERN_C
#ifdef __clang__
__attribute__((section(".text$_NtCRT_OnceCall")))
#endif
_Unwind_Reason_Code
_NtCRT_OncePersonality(
    _In_ int Version,
    _In_ _Unwind_Action Actions,
    _In_ _Unwind_Exception_Class Class,
    _In_ struct _Unwind_Exception* Exception,
    _In_ struct _Unwind_Context* Context)
{
    UNREFERENCED_PARAMETER(Class);
    UNREFERENCED_PARAMETER(Exception);
    if (Version != 1)
    {
        return _URC_FATAL_PHASE1_ERROR;
    }
    if ((Actions & _UA_CLEANUP_PHASE) != 0)
    {
        PEXCEPTION_REGISTRATION_RECORD Registration = (PEXCEPTION_REGISTRATION_RECORD)_Unwind_GetCFA(Context);

        NtCurrentTeb()->NtTib.ExceptionList = Registration->Next;
        OnceUnwound(Registration, 20);
    }
    return _URC_CONTINUE_UNWIND;
}
#endif

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
        _NtCRT_Wait(o, ONCE_WAITERS, sizeof(*o), NULL, 0);
    }
}

/* The pointers that WINPTHREADS_USE_DLLIMPORT refers to (__declspec(dllimport) of the winpthreads headers) */
NTCRT_IMPORT_ONLY(pthread_once, pthread_once);
