/*
 * The clock functions of winpthreads that take a _timespec32: the headers of MinGW-w64 13 and later call those that
 * take a timespec by names that end in 64, or in 32 with _USE_32BIT_TIME_T. Apart from the others, which a program
 * links whole with ld, and from those of the threads (PthreadTime32.cpp). A time that a _timespec32 cannot hold fails
 * with EOVERFLOW.
 */

#include "NtCRT.h"

#include <pthread.h>
#include <sys/timeb.h>

EXTERN_C int __cdecl _NtCRT_clock_gettime(clockid_t clock_id, struct timespec* tp);
EXTERN_C int __cdecl _NtCRT_clock_getres(clockid_t clock_id, struct timespec* res);
EXTERN_C int __cdecl _NtCRT_clock_settime(clockid_t clock_id, const struct timespec* tp);
EXTERN_C int __cdecl _NtCRT_clock_nanosleep(clockid_t clock_id, int flags, const struct timespec* request,
                                            struct timespec* remain);
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

/* Time to Time32 if Result is 0 */
static int Out32(int Result, const struct timespec* Time, struct _timespec32* Time32)
{
    if (Result != 0 || Time32 == NULL)
    {
        return Result;
    }
    if (Time->tv_sec != (__time32_t)Time->tv_sec)
    {
        NTCRT_ERRNO = EOVERFLOW;
        return -1;
    }
    Time32->tv_sec = (__time32_t)Time->tv_sec;
    Time32->tv_nsec = Time->tv_nsec;
    return 0;
}

NTCRT_API int __cdecl _NtCRT_clock_gettime32(clockid_t clock_id, struct _timespec32* tp)
{
    struct timespec Time;

    return Out32(_NtCRT_clock_gettime(clock_id, tp != NULL ? &Time : NULL), &Time, tp);
}

NTCRT_API int __cdecl _NtCRT_clock_getres32(clockid_t clock_id, struct _timespec32* res)
{
    struct timespec Time;

    return Out32(_NtCRT_clock_getres(clock_id, res != NULL ? &Time : NULL), &Time, res);
}

NTCRT_API int __cdecl _NtCRT_clock_settime32(clockid_t clock_id, const struct _timespec32* tp)
{
    struct timespec Time;

    return _NtCRT_clock_settime(clock_id, In32(tp, &Time));
}

/* remain is not written, see _NtCRT_nanosleep */
NTCRT_API int __cdecl _NtCRT_nanosleep32(const struct _timespec32* request, struct _timespec32* remain)
{
    struct timespec Time;

    UNREFERENCED_PARAMETER(remain);
    return _NtCRT_nanosleep(In32(request, &Time), NULL);
}

NTCRT_API int __cdecl _NtCRT_clock_nanosleep32(clockid_t clock_id, int flags, const struct _timespec32* request,
                                               struct _timespec32* remain)
{
    struct timespec Time;

    UNREFERENCED_PARAMETER(remain);
    return _NtCRT_clock_nanosleep(clock_id, flags, In32(request, &Time), NULL);
}

NTCRT_IMPORT(clock_gettime32, _NtCRT_clock_gettime32);
NTCRT_IMPORT(clock_getres32, _NtCRT_clock_getres32);
NTCRT_IMPORT(clock_settime32, _NtCRT_clock_settime32);
NTCRT_IMPORT(nanosleep32, _NtCRT_nanosleep32);
NTCRT_IMPORT(clock_nanosleep32, _NtCRT_clock_nanosleep32);
