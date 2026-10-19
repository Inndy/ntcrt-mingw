/* POSIX threads: creation, exit, join, cancellation, attributes and priorities */

#include "NtCRT.h"

#include <pthread.h>

/*
 * A pthread_t points to the NTCRT_PTHREAD of the thread, a TLS slot has it. pthread_create allocates it, the joiner
 * or the thread itself if detached frees it. A thread not created by pthread_create gets a detached one when
 * it asks for it (pthread_self).
 *
 * pthread_exit runs the cleanup handlers and the key destructors, then the thread ends in the TLS callback
 * (_NtCRT_ThreadDetach) after the thread_local destructors of C++ (the callback of libmingw32 precedes it): the
 * destructors of the values set since, then ThreadExit (_NtCRT_PthreadExit), which marks the thread ended and wakes
 * its joiner. The joiner then waits for the handle, signaled after the last callbacks.
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
    volatile LONG Pins; /* Of pthread_cancel, -1 when free */
    DWORD Tid;
    PNTCRT_PARKER Parker;              /* A reference, its event serves as CancelEvent */
    HANDLE CancelEvent;                /* Of the wait for the handle in pthread_join, set by pthread_cancel */
    const volatile void* WaitAddress; /* Of a cancellable wait, for pthread_cancel */
} NTCRT_PTHREAD, *PNTCRT_PTHREAD;

#define THREAD_DETACHED 1
#define THREAD_EXITED   2
#define THREAD_FOREIGN  4 /* Not created by pthread_create */

#define CANCEL_PENDING  1
#define CANCEL_DISABLED 2
#define CANCEL_ASYNC    4 /* Kept, acts as deferred */

static NTCRT_PTHREAD g_FallbackThread = { NULL, NULL, NULL, NULL, NULL, NULL, THREAD_DETACHED | THREAD_FOREIGN, 0,
                                          0,    0,    NULL, NULL, NULL };

/* The TLS slot of the pthread_t of a thread */
static DWORD g_SelfIndex = TLS_OUT_OF_INDEXES;

static bool ThreadSelfSlot(void)
{
    DWORD Index;

    if (__atomic_load_n(&g_SelfIndex, __ATOMIC_ACQUIRE) != TLS_OUT_OF_INDEXES)
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
    DWORD Index = __atomic_load_n(&g_SelfIndex, __ATOMIC_ACQUIRE);

    return Index != TLS_OUT_OF_INDEXES ? (PNTCRT_PTHREAD)_Inline_TlsGetValue2(Index) : NULL;
}

/* By its owner (the thread if detached or foreign, its joiner) or the last pin */
static void ThreadFree(PNTCRT_PTHREAD Thread)
{
    if (InterlockedDecrement(&Thread->Pins) < 0)
    {
        NtClose(Thread->Handle);
        /* Its own, not that of its parker */
        if (Thread->CancelEvent != NULL && (Thread->Parker == NULL || Thread->CancelEvent != Thread->Parker->Event))
        {
            NtClose(Thread->CancelEvent);
        }
        _NtCRT_ParkerRelease(Thread->Parker);
        RtlFreeHeap(NTCRT_HEAP, 0, Thread);
    }
}

static void ThreadExit(void)
{
    PNTCRT_PTHREAD Thread = ThreadSelf();

    if (Thread == NULL)
    {
        return;
    }
    _Inline_TlsSetValue(__atomic_load_n(&g_SelfIndex, __ATOMIC_RELAXED), NULL);
    if ((Thread->State & THREAD_FOREIGN) != 0 || (InterlockedOr(&Thread->State, THREAD_EXITED) & THREAD_DETACHED) != 0)
    {
        ThreadFree(Thread);
        return;
    }
    /* The wake does not read the data, which the joiner (or pthread_detach) may free once it sees the state */
    _NtCRT_Wake(&Thread->State, TRUE);
}

static PNTCRT_PTHREAD CurrentThread(void);

