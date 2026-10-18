/* pthread mutexes, in the word of pthread_mutex_t */

#include "NtCRT.h"

#include <pthread.h>

/*
 * An unlocked mutex is -1 - its type: -1 (PTHREAD_MUTEX_INITIALIZER, also 0), -2 (errorcheck) or -3 (recursive), as
 * the static initializers of winpthreads. A locked one is positive: the type in bits 0-1, bit 2 if threads may wait,
 * the recursion count - 1 in bits 3-30 and the thread id of the owner in bits 31-62.
 */
#define MUTEX_TYPE    ((LONG_PTR)3)
#define MUTEX_WAITERS ((LONG_PTR)4)
#define MUTEX_COUNT   ((LONG_PTR)0x7FFFFFF8)
#define MUTEX_ONE     ((LONG_PTR)8)
#define MUTEX_OWNER   (~(LONG_PTR)0x7FFFFFFF)

static LONG_PTR MutexSelf(void)
{
    return (LONG_PTR)_Inline_GetCurrentThreadId() << 31;
}

/* Lock, until Time of CLOCK_REALTIME if not NULL */
static int MutexLock(pthread_mutex_t* m, const struct timespec* Time)
{
    LONG_PTR Self = MutexSelf();
    LONG_PTR Waiters = 0;
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER Wait = NULL;
    bool Timed = false;

    for (;;)
    {
        LONG_PTR Raw = __atomic_load_n(m, __ATOMIC_RELAXED);

        if (Raw <= 0)
        {
            if (Raw < -3)
            {
                return EINVAL;
            }
            /* After a wait other threads may still wait */
            if (InterlockedCompareExchange64(m, Self | Waiters | (Raw == 0 ? 0 : -1 - Raw), Raw) == Raw)
            {
                return 0;
            }
            continue;
        }
        if ((Raw & MUTEX_OWNER) == Self)
        {
            if ((Raw & MUTEX_TYPE) == PTHREAD_MUTEX_RECURSIVE)
            {
                if ((Raw & MUTEX_COUNT) == MUTEX_COUNT)
                {
                    return EAGAIN;
                }
                InterlockedExchangeAdd64(m, MUTEX_ONE);
                return 0;
            }
            if ((Raw & MUTEX_TYPE) == PTHREAD_MUTEX_ERRORCHECK)
            {
                return EDEADLK;
            }
            /* A normal mutex deadlocks */
        }
        if (Time != NULL && !Timed)
        {
            if (!NTCRT_TIMESPEC_VALID(Time))
            {
                return EINVAL;
            }
            Wait = _NtCRT_Timeout(_NtCRT_TimespecUnits(Time), CLOCK_REALTIME, &Timeout);
            Timed = true;
        }
        /* The flag is set before the wait, a waiter that times out leaves it to the next unlock */
        if ((Raw & MUTEX_WAITERS) == 0)
        {
            if (InterlockedCompareExchange64(m, Raw | MUTEX_WAITERS, Raw) != Raw)
            {
                continue;
            }
            Raw |= MUTEX_WAITERS;
        }
        Waiters = MUTEX_WAITERS;
        if (_NtCRT_Wait(m, (ULONG_PTR)Raw, sizeof(*m), Wait, FALSE) == STATUS_TIMEOUT)
        {
            return ETIMEDOUT;
        }
    }
}

/* Unlock all levels, the count of the levels above one in *Count */
static int MutexUnlock(pthread_mutex_t* m, LONG_PTR* Count)
{
    LONG_PTR Raw = __atomic_load_n(m, __ATOMIC_RELAXED);

    if (Raw <= 0)
    {
        return EPERM;
    }
    /* A normal mutex has no owner to check, any thread may unlock it */
    if ((Raw & MUTEX_TYPE) != PTHREAD_MUTEX_NORMAL && (Raw & MUTEX_OWNER) != MutexSelf())
    {
        return EPERM;
    }
    if (Count != NULL)
    {
        *Count = Raw & MUTEX_COUNT;
    } else if ((Raw & MUTEX_COUNT) != 0)
    {
        InterlockedExchangeAdd64(m, -MUTEX_ONE);
        return 0;
    }
    if (InterlockedExchange64(m, -1 - (Raw & MUTEX_TYPE)) & MUTEX_WAITERS)
    {
        _NtCRT_Wake(m, FALSE);
    }
    return 0;
}

