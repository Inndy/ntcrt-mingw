/* pthread barriers and spin locks, in the word of pthread_barrier_t (or a block it points to) and pthread_spinlock_t */

#include "NtCRT.h"

#include <pthread.h>

/*
 * A barrier (a pointer) holds the count to wait for in bits 0-20, the threads arrived in bits 21-41, the threads
 * released that have not left in bits 42-62 and the generation in bit 63, which the last thread flips. The last
 * thread of a round waits until those of the previous round left, so a released thread sees the generation flipped
 * however long it waits to read the word. On x86 the word has 32 bits: it points to the state in the heap,
 * as in winpthreads.
 */
#define BARRIER_COUNT      ((ULONG64)0x1FFFFF)
#define BARRIER_ARRIVED    (BARRIER_COUNT << 21)
#define BARRIER_ONE        ((ULONG64)1 << 21)
#define BARRIER_RELEASED   (BARRIER_COUNT << 42)
#define BARRIER_LEFT       ((ULONG64)1 << 42)
#define BARRIER_GENERATION ((ULONG64)1 << 63)

typedef volatile ULONG64 BARRIER_STATE;

/* The state, NULL for a barrier destroyed (or on x64 never initialized) */
static BARRIER_STATE* BarrierState(pthread_barrier_t* b)
{
    return NTCRT_X86_X64((BARRIER_STATE*)__atomic_load_n(b, __ATOMIC_ACQUIRE), (BARRIER_STATE*)b);
}

NTCRT_API int pthread_barrier_init(pthread_barrier_t* b, const void* attr, unsigned int count)
{
    UNREFERENCED_PARAMETER(attr);
    if (count == 0 || count > BARRIER_COUNT)
    {
        return EINVAL;
    }
#ifdef _WIN64
    *b = (void*)(ULONG_PTR)count;
#else
    *b = RtlAllocateHeap(NTCRT_HEAP, 0, sizeof(ULONG64));
    if (*b == NULL)
    {
        return ENOMEM;
    }
    *(ULONG64*)*b = count;
#endif
    return 0;
}

/* A destroyed barrier is NULL, invalid as one never initialized (count 0). The released threads leave first. */
NTCRT_API int pthread_barrier_destroy(pthread_barrier_t* bp)
{
    BARRIER_STATE* b = BarrierState(bp);
    ULONG64 Raw;

    for (;;)
    {
        Raw = b != NULL ? __atomic_load_n(b, __ATOMIC_ACQUIRE) : 0;
        if ((Raw & BARRIER_COUNT) == 0)
        {
            return EINVAL;
        }
        if ((Raw & BARRIER_ARRIVED) != 0)
        {
            return EBUSY;
        }
        if ((Raw & BARRIER_RELEASED) == 0)
        {
            break;
        }
        _NtCRT_Wait(b, Raw, sizeof(*b), NULL, 0);
    }
#ifndef _WIN64
    RtlFreeHeap(NTCRT_HEAP, 0, (void*)b);
#endif
    *bp = NULL;
    return 0;
}

