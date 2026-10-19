/* Waits by address for the thread functions, on a parker per thread */

#include "NtCRT.h"

/*
 * A waiter queues a node on its stack in the bucket of the address and parks its thread until the node is claimed. A
 * waker takes the nodes of the address out of the bucket and marks them claimed under the lock, then unparks their
 * threads and marks the nodes done: it never waits for a waiter. The value is checked under the lock of the bucket,
 * which the waker takes after changing it, so no wake is lost. A waiter that times out or is cancelled takes its node
 * out itself. A claimed waiter leaves when its node is done, as RtlWaitOnAddress of Windows 8 and later does.
 *
 * The parker has a token: an unpark before the park makes the park return at once. The parker is
 * NtWaitForAlertByThreadId and NtAlertThreadByThreadId (Windows 8 and later), found at run time so that a program
 * loads on Windows 7, otherwise an auto-reset event of the thread (Vista and 7), which pthread_create creates and a
 * thread of CreateThread at its first wait. Neither runs user APCs: no wait of NtCRT is alertable. The waits of ntdll
 * (critical sections, SRW locks) take the same alerts, and a critical section deadlocks on one it did not send
 * (measured: threads alerted while they start and allocate hang the heap lock): a thread is alerted only for a node
 * claimed in a wait of NtCRT, which consumes the alert (a park, or one without a time after the node is done) before
 * it leaves. pthread_cancel claims the node of a cancellable wait like a waker (_NtCRT_CancelWait). A waker holds a
 * reference on the event of the thread it wakes, which may end and close it meanwhile; a thread whose event cannot be
 * created polls its node, sleeping 1 ms (a clock tick) at a time.
 */

typedef struct _NTCRT_WAIT_NODE
{
    struct _NTCRT_WAIT_NODE* Next;
    const volatile void* Address;
    PNTCRT_PARKER Parker; /* With events, NULL to poll */
    DWORD Thread;
    ULONG Flags; /* NTCRT_WAIT_CANCEL */
    volatile LONG State;
} NTCRT_WAIT_NODE;

#define NODE_CLAIMED 1 /* Taken out by a waker or pthread_cancel, which unparks the thread */
#define NODE_CANCEL  2 /* By pthread_cancel */
#define NODE_DONE    4 /* Unparked, the node may go */

typedef struct _NTCRT_WAIT_BUCKET
{
    RTL_SRWLOCK Lock;
    NTCRT_WAIT_NODE* Head;
} NTCRT_WAIT_BUCKET;

#define NTCRT_WAIT_BUCKET_BITS 6

static NTCRT_WAIT_BUCKET g_WaitBuckets[1 << NTCRT_WAIT_BUCKET_BITS];

static NTCRT_WAIT_BUCKET* WaitBucket(const volatile void* Address)
{
    return &g_WaitBuckets[((ULONG_PTR)Address >> 3) * 0x9E3779B97F4A7C15ULL >> (64 - NTCRT_WAIT_BUCKET_BITS)];
}

/*
 * A round of a wait for another thread that runs (the holder of a bucket, a waker that unparked): pause, then yield,
 * then sleep without alerts, which lets a thread of a lower priority that was preempted run
 */
EXTERN_C void _NtCRT_Backoff(ULONG* Rounds)
{
    LARGE_INTEGER Delay;

    if (++*Rounds < 1000)
    {
        YieldProcessor();
    } else if (*Rounds < 1100)
    {
        NtYieldExecution();
    } else
    {
        Delay.QuadPart = -10000;
        NtDelayExecution(FALSE, &Delay);
    }
}

/*
 * The lock of a bucket is an SRW lock, which waits by alerts too: a thread whose node may be claimed (the alert of
 * the claim on its way) does not wait in it, which could take that alert and leave its own for later
 */
static void Lock(NTCRT_WAIT_BUCKET* Bucket)
{
    RtlAcquireSRWLockExclusive(&Bucket->Lock);
}

/* Locked, or false once a waker claimed the node, which then needs no lock */
static bool LockClaimed(NTCRT_WAIT_BUCKET* Bucket, NTCRT_WAIT_NODE* Node)
{
    ULONG Rounds = 0;

    while (!RtlTryAcquireSRWLockExclusive(&Bucket->Lock))
    {
        if ((__atomic_load_n(&Node->State, __ATOMIC_ACQUIRE) & NODE_CLAIMED) != 0)
        {
            return false;
        }
        _NtCRT_Backoff(&Rounds);
    }
    return true;
}

