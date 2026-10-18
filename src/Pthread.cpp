/* POSIX threads: creation, exit, join, cancellation, attributes and priorities */

#include "NtCRT.h"

#include <pthread.h>

/*
 * A pthread_t points to the NTCRT_PTHREAD of the thread, a TLS slot has it. pthread_create allocates it, the joiner
 * or the thread itself if detached frees it. A thread not created by pthread_create gets a detached one when
 * it asks for it (pthread_self).
 *
 * A thread ends in the TLS callback (_NtCRT_ThreadDetach) after the thread_local destructors of C++ (the callback of
 * libmingw32 precedes it): the key destructors, emutls among them, then ThreadExit (_NtCRT_PthreadExit). A joiner
 * waits for the handle, signaled after the callbacks.
 */
typedef struct _NTCRT_PTHREAD
{
    void* (*Start)(void*);
    int (*StartC11)(void*);
    void* Arg;
    void* Result;
    HANDLE Handle;
    _pthread_cleanup* Cleanup; /* pthread_cleanup_push */
    volatile LONG State;
    volatile LONG Cancel;
} NTCRT_PTHREAD, *PNTCRT_PTHREAD;

#define THREAD_DETACHED 1
#define THREAD_EXITED   2
#define THREAD_FOREIGN  4 /* Not created by pthread_create */

#define CANCEL_PENDING  1
#define CANCEL_DISABLED 2
#define CANCEL_ASYNC    4 /* Kept, acts as deferred */

static NTCRT_PTHREAD g_FallbackThread = { NULL, NULL, NULL, NULL, NULL, NULL, THREAD_DETACHED | THREAD_FOREIGN, 0 };

/* The TLS slot of the pthread_t of a thread */
static volatile DWORD g_SelfIndex = TLS_OUT_OF_INDEXES;

static bool ThreadSelfSlot(void)
{
    DWORD Index;

    if (g_SelfIndex != TLS_OUT_OF_INDEXES)
    {
        return true;
    }
    Index = _Inline_TlsAlloc();
    if (Index == TLS_OUT_OF_INDEXES)
    {
        return false;
    }
    if ((DWORD)InterlockedCompareExchange((volatile LONG*)&g_SelfIndex, Index, TLS_OUT_OF_INDEXES) !=
        TLS_OUT_OF_INDEXES)
    {
        _Inline_TlsFree(Index);
    }
    return true;
}

/* NULL if the thread has none */
static PNTCRT_PTHREAD ThreadSelf(void)
{
    DWORD Index = g_SelfIndex;

    return Index != TLS_OUT_OF_INDEXES ? (PNTCRT_PTHREAD)_Inline_TlsGetValue2(Index) : NULL;
}

static void ThreadFree(PNTCRT_PTHREAD Thread)
{
    NtClose(Thread->Handle);
    RtlFreeHeap(NTCRT_HEAP, 0, Thread);
}

static void ThreadExit(void)
{
    PNTCRT_PTHREAD Thread = ThreadSelf();

    if (Thread == NULL)
    {
        return;
    }
    _Inline_TlsSetValue(g_SelfIndex, NULL);
    if ((Thread->State & THREAD_FOREIGN) != 0 || (InterlockedOr(&Thread->State, THREAD_EXITED) & THREAD_DETACHED) != 0)
    {
        ThreadFree(Thread);
    }
}

static PNTCRT_PTHREAD CurrentThread(void);

/* A thread gets its pthread_t now, the routine of a once may cancel it */
static _pthread_cleanup** CleanupList(void)
{
    PNTCRT_PTHREAD Thread = CurrentThread();

    return Thread != &g_FallbackThread ? &Thread->Cleanup : NULL;
}

/*
 * Exit work of the threads: the keys, then ThreadExit. A thread that has a pthread_t may be cancelled. Set at startup
 * by a program that links this, before any thread can get a pthread_t in the routine of a once.
 */
static void __cdecl ThreadExitHook(void)
{
    _NtCRT_PthreadExit = ThreadExit;
    _NtCRT_ThreadExit = _NtCRT_KeyExit;
    _NtCRT_CleanupList = CleanupList;
}
extern "C" __attribute__((section(".CRT$XIC"), used)) void(__cdecl* const _NtCRT_ThreadExitHookPtr)(void) =
    ThreadExitHook;

