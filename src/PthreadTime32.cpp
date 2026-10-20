/*
 * The thread functions of winpthreads that take a _timespec32, apart from those of the clocks (Time32.cpp) so that a
 * program that reads a clock does not link the locks
 */

#include "NtCRT.h"

#include <pthread.h>
#include <semaphore.h>

EXTERN_C int _NtCRT_pthread_delay_np(const struct timespec* interval);
EXTERN_C int _NtCRT_pthread_mutex_timedlock(pthread_mutex_t* m, const struct timespec* ts);
EXTERN_C int _NtCRT_pthread_rwlock_timedrdlock(pthread_rwlock_t* l, const struct timespec* ts);
EXTERN_C int _NtCRT_pthread_rwlock_timedwrlock(pthread_rwlock_t* l, const struct timespec* ts);
EXTERN_C int _NtCRT_pthread_cond_timedwait(pthread_cond_t* cv, pthread_mutex_t* external_mutex,
                                           const struct timespec* t);
EXTERN_C int _NtCRT_pthread_cond_timedwait_relative_np(pthread_cond_t* cv, pthread_mutex_t* external_mutex,
                                                       const struct timespec* t);
EXTERN_C int _NtCRT_sem_timedwait(sem_t* sem, const struct timespec* t);

/* The time of a _timespec32 in Buffer, NULL for NULL */
static const struct timespec* In32(const struct _timespec32* Time, struct timespec* Buffer)
{
    if (Time == NULL)
    {
        return NULL;
    }
    Buffer->tv_sec = Time->tv_sec;
    Buffer->tv_nsec = Time->tv_nsec;
    return Buffer;
}

NTCRT_API int _NtCRT_pthread_delay32_np(const struct _timespec32* interval)
{
    struct timespec Time;

    return _NtCRT_pthread_delay_np(In32(interval, &Time));
}

NTCRT_API int _NtCRT_pthread_mutex_timedlock32(pthread_mutex_t* m, const struct _timespec32* ts)
{
    struct timespec Time;

    return _NtCRT_pthread_mutex_timedlock(m, In32(ts, &Time));
}

NTCRT_API int _NtCRT_pthread_rwlock_timedrdlock32(pthread_rwlock_t* l, const struct _timespec32* ts)
{
    struct timespec Time;

    return _NtCRT_pthread_rwlock_timedrdlock(l, In32(ts, &Time));
}

NTCRT_API int _NtCRT_pthread_rwlock_timedwrlock32(pthread_rwlock_t* l, const struct _timespec32* ts)
{
    struct timespec Time;

    return _NtCRT_pthread_rwlock_timedwrlock(l, In32(ts, &Time));
}

NTCRT_API int _NtCRT_pthread_cond_timedwait32(pthread_cond_t* cv, pthread_mutex_t* external_mutex,
                                              const struct _timespec32* t)
{
    struct timespec Time;

    return _NtCRT_pthread_cond_timedwait(cv, external_mutex, In32(t, &Time));
}

NTCRT_API int _NtCRT_pthread_cond_timedwait32_relative_np(pthread_cond_t* cv, pthread_mutex_t* external_mutex,
                                                          const struct _timespec32* t)
{
    struct timespec Time;

    return _NtCRT_pthread_cond_timedwait_relative_np(cv, external_mutex, In32(t, &Time));
}

NTCRT_API int _NtCRT_sem_timedwait32(sem_t* sem, const struct _timespec32* t)
{
    struct timespec Time;

    return _NtCRT_sem_timedwait(sem, In32(t, &Time));
}

NTCRT_IMPORT(pthread_delay32_np, _NtCRT_pthread_delay32_np);
NTCRT_IMPORT(pthread_mutex_timedlock32, _NtCRT_pthread_mutex_timedlock32);
NTCRT_IMPORT(pthread_rwlock_timedrdlock32, _NtCRT_pthread_rwlock_timedrdlock32);
NTCRT_IMPORT(pthread_rwlock_timedwrlock32, _NtCRT_pthread_rwlock_timedwrlock32);
NTCRT_IMPORT(pthread_cond_timedwait32, _NtCRT_pthread_cond_timedwait32);
NTCRT_IMPORT(pthread_cond_timedwait32_relative_np, _NtCRT_pthread_cond_timedwait32_relative_np);
NTCRT_IMPORT(sem_timedwait32, _NtCRT_sem_timedwait32);