EXTERN_C int _NtCRT_MutexUnlockAll(LONG_PTR* Mutex, LONG_PTR* Count)
{
    return MutexUnlock(Mutex, Count);
}

EXTERN_C void _NtCRT_MutexRelock(LONG_PTR* Mutex, LONG_PTR Count)
{
    MutexLock(Mutex, NULL);
    if (Count != 0)
    {
        InterlockedExchangeAdd64(Mutex, Count);
    }
}

NTCRT_API int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* a)
{
    unsigned Type = a != NULL ? *a & MUTEX_TYPE : PTHREAD_MUTEX_NORMAL;

    if (Type > PTHREAD_MUTEX_RECURSIVE)
    {
        return EINVAL;
    }
    *m = -1 - (LONG_PTR)Type;
    return 0;
}

NTCRT_API int pthread_mutex_destroy(pthread_mutex_t* m)
{
    LONG_PTR Raw = __atomic_load_n(m, __ATOMIC_RELAXED);

    if (Raw > 0)
    {
        return EBUSY;
    }
    return Raw < -3 ? EINVAL : 0;
}

NTCRT_API int pthread_mutex_lock(pthread_mutex_t* m)
{
    return MutexLock(m, NULL);
}

NTCRT_API int pthread_mutex_timedlock(pthread_mutex_t* m, const struct timespec* ts)
{
    return MutexLock(m, ts);
}

NTCRT_API int pthread_mutex_trylock(pthread_mutex_t* m)
{
    LONG_PTR Raw = __atomic_load_n(m, __ATOMIC_RELAXED);
    LONG_PTR Self = MutexSelf();

    if (Raw <= 0)
    {
        if (Raw < -3)
        {
            return EINVAL;
        }
        return InterlockedCompareExchange64(m, Self | (Raw == 0 ? 0 : -1 - Raw), Raw) == Raw ? 0 : EBUSY;
    }
    if ((Raw & MUTEX_TYPE) == PTHREAD_MUTEX_RECURSIVE && (Raw & MUTEX_OWNER) == Self)
    {
        if ((Raw & MUTEX_COUNT) == MUTEX_COUNT)
        {
            return EAGAIN;
        }
        InterlockedExchangeAdd64(m, MUTEX_ONE);
        return 0;
    }
    return EBUSY;
}

NTCRT_API int pthread_mutex_unlock(pthread_mutex_t* m)
{
    return MutexUnlock(m, NULL);
}

/* Attributes: the type in bits 0-1, the protocol in bits 3-4 (PTHREAD_PRIO_*), the priority ceiling in bits 8-15 */

NTCRT_API int pthread_mutexattr_init(pthread_mutexattr_t* a)
{
    *a = PTHREAD_MUTEX_DEFAULT;
    return 0;
}

NTCRT_API int pthread_mutexattr_destroy(pthread_mutexattr_t* a)
{
    UNREFERENCED_PARAMETER(a);
    return 0;
}

NTCRT_API int pthread_mutexattr_gettype(const pthread_mutexattr_t* a, int* type)
{
    *type = (int)(*a & MUTEX_TYPE);
    return 0;
}

NTCRT_API int pthread_mutexattr_settype(pthread_mutexattr_t* a, int type)
{
    if ((unsigned)type > PTHREAD_MUTEX_RECURSIVE)
    {
        return EINVAL;
    }
    *a = (*a & ~(unsigned)MUTEX_TYPE) | (unsigned)type;
    return 0;
}

