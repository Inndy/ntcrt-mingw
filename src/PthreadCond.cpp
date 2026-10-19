/* pthread condition variables, in the word of pthread_cond_t */

#include "NtCRT.h"

#include <pthread.h>

/*
 * A count of the signals in bits 0-31, which a waiter reads before it unlocks the mutex and waits on while it is the
 * same (the wait compares these 32 bits). Bits 32-61 count the waiters from before they unlock the mutex until their
 * wait ends, from 0, or from all ones in -1 (PTHREAD_COND_INITIALIZER, the only value with bits 62-63 at 11). POSIX
 * lets a thread destroy a condition on which no thread is blocked, right after a broadcast, while a woken waiter may
 * still read it: pthread_cond_destroy waits for the count. Bits 62-63 are 01 or 10 for CLOCK_MONOTONIC, so -1 and 0
 * are of CLOCK_REALTIME.
 */
#define COND_SIGNALS   ((LONG_PTR)0xFFFFFFFF)
#define COND_WAITER    ((LONG_PTR)1 << 32)
#define COND_WAITERS   ((LONG_PTR)0x3FFFFFFF << 32)
#define COND_MONOTONIC ((LONG_PTR)1 << 62)

static int CondClock(LONG_PTR Raw)
{
    ULONG_PTR Bits = (ULONG_PTR)Raw >> 62;

    return Bits == 1 || Bits == 2 ? CLOCK_MONOTONIC : CLOCK_REALTIME;
}

/* Whether a waiter is counted */
static bool CondWaiters(LONG_PTR Raw)
{
    return (Raw & COND_WAITERS) != ((ULONG_PTR)Raw >> 62 == 3 ? COND_WAITERS : 0);
}

/* Adds Add to a field, 1 to the count of signals or COND_WAITER or -COND_WAITER to that of waiters, the old word */
static LONG_PTR CondAdd(pthread_cond_t* c, LONG_PTR Add, LONG_PTR Field)
{
    LONG_PTR Raw = __atomic_load_n(c, __ATOMIC_RELAXED);
    LONG_PTR Old;

    while ((Old = InterlockedCompareExchange64(c, (Raw & ~Field) | (LONG_PTR)(((ULONG_PTR)Raw + Add) & Field), Raw)) !=
           Raw)
    {
        Raw = Old;
    }
    return Raw;
}

/* A signal without waiters does not take the lock of the bucket */
static int CondWake(pthread_cond_t* c, BOOL All)
{
    if (CondWaiters(CondAdd(c, 1, COND_SIGNALS)))
    {
        _NtCRT_Wake(c, All);
    }
    return 0;
}

/* Wait until Time (absolute, of the clock of the condition, or relative), a cancellation point */
static int CondWait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* Time, bool Relative)
{
    LONG_PTR Raw;
    LONG_PTR Count;
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER Wait = NULL;
    bool (*Requested)(void);
    bool Cancel;
    NTSTATUS Status;
    int Error;

    _NtCRT_CancelPoint(NULL, NULL);
    if (Time != NULL)
    {
        if (!NTCRT_TIMESPEC_VALID(Time))
        {
            return EINVAL;
        }
        Wait = _NtCRT_Timeout(_NtCRT_TimespecUnits(Time),
                              Relative ? -1 : CondClock(__atomic_load_n(c, __ATOMIC_RELAXED)),
                              &Timeout);
    }
    /* Counted while the mutex is held: a thread that takes it then and destroys the condition waits for this one */
    Raw = CondAdd(c, COND_WAITER, COND_WAITERS);
    Error = _NtCRT_MutexUnlockAll(m, &Count);
    if (Error != 0)
    {
        CondAdd(c, -COND_WAITER, COND_WAITERS);
        return Error;
    }
    /*
     * Spin for a signal that may come soon, unless threads are parked (a signal ends the spin of all spinners, but
     * unparks one). pthread_cancel unparks the thread, a cancellation ends the wait.
     */
    for (int Spins = _NtCRT_Parked(c) ? 0 : _NtCRT_Spins();
         Spins > 0 && (ULONG)__atomic_load_n(c, __ATOMIC_RELAXED) == (ULONG)Raw;
         Spins--)
    {
        YieldProcessor();
    }
    Status = _NtCRT_Wait(c, (ULONG)Raw, sizeof(LONG), Wait, NTCRT_WAIT_CANCEL);
    /*
     * A cancellation that acts passes on a signal the thread may have taken from others, the last use of the condition.
     * One that comes later acts at the next cancellation point: the signal is taken.
     */
    Requested = __atomic_load_n(&_NtCRT_CancelRequested, __ATOMIC_ACQUIRE);
    Cancel = Requested != NULL && Requested();
    if (Cancel && Status == STATUS_SUCCESS)
    {
        CondWake(c, FALSE);
    }
    CondAdd(c, -COND_WAITER, COND_WAITERS);
    _NtCRT_MutexRelock(m, Count);
    if (Cancel)
    {
        _NtCRT_CancelPoint(NULL, NULL);
    }
    return Status == STATUS_TIMEOUT ? ETIMEDOUT : 0;
}