/* A thread gets its pthread_t now, the routine of a once may cancel it */
static _pthread_cleanup** CleanupList(void)
{
    PNTCRT_PTHREAD Thread = CurrentThread();

    return Thread != &g_FallbackThread ? &Thread->Cleanup : NULL;
}

/* Where a cancellable wait of the thread publishes its address, NULL without a pthread_t (none can cancel it) */
static const volatile void* volatile* WaitSlot(void)
{
    PNTCRT_PTHREAD Thread = ThreadSelf();

    return Thread != NULL ? &Thread->WaitAddress : NULL;
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
    _NtCRT_DelayHook = _NtCRT_Delay;
    _NtCRT_WaitSlot = WaitSlot;
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
        Thread->Tid = _Inline_GetCurrentThreadId();
        Thread->Parker = _NtCRT_ParkerSelf();
        NtDuplicateObject(NtCurrentProcess(),
                          NtCurrentThread(),
                          NtCurrentProcess(),
                          &Thread->Handle,
                          0,
                          0,
                          DUPLICATE_SAME_ACCESS);
        if (!ThreadSelfSlot() || !_Inline_TlsSetValue(__atomic_load_n(&g_SelfIndex, __ATOMIC_RELAXED), Thread))
        {
            ThreadFree(Thread);
            return &g_FallbackThread;
        }
    }
    return Thread;
}

static NTSTATUS NTAPI ThreadStart(PVOID Parameter)
{
    PNTCRT_PTHREAD Thread = (PNTCRT_PTHREAD)Parameter;

    /* A return is pthread_exit */
    if (Thread->StartC11 != NULL)
    {
        pthread_exit((void*)(INT_PTR)Thread->StartC11(Thread->Arg));
    }
    pthread_exit(Thread->Start(Thread->Arg));
    __builtin_unreachable();
}

/*
 * TlsSetValue of the slots of pthread_self and of the per-thread data (with the event of the parker) for the thread,
 * still suspended: they can fail only here
 */
static bool ThreadSetSelf(PNTCRT_PTHREAD Thread, DWORD PtdIndex, PNTCRT_PTD Ptd)
{
    THREAD_BASIC_INFORMATION Info;
    DWORD Indexes[2] = { __atomic_load_n(&g_SelfIndex, __ATOMIC_RELAXED), PtdIndex };
    PVOID Values[2] = { Thread, Ptd };
    PVOID* Slots = NULL;

    if (!NT_SUCCESS(NtQueryInformationThread(Thread->Handle, ThreadBasicInformation, &Info, sizeof(Info), NULL)))
    {
        return false;
    }
    for (int i = 0; i < 2 && Indexes[i] != TLS_OUT_OF_INDEXES; i++)
    {
        if (Indexes[i] < TLS_MINIMUM_AVAILABLE)
        {
            Info.TebBaseAddress->TlsSlots[Indexes[i]] = Values[i];
            continue;
        }
        /* The loader frees them at the exit of the thread */
        if (Slots == NULL)
        {
            Slots = (PVOID*)RtlAllocateHeap(RtlProcessHeap(), HEAP_ZERO_MEMORY, TLS_EXPANSION_SLOTS * sizeof(PVOID));
            if (Slots == NULL)
            {
                return false;
            }
            Info.TebBaseAddress->TlsExpansionSlots = Slots;
        }
        Slots[Indexes[i] - TLS_MINIMUM_AVAILABLE] = Values[i];
    }
    return true;
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
    CLIENT_ID ClientId;
    PNTCRT_PTD Ptd = NULL;
    DWORD PtdIndex = TLS_OUT_OF_INDEXES;
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
    /* With events, that of the thread: a reference of its per-thread data and one of pthread_cancel */
    if (!_NtCRT_ParkerCreate(&Thread->Parker, 2))
    {
        goto _Fail;
    }
    if (Thread->Parker != NULL)
    {
        Ptd = (PNTCRT_PTD)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, sizeof(*Ptd));
        if (Ptd == NULL || (PtdIndex = _NtCRT_PtdSlot()) == TLS_OUT_OF_INDEXES)
        {
            goto _Fail;
        }
        Ptd->Parker = Thread->Parker;
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
                                 &ClientId);
    if (!NT_SUCCESS(Status))
    {
        goto _Fail;
    }
    Thread->Tid = (DWORD)(ULONG_PTR)ClientId.UniqueThread;
    if (!ThreadSetSelf(Thread, PtdIndex, Ptd))
    {
        NtTerminateThread(Thread->Handle, STATUS_NO_MEMORY);
        NtClose(Thread->Handle);
        goto _Fail;
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

_Fail:
    /* Both references */
    _NtCRT_ParkerRelease(Thread->Parker);
    _NtCRT_ParkerRelease(Thread->Parker);
    RtlFreeHeap(NTCRT_HEAP, 0, Ptd);
    RtlFreeHeap(NTCRT_HEAP, 0, Thread);
    return EAGAIN;
}