static PNTCRT_PTHREAD CurrentThread(void)
{
    PNTCRT_PTHREAD Thread = ThreadSelf();

    if (Thread == NULL)
    {
        Thread = (PNTCRT_PTHREAD)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, sizeof(*Thread));
        if (Thread == NULL)
        {
            return &g_FallbackThread;
        }
        Thread->State = THREAD_DETACHED | THREAD_FOREIGN;
        NtDuplicateObject(NtCurrentProcess(),
                          NtCurrentThread(),
                          NtCurrentProcess(),
                          &Thread->Handle,
                          0,
                          0,
                          DUPLICATE_SAME_ACCESS);
        if (!ThreadSelfSlot() || !_Inline_TlsSetValue(g_SelfIndex, Thread))
        {
            ThreadFree(Thread);
            return &g_FallbackThread;
        }
    }
    return Thread;
}

static __attribute__((noreturn)) void ThreadEnd(PNTCRT_PTHREAD Thread, void* Result)
{
    Thread->Result = Result;
    RtlExitUserThread((NTSTATUS)(ULONG_PTR)Result);
}

static NTSTATUS NTAPI ThreadStart(PVOID Parameter)
{
    PNTCRT_PTHREAD Thread = (PNTCRT_PTHREAD)Parameter;

    /* pthread_create allocated the slot */
    _Inline_TlsSetValue(g_SelfIndex, Thread);
    if (Thread->StartC11 != NULL)
    {
        ThreadEnd(Thread, (void*)(INT_PTR)Thread->StartC11(Thread->Arg));
    }
    ThreadEnd(Thread, Thread->Start(Thread->Arg));
}

/* The priorities of Windows (THREAD_PRIORITY_*) as SetThreadPriority takes them */
static NTSTATUS ThreadSetPriority(HANDLE Handle, int Priority)
{
    LONG Base = Priority;

    /* Between the levels toward normal, as winpthreads */
    if (Priority <= THREAD_PRIORITY_IDLE)
    {
        Base = -(HIGH_PRIORITY + 1) / 2;
    } else if (Priority <= THREAD_PRIORITY_LOWEST)
    {
        Base = THREAD_PRIORITY_LOWEST;
    } else if (Priority >= THREAD_PRIORITY_TIME_CRITICAL)
    {
        Base = (HIGH_PRIORITY + 1) / 2;
    } else if (Priority >= THREAD_PRIORITY_HIGHEST)
    {
        Base = THREAD_PRIORITY_HIGHEST;
    }
    return NtSetInformationThread(Handle, ThreadBasePriority, &Base, sizeof(Base));
}

static NTSTATUS ThreadGetPriority(HANDLE Handle, int* Priority)
{
    THREAD_BASIC_INFORMATION Info;
    NTSTATUS Status = NtQueryInformationThread(Handle, ThreadBasicInformation, &Info, sizeof(Info), NULL);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    *Priority = Info.BasePriority >= (HIGH_PRIORITY + 1) / 2    ? THREAD_PRIORITY_TIME_CRITICAL
                : Info.BasePriority <= -(HIGH_PRIORITY + 1) / 2 ? THREAD_PRIORITY_IDLE
                                                                : Info.BasePriority;
    return Status;
}

EXTERN_C int _NtCRT_ThreadCreate(ULONG_PTR* th, const void* Attr, void* (*Start)(void*), int (*StartC11)(void*),
                                 void* Arg)
{
    const pthread_attr_t* attr = (const pthread_attr_t*)Attr;
    PNTCRT_PTHREAD Thread;
    NTSTATUS Status;

    if (!ThreadSelfSlot())
    {
        return EAGAIN;
    }
    Thread = (PNTCRT_PTHREAD)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, sizeof(*Thread));
    if (Thread == NULL)
    {
        return EAGAIN;
    }
    Thread->Start = Start;
    Thread->StartC11 = StartC11;
    Thread->Arg = Arg;
    if (attr != NULL)
    {
        Thread->State = (attr->p_state & PTHREAD_CREATE_DETACHED) != 0 ? THREAD_DETACHED : 0;
        Thread->Cancel = ((attr->p_state & PTHREAD_CANCEL_ENABLE) == 0 ? CANCEL_DISABLED : 0) |
                         ((attr->p_state & PTHREAD_CANCEL_ASYNCHRONOUS) != 0 ? CANCEL_ASYNC : 0);
    }
    /* Suspended until the handle is stored: a detached thread frees its NTCRT_PTHREAD */
    Status = RtlCreateUserThread(NtCurrentProcess(),
                                 NULL,
                                 TRUE,
                                 0,
                                 attr != NULL ? attr->s_size : 0,
                                 0,
                                 (PTHREAD_START_ROUTINE)ThreadStart,
                                 Thread,
                                 &Thread->Handle,
                                 NULL);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Thread);
        return EAGAIN;
    }
    if (attr != NULL)
    {
        int Priority = attr->param.sched_priority;

        /* Windows starts a thread at the normal priority, not that of its creator */
        if ((attr->p_state & PTHREAD_INHERIT_SCHED) != 0 && !NT_SUCCESS(ThreadGetPriority(NtCurrentThread(), &Priority)))
        {
            Priority = 0;
        }
        if (Priority != 0)
        {
            ThreadSetPriority(Thread->Handle, Priority);
        }
    }
    *th = (ULONG_PTR)Thread;
    NtResumeThread(Thread->Handle, NULL);
    return 0;
}

