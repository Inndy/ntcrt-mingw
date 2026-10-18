/* pthread barriers and spin locks, in the word of pthread_barrier_t and pthread_spinlock_t */

#include "NtCRT.h"

#include <pthread.h>

/*
 * A barrier (a pointer) holds the count to wait for in bits 0-21, the threads arrived in bits 22-43 and the
 * generation in bits 44-63, which the last thread increments. Other threads may complete rounds before a woken thread
 * reads the word, its generation differs unless 2^20 rounds passed.
 */
#define BARRIER_COUNT      ((ULONG_PTR)0x3FFFFF)
#define BARRIER_ARRIVED    ((ULONG_PTR)0x3FFFFF << 22)
#define BARRIER_ONE        ((ULONG_PTR)1 << 22)
#define BARRIER_GENERATION (~(ULONG_PTR)0 << 44)
#define BARRIER_NEXT       ((ULONG_PTR)1 << 44)

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

NTCRT_API int pthread_barrier_destroy(pthread_barrier_t* b)
{
    if (((ULONG_PTR)__atomic_load_n(b, __ATOMIC_ACQUIRE) & BARRIER_ARRIVED) != 0)
    {
        return EBUSY;
    }
    *b = NULL;
    return 0;
}

NTCRT_API int pthread_barrier_wait(pthread_barrier_t* b)
{
    void* Old = __atomic_load_n(b, __ATOMIC_ACQUIRE);
    ULONG_PTR Raw;
    ULONG_PTR Next;

    do
    {
        Raw = (ULONG_PTR)Old;
        if ((Raw & BARRIER_COUNT) == 0)
        {
            return EINVAL;
        }
        Next = (Raw & BARRIER_ARRIVED) + BARRIER_ONE == (Raw & BARRIER_COUNT) << 22
                   ? (Raw & ~BARRIER_ARRIVED) + BARRIER_NEXT
                   : Raw + BARRIER_ONE;
    } while (!__atomic_compare_exchange_n(b, &Old, (void*)Next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    if (((Raw ^ Next) & BARRIER_GENERATION) != 0)
    {
        _NtCRT_Wake(b, TRUE);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    /* The arrivals change the word too */
    for (;;)
    {
        Next = (ULONG_PTR)__atomic_load_n(b, __ATOMIC_ACQUIRE);
        if (((Raw ^ Next) & BARRIER_GENERATION) != 0)
        {
            return 0;
        }
        _NtCRT_Wait(b, Next, sizeof(*b), NULL, FALSE);
    }
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
    return *(volatile LONG_PTR*)l == 1 ? EBUSY : 0;
}

NTCRT_API int pthread_spin_lock(pthread_spinlock_t* l)
{
    ULONG Spins = 0;

    while (InterlockedExchange64(l, 1) == 1)
    {
        while (*(volatile LONG_PTR*)l == 1)
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
    if (*(volatile LONG_PTR*)l != 1)
    {
        return EPERM;
    }
    InterlockedExchange64(l, -1);
    return 0;
}