NTCRT_API int pthread_create(pthread_t* th, const pthread_attr_t* attr, void* (*func)(void*), void* arg)
{
    return _NtCRT_ThreadCreate(th, attr, func, NULL, arg);
}

NTCRT_API void pthread_exit(void* res)
{
    PNTCRT_PTHREAD Thread = CurrentThread();
    _pthread_cleanup* Cleanup;

    /* The cleanup handlers and the key destructors are not cancelled, the type is deferred again */
    InterlockedOr(&Thread->Cancel, CANCEL_DISABLED);
    InterlockedAnd(&Thread->Cancel, ~CANCEL_ASYNC);
    while ((Cleanup = Thread->Cleanup) != NULL)
    {
        Thread->Cleanup = Cleanup->next;
        Cleanup->func(Cleanup->arg);
    }
    /* Outside the loader lock, which the TLS callback holds: a destructor may create or join a thread */
    _NtCRT_KeyDestructors();
    /*
     * The last thread of the process ends it with its status, as Windows does: ntdll calls RtlExitUserProcess, whose
     * DLL_PROCESS_DETACH runs the atexit functions and flushes the streams (see Startup.cpp)
     */
    Thread->Result = res;
    RtlExitUserThread((NTSTATUS)(ULONG_PTR)res);
}

NTCRT_API pthread_t pthread_self(void)
{
    return (pthread_t)CurrentThread();
}

NTCRT_API int pthread_equal(pthread_t t1, pthread_t t2)
{
    return t1 == t2;
}

/*
 * The event that pthread_cancel sets for a thread that waits for the handle of another in pthread_join: that of its
 * parker, or one of its own made at its first such wait (NtWaitForAlertByThreadId waits for no handle). NULL if none
 * can be had.
 */
static HANDLE CancelEvent(PNTCRT_PTHREAD Self)
{
    HANDLE Event = Self->Parker != NULL ? Self->Parker->Event : NULL;

    if (Self == &g_FallbackThread)
    {
        return NULL;
    }
    if (Self->CancelEvent == NULL &&
        (Event != NULL || NT_SUCCESS(NtCreateEvent(&Event, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE))))
    {
        __atomic_store_n(&Self->CancelEvent, Event, __ATOMIC_SEQ_CST);
    }
    return Self->CancelEvent;
}

/*
 * Wait for the handle of a thread marked ended, until its last TLS callbacks returned, a cancellation point: with
 * the event, which pthread_cancel sets after the cancellation state, or without 10 ms at a time
 */
static NTSTATUS WaitEnded(HANDLE Handle)
{
    HANDLE Handles[2] = { Handle, CancelEvent(CurrentThread()) };
    LARGE_INTEGER Slice;
    NTSTATUS Status;

    Slice.QuadPart = -100000;
    do
    {
        /* The event was published before the state is read, as pthread_cancel reads it after the state is set */
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        pthread_testcancel();
        Status = Handles[1] != NULL ? NtWaitForMultipleObjects(2, Handles, WaitAny, FALSE, NULL)
                                    : NtWaitForSingleObject(Handle, FALSE, &Slice);
    } while (Status == STATUS_WAIT_1 || Status == STATUS_TIMEOUT);
    return Status;
}

