/* pthread mutexes, in the word of pthread_mutex_t */

#include "NtCRT.h"

#include <pthread.h>

/*
 * An unlocked mutex is -1 - its type: -1 (PTHREAD_MUTEX_INITIALIZER, also 0), -2 (errorcheck) or -3 (recursive), as
 * the static initializers of winpthreads. A locked one has the type in bits 0-1, bit 2 if threads may wait and the
 * recursion count - 1 above. On x64 the count is in bits 3-30 and the thread id of the owner in bits 31-62. On x86,
 * where the word has 32 bits, the count is in bits 3-11 and the owner is the address of its TEB, a page: a locked word
 * may be negative there. The levels beyond the count are kept by the owner (NTCRT_PTD::MutexLevels).
 */
#define MUTEX_TYPE    ((LONG_PTR)3)
#define MUTEX_WAITERS ((LONG_PTR)4)
#define MUTEX_COUNT   ((LONG_PTR)NTCRT_X86_X64(0xFF8, 0x7FFFFFF8))
#define MUTEX_ONE     ((LONG_PTR)8)
#define MUTEX_OWNER   (~(LONG_PTR)NTCRT_X86_X64(0xFFF, 0x7FFFFFFF))

static LONG_PTR MutexSelf(void)
{
    return NTCRT_X86_X64((LONG_PTR)NtCurrentTeb(), (LONG_PTR)_Inline_GetCurrentThreadId() << 31);
}

static bool MutexUnlocked(LONG_PTR Raw)
{
    return NTCRT_X86_X64((ULONG_PTR)Raw + 3 <= 3, Raw <= 0);
}

/* Not a mutex, which x86 cannot tell */
static bool MutexInvalid([[maybe_unused]] LONG_PTR Raw)
{
    return NTCRT_X86_X64(false, Raw < -3);
}

#ifndef _WIN64
/* The levels of m beyond its count that the thread holds, NULL if none and !Add or there is no room */
static ULONG* MutexLevels(pthread_mutex_t* m, bool Add)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();
    ULONG* Free = NULL;

    if (Ptd == &_NtCRT_FallbackPtd)
    {
        return NULL;
    }
    for (auto& Entry : Ptd->MutexLevels)
    {
        if (Entry.Mutex == m && Entry.Levels != 0)
        {
            return &Entry.Levels;
        }
        if (Entry.Levels == 0 && Free == NULL && Add)
        {
            Entry.Mutex = m;
            Free = &Entry.Levels;
        }
    }
    return Free;
}
#endif

/* Lock a recursive mutex the thread holds again */
static int MutexRecurse(pthread_mutex_t* m, LONG_PTR Raw)
{
    if ((Raw & MUTEX_COUNT) == MUTEX_COUNT)
    {
#ifndef _WIN64
        ULONG* Levels = MutexLevels(m, true);

        if (Levels != NULL && *Levels != MAXULONG)
        {
            ++*Levels;
            return 0;
        }
#endif
        return EAGAIN;
    }
    __atomic_fetch_add(m, MUTEX_ONE, __ATOMIC_SEQ_CST);
    return 0;
}