static void Unlock(NTCRT_WAIT_BUCKET* Bucket)
{
    RtlReleaseSRWLockExclusive(&Bucket->Lock);
}

EXTERN_C int _NtCRT_Spins(void)
{
    static int s_Spins = -1;
    int Spins = __atomic_load_n(&s_Spins, __ATOMIC_RELAXED);
    PROCESS_BASIC_INFORMATION Info;

    if (Spins < 0)
    {
        Spins = NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessBasicInformation, &Info, sizeof(Info),
                                                     NULL)) &&
                        (Info.AffinityMask & (Info.AffinityMask - 1)) != 0
                    ? 100
                    : 0;
        __atomic_store_n(&s_Spins, Spins, __ATOMIC_RELAXED);
    }
    return Spins;
}

/* Read without the lock: a waiter may come or go meanwhile, the spin is only shorter or longer */
EXTERN_C bool _NtCRT_Parked(const volatile void* Address)
{
    return __atomic_load_n(&WaitBucket(Address)->Head, __ATOMIC_RELAXED) != NULL;
}

/* NTCRT_PARKER_ALERT or NTCRT_PARKER_EVENT once chosen, a test may set the latter before the first wait */
extern "C"
{
    LONG _NtCRT_ParkerMode = 0;
}

static NTSTATUS(NTAPI* g_WaitForAlert)(PVOID Address, PLARGE_INTEGER Timeout);
static NTSTATUS(NTAPI* g_AlertThread)(HANDLE ThreadId);

static LONG ParkerMode(void)
{
    static WCHAR NtdllName[] = L"ntdll.dll";
    UNICODE_STRING Ntdll = { sizeof(NtdllName) - sizeof(WCHAR), sizeof(NtdllName), NtdllName };
    LONG Mode = __atomic_load_n(&_NtCRT_ParkerMode, __ATOMIC_ACQUIRE);
    ANSI_STRING Name;
    PVOID Base, Wait = NULL, Alert = NULL;

    if (Mode != 0)
    {
        return Mode;
    }
    if (NT_SUCCESS(_Inline_LdrGetDllHandle(NULL, NULL, &Ntdll, &Base)))
    {
        _Inline_RtlInitAnsiString(&Name, "NtWaitForAlertByThreadId");
        LdrGetProcedureAddress(Base, &Name, 0, &Wait);
        _Inline_RtlInitAnsiString(&Name, "NtAlertThreadByThreadId");
        LdrGetProcedureAddress(Base, &Name, 0, &Alert);
    }
    Mode = NTCRT_PARKER_EVENT;
    if (Wait != NULL && Alert != NULL)
    {
        __atomic_store_n(&g_WaitForAlert, (decltype(g_WaitForAlert))Wait, __ATOMIC_RELAXED);
        __atomic_store_n(&g_AlertThread, (decltype(g_AlertThread))Alert, __ATOMIC_RELAXED);
        Mode = NTCRT_PARKER_ALERT;
    }
    /* Another thread or a test may have chosen first */
    if (InterlockedCompareExchange(&_NtCRT_ParkerMode, Mode, 0) != 0)
    {
        Mode = __atomic_load_n(&_NtCRT_ParkerMode, __ATOMIC_ACQUIRE);
    }
    return Mode;
}

/* The event of a thread is in its per-thread data, PARKER_NONE if it could not be created */
#define PARKER_NONE ((PNTCRT_PARKER)1)

EXTERN_C bool _NtCRT_ParkerCreate(PNTCRT_PARKER* Parker, LONG Refs)
{
    PNTCRT_PARKER New;

    *Parker = NULL;
    if (ParkerMode() != NTCRT_PARKER_EVENT)
    {
        return true;
    }
    /* Before the per-thread data holds a parker */
    __atomic_store_n(&_NtCRT_ParkerExit, &_NtCRT_ParkerRelease, __ATOMIC_RELEASE);
    New = (PNTCRT_PARKER)RtlAllocateHeap(NTCRT_HEAP, 0, sizeof(*New));
    if (New == NULL)
    {
        return false;
    }
    if (!NT_SUCCESS(NtCreateEvent(&New->Event, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE)))
    {
        RtlFreeHeap(NTCRT_HEAP, 0, New);
        return false;
    }
    New->Refs = Refs;
    *Parker = New;
    return true;
}

