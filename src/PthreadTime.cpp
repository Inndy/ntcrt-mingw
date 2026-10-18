/* Times of the thread functions in 100 ns units, and the time helpers of winpthreads */

#include "NtCRT.h"

#include <pthread.h>

/* 100 ns units from 1601 (NT) to 1970 (Unix) */
#define NTCRT_UNIX_EPOCH 116444736000000000LL

EXTERN_C LONGLONG _NtCRT_MonotonicTime(void)
{
    LARGE_INTEGER Counter;
    LARGE_INTEGER Frequency;

    RtlQueryPerformanceCounter(&Counter);
    RtlQueryPerformanceFrequency(&Frequency);
    return Counter.QuadPart / Frequency.QuadPart * 10000000 +
           Counter.QuadPart % Frequency.QuadPart * 10000000 / Frequency.QuadPart;
}

EXTERN_C LONGLONG _NtCRT_TimespecUnits(const struct timespec* Time)
{
    LONGLONG Seconds = Time->tv_sec;

    if (Seconds >= MAXLONGLONG / 10000000 - 1)
    {
        return NTCRT_TIME_NEVER;
    }
    /* Long past, far from overflows in _NtCRT_Timeout */
    if (Seconds < -(1LL << 39))
    {
        return -(1LL << 60);
    }
    return Seconds * 10000000 + (Time->tv_nsec + 99) / 100;
}

EXTERN_C PLARGE_INTEGER _NtCRT_Timeout(LONGLONG Units, int Clock, PLARGE_INTEGER Timeout)
{
    if (Units == NTCRT_TIME_NEVER)
    {
        return NULL;
    }
    if (Clock == CLOCK_REALTIME)
    {
        /* Positive is absolute system time, which follows changes of the clock */
        if (Units >= NTCRT_TIME_NEVER - NTCRT_UNIX_EPOCH)
        {
            return NULL;
        }
        Units += NTCRT_UNIX_EPOCH;
        Timeout->QuadPart = Units > 0 ? Units : 0;
    } else
    {
        if (Clock == CLOCK_MONOTONIC)
        {
            Units -= _NtCRT_MonotonicTime();
        }
        /* Negative is relative, 0 (a time past) ends the wait at once */
        Timeout->QuadPart = Units > 0 ? -Units : 0;
    }
    return Timeout;
}

/* Milliseconds until a time of CLOCK_REALTIME, now, and of a time */

static ULONGLONG RealtimeUnits(void)
{
    LARGE_INTEGER Time;

    NtQuerySystemTime(&Time);
    return Time.QuadPart - NTCRT_UNIX_EPOCH;
}

NTCRT_API unsigned long long _pthread_time_in_ms(void)
{
    return RealtimeUnits() / 10000;
}

NTCRT_API unsigned long long _pthread_time_in_ms_from_timespec(const struct timespec* ts)
{
    return (unsigned long long)ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
}

NTCRT_API unsigned long long _pthread_rel_time_in_ms(const struct timespec* ts)
{
    unsigned long long Time = _pthread_time_in_ms_from_timespec(ts);
    unsigned long long Now = _pthread_time_in_ms();

    return Time > Now ? Time - Now : 0;
}
