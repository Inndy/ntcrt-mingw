/* POSIX semaphores without a name, in the word of sem_t on x64, in a block it points to on x86 */

#include "NtCRT.h"

#include <pthread.h>
#include <semaphore.h>

/*
 * sem_t is a pointer: the value in bits 0-31, the number of waiters in bits 32-63. On x86 the pointer has 32 bits: it
 * points to the state in the heap, as in winpthreads.
 */
#define SEM_VALUE  ((ULONG64)0xFFFFFFFF)
#define SEM_WAITER ((ULONG64)1 << 32)

typedef volatile ULONG64 SEM_STATE;

/* The state, NULL for a semaphore destroyed on x86 */
static SEM_STATE* SemState(sem_t* sem)
{
    return NTCRT_X86_X64((SEM_STATE*)__atomic_load_n(sem, __ATOMIC_ACQUIRE), (SEM_STATE*)sem);
}

static int SemError(int Error)
{
    NTCRT_ERRNO = Error;
    return -1;
}

/* Add Delta to the word, if Check accepts it */
template<typename TCheck>
static bool SemUpdate(SEM_STATE* sem, ULONG64* Raw, LONG64 Delta, TCheck Check)
{
    ULONG64 Old = __atomic_load_n(sem, __ATOMIC_ACQUIRE);

    do
    {
        *Raw = Old;
        if (!Check(*Raw))
        {
            return false;
        }
    } while (!__atomic_compare_exchange_n(sem, &Old, *Raw + (ULONG64)Delta, false, __ATOMIC_ACQ_REL,
                                          __ATOMIC_ACQUIRE));
    return true;
}

static bool SemTake(SEM_STATE* sem, ULONG64* Raw)
{
    return SemUpdate(sem, Raw, -1, [](ULONG64 Value) { return (Value & SEM_VALUE) != 0; });
}

/* A cancelled thread leaves a post to another waiter */
static void SemPass(void* Object)
{
    _NtCRT_Wake(Object, FALSE);
}

/* Wait until Time of CLOCK_REALTIME if not NULL, a cancellation point */
static int SemWait(sem_t* sp, const struct timespec* Time)
{
    SEM_STATE* sem = SemState(sp);
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER Wait = NULL;
    ULONG64 Raw;
    NTSTATUS Status;

    _NtCRT_CancelPoint(NULL, NULL);
    if (sem == NULL)
    {
        return SemError(EINVAL);
    }
    for (int Spins = _NtCRT_Spins();; Spins--)
    {
        if (SemTake(sem, &Raw))
        {
            return 0;
        }
        if (Spins == 0 || (Raw & ~SEM_VALUE) != 0)
        {
            break;
        }
        YieldProcessor();
    }
    if (Time != NULL)
    {
        if (!NTCRT_TIMESPEC_VALID(Time))
        {
            return SemError(EINVAL);
        }
        Wait = _NtCRT_Timeout(_NtCRT_TimespecUnits(Time), CLOCK_REALTIME, &Timeout);
    }
    for (;;)
    {
        /* Count the waiter while the value is 0, or take it */
        if (!SemUpdate(sem, &Raw, SEM_WAITER, [](ULONG64 Value) { return (Value & SEM_VALUE) == 0; }))
        {
            if (SemTake(sem, &Raw))
            {
                return 0;
            }
            continue;
        }
        Status = _NtCRT_Wait(sem, Raw + SEM_WAITER, sizeof(*sem), Wait, NTCRT_WAIT_CANCEL);
        SemUpdate(sem, &Raw, -(LONG64)SEM_WAITER, [](ULONG64) { return true; });
        _NtCRT_CancelPoint(SemPass, (void*)sem);
        if (SemTake(sem, &Raw))
        {
            return 0;
        }
        if (Status == STATUS_TIMEOUT)
        {
            return SemError(ETIMEDOUT);
        }
    }
}

NTCRT_API int sem_init(sem_t* sem, int pshared, unsigned int value)
{
    if (value > SEM_VALUE_MAX)
    {
        return SemError(EINVAL);
    }
    if (pshared != 0)
    {
        return SemError(ENOSYS);
    }
#ifdef _WIN64
    *sem = (void*)(ULONG_PTR)value;
#else
    *sem = RtlAllocateHeap(NTCRT_HEAP, 0, sizeof(ULONG64));
    if (*sem == NULL)
    {
        return SemError(ENOSPC);
    }
    *(ULONG64*)*sem = value;
#endif
    return 0;
}

