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
NTCRT_API int __cdecl _NtCRT_clock_gettime(clockid_t clock_id, struct timespec* tp)
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
NTCRT_API int _NtCRT_timespec_get(struct timespec* ts, int base)
{
    if (base != 1 /* TIME_UTC */ || _NtCRT_clock_gettime(CLOCK_REALTIME, ts) != 0)
    {
        return 0;
    }
    return base;
}

/* timespec_get with a 32-bit time_t on x86 (_timespec32_get of UCRT, see <threads.h>), x86 only */
EXTERN_C int _NtCRT__timespec32_get(struct _timespec32* ts, int base);

#ifndef _WIN64
NTCRT_API int _NtCRT__timespec32_get(struct _timespec32* ts, int base)
{
    struct timespec Time;

    if (_NtCRT_timespec_get(&Time, base) == 0 || Time.tv_sec != (__time32_t)Time.tv_sec)
    {
        return 0;
    }
    ts->tv_sec = (__time32_t)Time.tv_sec;
    ts->tv_nsec = Time.tv_nsec;
    return base;
}
#endif

NTCRT_API int __cdecl _NtCRT_clock_getres(clockid_t clock_id, struct timespec* res)
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

NTCRT_API int __cdecl _NtCRT_clock_settime(clockid_t clock_id, const struct timespec* tp)
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
    NTSTATUS (*Delay)(PLARGE_INTEGER);

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
        } else if (Wait->QuadPart == 0)
        {
            /* The time passed */
            return 0;
        }
        /* Not alertable, a program that can cancel threads parks them (pthread_cancel unparks) */
        Delay = __atomic_load_n(&_NtCRT_DelayHook, __ATOMIC_ACQUIRE);
        Status = Delay != NULL ? Delay(Wait) : NtDelayExecution(FALSE, Wait);
        if (Status == STATUS_ALERTED)
        {
            _NtCRT_CancelPoint(NULL, NULL);
        } else if (Clock != CLOCK_MONOTONIC)
        {
            return 0;
        }
        /* A relative park may end before its time (with a finer timer resolution): the clock tells */
    }
}

/* The sleep is not interrupted, remain is not written */
NTCRT_API int __cdecl _NtCRT_nanosleep(const struct timespec* request, struct timespec* remain)
{
    UNREFERENCED_PARAMETER(remain);
    if (request == NULL || request->tv_sec < 0 || !NTCRT_TIMESPEC_VALID(request))
    {
        return ClockError(EINVAL);
    }
    return ClockSleep(_NtCRT_TimespecUnits(request), -1);
}

NTCRT_API int __cdecl _NtCRT_clock_nanosleep(clockid_t clock_id, int flags, const struct timespec* request,
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
    return _NtCRT_clock_nanosleep(clock_id, flags, rqtp, rmtp);
}

NTCRT_API int _NtCRT_pthread_delay_np(const struct timespec* interval)
{
    return _NtCRT_nanosleep(interval, NULL) == 0 ? 0 : EINVAL;
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

EXTERN_C int __cdecl __wrap_usleep(unsigned int useconds)
{
    ClockSleep((LONGLONG)useconds * 10, -1);
    return 0;
}

/*
 * The names by the stubs of the Makefile, a program may define one of them itself. "__imp_" are the pointers that
 * WINPTHREADS_USE_DLLIMPORT refers to (__declspec(dllimport) of the winpthreads headers).
 */
NTCRT_IMPORT(sleep, __wrap_sleep);
NTCRT_IMPORT(usleep, __wrap_usleep);
NTCRT_IMPORT(nanosleep, _NtCRT_nanosleep);
NTCRT_IMPORT(timespec_get, _NtCRT_timespec_get);
NTCRT_IMPORT(_timespec64_get, _NtCRT_timespec_get);
NTCRT_IMPORT(_timespec32_get, _NtCRT__timespec32_get);
NTCRT_IMPORT(clock_gettime, _NtCRT_clock_gettime);
NTCRT_IMPORT(clock_getres, _NtCRT_clock_getres);
NTCRT_IMPORT(clock_settime, _NtCRT_clock_settime);
NTCRT_IMPORT(clock_nanosleep, _NtCRT_clock_nanosleep);
NTCRT_IMPORT_ONLY(__pthread_clock_nanosleep, __pthread_clock_nanosleep);
NTCRT_IMPORT(pthread_delay_np, _NtCRT_pthread_delay_np);
NTCRT_IMPORT(nanosleep64, _NtCRT_nanosleep);
NTCRT_IMPORT(clock_gettime64, _NtCRT_clock_gettime);
NTCRT_IMPORT(clock_getres64, _NtCRT_clock_getres);
NTCRT_IMPORT(clock_settime64, _NtCRT_clock_settime);
NTCRT_IMPORT(clock_nanosleep64, _NtCRT_clock_nanosleep);
NTCRT_IMPORT(pthread_delay64_np, _NtCRT_pthread_delay_np);
