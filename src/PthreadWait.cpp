/* Waits by address for the thread functions, on the keyed event of the system (Vista and later) */

#include "NtCRT.h"

/*
 * A waiter queues a node on its stack in the bucket of the address and waits on the keyed event with the node as the
 * key. A waker takes the nodes of the address out of the bucket and releases them, NtReleaseKeyedEvent returns once
 * the waiter waits. A waiter that times out or is alerted takes its node out itself, or if a waker took it first
 * waits for the release it is due. The value is checked under the lock of the bucket, which the waker takes after
 * changing it, so no wake is lost. RtlWaitOnAddress would do this from Windows 8 on.
 *
 * An APC runs in an alertable wait while the node is queued: a wake by the APC skips the node of its thread, whose
 * wait ends with STATUS_USER_APC after it, or the release would wait for the thread itself. The release of an alertable
 * node is timed, since its thread may be in an APC that waits for the waker: two threads posting to each other in an
 * APC, or an APC taking a mutex the waker holds. A waker that times out abandons the node and alerts its thread, whose
 * wait then ends; the waiter that finds its node taken claims the release, and either one that loses the race on the
 * state knows the other has it: an abandoned waiter reports the wake, a claimed release is made in full.
 */

#define NTCRT_WAIT_QUEUED 0
#define NTCRT_WAIT_ABANDONED 1
#define NTCRT_WAIT_CLAIMED 2
#define NTCRT_THREAD_ALERT 0x0004

typedef struct _NTCRT_WAIT_NODE
{
    struct _NTCRT_WAIT_NODE* Next;
    const volatile void* Address;
    DWORD Thread;
    volatile LONG State;
    BOOLEAN Alertable;
} NTCRT_WAIT_NODE;

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

EXTERN_C
NTSTATUS
_NtCRT_Wait(
    _In_ const volatile void* Address,
    _In_ ULONG_PTR Value,
    _In_ ULONG Size,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ BOOLEAN Alertable)
{
    NTCRT_WAIT_BUCKET* Bucket = WaitBucket(Address);
    NTCRT_WAIT_NODE Node = { NULL, Address, _Inline_GetCurrentThreadId(), NTCRT_WAIT_QUEUED, Alertable };
    NTCRT_WAIT_NODE** Link;
    ULONG_PTR Current = 0;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&Bucket->Lock);
    /* The lock orders the read, memcpy reads any object */
    memcpy(&Current, (const void*)Address, Size);
    if (Current != Value)
    {
        RtlReleaseSRWLockExclusive(&Bucket->Lock);
        return STATUS_SUCCESS;
    }
    for (Link = &Bucket->Head; *Link != NULL; Link = &(*Link)->Next)
    {
    }
    *Link = &Node;
    RtlReleaseSRWLockExclusive(&Bucket->Lock);

    Status = NtWaitForKeyedEvent(NULL, &Node, Alertable, Timeout);
    if (Status != STATUS_SUCCESS)
    {
        RtlAcquireSRWLockExclusive(&Bucket->Lock);
        for (Link = &Bucket->Head; *Link != NULL && *Link != &Node; Link = &(*Link)->Next)
        {
        }
        if (*Link != NULL)
        {
            *Link = Node.Next;
            RtlReleaseSRWLockExclusive(&Bucket->Lock);
            return Status;
        }
        RtlReleaseSRWLockExclusive(&Bucket->Lock);
        /* A waker has the node and releases it unless it gave up, a wake taken is reported */
        if (_InterlockedCompareExchange(&Node.State, NTCRT_WAIT_CLAIMED, NTCRT_WAIT_QUEUED) == NTCRT_WAIT_QUEUED)
        {
            NtWaitForKeyedEvent(NULL, &Node, FALSE, NULL);
        }
        Status = STATUS_SUCCESS;
    }
    return Status;
}

static void Release(NTCRT_WAIT_NODE* Node)
{
    LARGE_INTEGER Timeout;
    OBJECT_ATTRIBUTES ObjectAttributes;
    CLIENT_ID ClientId = { NULL, (HANDLE)(ULONG_PTR)Node->Thread };
    HANDLE Handle;

    if (Node->Alertable)
    {
        Timeout.QuadPart = -10000;
        if (NtReleaseKeyedEvent(NULL, Node, FALSE, &Timeout) == STATUS_SUCCESS)
        {
            return;
        }
        InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
        if (NT_SUCCESS(NtOpenThread(&Handle, NTCRT_THREAD_ALERT, &ObjectAttributes, &ClientId)))
        {
            if (_InterlockedCompareExchange(&Node->State, NTCRT_WAIT_ABANDONED, NTCRT_WAIT_QUEUED) ==
                NTCRT_WAIT_QUEUED)
            {
                /* A stray alert later is a spurious wakeup */
                NtAlertThread(Handle);
                NtClose(Handle);
                return;
            }
            NtClose(Handle);
        }
    }
    NtReleaseKeyedEvent(NULL, Node, FALSE, NULL);
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
    DWORD Self = _Inline_GetCurrentThreadId();

    RtlAcquireSRWLockExclusive(&Bucket->Lock);
    while ((Node = *Link) != NULL)
    {
        if (Node->Address == Address && Node->Thread != Self)
        {
            *Link = Node->Next;
            Node->Next = Woken;
            Woken = Node;
            if (!All)
            {
                break;
            }
        } else
        {
            Link = &Node->Next;
        }
    }
    RtlReleaseSRWLockExclusive(&Bucket->Lock);

    /* The node is gone once released or abandoned */
    while ((Node = Woken) != NULL)
    {
        Woken = Node->Next;
        Release(Node);
    }
}