/* Only process-private objects: the waits are of the process */

NTCRT_API int pthread_mutexattr_getpshared(const pthread_mutexattr_t* a, int* type)
{
    UNREFERENCED_PARAMETER(a);
    *type = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

NTCRT_API int pthread_mutexattr_setpshared(pthread_mutexattr_t* a, int type)
{
    UNREFERENCED_PARAMETER(a);
    if (type == PTHREAD_PROCESS_PRIVATE)
    {
        return 0;
    }
    return type == PTHREAD_PROCESS_SHARED ? ENOTSUP : EINVAL;
}

/* The protocol and the ceiling are kept, they have no effect */

NTCRT_API int pthread_mutexattr_getprotocol(const pthread_mutexattr_t* a, int* type)
{
    *type = (int)(*a & (PTHREAD_PRIO_INHERIT | PTHREAD_PRIO_PROTECT));
    return 0;
}

NTCRT_API int pthread_mutexattr_setprotocol(pthread_mutexattr_t* a, int type)
{
    if (type != PTHREAD_PRIO_NONE && type != PTHREAD_PRIO_INHERIT && type != PTHREAD_PRIO_PROTECT)
    {
        return EINVAL;
    }
    *a = (*a & ~(unsigned)(PTHREAD_PRIO_INHERIT | PTHREAD_PRIO_PROTECT)) | (unsigned)type;
    return 0;
}

NTCRT_API int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t* a, int* prio)
{
    *prio = (signed char)(*a >> 8);
    return 0;
}

NTCRT_API int pthread_mutexattr_setprioceiling(pthread_mutexattr_t* a, int prio)
{
    if (prio < THREAD_PRIORITY_IDLE || prio > THREAD_PRIORITY_TIME_CRITICAL)
    {
        return EINVAL;
    }
    *a = (*a & ~0xFF00U) | ((unsigned)prio & 0xFF) << 8;
    return 0;
}

/* The pointers that WINPTHREADS_USE_DLLIMPORT refers to (__declspec(dllimport) of the winpthreads headers) */
NTCRT_IMPORT_ONLY(pthread_mutex_init, pthread_mutex_init);
NTCRT_IMPORT_ONLY(pthread_mutex_destroy, pthread_mutex_destroy);
NTCRT_IMPORT_ONLY(pthread_mutex_lock, pthread_mutex_lock);
NTCRT_IMPORT_ONLY(pthread_mutex_timedlock, pthread_mutex_timedlock);
NTCRT_IMPORT_ONLY(pthread_mutex_trylock, pthread_mutex_trylock);
NTCRT_IMPORT_ONLY(pthread_mutex_unlock, pthread_mutex_unlock);
NTCRT_IMPORT_ONLY(pthread_mutexattr_init, pthread_mutexattr_init);
NTCRT_IMPORT_ONLY(pthread_mutexattr_destroy, pthread_mutexattr_destroy);
NTCRT_IMPORT_ONLY(pthread_mutexattr_gettype, pthread_mutexattr_gettype);
NTCRT_IMPORT_ONLY(pthread_mutexattr_settype, pthread_mutexattr_settype);
NTCRT_IMPORT_ONLY(pthread_mutexattr_getpshared, pthread_mutexattr_getpshared);
NTCRT_IMPORT_ONLY(pthread_mutexattr_setpshared, pthread_mutexattr_setpshared);
NTCRT_IMPORT_ONLY(pthread_mutexattr_getprotocol, pthread_mutexattr_getprotocol);
NTCRT_IMPORT_ONLY(pthread_mutexattr_setprotocol, pthread_mutexattr_setprotocol);
NTCRT_IMPORT_ONLY(pthread_mutexattr_getprioceiling, pthread_mutexattr_getprioceiling);
NTCRT_IMPORT_ONLY(pthread_mutexattr_setprioceiling, pthread_mutexattr_setprioceiling);
