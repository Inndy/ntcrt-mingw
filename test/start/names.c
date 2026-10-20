/*
 * A program may define a name of the C library or POSIX that NtCRT has, beside other functions of the same source
 * file of NtCRT: strtold beside strtod (tinycc's tcctest.c), clock_gettime and usleep beside nanosleep, wcstoll
 * beside wcstoull. Its own functions are called, the others are those of NtCRT.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

long double strtold(const char* s, char** end)
{
    (void)s;
    (void)end;
    return 42;
}

long double wcstold(const wchar_t* s, wchar_t** end)
{
    (void)s;
    (void)end;
    return 43;
}

int clock_gettime(clockid_t clock, struct timespec* tp)
{
    (void)clock;
    tp->tv_sec = 44;
    tp->tv_nsec = 0;
    return 0;
}

int usleep(useconds_t us)
{
    (void)us;
    return 45;
}

long long wcstoll(const wchar_t* s, wchar_t** end, int base)
{
    (void)s;
    (void)end;
    (void)base;
    return 46;
}

int main(void)
{
    struct timespec ts = {0, 1000};

    if (strtold("1", NULL) != 42 || wcstold(L"1", NULL) != 43 || clock_gettime(CLOCK_REALTIME, &ts) != 0 ||
        ts.tv_sec != 44 || usleep(1) != 45 || wcstoll(L"1", NULL, 10) != 46)
    {
        printf("names: own functions not called\n");
        return 1;
    }
    ts.tv_sec = 0;
    if (strtod("1.5", NULL) != 1.5 || nanosleep(&ts, NULL) != 0 || wcstoull(L"7", NULL, 10) != 7)
    {
        printf("names: functions of NtCRT failed\n");
        return 1;
    }
    printf("names: ok\n");
    return 0;
}