NTCRT_API int pthread_create(pthread_t* th, const pthread_attr_t* attr, void* (*func)(void*), void* arg)
{
    return _NtCRT_ThreadCreate(th, attr, func, NULL, arg);
}

NTCRT_API void pthread_exit(void* res)
{
    PNTCRT_PTHREAD Thread = CurrentThread();
    _pthread_cleanup* Cleanup;

    /* The cleanup handlers are not cancelled */
    InterlockedOr(&Thread->Cancel, CANCEL_DISABLED);
    while ((Cleanup = Thread->Cleanup) != NULL)
    {
        Thread->Cleanup = Cleanup->next;
        Cleanup->func(Cleanup->arg);
    }
    ThreadEnd(Thread, res);
}

NTCRT_API pthread_t pthread_self(void)
{
    return (pthread_t)CurrentThread();
}

NTCRT_API int pthread_equal(pthread_t t1, pthread_t t2)
{
    return t1 == t2;
}

/* Wait for the end of a thread, Timeout 0 for _pthread_tryjoin */
static int ThreadJoin(pthread_t t, void** res, PLARGE_INTEGER Timeout)
{
    PNTCRT_PTHREAD Thread = (PNTCRT_PTHREAD)t;
    NTSTATUS Status;

    if (Thread == NULL)
    {
        return ESRCH;
    }
    if (Thread == ThreadSelf())
    {
        return EDEADLK;
    }
    if ((Thread->State & THREAD_DETACHED) != 0)
    {
        return EINVAL;
    }
    pthread_testcancel();
    while ((Status = NtWaitForSingleObject(Thread->Handle, TRUE, Timeout)) != STATUS_WAIT_0)
    {
        if (Status == STATUS_TIMEOUT)
        {
            return EBUSY;
        }
        if (Status != STATUS_ALERTED && Status != STATUS_USER_APC)
        {
            return ESRCH;
        }
        pthread_testcancel();
    }
    if (res != NULL)
    {
        *res = Thread->Result;
    }
    ThreadFree(Thread);
    return 0;
}

NTCRT_API int pthread_join(pthread_t t, void** res)
{
    return ThreadJoin(t, res, NULL);
}

NTCRT_API int _pthread_tryjoin(pthread_t t, void** res)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = 0;
    return ThreadJoin(t, res, &Timeout);
}

NTCRT_API int pthread_detach(pthread_t t)
{
    PNTCRT_PTHREAD Thread = (PNTCRT_PTHREAD)t;
    LONG State;

    if (Thread == NULL)
    {
        return ESRCH;
    }
    State = InterlockedOr(&Thread->State, THREAD_DETACHED);
    if ((State & THREAD_DETACHED) != 0)
    {
        return EINVAL;
    }
    if ((State & THREAD_EXITED) != 0)
    {
        ThreadFree(Thread);
    }
    return 0;
}

/*
 * Cancellation is deferred: a cancelled thread exits at a cancellation point (pthread_testcancel, pthread_join,
 * pthread_cond_wait, pthread_cond_timedwait, sem_wait, sem_timedwait, nanosleep, clock_nanosleep, pthread_delay_np).
 * These wait alertably, the alert of pthread_cancel ends the wait.
 */

static void TestCancel(void (*Pass)(void*), void* Object)
{
    PNTCRT_PTHREAD Thread = ThreadSelf();

    if (Thread != NULL && (Thread->Cancel & (CANCEL_PENDING | CANCEL_DISABLED)) == CANCEL_PENDING)
    {
        if (Pass != NULL)
        {
            Pass(Object);
        }
        pthread_exit(PTHREAD_CANCELED);
    }
}

