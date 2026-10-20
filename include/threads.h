/*
 * C11 threads for NtCRT, on its pthread functions (libntcrt.a, see README.md). The ntcrt.specs and ntcrt.cfg drivers
 * add this directory to the system include path. The objects are those of the <pthread.h> of winpthreads: thrd_t is
 * a pthread_t, mtx_t a pthread_mutex_t, cnd_t a pthread_cond_t, tss_t a pthread_key_t and once_flag a pthread_once_t.
 */

#ifndef _NTCRT_THREADS_H
#define _NTCRT_THREADS_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
#define thread_local _Thread_local
#endif

/*
 * The functions that take a timespec have the names that end in 32 with the 32-bit time_t of the headers of msvcrt
 * on x86 (_USE_32BIT_TIME_T), as those of winpthreads
 */
#ifdef _USE_32BIT_TIME_T
#define _NTCRT_TIME32(Name) __MINGW_ASM_CALL(Name)
#else
#define _NTCRT_TIME32(Name)
#endif

/* timespec_get of C11, which the <time.h> of MinGW-w64 declares for UCRT only */
#ifndef TIME_UTC
#define TIME_UTC 1
int timespec_get(struct timespec* ts, int base) _NTCRT_TIME32(_timespec32_get);
#endif

#define ONCE_FLAG_INIT 0
#define TSS_DTOR_ITERATIONS 256

typedef __UINTPTR_TYPE__ thrd_t;
typedef int (*thrd_start_t)(void*);
typedef __INTPTR_TYPE__ mtx_t;
typedef __INTPTR_TYPE__ cnd_t;
typedef unsigned tss_t;
typedef void (*tss_dtor_t)(void*);
typedef long once_flag;

enum
{
    thrd_success = 0,
    thrd_busy = 1,
    thrd_error = 2,
    thrd_nomem = 3,
    thrd_timedout = 4
};

enum
{
    mtx_plain = 0,
    mtx_recursive = 1,
    mtx_timed = 2
};

void call_once(once_flag* flag, void (*func)(void));

int cnd_broadcast(cnd_t* cond);
void cnd_destroy(cnd_t* cond);
int cnd_init(cnd_t* cond);
int cnd_signal(cnd_t* cond);
int cnd_timedwait(cnd_t* __restrict cond, mtx_t* __restrict mtx, const struct timespec* __restrict ts)
    _NTCRT_TIME32(cnd_timedwait32);
int cnd_wait(cnd_t* cond, mtx_t* mtx);

void mtx_destroy(mtx_t* mtx);
int mtx_init(mtx_t* mtx, int type);
int mtx_lock(mtx_t* mtx);
int mtx_timedlock(mtx_t* __restrict mtx, const struct timespec* __restrict ts) _NTCRT_TIME32(mtx_timedlock32);
int mtx_trylock(mtx_t* mtx);
int mtx_unlock(mtx_t* mtx);

int thrd_create(thrd_t* thr, thrd_start_t func, void* arg);
thrd_t thrd_current(void);
int thrd_detach(thrd_t thr);
int thrd_equal(thrd_t thr0, thrd_t thr1);
__attribute__((__noreturn__)) void thrd_exit(int res);
int thrd_join(thrd_t thr, int* res);
int thrd_sleep(const struct timespec* duration, struct timespec* remaining) _NTCRT_TIME32(thrd_sleep32);
void thrd_yield(void);

int tss_create(tss_t* key, tss_dtor_t dtor);
void tss_delete(tss_t key);
void* tss_get(tss_t key);
int tss_set(tss_t key, void* val);

#ifdef __cplusplus
}
#endif

#endif
