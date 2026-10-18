/* sched_yield, the scheduling stubs of POSIX and the processors and concurrency of winpthreads */

#include "NtCRT.h"

#include <pthread.h>

NTCRT_API int sched_yield(void)
{
    NtYieldExecution();
    return 0;
}

/* SCHED_OTHER only, the priorities of pthread_setschedparam are those of Windows */

static int SchedPolicyError(int pol)
{
    if (pol < SCHED_MIN || pol > SCHED_MAX)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return 0;
}

NTCRT_API int sched_get_priority_min(int pol)
{
    return SchedPolicyError(pol) != 0 ? -1 : THREAD_PRIORITY_IDLE;
}

NTCRT_API int sched_get_priority_max(int pol)
{
    return SchedPolicyError(pol) != 0 ? -1 : THREAD_PRIORITY_TIME_CRITICAL;
}

NTCRT_API int sched_getscheduler(pid_t pid)
{
    UNREFERENCED_PARAMETER(pid);
    return SCHED_OTHER;
}

NTCRT_API int sched_setscheduler(pid_t pid, int pol, const struct sched_param* param)
{
    UNREFERENCED_PARAMETER(pid);
    UNREFERENCED_PARAMETER(param);
    if (SchedPolicyError(pol) != 0)
    {
        return -1;
    }
    if (pol != SCHED_OTHER)
    {
        NTCRT_ERRNO = ENOTSUP;
        return -1;
    }
    return SCHED_OTHER;
}

/* The processors of the affinity of the process */

static KAFFINITY ProcessAffinity(void)
{
    PROCESS_BASIC_INFORMATION Info;

    if (!NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessBasicInformation, &Info, sizeof(Info), NULL)))
    {
        return 1;
    }
    return Info.AffinityMask;
}

NTCRT_API int pthread_num_processors_np(void)
{
    int Count = __builtin_popcountll(ProcessAffinity());

    return Count != 0 ? Count : 1;
}

/* Keep the first n processors of the affinity, the number kept is returned */
NTCRT_API int pthread_set_num_processors_np(int n)
{
    KAFFINITY Affinity = ProcessAffinity();
    KAFFINITY Kept = 0;
    int Count = 0;

    for (int Bit = 0; Bit < 64 && Count < n; Bit++)
    {
        if ((Affinity & ((KAFFINITY)1 << Bit)) != 0)
        {
            Kept |= (KAFFINITY)1 << Bit;
            Count++;
        }
    }
    if (Count == 0 ||
        !NT_SUCCESS(NtSetInformationProcess(NtCurrentProcess(), ProcessAffinityMask, &Kept, sizeof(Kept))))
    {
        return pthread_num_processors_np();
    }
    return Count;
}

/* The concurrency level is kept, it has no effect */

static int g_Concurrency;

NTCRT_API int pthread_getconcurrency(void)
{
    return g_Concurrency;
}

NTCRT_API int pthread_setconcurrency(int new_level)
{
    if (new_level < 0)
    {
        return EINVAL;
    }
    g_Concurrency = new_level;
    return 0;
}

NTCRT_API int pthread_get_concurrency(int* val)
{
    *val = g_Concurrency;
    return 0;
}

NTCRT_API int pthread_set_concurrency(int val)
{
    return pthread_setconcurrency(val);
}

/* Nothing to do: the waits follow changes of the clock, the thread functions need no initialization */

NTCRT_API void* pthread_timechange_handler_np(void* dummy)
{
    UNREFERENCED_PARAMETER(dummy);
    return NULL;
}

NTCRT_API void pthread_tls_init(void)
{
}

/* The pointers that WINPTHREADS_USE_DLLIMPORT refers to (__declspec(dllimport) of the winpthreads headers) */
NTCRT_IMPORT_ONLY(sched_yield, sched_yield);
NTCRT_IMPORT_ONLY(sched_get_priority_min, sched_get_priority_min);
NTCRT_IMPORT_ONLY(sched_get_priority_max, sched_get_priority_max);
NTCRT_IMPORT_ONLY(sched_getscheduler, sched_getscheduler);
NTCRT_IMPORT_ONLY(sched_setscheduler, sched_setscheduler);
NTCRT_IMPORT_ONLY(pthread_num_processors_np, pthread_num_processors_np);
NTCRT_IMPORT_ONLY(pthread_set_num_processors_np, pthread_set_num_processors_np);
NTCRT_IMPORT_ONLY(pthread_getconcurrency, pthread_getconcurrency);
NTCRT_IMPORT_ONLY(pthread_setconcurrency, pthread_setconcurrency);
NTCRT_IMPORT_ONLY(pthread_get_concurrency, pthread_get_concurrency);
NTCRT_IMPORT_ONLY(pthread_set_concurrency, pthread_set_concurrency);
NTCRT_IMPORT_ONLY(pthread_tls_init, pthread_tls_init);
NTCRT_IMPORT_ONLY(pthread_timechange_handler_np, pthread_timechange_handler_np);
