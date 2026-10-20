/* C11 threads (<threads.h>): thrd_* and tss_* on the pthread functions */

#include "NtCRT.h"

#include <pthread.h>
#include <threads.h>

static_assert(sizeof(thrd_t) == sizeof(pthread_t) && sizeof(tss_t) == sizeof(pthread_key_t), "threads.h");

static int ThrdResult(int Error)
{
    return Error == 0 ? thrd_success : Error == ENOMEM ? thrd_nomem : thrd_error;
}

NTCRT_API int thrd_create(thrd_t* thr, thrd_start_t func, void* arg)
{
    int Error = _NtCRT_ThreadCreate(thr, NULL, NULL, func, arg);

    return Error == EAGAIN ? thrd_nomem : ThrdResult(Error);
}

NTCRT_API thrd_t thrd_current(void)
{
    return pthread_self();
}

NTCRT_API int thrd_detach(thrd_t thr)
{
    return ThrdResult(pthread_detach(thr));
}

NTCRT_API int thrd_equal(thrd_t thr0, thrd_t thr1)
{
    return thr0 == thr1;
}

NTCRT_API void thrd_exit(int res)
{
    pthread_exit((void*)(INT_PTR)res);
    __builtin_unreachable();
}

NTCRT_API int thrd_join(thrd_t thr, int* res)
{
    void* Result;
    int Error = pthread_join(thr, &Result);

    if (Error == 0 && res != NULL)
    {
        *res = (int)(INT_PTR)Result;
    }
    return ThrdResult(Error);
}

/* Not interrupted: 0, or -2 for an invalid duration */
NTCRT_API int thrd_sleep(const struct timespec* duration, struct timespec* remaining)
{
    return _NtCRT_nanosleep(duration, remaining) == 0 ? 0 : -2;
}

NTCRT_API void thrd_yield(void)
{
    sched_yield();
}

NTCRT_API int tss_create(tss_t* key, tss_dtor_t dtor)
{
    return ThrdResult(pthread_key_create(key, dtor));
}

NTCRT_API void tss_delete(tss_t key)
{
    pthread_key_delete(key);
}

NTCRT_API void* tss_get(tss_t key)
{
    return pthread_getspecific(key);
}

NTCRT_API int tss_set(tss_t key, void* val)
{
    return ThrdResult(pthread_setspecific(key, val));
}