NTCRT_API int sem_destroy(sem_t* sp)
{
    SEM_STATE* sem = SemState(sp);

    if (sem == NULL)
    {
        return SemError(EINVAL);
    }
    if ((__atomic_load_n(sem, __ATOMIC_ACQUIRE) & ~SEM_VALUE) != 0)
    {
        return SemError(EBUSY);
    }
#ifndef _WIN64
    RtlFreeHeap(NTCRT_HEAP, 0, (void*)sem);
    *sp = NULL;
#endif
    return 0;
}

NTCRT_API int sem_trywait(sem_t* sp)
{
    SEM_STATE* sem = SemState(sp);
    ULONG64 Raw;

    if (sem == NULL)
    {
        return SemError(EINVAL);
    }
    return SemTake(sem, &Raw) ? 0 : SemError(EAGAIN);
}

NTCRT_API int sem_wait(sem_t* sem)
{
    return SemWait(sem, NULL);
}

NTCRT_API int _NtCRT_sem_timedwait(sem_t* sem, const struct timespec* t)
{
    return SemWait(sem, t);
}

NTCRT_API int sem_post_multiple(sem_t* sp, int count)
{
    SEM_STATE* sem = SemState(sp);
    ULONG64 Raw;

    if (sem == NULL || count <= 0)
    {
        return SemError(EINVAL);
    }
    if (!SemUpdate(sem, &Raw, count, [count](ULONG64 Value) {
            return (Value & SEM_VALUE) <= (ULONG64)(SEM_VALUE_MAX - count);
        }))
    {
        return SemError(EOVERFLOW);
    }
    if ((Raw & ~SEM_VALUE) != 0)
    {
        _NtCRT_Wake(sem, count > 1);
    }
    return 0;
}

NTCRT_API int sem_post(sem_t* sem)
{
    return sem_post_multiple(sem, 1);
}

NTCRT_API int sem_getvalue(sem_t* sp, int* sval)
{
    SEM_STATE* sem = SemState(sp);

    if (sem == NULL)
    {
        return SemError(EINVAL);
    }
    *sval = (int)(__atomic_load_n(sem, __ATOMIC_ACQUIRE) & SEM_VALUE);
    return 0;
}

/* Named semaphores are not supported */

NTCRT_API sem_t* sem_open(const char* name, int oflag, mode_t mode, unsigned int value)
{
    UNREFERENCED_PARAMETER(name);
    UNREFERENCED_PARAMETER(oflag);
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(value);
    NTCRT_ERRNO = ENOSYS;
    return SEM_FAILED;
}

NTCRT_API int sem_close(sem_t* sem)
{
    UNREFERENCED_PARAMETER(sem);
    return SemError(ENOSYS);
}

NTCRT_API int sem_unlink(const char* name)
{
    UNREFERENCED_PARAMETER(name);
    return SemError(ENOSYS);
}

/* The pointers that WINPTHREADS_USE_DLLIMPORT refers to (__declspec(dllimport) of the winpthreads headers) */
NTCRT_IMPORT_ONLY(sem_init, sem_init);
NTCRT_IMPORT_ONLY(sem_destroy, sem_destroy);
NTCRT_IMPORT_ONLY(sem_trywait, sem_trywait);
NTCRT_IMPORT_ONLY(sem_wait, sem_wait);
NTCRT_IMPORT(sem_timedwait, _NtCRT_sem_timedwait);
NTCRT_IMPORT(sem_timedwait64, _NtCRT_sem_timedwait);
NTCRT_IMPORT_ONLY(sem_post_multiple, sem_post_multiple);
NTCRT_IMPORT_ONLY(sem_post, sem_post);
NTCRT_IMPORT_ONLY(sem_getvalue, sem_getvalue);
NTCRT_IMPORT_ONLY(sem_open, sem_open);
NTCRT_IMPORT_ONLY(sem_close, sem_close);
NTCRT_IMPORT_ONLY(sem_unlink, sem_unlink);
