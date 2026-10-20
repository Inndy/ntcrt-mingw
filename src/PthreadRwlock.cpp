/* pthread read-write locks, in the word of pthread_rwlock_t */

#include "NtCRT.h"

#include <pthread.h>

/*
 * Unlocked is -1 (PTHREAD_RWLOCK_INITIALIZER) or 0. Locked, bit 1 if threads may wait: by a writer bit 0 and its thread
 * id in bits 31-62, by readers their count in bits 2-62. On x86, where the word has 32 bits, the writer is the
 * address of its TEB (a page) and the readers count in bits 2-31: a locked word may be negative there. Readers are
 * preferred, a reader takes the lock whenever no writer holds it, so a thread may take a read lock again while a
 * writer waits.
 */
#define RW_WRITER  ((LONG_PTR)1)
#define RW_WAITERS ((LONG_PTR)2)
#define RW_READER  ((LONG_PTR)4)
#define RW_OWNER   (~(LONG_PTR)NTCRT_X86_X64(0xFFF, 0x7FFFFFFF))

static LONG_PTR RwSelf(void)
{
    return NTCRT_X86_X64((LONG_PTR)NtCurrentTeb(), (LONG_PTR)_Inline_GetCurrentThreadId() << 31);
}

static bool RwUnlocked(LONG_PTR Raw)
{
    return NTCRT_X86_X64(Raw == 0 || Raw == -1, Raw <= 0);
}

/* Not a lock, which x86 cannot tell */
static bool RwInvalid([[maybe_unused]] LONG_PTR Raw)
{
    return NTCRT_X86_X64(false, Raw < -1);
}

/* Take the lock to read or write, until Time of CLOCK_REALTIME if not NULL, once if Try */
static int RwLock(pthread_rwlock_t* l, bool Write, const struct timespec* Time, bool Try)
{
    LONG_PTR Self = RwSelf();
    int Spins = Try ? 0 : _NtCRT_Spins();
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER Wait = NULL;
    bool Timed = false;

    for (;;)
    {
        LONG_PTR Raw = __atomic_load_n(l, __ATOMIC_RELAXED);
        LONG_PTR Next;

        if (RwInvalid(Raw))
        {
            return EINVAL;
        }
        if (RwUnlocked(Raw))
        {
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
            if (Spins > 0 && (Raw & RW_WAITERS) == 0)
            {
                Spins--;
                YieldProcessor();
                continue;
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
                if (__sync_val_compare_and_swap(l, Raw, Raw | RW_WAITERS) != Raw)
                {
                    continue;
                }
                Raw |= RW_WAITERS;
            }
            if (_NtCRT_Wait(l, (ULONG_PTR)Raw, sizeof(*l), Wait, 0) == STATUS_TIMEOUT)
            {
                return ETIMEDOUT;
            }
            continue;
        }
        if (__sync_val_compare_and_swap(l, Raw, Next) == Raw)
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

    if (RwInvalid(Raw))
    {
        return EINVAL;
    }
    return RwUnlocked(Raw) ? 0 : EBUSY;
}

NTCRT_API int pthread_rwlock_rdlock(pthread_rwlock_t* l)
{
    return RwLock(l, false, NULL, false);
}

NTCRT_API int _NtCRT_pthread_rwlock_timedrdlock(pthread_rwlock_t* l, const struct timespec* ts)
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

NTCRT_API int _NtCRT_pthread_rwlock_timedwrlock(pthread_rwlock_t* rwlock, const struct timespec* ts)
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

    if (RwUnlocked(Raw) || RwInvalid(Raw))
    {
        return EPERM;
    }
    if ((Raw & RW_WRITER) != 0)
    {
        if ((Raw & RW_OWNER) != RwSelf())
        {
            return EPERM;
        }
        Raw = __atomic_exchange_n(l, 0, __ATOMIC_SEQ_CST);
    } else
    {
        /* The last reader leaves it unlocked */
        while ((Old = __sync_val_compare_and_swap(l, Raw, (ULONG_PTR)Raw >= 2 * RW_READER ? Raw - RW_READER : 0)) !=
               Raw)
        {
            Raw = Old;
        }
        if ((ULONG_PTR)Raw >= 2 * RW_READER)
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

/* The pointers that WINPTHREADS_USE_DLLIMPORT refers to (__declspec(dllimport) of the winpthreads headers) */
NTCRT_IMPORT_ONLY(pthread_rwlock_init, pthread_rwlock_init);
NTCRT_IMPORT_ONLY(pthread_rwlock_destroy, pthread_rwlock_destroy);
NTCRT_IMPORT_ONLY(pthread_rwlock_rdlock, pthread_rwlock_rdlock);
NTCRT_IMPORT(pthread_rwlock_timedrdlock, _NtCRT_pthread_rwlock_timedrdlock);
NTCRT_IMPORT(pthread_rwlock_timedrdlock64, _NtCRT_pthread_rwlock_timedrdlock);
NTCRT_IMPORT_ONLY(pthread_rwlock_tryrdlock, pthread_rwlock_tryrdlock);
NTCRT_IMPORT_ONLY(pthread_rwlock_wrlock, pthread_rwlock_wrlock);
NTCRT_IMPORT(pthread_rwlock_timedwrlock, _NtCRT_pthread_rwlock_timedwrlock);
NTCRT_IMPORT(pthread_rwlock_timedwrlock64, _NtCRT_pthread_rwlock_timedwrlock);
NTCRT_IMPORT_ONLY(pthread_rwlock_trywrlock, pthread_rwlock_trywrlock);
NTCRT_IMPORT_ONLY(pthread_rwlock_unlock, pthread_rwlock_unlock);
NTCRT_IMPORT_ONLY(pthread_rwlockattr_init, pthread_rwlockattr_init);
NTCRT_IMPORT_ONLY(pthread_rwlockattr_destroy, pthread_rwlockattr_destroy);
NTCRT_IMPORT_ONLY(pthread_rwlockattr_getpshared, pthread_rwlockattr_getpshared);
NTCRT_IMPORT_ONLY(pthread_rwlockattr_setpshared, pthread_rwlockattr_setpshared);
