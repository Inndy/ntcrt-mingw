/* POSIX clocks and sleeps of <pthread_time.h>: clock_gettime, clock_nanosleep, nanosleep */

#include "NtCRT.h"

#include <pthread.h>

/* 100 ns units from 1601 (NT) to 1970 (Unix) */
#define NTCRT_UNIX_EPOCH 116444736000000000LL

static int ClockError(int Error)
{
    NTCRT_ERRNO = Error;
    return -1;
}

static void UnitsToTimespec(LONGLONG Units, struct timespec* tp)
{
    tp->tv_sec = Units / 10000000;
    tp->tv_nsec = (long)(Units % 10000000) * 100;
}

/*
 * CLOCK_REALTIME and CLOCK_REALTIME_COARSE are the system time (the resolution of the clock tick, as time and
 * GetSystemTimeAsFileTime), CLOCK_MONOTONIC is QueryPerformanceCounter, the CPU time clocks are of NtQuery*Times.
 */
NTCRT_API int __cdecl clock_gettime(clockid_t clock_id, struct timespec* tp)
{
    KERNEL_USER_TIMES Times;
    LARGE_INTEGER Time;
    NTSTATUS Status;

    switch (clock_id)
    {
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
        NtQuerySystemTime(&Time);
        UnitsToTimespec(Time.QuadPart - NTCRT_UNIX_EPOCH, tp);
        return 0;
    case CLOCK_MONOTONIC:
        RtlQueryPerformanceCounter(&Time);
        {
            LARGE_INTEGER Frequency;

            RtlQueryPerformanceFrequency(&Frequency);
            tp->tv_sec = Time.QuadPart / Frequency.QuadPart;
            tp->tv_nsec = (long)(Time.QuadPart % Frequency.QuadPart * 1000000000 / Frequency.QuadPart);
        }
        return 0;
    case CLOCK_PROCESS_CPUTIME_ID:
        Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessTimes, &Times, sizeof(Times), NULL);
        break;
    case CLOCK_THREAD_CPUTIME_ID:
        Status = NtQueryInformationThread(NtCurrentThread(), ThreadTimes, &Times, sizeof(Times), NULL);
        break;
    default:
        return ClockError(EINVAL);
    }
    if (!NT_SUCCESS(Status))
    {
        return ClockError(EINVAL);
    }
    UnitsToTimespec(Times.KernelTime.QuadPart + Times.UserTime.QuadPart, tp);
    return 0;
}

/* C11, declared by <threads.h> of NtCRT */
NTCRT_API int timespec_get(struct timespec* ts, int base)
{
    if (base != 1 /* TIME_UTC */ || clock_gettime(CLOCK_REALTIME, ts) != 0)
    {
        return 0;
    }
    return base;
}

NTCRT_API int __cdecl clock_getres(clockid_t clock_id, struct timespec* res)
{
    ULONG Maximum, Minimum, Current;

    if (clock_id < CLOCK_REALTIME || clock_id > CLOCK_REALTIME_COARSE)
    {
        return ClockError(EINVAL);
    }
    if (res == NULL)
    {
        return 0;
    }
    if (clock_id == CLOCK_MONOTONIC)
    {
        LARGE_INTEGER Frequency;

        RtlQueryPerformanceFrequency(&Frequency);
        res->tv_sec = 0;
        res->tv_nsec = (long)((1000000000 + Frequency.QuadPart - 1) / Frequency.QuadPart);
        return 0;
    }
    /* The others change at the clock tick */
    if (!NT_SUCCESS(NtQueryTimerResolution(&Maximum, &Minimum, &Current)))
    {
        Current = 156250;
    }
    UnitsToTimespec(Current, res);
    return 0;
}

NTCRT_API int __cdecl clock_settime(clockid_t clock_id, const struct timespec* tp)
{
    LARGE_INTEGER Time;
    LONGLONG Units;
    NTSTATUS Status;

    if (clock_id != CLOCK_REALTIME || !NTCRT_TIMESPEC_VALID(tp))
    {
        return ClockError(EINVAL);
    }
    /* Before 1601 or past the end of the NT time */
    Units = _NtCRT_TimespecUnits(tp);
    if (Units < -NTCRT_UNIX_EPOCH || Units > MAXLONGLONG - NTCRT_UNIX_EPOCH)
    {
        return ClockError(EINVAL);
    }
    Time.QuadPart = Units + NTCRT_UNIX_EPOCH;
    Status = NtSetSystemTime(&Time, NULL);
    if (!NT_SUCCESS(Status))
    {
        return ClockError(Status == STATUS_PRIVILEGE_NOT_HELD ? EPERM : EINVAL);
    }
    return 0;
}