EXTERN_C void _NtCRT_ParkerRelease(PNTCRT_PARKER Parker)
{
    if (Parker > PARKER_NONE && InterlockedDecrement(&Parker->Refs) == 0)
    {
        NtClose(Parker->Event);
        RtlFreeHeap(NTCRT_HEAP, 0, Parker);
    }
}

/* With events, that of the thread (a thread of CreateThread creates it now), NULL to poll */
static PNTCRT_PARKER ParkerSelf(void)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();

    /* Shared by the threads without their own */
    if (Ptd == &_NtCRT_FallbackPtd)
    {
        return NULL;
    }
    if (Ptd->Parker == NULL && !_NtCRT_ParkerCreate(&Ptd->Parker, 1))
    {
        Ptd->Parker = PARKER_NONE;
    }
    return Ptd->Parker != PARKER_NONE ? Ptd->Parker : NULL;
}

EXTERN_C PNTCRT_PARKER _NtCRT_ParkerSelf(void)
{
    PNTCRT_PARKER Parker = ParkerMode() == NTCRT_PARKER_EVENT ? ParkerSelf() : NULL;

    if (Parker != NULL)
    {
        InterlockedIncrement(&Parker->Refs);
    }
    return Parker;
}

/* Unpark the thread of a claimed node (taken out under the lock), then mark it done: it may go then */
static void Release(NTCRT_WAIT_NODE* Node)
{
    DWORD Thread = Node->Thread;
    PNTCRT_PARKER Parker = Node->Parker;

    if (__atomic_load_n(&_NtCRT_ParkerMode, __ATOMIC_RELAXED) == NTCRT_PARKER_ALERT)
    {
        __atomic_load_n(&g_AlertThread, __ATOMIC_RELAXED)((HANDLE)(ULONG_PTR)Thread);
    } else if (Parker != NULL)
    {
        NtSetEvent(Parker->Event, NULL);
    }
    _NtCRT_ParkerRelease(Parker);
    __atomic_fetch_or(&Node->State, NODE_DONE, __ATOMIC_RELEASE);
}

/*
 * Park until unparked or Timeout (an NT timeout, NULL for none), STATUS_TIMEOUT or not. Without an event the thread
 * sleeps a millisecond at most and returns STATUS_ALERTED unless the time passed.
 */
static NTSTATUS Park(const volatile void* Address, PNTCRT_PARKER Parker, PLARGE_INTEGER Timeout)
{
    LARGE_INTEGER Delay;

    if (__atomic_load_n(&_NtCRT_ParkerMode, __ATOMIC_RELAXED) == NTCRT_PARKER_ALERT)
    {
        return __atomic_load_n(&g_WaitForAlert, __ATOMIC_RELAXED)((PVOID)Address, Timeout);
    }
    if (Parker != NULL)
    {
        return NtWaitForSingleObject(Parker->Event, FALSE, Timeout);
    }
    Delay.QuadPart = -10000;
    if (Timeout != NULL)
    {
        LONGLONG Left = Timeout->QuadPart;

        if (Left > 0)
        {
            LARGE_INTEGER Now;

            NtQuerySystemTime(&Now);
            Left = Now.QuadPart - Left;
        }
        if (Left >= 0)
        {
            return STATUS_TIMEOUT;
        }
        if (Left > Delay.QuadPart)
        {
            Delay.QuadPart = Left;
        }
    }
    NtDelayExecution(FALSE, &Delay);
    return STATUS_ALERTED;
}

/*
 * The node was claimed: wait until its waker unparked the thread and marked it done, then consume the alert unless
 * the last park took it (Alerted), which no other wait of the thread may see. The state of the node.
 */
