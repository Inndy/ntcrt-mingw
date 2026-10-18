/* Included before each test of pthreads4w: what the tests use of pthreads4w beyond POSIX and winpthreads */
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>

/* The time relative from now (now for NULL) by CLOCK_REALTIME, the clock of the timed waits */
static struct timespec *pthread_win32_getabstime_np(struct timespec *abstime, const struct timespec *relative)
{
    clock_gettime(CLOCK_REALTIME, abstime);
    if (relative == NULL)
        return abstime;
    abstime->tv_sec += relative->tv_sec;
    abstime->tv_nsec += relative->tv_nsec;
    if (abstime->tv_nsec >= 1000000000) {
        abstime->tv_sec++;
        abstime->tv_nsec -= 1000000000;
    }
    return abstime;
}

/* The handle of a thread, pthread_gethandle of winpthreads */
#define pthread_getw32threadhandle_np(t) ((HANDLE)pthread_gethandle(t))

/* Robust mutexes are not supported, the robust passes of the mutex tests (test.h) are left to the normal ones */
#define PTHREAD_MUTEX_STALLED 0
#define PTHREAD_MUTEX_ROBUST 1
static int pthread_mutexattr_getrobust(const pthread_mutexattr_t *attr, int *robust)
{
    (void)attr;
    *robust = PTHREAD_MUTEX_STALLED;
    return 0;
}
static int pthread_mutexattr_setrobust(pthread_mutexattr_t *attr, int robust)
{
    (void)attr;
    return robust == PTHREAD_MUTEX_STALLED ? 0 : ENOTSUP;
}

/* kernel32: the error box of a crash (the tests do not crash), the priority class of the process */
#define SetErrorMode(mode) ((UINT)(mode))
#define SetPriorityClass(process, class) ((void)(process), (void)(class), TRUE)

/* MinGW-w64 has rand_r for _POSIX only */
#ifndef rand_r
#define rand_r(seed) ((void)(seed), rand())
#endif
