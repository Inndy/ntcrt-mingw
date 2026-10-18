/* pthread barriers and spin locks, in the word of pthread_barrier_t and pthread_spinlock_t */

#include "NtCRT.h"

#include <pthread.h>

/*
 * A barrier (a pointer) holds the count to wait for in bits 0-20, the threads arrived in bits 21-41, the threads
 * released that have not left in bits 42-62 and the generation in bit 63, which the last thread flips. The last
 * thread of a round waits until those of the previous round left, so a released thread sees the generation flipped
 * however long it waits to read the word.
 */
#define BARRIER_COUNT      ((ULONG_PTR)0x1FFFFF)
#define BARRIER_ARRIVED    (BARRIER_COUNT << 21)
#define BARRIER_ONE        ((ULONG_PTR)1 << 21)
#define BARRIER_RELEASED   (BARRIER_COUNT << 42)
#define BARRIER_LEFT       ((ULONG_PTR)1 << 42)
#define BARRIER_GENERATION ((ULONG_PTR)1 << 63)

NTCRT_API int pthread_barrier_init(pthread_barrier_t* b, const void* attr, unsigned int count)
{
    UNREFERENCED_PARAMETER(attr);
    if (count == 0 || count > BARRIER_COUNT)
    {
        return EINVAL;
    }
    *b = (void*)(ULONG_PTR)count;
    return 0;
}

/* A destroyed barrier is NULL, invalid as one never initialized (count 0). The released threads leave first. */
NTCRT_API int pthread_barrier_destroy(pthread_barrier_t* b)
{
    ULONG_PTR Raw;

    for (;;)
    {
        Raw = (ULONG_PTR)__atomic_load_n(b, __ATOMIC_ACQUIRE);
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
        _NtCRT_Wait(b, Raw, sizeof(*b), NULL, FALSE);
    }
    *b = NULL;
    return 0;
}

NTCRT_API int pthread_barrier_wait(pthread_barrier_t* b)
{
    void* Old = __atomic_load_n(b, __ATOMIC_ACQUIRE);
    ULONG_PTR Raw;
    ULONG_PTR Next;
    ULONG_PTR Count;

    for (;;)
    {
        Raw = (ULONG_PTR)Old;
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
            _NtCRT_Wait(b, Raw, sizeof(*b), NULL, FALSE);
            Old = __atomic_load_n(b, __ATOMIC_ACQUIRE);
            continue;
        }
        if (__atomic_compare_exchange_n(b, &Old, (void*)Next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
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
        if ((((ULONG_PTR)Old ^ Raw) & BARRIER_GENERATION) != 0)
        {
            break;
        }
        _NtCRT_Wait(b, (ULONG_PTR)Old, sizeof(*b), NULL, FALSE);
    }
    do
    {
        Next = (ULONG_PTR)Old - BARRIER_LEFT;
    } while (!__atomic_compare_exchange_n(b, &Old, (void*)Next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
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

    while (InterlockedExchange64(l, 1) == 1)
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
    return InterlockedExchange64(l, 1) == 1 ? EBUSY : 0;
}

NTCRT_API int pthread_spin_unlock(pthread_spinlock_t* l)
{
    if (__atomic_load_n(l, __ATOMIC_RELAXED) != 1)
    {
        return EPERM;
    }
    InterlockedExchange64(l, -1);
    return 0;
}