NTCRT_API int pthread_cond_init(pthread_cond_t* cv, const pthread_condattr_t* a)
{
    *cv = a != NULL && *a == CLOCK_MONOTONIC ? COND_MONOTONIC : -1;
    return 0;
}

/*
 * Waits for the waiters that a signal or a broadcast woke to leave, EBUSY while a thread is blocked on the condition
 * (its node queued, see PthreadWait.cpp)
 */
NTCRT_API int pthread_cond_destroy(pthread_cond_t* cv)
{
    ULONG Rounds = 0;

    while (CondWaiters(__atomic_load_n(cv, __ATOMIC_ACQUIRE)))
    {
        if (_NtCRT_Waiting(cv))
        {
            return EBUSY;
        }
        _NtCRT_Backoff(&Rounds);
    }
    return 0;
}

NTCRT_API int pthread_cond_signal(pthread_cond_t* cv)
{
    return CondWake(cv, FALSE);
}

NTCRT_API int pthread_cond_broadcast(pthread_cond_t* cv)
{
    return CondWake(cv, TRUE);
}

NTCRT_API int pthread_cond_wait(pthread_cond_t* cv, pthread_mutex_t* external_mutex)
{
    return CondWait(cv, external_mutex, NULL, false);
}

NTCRT_API int pthread_cond_timedwait(pthread_cond_t* cv, pthread_mutex_t* external_mutex, const struct timespec* t)
{
    return CondWait(cv, external_mutex, t, false);
}

NTCRT_API int pthread_cond_timedwait_relative_np(pthread_cond_t* cv, pthread_mutex_t* external_mutex,
                                                 const struct timespec* t)
{
    return CondWait(cv, external_mutex, t, true);
}

/* Attributes: the clock */

NTCRT_API int pthread_condattr_init(pthread_condattr_t* a)
{
    *a = CLOCK_REALTIME;
    return 0;
}

NTCRT_API int pthread_condattr_destroy(pthread_condattr_t* a)
{
    UNREFERENCED_PARAMETER(a);
    return 0;
}

NTCRT_API int pthread_condattr_getpshared(const pthread_condattr_t* a, int* s)
{
    UNREFERENCED_PARAMETER(a);
    *s = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

NTCRT_API int pthread_condattr_setpshared(pthread_condattr_t* a, int s)
{
    UNREFERENCED_PARAMETER(a);
    if (s == PTHREAD_PROCESS_PRIVATE)
    {
        return 0;
    }
    return s == PTHREAD_PROCESS_SHARED ? ENOTSUP : EINVAL;
}

NTCRT_API int pthread_condattr_getclock(const pthread_condattr_t* attr, clockid_t* clock_id)
{
    *clock_id = *attr;
    return 0;
}

NTCRT_API int pthread_condattr_setclock(pthread_condattr_t* attr, clockid_t clock_id)
{
    if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC)
    {
        return EINVAL;
    }
    *attr = clock_id;
    return 0;
}

/* The pointers that WINPTHREADS_USE_DLLIMPORT refers to (__declspec(dllimport) of the winpthreads headers) */
NTCRT_IMPORT_ONLY(pthread_cond_init, pthread_cond_init);
NTCRT_IMPORT_ONLY(pthread_cond_destroy, pthread_cond_destroy);
NTCRT_IMPORT_ONLY(pthread_cond_signal, pthread_cond_signal);
NTCRT_IMPORT_ONLY(pthread_cond_broadcast, pthread_cond_broadcast);
NTCRT_IMPORT_ONLY(pthread_cond_wait, pthread_cond_wait);
NTCRT_IMPORT_ONLY(pthread_cond_timedwait, pthread_cond_timedwait);
NTCRT_IMPORT_ONLY(pthread_cond_timedwait_relative_np, pthread_cond_timedwait_relative_np);
NTCRT_IMPORT_ONLY(pthread_condattr_init, pthread_condattr_init);
NTCRT_IMPORT_ONLY(pthread_condattr_destroy, pthread_condattr_destroy);
NTCRT_IMPORT_ONLY(pthread_condattr_getpshared, pthread_condattr_getpshared);
NTCRT_IMPORT_ONLY(pthread_condattr_setpshared, pthread_condattr_setpshared);
NTCRT_IMPORT_ONLY(pthread_condattr_getclock, pthread_condattr_getclock);
NTCRT_IMPORT_ONLY(pthread_condattr_setclock, pthread_condattr_setclock);