NTCRT_API int pthread_barrier_wait(pthread_barrier_t* bp)
{
    BARRIER_STATE* b = BarrierState(bp);
    ULONG64 Old = b != NULL ? __atomic_load_n(b, __ATOMIC_ACQUIRE) : 0;
    ULONG64 Raw;
    ULONG64 Next;
    ULONG64 Count;

    for (;;)
    {
        Raw = Old;
        Count = Raw & BARRIER_COUNT;
        if (Count == 0)
        {
            return EINVAL;
        }
        if ((Raw & BARRIER_ARRIVED) + BARRIER_ONE != Count << 21)
        {
            Next = Raw + BARRIER_ONE;
        } else if ((Raw & BARRIER_RELEASED) == 0)
        {
            Next = (~Raw & BARRIER_GENERATION) | (Count - 1) << 42 | Count;
        } else
        {
            /* The last, until the previous round left */
            _NtCRT_Wait(b, Raw, sizeof(*b), NULL, 0);
            Old = __atomic_load_n(b, __ATOMIC_ACQUIRE);
            continue;
        }
        if (__atomic_compare_exchange_n(b, &Old, Next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
            break;
        }
    }
    if (((Raw ^ Next) & BARRIER_GENERATION) != 0)
    {
        _NtCRT_Wake(b, TRUE);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    /* The arrivals and the threads leaving change the word too */
    for (;;)
    {
        Old = __atomic_load_n(b, __ATOMIC_ACQUIRE);
        if (((Old ^ Raw) & BARRIER_GENERATION) != 0)
        {
            break;
        }
        _NtCRT_Wait(b, Old, sizeof(*b), NULL, 0);
    }
    do
    {
        Next = Old - BARRIER_LEFT;
    } while (!__atomic_compare_exchange_n(b, &Old, Next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    /* The last to leave wakes the last of the next round or pthread_barrier_destroy */
    if ((Next & BARRIER_RELEASED) == 0)
    {
        _NtCRT_Wake(b, TRUE);
    }
    return 0;
}

NTCRT_API int pthread_barrierattr_init(void** attr)
{
    *attr = NULL;
    return 0;
}

NTCRT_API int pthread_barrierattr_destroy(void** attr)
{
    UNREFERENCED_PARAMETER(attr);
    return 0;
}

NTCRT_API int pthread_barrierattr_setpshared(void** attr, int s)
{
    UNREFERENCED_PARAMETER(attr);
    if (s == PTHREAD_PROCESS_PRIVATE)
    {
        return 0;
    }
    return s == PTHREAD_PROCESS_SHARED ? ENOTSUP : EINVAL;
}

NTCRT_API int pthread_barrierattr_getpshared(void** attr, int* s)
{
    UNREFERENCED_PARAMETER(attr);
    *s = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

/* A spin lock is 1 when locked, -1 (PTHREAD_SPINLOCK_INITIALIZER) or 0 when not. It works across processes too. */

NTCRT_API int pthread_spin_init(pthread_spinlock_t* l, int pshared)
{
    UNREFERENCED_PARAMETER(pshared);
    *l = -1;
    return 0;
}

NTCRT_API int pthread_spin_destroy(pthread_spinlock_t* l)
{
    return __atomic_load_n(l, __ATOMIC_RELAXED) == 1 ? EBUSY : 0;
}

NTCRT_API int pthread_spin_lock(pthread_spinlock_t* l)
{
    ULONG Spins = 0;

    while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE) == 1)
    {
        while (__atomic_load_n(l, __ATOMIC_RELAXED) == 1)
        {
            if (++Spins % 1024 == 0)
            {
                NtYieldExecution();
            } else
            {
                YieldProcessor();
            }
        }
    }
    return 0;
}

NTCRT_API int pthread_spin_trylock(pthread_spinlock_t* l)
{
    return __atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE) == 1 ? EBUSY : 0;
}

NTCRT_API int pthread_spin_unlock(pthread_spinlock_t* l)
{
    if (__atomic_load_n(l, __ATOMIC_RELAXED) != 1)
    {
        return EPERM;
    }
    __atomic_store_n(l, -1, __ATOMIC_RELEASE);
    return 0;
}

/* The pointers that WINPTHREADS_USE_DLLIMPORT refers to (__declspec(dllimport) of the winpthreads headers) */
NTCRT_IMPORT_ONLY(pthread_barrier_init, pthread_barrier_init);
NTCRT_IMPORT_ONLY(pthread_barrier_destroy, pthread_barrier_destroy);
NTCRT_IMPORT_ONLY(pthread_barrier_wait, pthread_barrier_wait);
NTCRT_IMPORT_ONLY(pthread_barrierattr_init, pthread_barrierattr_init);
NTCRT_IMPORT_ONLY(pthread_barrierattr_destroy, pthread_barrierattr_destroy);
NTCRT_IMPORT_ONLY(pthread_barrierattr_setpshared, pthread_barrierattr_setpshared);
NTCRT_IMPORT_ONLY(pthread_barrierattr_getpshared, pthread_barrierattr_getpshared);
NTCRT_IMPORT_ONLY(pthread_spin_init, pthread_spin_init);
NTCRT_IMPORT_ONLY(pthread_spin_destroy, pthread_spin_destroy);
NTCRT_IMPORT_ONLY(pthread_spin_lock, pthread_spin_lock);
NTCRT_IMPORT_ONLY(pthread_spin_trylock, pthread_spin_trylock);
NTCRT_IMPORT_ONLY(pthread_spin_unlock, pthread_spin_unlock);
