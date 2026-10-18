/* C11 threads (<threads.h>): mtx_*, cnd_* and call_once on the pthread functions */

#include "NtCRT.h"

#include <pthread.h>
#include <threads.h>

static_assert(sizeof(mtx_t) == sizeof(pthread_mutex_t) && sizeof(cnd_t) == sizeof(pthread_cond_t) &&
                  sizeof(once_flag) == sizeof(pthread_once_t),
              "threads.h");

static int MtxResult(int Error)
{
    return Error == 0 ? thrd_success : Error == EBUSY ? thrd_busy : Error == ETIMEDOUT ? thrd_timedout : thrd_error;
}

NTCRT_API void call_once(once_flag* flag, void (*func)(void))
{
    pthread_once(flag, func);
}

/* Any mutex is timed, a recursive one is PTHREAD_MUTEX_RECURSIVE and a plain one PTHREAD_MUTEX_NORMAL */
NTCRT_API int mtx_init(mtx_t* mtx, int type)
{
    pthread_mutexattr_t Attr = PTHREAD_MUTEX_NORMAL;

    if ((type & ~(mtx_recursive | mtx_timed)) != 0)
    {
        return thrd_error;
    }
    if ((type & mtx_recursive) != 0)
    {
        Attr = PTHREAD_MUTEX_RECURSIVE;
    }
    return MtxResult(pthread_mutex_init(mtx, &Attr));
}

NTCRT_API void mtx_destroy(mtx_t* mtx)
{
    pthread_mutex_destroy(mtx);
}

NTCRT_API int mtx_lock(mtx_t* mtx)
{
    return MtxResult(pthread_mutex_lock(mtx));
}

NTCRT_API int mtx_timedlock(mtx_t* __restrict mtx, const struct timespec* __restrict ts)
{
    return MtxResult(pthread_mutex_timedlock(mtx, ts));
}

NTCRT_API int mtx_trylock(mtx_t* mtx)
{
    return MtxResult(pthread_mutex_trylock(mtx));
}

NTCRT_API int mtx_unlock(mtx_t* mtx)
{
    return MtxResult(pthread_mutex_unlock(mtx));
}

NTCRT_API int cnd_init(cnd_t* cond)
{
    return MtxResult(pthread_cond_init(cond, NULL));
}

NTCRT_API void cnd_destroy(cnd_t* cond)
{
    pthread_cond_destroy(cond);
}

NTCRT_API int cnd_signal(cnd_t* cond)
{
    return MtxResult(pthread_cond_signal(cond));
}

NTCRT_API int cnd_broadcast(cnd_t* cond)
{
    return MtxResult(pthread_cond_broadcast(cond));
}

NTCRT_API int cnd_wait(cnd_t* cond, mtx_t* mtx)
{
    return MtxResult(pthread_cond_wait(cond, mtx));
}

NTCRT_API int cnd_timedwait(cnd_t* __restrict cond, mtx_t* __restrict mtx, const struct timespec* __restrict ts)
{
    return MtxResult(pthread_cond_timedwait(cond, mtx, ts));
}