/*
 * Sleep until Units of Clock (absolute) or for Units (Clock -1), a cancellation point. An absolute time of
 * CLOCK_REALTIME follows changes of the clock. A relative sleep is measured on CLOCK_MONOTONIC.
 */
static int ClockSleep(LONGLONG Units, int Clock)
{
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER Wait;
    NTSTATUS Status;

    _NtCRT_CancelPoint(NULL, NULL);
    if (Clock == -1)
    {
        LONGLONG Now = _NtCRT_MonotonicTime();

        Units = Units < NTCRT_TIME_NEVER - Now ? Units + Now : NTCRT_TIME_NEVER;
        Clock = CLOCK_MONOTONIC;
    }
    for (;;)
    {
        Wait = _NtCRT_Timeout(Units, Clock, &Timeout);
        if (Wait == NULL)
        {
            Timeout.QuadPart = MINLONGLONG;
            Wait = &Timeout;
        }
        Status = NtDelayExecution(TRUE, Wait);
        if (Status != STATUS_ALERTED && Status != STATUS_USER_APC)
        {
            return 0;
        }
        _NtCRT_CancelPoint(NULL, NULL);
    }
}

/* The sleep is not interrupted, remain is not written */
NTCRT_API int __cdecl nanosleep(const struct timespec* request, struct timespec* remain)
{
    UNREFERENCED_PARAMETER(remain);
    if (request == NULL || request->tv_sec < 0 || !NTCRT_TIMESPEC_VALID(request))
    {
        return ClockError(EINVAL);
    }
    return ClockSleep(_NtCRT_TimespecUnits(request), -1);
}

NTCRT_API int __cdecl clock_nanosleep(clockid_t clock_id, int flags, const struct timespec* request,
                                      struct timespec* remain)
{
    UNREFERENCED_PARAMETER(remain);
    if (clock_id == CLOCK_PROCESS_CPUTIME_ID || clock_id == CLOCK_THREAD_CPUTIME_ID)
    {
        return ENOTSUP;
    }
    if ((clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC && clock_id != CLOCK_REALTIME_COARSE) ||
        request == NULL || !NTCRT_TIMESPEC_VALID(request))
    {
        return EINVAL;
    }
    if ((flags & TIMER_ABSTIME) == 0)
    {
        if (request->tv_sec < 0)
        {
            return EINVAL;
        }
        return ClockSleep(_NtCRT_TimespecUnits(request), -1);
    }
    return ClockSleep(_NtCRT_TimespecUnits(request), clock_id == CLOCK_MONOTONIC ? CLOCK_MONOTONIC : CLOCK_REALTIME);
}

NTCRT_API int __pthread_clock_nanosleep(clockid_t clock_id, int flags, const struct timespec* rqtp,
                                        struct timespec* rmtp)
{
    return clock_nanosleep(clock_id, flags, rqtp, rmtp);
}

NTCRT_API int pthread_delay_np(const struct timespec* interval)
{
    return nanosleep(interval, NULL) == 0 ? 0 : EINVAL;
}

/*
 * sleep and usleep, cancellation points: those of libmingwex wait by Sleep, which is not. The specs wrap them to
 * these, lld takes "sleep" and "usleep" of libntcrt, which precedes libmingwex in ntcrt.cfg, before it applies --wrap.
 */
EXTERN_C unsigned int __cdecl __wrap_sleep(unsigned int seconds)
{
    ClockSleep((LONGLONG)seconds * 10000000, -1);
    return 0;
}

EXTERN_C unsigned int __cdecl sleep(unsigned int seconds) __attribute__((alias("__wrap_sleep")));

EXTERN_C int __cdecl __wrap_usleep(unsigned int useconds)
{
    ClockSleep((LONGLONG)useconds * 10, -1);
    return 0;
}

EXTERN_C int __cdecl usleep(unsigned int useconds) __attribute__((alias("__wrap_usleep")));
