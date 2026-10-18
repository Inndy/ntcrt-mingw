/* Threads of msvcrt: _beginthread, _beginthreadex, _endthread and _endthreadex */

#include "NtCRT.h"

#include <errno.h>

typedef unsigned(__stdcall* _NtCRT_beginthreadex_proc)(void*);
typedef void(__cdecl* _NtCRT_beginthread_proc)(void*);

typedef struct _NTCRT_THREAD_PARAM
{
    _NtCRT_beginthreadex_proc StartEx;
    _NtCRT_beginthread_proc Start;
    void* ArgList;
    HANDLE Handle;
} NTCRT_THREAD_PARAM, *PNTCRT_THREAD_PARAM;

static
NTSTATUS
NTAPI
_NtCRT_ThreadStartEx(
    _In_ PVOID Parameter)
{
    NTCRT_THREAD_PARAM Param = *(PNTCRT_THREAD_PARAM)Parameter;
    unsigned Result = 0;

    RtlFreeHeap(NTCRT_HEAP, 0, Parameter);
    if (Param.StartEx != NULL)
    {
        Result = Param.StartEx(Param.ArgList);
    } else
    {
        Param.Start(Param.ArgList);
    }
    /* The TLS callback frees the per-thread data after the thread_local destructors of C++ */
    RtlExitUserThread((NTSTATUS)Result);
}

static
HANDLE
_NtCRT_CreateThread(
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ unsigned StackSize,
    _In_ PNTCRT_THREAD_PARAM Template,
    _In_ bool Suspended,
    _Out_opt_ unsigned* ThreadId)
{
    NTSTATUS Status;
    HANDLE Handle;
    CLIENT_ID ClientId;
    PNTCRT_THREAD_PARAM Param;

    Param = (PNTCRT_THREAD_PARAM)RtlAllocateHeap(NTCRT_HEAP, 0, sizeof(*Param));
    if (Param == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    *Param = *Template;
    Status = RtlCreateUserThread(NtCurrentProcess(),
                                 SecurityDescriptor,
                                 Suspended,
                                 0,
                                 0,
                                 StackSize,
                                 (PTHREAD_START_ROUTINE)_NtCRT_ThreadStartEx,
                                 Param,
                                 &Handle,
                                 &ClientId);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Param);
        _NtCRT_SetErrnoFromNtStatus(Status);
        return NULL;
    }
    if (ThreadId != NULL)
    {
        *ThreadId = (unsigned)(ULONG_PTR)ClientId.UniqueThread;
    }
    return Handle;
}

EXTERN_C
uintptr_t
__cdecl
_NtCRT__beginthreadex(
    _In_opt_ void* security,
    _In_ unsigned stack_size,
    _In_ _NtCRT_beginthreadex_proc start_address,
    _In_opt_ void* arglist,
    _In_ unsigned initflag,
    _Out_opt_ unsigned* thrdaddr)
{
    NTCRT_THREAD_PARAM Param = { start_address, NULL, arglist, NULL };

    if (start_address == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return 0;
    }
    return (uintptr_t)_NtCRT_CreateThread(security != NULL ? ((LPSECURITY_ATTRIBUTES)security)->lpSecurityDescriptor
                                                           : NULL,
                                          stack_size,
                                          &Param,
                                          (initflag & CREATE_SUSPENDED) != 0,
                                          thrdaddr);
}

EXTERN_C
uintptr_t
__cdecl
_NtCRT__beginthread(
    _In_ _NtCRT_beginthread_proc start_address,
    _In_ unsigned stack_size,
    _In_opt_ void* arglist)
{
    NTCRT_THREAD_PARAM Param = { NULL, start_address, arglist, NULL };
    HANDLE Handle;

    if (start_address == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return (uintptr_t)-1;
    }
    /* _beginthread closes the handle on thread exit in msvcrt, here the caller never owns it */
    Handle = _NtCRT_CreateThread(NULL, stack_size, &Param, false, NULL);
    if (Handle == NULL)
    {
        return (uintptr_t)-1;
    }
    NtClose(Handle);
    return (uintptr_t)Handle;
}

EXTERN_C __attribute__((noreturn)) void __cdecl _NtCRT__endthreadex(unsigned retval)
{
    RtlExitUserThread((NTSTATUS)retval);
}

EXTERN_C __attribute__((noreturn)) void __cdecl _NtCRT__endthread(void)
{
    _NtCRT__endthreadex(0);
}

NTCRT_IMPORT(_beginthreadex, _NtCRT__beginthreadex);
NTCRT_IMPORT(_beginthread, _NtCRT__beginthread);
NTCRT_IMPORT(_endthreadex, _NtCRT__endthreadex);
NTCRT_IMPORT(_endthread, _NtCRT__endthread);
