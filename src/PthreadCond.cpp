/* pthread condition variables, in the word of pthread_cond_t */

#include "NtCRT.h"

#include <pthread.h>

/*
 * A count of the signals in bits 0-61, which a waiter reads before it unlocks the mutex and waits on while it is the
 * same. Bits 62-63 are 01 or 10 for CLOCK_MONOTONIC, so -1 (PTHREAD_COND_INITIALIZER) and 0 are of CLOCK_REALTIME.
 */
#define COND_COUNT     ((LONG_PTR)0x3FFFFFFFFFFFFFFF)
#define COND_MONOTONIC ((LONG_PTR)1 << 62)

static int CondClock(LONG_PTR Raw)
{
    ULONG_PTR Bits = (ULONG_PTR)Raw >> 62;

    return Bits == 1 || Bits == 2 ? CLOCK_MONOTONIC : CLOCK_REALTIME;
}

static int CondWake(pthread_cond_t* c, BOOL All)
{
    LONG_PTR Raw = *(volatile LONG_PTR*)c;
    LONG_PTR Old;

    while ((Old = InterlockedCompareExchange64(c, (Raw & ~COND_COUNT) | (LONG_PTR)(((ULONG_PTR)Raw + 1) & COND_COUNT),
                                               Raw)) != Raw)
    {
        Raw = Old;
    }
    _NtCRT_Wake(c, All);
    return 0;
}

static void CondPass(void* Object)
{
    CondWake((pthread_cond_t*)Object, FALSE);
}

/* Wait until Time (absolute, of the clock of the condition, or relative), a cancellation point */
static int CondWait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* Time, bool Relative)
{
    LONG_PTR Raw = *(volatile LONG_PTR*)c;
    LONG_PTR Count;
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER Wait = NULL;
    NTSTATUS Status;
    int Error;

    if (_NtCRT_TestCancel != NULL)
    {
        _NtCRT_TestCancel(NULL, NULL);
    }
    if (Time != NULL)
    {
        if (!NTCRT_TIMESPEC_VALID(Time))
        {
            return EINVAL;
        }
        Wait = _NtCRT_Timeout(_NtCRT_TimespecUnits(Time), Relative ? -1 : CondClock(Raw), &Timeout);
    }
    Error = _NtCRT_MutexUnlockAll(m, &Count);
    if (Error != 0)
    {
        return Error;
    }
    /* Alertable for pthread_cancel, an APC or an alert is a spurious wakeup */
    Status = _NtCRT_Wait(c, (ULONG_PTR)Raw, sizeof(*c), Wait, TRUE);
    _NtCRT_MutexRelock(m, Count);
    /* A cancelled thread takes no signal from another */
    if (_NtCRT_TestCancel != NULL)
    {
        _NtCRT_TestCancel(Status == STATUS_SUCCESS ? CondPass : NULL, c);
    }
    return Status == STATUS_TIMEOUT ? ETIMEDOUT : 0;
}

NTCRT_API int pthread_cond_init(pthread_cond_t* cv, const pthread_condattr_t* a)
{
    *cv = a != NULL && *a == CLOCK_MONOTONIC ? COND_MONOTONIC : -1;
    return 0;
}

NTCRT_API int pthread_cond_destroy(pthread_cond_t* cv)
{
    UNREFERENCED_PARAMETER(cv);
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