static LONG Claimed(NTCRT_WAIT_NODE* Node, bool Alerted)
{
    LARGE_INTEGER Zero;
    ULONG Rounds = 0;
    LONG State;

    while (((State = __atomic_load_n(&Node->State, __ATOMIC_ACQUIRE)) & NODE_DONE) == 0)
    {
        _NtCRT_Backoff(&Rounds);
    }
    if (!Alerted && __atomic_load_n(&_NtCRT_ParkerMode, __ATOMIC_RELAXED) == NTCRT_PARKER_ALERT)
    {
        Zero.QuadPart = 0;
        __atomic_load_n(&g_WaitForAlert, __ATOMIC_RELAXED)((PVOID)Node->Address, &Zero);
    }
    return State;
}

EXTERN_C
NTSTATUS
_NtCRT_Wait(
    _In_ const volatile void* Address,
    _In_ ULONG_PTR Value,
    _In_ ULONG Size,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ ULONG Flags)
{
    NTCRT_WAIT_BUCKET* Bucket = WaitBucket(Address);
    LONG Mode = ParkerMode();
    NTCRT_WAIT_NODE Node = { NULL, Address, Mode == NTCRT_PARKER_EVENT ? ParkerSelf() : NULL,
                             _Inline_GetCurrentThreadId(), Flags, 0 };
    const volatile void* volatile* Slot = NULL;
    const volatile void* volatile* (*GetSlot)(void);
    bool (*Cancelled)(void);
    NTCRT_WAIT_NODE** Link;
    LARGE_INTEGER Left;
    PLARGE_INTEGER Wait = Timeout;
    LONGLONG Deadline = 0;
    ULONG_PTR Current;
    NTSTATUS Status = STATUS_TIMEOUT;
    LONG State;

    /*
     * A relative time is counted from now on the performance counter (the interrupt time advances by ticks, a read
     * may be a tick old), a spurious return parks for the rest; past the end of the clock never
     */
    if (Timeout != NULL && Timeout->QuadPart < 0)
    {
        LONGLONG Now = _NtCRT_MonotonicTime();

        Wait = Timeout->QuadPart < Now - MAXLONGLONG ? NULL : &Left;
        Deadline = Wait != NULL ? Now - Timeout->QuadPart : 0;
    }
    /*
     * A cancellable wait publishes its address for pthread_cancel, which sets the cancellation, then reads it: the
     * thread looks at the cancellation after it published the address, so one of them sees the other
     */
    GetSlot = __atomic_load_n(&_NtCRT_WaitSlot, __ATOMIC_ACQUIRE);
    if ((Flags & NTCRT_WAIT_CANCEL) != 0 && GetSlot != NULL && (Slot = GetSlot()) != NULL)
    {
        __atomic_store_n(Slot, Address, __ATOMIC_SEQ_CST);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
    }
    Lock(Bucket);
    /* The lock orders the read of the word, 32 or 64 bits */
    Current = Size == sizeof(LONG) ? (ULONG)__atomic_load_n((const volatile LONG*)Address, __ATOMIC_RELAXED)
                                   : (ULONG_PTR)__atomic_load_n((const volatile LONG_PTR*)Address, __ATOMIC_RELAXED);
    if (Current != Value)
    {
        Unlock(Bucket);
        Status = STATUS_SUCCESS;
        goto _Leave;
    }
    for (Link = &Bucket->Head; *Link != NULL; Link = &(*Link)->Next)
    {
    }
    *Link = &Node;
    Unlock(Bucket);

    for (;;)
    {
        /* Before each park: pthread_cancel claims no node that the thread had not queued yet */
        Cancelled = __atomic_load_n(&_NtCRT_CancelRequested, __ATOMIC_ACQUIRE);
        if ((Flags & NTCRT_WAIT_CANCEL) != 0 && Cancelled != NULL && Cancelled())
        {
            Status = STATUS_CANCELLED;
            break;
        }
        /* A relative wait may end before its time (with a finer timer resolution): it waits for the rest */
        if (Wait == &Left)
        {
            Left.QuadPart = _NtCRT_MonotonicTime() - Deadline;
            if (Left.QuadPart >= 0)
            {
                Status = STATUS_TIMEOUT;
                break;
            }
        }
        Status = Park(Address, Node.Parker, Wait);
        if ((__atomic_load_n(&Node.State, __ATOMIC_ACQUIRE) & NODE_CLAIMED) != 0)
        {
            State = Claimed(&Node, Status == STATUS_ALERTED);
            goto _Claimed;
        }
        if (Status == STATUS_TIMEOUT && Wait != &Left)
        {
            break;
        }
    }

    if (LockClaimed(Bucket, &Node))
    {
        for (Link = &Bucket->Head; *Link != NULL && *Link != &Node; Link = &(*Link)->Next)
        {
        }
        if (*Link != NULL)
        {
            *Link = Node.Next;
            Unlock(Bucket);
            goto _Leave;
        }
        Unlock(Bucket);
    }
    /* A waker or pthread_cancel took the node: the wake is reported */
    State = Claimed(&Node, false);

_Claimed:
    /* A cancellation that the thread disabled after pthread_cancel read its address is a spurious wake */
    Status = STATUS_SUCCESS;
    if ((State & NODE_CANCEL) != 0)
    {
        Cancelled = __atomic_load_n(&_NtCRT_CancelRequested, __ATOMIC_ACQUIRE);
        Status = Cancelled != NULL && Cancelled() ? STATUS_CANCELLED : STATUS_SUCCESS;
    }
_Leave:
    if (Slot != NULL)
    {
        __atomic_store_n(Slot, NULL, __ATOMIC_RELAXED);
    }
    return Status;
}