NTCRT_API void pthread_testcancel(void)
{
    TestCancel(NULL, NULL);
}

NTCRT_API int pthread_cancel(pthread_t t)
{
    PNTCRT_PTHREAD Thread = (PNTCRT_PTHREAD)t;

    if (Thread == NULL)
    {
        return ESRCH;
    }
    _NtCRT_TestCancel = TestCancel;
    InterlockedOr(&Thread->Cancel, CANCEL_PENDING);
    NtAlertThread(Thread->Handle);
    return 0;
}

NTCRT_API int pthread_setcancelstate(int state, int* oldstate)
{
    PNTCRT_PTHREAD Thread;
    LONG Old;

    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
    {
        return EINVAL;
    }
    Thread = CurrentThread();
    Old = state == PTHREAD_CANCEL_ENABLE ? InterlockedAnd(&Thread->Cancel, ~CANCEL_DISABLED)
                                         : InterlockedOr(&Thread->Cancel, CANCEL_DISABLED);
    if (oldstate != NULL)
    {
        *oldstate = (Old & CANCEL_DISABLED) != 0 ? PTHREAD_CANCEL_DISABLE : PTHREAD_CANCEL_ENABLE;
    }
    return 0;
}

NTCRT_API int pthread_setcanceltype(int type, int* oldtype)
{
    PNTCRT_PTHREAD Thread;
    LONG Old;

    if (type != PTHREAD_CANCEL_DEFERRED && type != PTHREAD_CANCEL_ASYNCHRONOUS)
    {
        return EINVAL;
    }
    Thread = CurrentThread();
    Old = type == PTHREAD_CANCEL_DEFERRED ? InterlockedAnd(&Thread->Cancel, ~CANCEL_ASYNC)
                                          : InterlockedOr(&Thread->Cancel, CANCEL_ASYNC);
    if (oldtype != NULL)
    {
        *oldtype = (Old & CANCEL_ASYNC) != 0 ? PTHREAD_CANCEL_ASYNCHRONOUS : PTHREAD_CANCEL_DEFERRED;
    }
    return 0;
}

/* Signals cannot be sent to a thread, 0 checks nothing */
NTCRT_API int pthread_kill(pthread_t t, int sig)
{
    if (t == 0)
    {
        return ESRCH;
    }
    return sig == 0 ? 0 : EINVAL;
}

NTCRT_API _pthread_cleanup** pthread_getclean(void)
{
    return &CurrentThread()->Cleanup;
}

NTCRT_API void* pthread_gethandle(pthread_t t)
{
    return t != 0 ? ((PNTCRT_PTHREAD)t)->Handle : NULL;
}

NTCRT_API void* pthread_getevent(void)
{
    return NULL;
}

/* Priorities are those of Windows, THREAD_PRIORITY_IDLE to THREAD_PRIORITY_TIME_CRITICAL */

NTCRT_API int pthread_getschedparam(pthread_t thread, int* pol, struct sched_param* param)
{
    int Priority;

    if (thread == 0 || !NT_SUCCESS(ThreadGetPriority(((PNTCRT_PTHREAD)thread)->Handle, &Priority)))
    {
        return ESRCH;
    }
    *pol = SCHED_OTHER;
    param->sched_priority = Priority;
    return 0;
}

NTCRT_API int pthread_setschedparam(pthread_t thread, int pol, const struct sched_param* param)
{
    if (thread == 0)
    {
        return ESRCH;
    }
    if (pol < SCHED_MIN || pol > SCHED_MAX || param->sched_priority < THREAD_PRIORITY_IDLE ||
        param->sched_priority > THREAD_PRIORITY_TIME_CRITICAL)
    {
        return EINVAL;
    }
    /* SCHED_FIFO and SCHED_RR are not those of Windows, as pthread_attr_setschedpolicy */
    if (pol != SCHED_OTHER)
    {
        return ENOTSUP;
    }
    return NT_SUCCESS(ThreadSetPriority(((PNTCRT_PTHREAD)thread)->Handle, param->sched_priority)) ? 0 : EPERM;
}

/* Attributes */

NTCRT_API int pthread_attr_init(pthread_attr_t* attr)
{
    attr->p_state = PTHREAD_DEFAULT_ATTR;
    attr->stack = NULL;
    attr->s_size = 0;
    attr->param.sched_priority = 0;
    return 0;
}