/* Wait for the end of a thread, Timeout 0 for _pthread_tryjoin */
static int ThreadJoin(pthread_t t, void** res, PLARGE_INTEGER Timeout)
{
    PNTCRT_PTHREAD Thread = (PNTCRT_PTHREAD)t;
    LARGE_INTEGER Zero;
    NTSTATUS Status;
    LONG State;

    if (Thread == NULL)
    {
        return ESRCH;
    }
    if (Thread == ThreadSelf())
    {
        return EDEADLK;
    }
    if ((__atomic_load_n(&Thread->State, __ATOMIC_RELAXED) & THREAD_DETACHED) != 0)
    {
        return EINVAL;
    }
    pthread_testcancel();
    /* Until ThreadExit marks the thread ended, a cancellation point */
    while (((State = __atomic_load_n(&Thread->State, __ATOMIC_ACQUIRE)) & THREAD_EXITED) == 0)
    {
        if (_NtCRT_Wait(&Thread->State, (ULONG)State, sizeof(State), Timeout, NTCRT_WAIT_CANCEL) == STATUS_TIMEOUT)
        {
            return EBUSY;
        }
        pthread_testcancel();
    }
    /* The rest of its TLS callbacks, which have mostly returned: those of the program after that of NtCRT may wait */
    Zero.QuadPart = 0;
    Status = NtWaitForSingleObject(Thread->Handle, FALSE, &Zero);
    if (Status == STATUS_TIMEOUT)
    {
        if (Timeout != NULL)
        {
            return EBUSY;
        }
        Status = WaitEnded(Thread->Handle);
    }
    if (Status != STATUS_WAIT_0)
    {
        return ESRCH;
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
 * pthread_cond_wait, pthread_cond_timedwait, sem_wait, sem_timedwait, nanosleep, clock_nanosleep, sleep, usleep,
 * pthread_delay_np).
 * pthread_cancel marks the thread, then ends the wait of these (_NtCRT_CancelWait on the address the wait published
 * before it looks at the mark, never by an alert to a thread that does not wait there) or sets the event of the wait
 * for the handle in pthread_join. A wait with cancellation disabled takes it as a spurious return and waits again.
 */

static bool CancelRequested(void)
{
    PNTCRT_PTHREAD Thread = ThreadSelf();

    return Thread != NULL &&
           (__atomic_load_n(&Thread->Cancel, __ATOMIC_ACQUIRE) & (CANCEL_PENDING | CANCEL_DISABLED)) == CANCEL_PENDING;
}

static void TestCancel(void (*Pass)(void*), void* Object)
{
    if (CancelRequested())
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
    const volatile void* Address;
    HANDLE Event;

    if (Thread == NULL)
    {
        return ESRCH;
    }
    __atomic_store_n(&_NtCRT_TestCancel, &TestCancel, __ATOMIC_RELEASE);
    __atomic_store_n(&_NtCRT_CancelRequested, &CancelRequested, __ATOMIC_RELEASE);
    /* A detached thread may end and free its data at the request, before the unpark */
    InterlockedIncrement(&Thread->Pins);
    InterlockedOr(&Thread->Cancel, CANCEL_PENDING);
    /* Its cancellable wait, which it published before it looks at the cancellation */
    Address = __atomic_load_n(&Thread->WaitAddress, __ATOMIC_SEQ_CST);
    if (Address != NULL)
    {
        _NtCRT_CancelWait(Thread->Tid, Address);
    }
    /* A wait for the handle of another thread in pthread_join */
    Event = __atomic_load_n(&Thread->CancelEvent, __ATOMIC_SEQ_CST);
    if (Event != NULL)
    {
        NtSetEvent(Event, NULL);
    }
    ThreadFree(Thread);
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

/* The pointers that WINPTHREADS_USE_DLLIMPORT refers to (__declspec(dllimport) of the winpthreads headers) */
NTCRT_IMPORT_ONLY(pthread_exit, pthread_exit);
NTCRT_IMPORT_ONLY(pthread_create, pthread_create);
NTCRT_IMPORT_ONLY(pthread_self, pthread_self);
NTCRT_IMPORT_ONLY(pthread_equal, pthread_equal);
NTCRT_IMPORT_ONLY(pthread_testcancel, pthread_testcancel);
NTCRT_IMPORT_ONLY(pthread_join, pthread_join);
NTCRT_IMPORT_ONLY(_pthread_tryjoin, _pthread_tryjoin);
NTCRT_IMPORT_ONLY(pthread_detach, pthread_detach);
NTCRT_IMPORT_ONLY(pthread_cancel, pthread_cancel);
NTCRT_IMPORT_ONLY(pthread_setcancelstate, pthread_setcancelstate);
NTCRT_IMPORT_ONLY(pthread_setcanceltype, pthread_setcanceltype);
NTCRT_IMPORT_ONLY(pthread_kill, pthread_kill);
NTCRT_IMPORT_ONLY(pthread_getclean, pthread_getclean);
NTCRT_IMPORT_ONLY(pthread_gethandle, pthread_gethandle);
NTCRT_IMPORT_ONLY(pthread_getevent, pthread_getevent);
NTCRT_IMPORT_ONLY(pthread_getschedparam, pthread_getschedparam);
NTCRT_IMPORT_ONLY(pthread_setschedparam, pthread_setschedparam);
NTCRT_IMPORT_ONLY(pthread_attr_init, pthread_attr_init);
NTCRT_IMPORT_ONLY(pthread_attr_destroy, pthread_attr_destroy);
NTCRT_IMPORT_ONLY(_pthread_get_state, _pthread_get_state);
NTCRT_IMPORT_ONLY(_pthread_set_state, _pthread_set_state);
NTCRT_IMPORT_ONLY(pthread_attr_setdetachstate, pthread_attr_setdetachstate);
NTCRT_IMPORT_ONLY(pthread_attr_getdetachstate, pthread_attr_getdetachstate);
NTCRT_IMPORT_ONLY(pthread_attr_setinheritsched, pthread_attr_setinheritsched);
NTCRT_IMPORT_ONLY(pthread_attr_getinheritsched, pthread_attr_getinheritsched);
NTCRT_IMPORT_ONLY(pthread_attr_setscope, pthread_attr_setscope);
NTCRT_IMPORT_ONLY(pthread_attr_getscope, pthread_attr_getscope);
NTCRT_IMPORT_ONLY(pthread_attr_getstack, pthread_attr_getstack);
NTCRT_IMPORT_ONLY(pthread_attr_setstack, pthread_attr_setstack);
NTCRT_IMPORT_ONLY(pthread_attr_getstackaddr, pthread_attr_getstackaddr);
NTCRT_IMPORT_ONLY(pthread_attr_setstackaddr, pthread_attr_setstackaddr);
NTCRT_IMPORT_ONLY(pthread_attr_getstacksize, pthread_attr_getstacksize);
NTCRT_IMPORT_ONLY(pthread_attr_setstacksize, pthread_attr_setstacksize);
NTCRT_IMPORT_ONLY(pthread_attr_setschedparam, pthread_attr_setschedparam);
NTCRT_IMPORT_ONLY(pthread_attr_getschedparam, pthread_attr_getschedparam);
NTCRT_IMPORT_ONLY(pthread_attr_setschedpolicy, pthread_attr_setschedpolicy);
NTCRT_IMPORT_ONLY(pthread_attr_getschedpolicy, pthread_attr_getschedpolicy);