EXTERN_C
void
_NtCRT_Wake(
    _In_ const volatile void* Address,
    _In_ BOOL All)
{
    NTCRT_WAIT_BUCKET* Bucket = WaitBucket(Address);
    NTCRT_WAIT_NODE* Woken = NULL;
    NTCRT_WAIT_NODE** Link = &Bucket->Head;
    NTCRT_WAIT_NODE* Node;

    Lock(Bucket);
    while ((Node = *Link) != NULL)
    {
        if (Node->Address == Address)
        {
            *Link = Node->Next;
            Node->Next = Woken;
            Woken = Node;
            __atomic_store_n(&Node->State, NODE_CLAIMED, __ATOMIC_RELAXED);
            if (Node->Parker != NULL)
            {
                InterlockedIncrement(&Node->Parker->Refs);
            }
            if (!All)
            {
                break;
            }
        } else
        {
            Link = &Node->Next;
        }
    }
    Unlock(Bucket);

    /* A node is gone once done: the next is read before */
    while ((Node = Woken) != NULL)
    {
        Woken = Node->Next;
        Release(Node);
    }
}

/* A waker takes the nodes it claims out: a woken thread is not waiting */
EXTERN_C bool _NtCRT_Waiting(const volatile void* Address)
{
    NTCRT_WAIT_BUCKET* Bucket = WaitBucket(Address);
    NTCRT_WAIT_NODE* Node;

    Lock(Bucket);
    for (Node = Bucket->Head; Node != NULL && Node->Address != Address; Node = Node->Next)
    {
    }
    Unlock(Bucket);
    return Node != NULL;
}

/* pthread_cancel: end the cancellable wait of Thread on Address, if it waits there */
EXTERN_C void _NtCRT_CancelWait(DWORD Thread, const volatile void* Address)
{
    NTCRT_WAIT_BUCKET* Bucket = WaitBucket(Address);
    NTCRT_WAIT_NODE** Link;
    NTCRT_WAIT_NODE* Node;

    Lock(Bucket);
    for (Link = &Bucket->Head; (Node = *Link) != NULL; Link = &Node->Next)
    {
        if (Node->Address == Address && Node->Thread == Thread && (Node->Flags & NTCRT_WAIT_CANCEL) != 0)
        {
            *Link = Node->Next;
            __atomic_store_n(&Node->State, NODE_CLAIMED | NODE_CANCEL, __ATOMIC_RELAXED);
            if (Node->Parker != NULL)
            {
                InterlockedIncrement(&Node->Parker->Refs);
            }
            break;
        }
    }
    Unlock(Bucket);
    if (Node != NULL)
    {
        Release(Node);
    }
}

/*
 * The sleeps of a program that can cancel threads, a cancellable wait on an address of their own: STATUS_SUCCESS when
 * the time passed, else STATUS_ALERTED
 */
EXTERN_C NTSTATUS _NtCRT_Delay(PLARGE_INTEGER Timeout)
{
    LONG Never = 0;

    return _NtCRT_Wait(&Never, 0, sizeof(Never), Timeout, NTCRT_WAIT_CANCEL) == STATUS_TIMEOUT ? STATUS_SUCCESS
                                                                                               : STATUS_ALERTED;
}
