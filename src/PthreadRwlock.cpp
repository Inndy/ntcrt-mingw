/* pthread read-write locks, in the word of pthread_rwlock_t */

#include "NtCRT.h"

#include <pthread.h>

/*
 * Unlocked is -1 (PTHREAD_RWLOCK_INITIALIZER) or 0. Locked is positive, bit 1 if threads may wait: by a writer bit 0
 * and its thread id in bits 31-62, by readers their count in bits 2-62. Readers are preferred, a reader takes the
 * lock whenever no writer holds it, so a thread may take a read lock again while a writer waits.
 */
#define RW_WRITER  ((LONG_PTR)1)
#define RW_WAITERS ((LONG_PTR)2)
#define RW_READER  ((LONG_PTR)4)
#define RW_OWNER   (~(LONG_PTR)0x7FFFFFFF)

static LONG_PTR RwSelf(void)
{
    return (LONG_PTR)_Inline_GetCurrentThreadId() << 31;
}

/* Take the lock to read or write, until Time of CLOCK_REALTIME if not NULL, once if Try */
static int RwLock(pthread_rwlock_t* l, bool Write, const struct timespec* Time, bool Try)
{
    LONG_PTR Self = RwSelf();
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER Wait = NULL;
    bool Timed = false;

    for (;;)
    {
        LONG_PTR Raw = __atomic_load_n(l, __ATOMIC_RELAXED);
        LONG_PTR Next;

        if (Raw <= 0)
        {
            if (Raw < -1)
            {
                return EINVAL;
            }
            Next = Write ? Self | RW_WRITER : RW_READER;
        } else if ((Raw & RW_WRITER) == 0 && !Write)
        {
            Next = Raw + RW_READER;
        } else
        {
            if (Try)
            {
                return EBUSY;
            }
            if ((Raw & RW_WRITER) != 0 && (Raw & RW_OWNER) == Self)
            {
                return EDEADLK;
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
            if ((Raw & RW_WAITERS) == 0)
            {
                if (InterlockedCompareExchange64(l, Raw | RW_WAITERS, Raw) != Raw)
                {
                    continue;
                }
                Raw |= RW_WAITERS;
            }
            if (_NtCRT_Wait(l, (ULONG_PTR)Raw, sizeof(*l), Wait, FALSE) == STATUS_TIMEOUT)
            {
                return ETIMEDOUT;
            }
            continue;
        }
        if (InterlockedCompareExchange64(l, Next, Raw) == Raw)
        {
            return 0;
        }
    }
}

NTCRT_API int pthread_rwlock_init(pthread_rwlock_t* rwlock_, const pthread_rwlockattr_t* attr)
{
    UNREFERENCED_PARAMETER(attr);
    *rwlock_ = -1;
    return 0;
}

NTCRT_API int pthread_rwlock_destroy(pthread_rwlock_t* l)
{
    LONG_PTR Raw = __atomic_load_n(l, __ATOMIC_RELAXED);

    if (Raw > 0)
    {
        return EBUSY;
    }
    return Raw < -1 ? EINVAL : 0;
}

NTCRT_API int pthread_rwlock_rdlock(pthread_rwlock_t* l)
{
    return RwLock(l, false, NULL, false);
}

NTCRT_API int pthread_rwlock_timedrdlock(pthread_rwlock_t* l, const struct timespec* ts)
{
    return RwLock(l, false, ts, false);
}

NTCRT_API int pthread_rwlock_tryrdlock(pthread_rwlock_t* l)
{
    return RwLock(l, false, NULL, true);
}

NTCRT_API int pthread_rwlock_wrlock(pthread_rwlock_t* l)
{
    return RwLock(l, true, NULL, false);
}

NTCRT_API int pthread_rwlock_timedwrlock(pthread_rwlock_t* rwlock, const struct timespec* ts)
{
    return RwLock(rwlock, true, ts, false);
}

NTCRT_API int pthread_rwlock_trywrlock(pthread_rwlock_t* l)
{
    return RwLock(l, true, NULL, true);
}

NTCRT_API int pthread_rwlock_unlock(pthread_rwlock_t* l)
{
    LONG_PTR Raw = __atomic_load_n(l, __ATOMIC_RELAXED);
    LONG_PTR Old;

    if (Raw <= 0)
    {
        return EPERM;
    }
    if ((Raw & RW_WRITER) != 0)
    {
        if ((Raw & RW_OWNER) != RwSelf())
        {
            return EPERM;
        }
        Raw = InterlockedExchange64(l, 0);
    } else
    {
        /* The last reader leaves it unlocked */
        while ((Old = InterlockedCompareExchange64(l, Raw >= 2 * RW_READER ? Raw - RW_READER : 0, Raw)) != Raw)
        {
            Raw = Old;
        }
        if (Raw >= 2 * RW_READER)
        {
            return 0;
        }
    }
    if ((Raw & RW_WAITERS) != 0)
    {
        _NtCRT_Wake(l, TRUE);
    }
    return 0;
}

/* Attributes: only process-private locks */

NTCRT_API int pthread_rwlockattr_init(pthread_rwlockattr_t* a)
{
    *a = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

NTCRT_API int pthread_rwlockattr_destroy(pthread_rwlockattr_t* a)
{
    UNREFERENCED_PARAMETER(a);
    return 0;
}

NTCRT_API int pthread_rwlockattr_getpshared(pthread_rwlockattr_t* a, int* s)
{
    UNREFERENCED_PARAMETER(a);
    *s = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

NTCRT_API int pthread_rwlockattr_setpshared(pthread_rwlockattr_t* a, int s)
{
    UNREFERENCED_PARAMETER(a);
    if (s == PTHREAD_PROCESS_PRIVATE)
    {
        return 0;
    }
    return s == PTHREAD_PROCESS_SHARED ? ENOTSUP : EINVAL;
}
