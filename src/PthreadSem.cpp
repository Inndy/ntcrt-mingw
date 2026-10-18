/* POSIX semaphores without a name, in the word of sem_t */

#include "NtCRT.h"

#include <pthread.h>
#include <semaphore.h>

/* sem_t is a pointer: the value in bits 0-31, the number of waiters in bits 32-63 */
#define SEM_VALUE  ((ULONG_PTR)0xFFFFFFFF)
#define SEM_WAITER ((ULONG_PTR)1 << 32)

static int SemError(int Error)
{
    NTCRT_ERRNO = Error;
    return -1;
}

/* Add Delta to the word, if Check accepts it */
template<typename TCheck>
static bool SemUpdate(sem_t* sem, ULONG_PTR* Raw, LONG_PTR Delta, TCheck Check)
{
    void* Old = __atomic_load_n(sem, __ATOMIC_ACQUIRE);

    do
    {
        *Raw = (ULONG_PTR)Old;
        if (!Check(*Raw))
        {
            return false;
        }
    } while (!__atomic_compare_exchange_n(sem, &Old, (void*)(*Raw + (ULONG_PTR)Delta), false, __ATOMIC_ACQ_REL,
                                          __ATOMIC_ACQUIRE));
    return true;
}

static bool SemTake(sem_t* sem, ULONG_PTR* Raw)
{
    return SemUpdate(sem, Raw, -1, [](ULONG_PTR Value) { return (Value & SEM_VALUE) != 0; });
}

/* A cancelled thread leaves a post to another waiter */
static void SemPass(void* Object)
{
    _NtCRT_Wake(Object, FALSE);
}

/* Wait until Time of CLOCK_REALTIME if not NULL, a cancellation point */
static int SemWait(sem_t* sem, const struct timespec* Time)
{
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER Wait = NULL;
    ULONG_PTR Raw;
    NTSTATUS Status;

    _NtCRT_CancelPoint(NULL, NULL);
    if (SemTake(sem, &Raw))
    {
        return 0;
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
        if (!SemUpdate(sem, &Raw, SEM_WAITER, [](ULONG_PTR Value) { return (Value & SEM_VALUE) == 0; }))
        {
            if (SemTake(sem, &Raw))
            {
                return 0;
            }
            continue;
        }
        Status = _NtCRT_Wait(sem, Raw + SEM_WAITER, sizeof(*sem), Wait, TRUE);
        SemUpdate(sem, &Raw, -(LONG_PTR)SEM_WAITER, [](ULONG_PTR) { return true; });
        _NtCRT_CancelPoint(SemPass, sem);
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
    *sem = (void*)(ULONG_PTR)value;
    return 0;
}

NTCRT_API int sem_destroy(sem_t* sem)
{
    if (((ULONG_PTR)__atomic_load_n(sem, __ATOMIC_ACQUIRE) & ~SEM_VALUE) != 0)
    {
        return SemError(EBUSY);
    }
    return 0;
}

NTCRT_API int sem_trywait(sem_t* sem)
{
    ULONG_PTR Raw;

    return SemTake(sem, &Raw) ? 0 : SemError(EAGAIN);
}

NTCRT_API int sem_wait(sem_t* sem)
{
    return SemWait(sem, NULL);
}

NTCRT_API int sem_timedwait(sem_t* sem, const struct timespec* t)
{
    return SemWait(sem, t);
}

NTCRT_API int sem_post_multiple(sem_t* sem, int count)
{
    ULONG_PTR Raw;

    if (count <= 0)
    {
        return SemError(EINVAL);
    }
    if (!SemUpdate(sem, &Raw, count, [count](ULONG_PTR Value) {
            return (Value & SEM_VALUE) <= (ULONG_PTR)(SEM_VALUE_MAX - count);
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

NTCRT_API int sem_getvalue(sem_t* sem, int* sval)
{
    *sval = (int)((ULONG_PTR)__atomic_load_n(sem, __ATOMIC_ACQUIRE) & SEM_VALUE);
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