NTCRT_API int pthread_attr_destroy(pthread_attr_t* attr)
{
    UNREFERENCED_PARAMETER(attr);
    return 0;
}

NTCRT_API unsigned _pthread_get_state(const pthread_attr_t* attr, unsigned flag)
{
    return attr->p_state & flag;
}

NTCRT_API int _pthread_set_state(pthread_attr_t* attr, unsigned flag, unsigned val)
{
    if ((~flag & val) != 0)
    {
        return EINVAL;
    }
    attr->p_state = (attr->p_state & ~flag) | val;
    return 0;
}

NTCRT_API int pthread_attr_setdetachstate(pthread_attr_t* a, int flag)
{
    if (flag != PTHREAD_CREATE_JOINABLE && flag != PTHREAD_CREATE_DETACHED)
    {
        return EINVAL;
    }
    return _pthread_set_state(a, PTHREAD_CREATE_DETACHED, (unsigned)flag);
}

NTCRT_API int pthread_attr_getdetachstate(const pthread_attr_t* a, int* flag)
{
    *flag = (int)_pthread_get_state(a, PTHREAD_CREATE_DETACHED);
    return 0;
}

NTCRT_API int pthread_attr_setinheritsched(pthread_attr_t* a, int flag)
{
    if (flag != PTHREAD_EXPLICIT_SCHED && flag != PTHREAD_INHERIT_SCHED)
    {
        return EINVAL;
    }
    return _pthread_set_state(a, PTHREAD_INHERIT_SCHED, (unsigned)flag);
}

NTCRT_API int pthread_attr_getinheritsched(const pthread_attr_t* a, int* flag)
{
    *flag = (int)_pthread_get_state(a, PTHREAD_INHERIT_SCHED);
    return 0;
}

NTCRT_API int pthread_attr_setscope(pthread_attr_t* a, int flag)
{
    if (flag != PTHREAD_SCOPE_PROCESS && flag != PTHREAD_SCOPE_SYSTEM)
    {
        return EINVAL;
    }
    return _pthread_set_state(a, PTHREAD_SCOPE_SYSTEM, (unsigned)flag);
}

NTCRT_API int pthread_attr_getscope(const pthread_attr_t* a, int* flag)
{
    *flag = (int)_pthread_get_state(a, PTHREAD_SCOPE_SYSTEM);
    return 0;
}

/* The stack address is kept, a thread gets a stack of the size from the system */

NTCRT_API int pthread_attr_getstack(const pthread_attr_t* attr, void** stack, size_t* size)
{
    *stack = attr->stack;
    *size = attr->s_size;
    return 0;
}

NTCRT_API int pthread_attr_setstack(pthread_attr_t* attr, void* stack, size_t size)
{
    attr->stack = stack;
    attr->s_size = size;
    return 0;
}

NTCRT_API int pthread_attr_getstackaddr(const pthread_attr_t* attr, void** stack)
{
    *stack = attr->stack;
    return 0;
}

NTCRT_API int pthread_attr_setstackaddr(pthread_attr_t* attr, void* stack)
{
    attr->stack = stack;
    return 0;
}

NTCRT_API int pthread_attr_getstacksize(const pthread_attr_t* attr, size_t* size)
{
    *size = attr->s_size;
    return 0;
}

NTCRT_API int pthread_attr_setstacksize(pthread_attr_t* attr, size_t size)
{
    attr->s_size = size;
    return 0;
}

NTCRT_API int pthread_attr_setschedparam(pthread_attr_t* attr, const struct sched_param* param)
{
    attr->param = *param;
    return 0;
}

NTCRT_API int pthread_attr_getschedparam(const pthread_attr_t* attr, struct sched_param* param)
{
    *param = attr->param;
    return 0;
}

NTCRT_API int pthread_attr_setschedpolicy(pthread_attr_t* attr, int pol)
{
    UNREFERENCED_PARAMETER(attr);
    if (pol < SCHED_MIN || pol > SCHED_MAX)
    {
        return EINVAL;
    }
    return pol == SCHED_OTHER ? 0 : ENOTSUP;
}

NTCRT_API int pthread_attr_getschedpolicy(const pthread_attr_t* attr, int* pol)
{
    UNREFERENCED_PARAMETER(attr);
    *pol = SCHED_OTHER;
    return 0;
}