/* Lock, until Time of CLOCK_REALTIME if not NULL */
static int MutexLock(pthread_mutex_t* m, const struct timespec* Time)
{
    LONG_PTR Self = MutexSelf();
    LONG_PTR Waiters = 0;
    int Spins = _NtCRT_Spins();
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER Wait = NULL;
    bool Timed = false;

    for (;;)
    {
        LONG_PTR Raw = __atomic_load_n(m, __ATOMIC_RELAXED);

        if (MutexInvalid(Raw))
        {
            return EINVAL;
        }
        if (MutexUnlocked(Raw))
        {
            /* After a wait other threads may still wait */
            if (__sync_val_compare_and_swap(m, Raw, Self | Waiters | (Raw == 0 ? 0 : -1 - Raw)) == Raw)
            {
                return 0;
            }
            continue;
        }
        if ((Raw & MUTEX_OWNER) == Self)
        {
            if ((Raw & MUTEX_TYPE) == PTHREAD_MUTEX_RECURSIVE)
            {
                return MutexRecurse(m, Raw);
            }
            if ((Raw & MUTEX_TYPE) == PTHREAD_MUTEX_ERRORCHECK)
            {
                return EDEADLK;
            }
            /* A normal mutex deadlocks */
        } else if (Spins > 0 && (Raw & MUTEX_WAITERS) == 0)
        {
            /* The owner may unlock it soon */
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
        /* The flag is set before the wait, a waiter that times out leaves it to the next unlock */
        if ((Raw & MUTEX_WAITERS) == 0)
        {
            if (__sync_val_compare_and_swap(m, Raw, Raw | MUTEX_WAITERS) != Raw)
            {
                continue;
            }
            Raw |= MUTEX_WAITERS;
        }
        Waiters = MUTEX_WAITERS;
        if (_NtCRT_Wait(m, (ULONG_PTR)Raw, sizeof(*m), Wait, 0) == STATUS_TIMEOUT)
        {
            return ETIMEDOUT;
        }
    }
}

/*
 * Unlock all levels if Count is not NULL, the count of the levels above one in *Count: that of the word, on x86 with
 * the levels beyond it in bits 12-43
 */
static int MutexUnlock(pthread_mutex_t* m, LONG64* Count)
{
    LONG_PTR Raw = __atomic_load_n(m, __ATOMIC_RELAXED);

    if (MutexUnlocked(Raw) || MutexInvalid(Raw))
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
    }
#ifndef _WIN64
    if ((Raw & MUTEX_COUNT) == MUTEX_COUNT)
    {
        ULONG* Levels = MutexLevels(m, false);

        if (Levels != NULL && Count == NULL)
        {
            --*Levels;
            return 0;
        }
        if (Levels != NULL)
        {
            *Count |= (LONG64)*Levels << 12;
            *Levels = 0;
        }
    }
#endif
    if (Count == NULL && (Raw & MUTEX_COUNT) != 0)
    {
        __atomic_fetch_sub(m, MUTEX_ONE, __ATOMIC_SEQ_CST);
        return 0;
    }
    if (__atomic_exchange_n(m, -1 - (Raw & MUTEX_TYPE), __ATOMIC_SEQ_CST) & MUTEX_WAITERS)
    {
        _NtCRT_Wake(m, FALSE);
    }
    return 0;
}

EXTERN_C int _NtCRT_MutexUnlockAll(intptr_t* Mutex, LONG64* Count)
{
    return MutexUnlock(Mutex, Count);
}

EXTERN_C void _NtCRT_MutexRelock(intptr_t* Mutex, LONG64 Count)
{
    MutexLock(Mutex, NULL);
    if ((Count & MUTEX_COUNT) != 0)
    {
        __atomic_fetch_add(Mutex, (LONG_PTR)(Count & MUTEX_COUNT), __ATOMIC_SEQ_CST);
    }
#ifndef _WIN64
    /* The entry that the unlock freed is free */
    if ((Count >> 12) != 0)
    {
        *MutexLevels(Mutex, true) = (ULONG)(Count >> 12);
    }
#endif
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

    if (MutexInvalid(Raw))
    {
        return EINVAL;
    }
    return MutexUnlocked(Raw) ? 0 : EBUSY;
}

NTCRT_API int pthread_mutex_lock(pthread_mutex_t* m)
{
    return MutexLock(m, NULL);
}

NTCRT_API int _NtCRT_pthread_mutex_timedlock(pthread_mutex_t* m, const struct timespec* ts)
{
    return MutexLock(m, ts);
}

NTCRT_API int pthread_mutex_trylock(pthread_mutex_t* m)
{
    LONG_PTR Raw = __atomic_load_n(m, __ATOMIC_RELAXED);
    LONG_PTR Self = MutexSelf();

    if (MutexInvalid(Raw))
    {
        return EINVAL;
    }
    if (MutexUnlocked(Raw))
    {
        return __sync_val_compare_and_swap(m, Raw, Self | (Raw == 0 ? 0 : -1 - Raw)) == Raw ? 0 : EBUSY;
    }
    if ((Raw & MUTEX_TYPE) == PTHREAD_MUTEX_RECURSIVE && (Raw & MUTEX_OWNER) == Self)
    {
        return MutexRecurse(m, Raw);
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
NTCRT_IMPORT(pthread_mutex_timedlock, _NtCRT_pthread_mutex_timedlock);
NTCRT_IMPORT(pthread_mutex_timedlock64, _NtCRT_pthread_mutex_timedlock);
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
