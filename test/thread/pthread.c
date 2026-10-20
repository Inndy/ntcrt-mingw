// The pthread functions of NtCRT: races of several threads, the mutex types, timeouts, keys, once, cancellation

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <process.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <windows.h>

#define THREADS 8

static int g_Failures;

#define CHECK(Cond)                                                                                                   \
    do                                                                                                                \
    {                                                                                                                 \
        if (!(Cond))                                                                                                  \
        {                                                                                                             \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #Cond);                                                         \
            g_Failures++;                                                                                             \
        }                                                                                                             \
    } while (0)

static long long NowMs(void)
{
    struct timespec Time;

    clock_gettime(CLOCK_MONOTONIC, &Time);
    return Time.tv_sec * 1000LL + Time.tv_nsec / 1000000;
}

static long long NowNs(void)
{
    struct timespec Time;

    clock_gettime(CLOCK_MONOTONIC, &Time);
    return Time.tv_sec * 1000000000LL + Time.tv_nsec;
}

static struct timespec RealtimeIn(long Ms)
{
    struct timespec Time;

    clock_gettime(CLOCK_REALTIME, &Time);
    Time.tv_sec += Ms / 1000;
    Time.tv_nsec += Ms % 1000 * 1000000;
    if (Time.tv_nsec >= 1000000000)
    {
        Time.tv_sec++;
        Time.tv_nsec -= 1000000000;
    }
    return Time;
}

static void SleepMs(long Ms)
{
    struct timespec Time = { Ms / 1000, Ms % 1000 * 1000000 };

    nanosleep(&Time, NULL);
}

static void RunThreads(void* (*Func)(void*), void* Arg)
{
    pthread_t Threads[THREADS];

    for (int i = 0; i < THREADS; i++)
    {
        CHECK(pthread_create(&Threads[i], NULL, Func, Arg) == 0);
    }
    for (int i = 0; i < THREADS; i++)
    {
        CHECK(pthread_join(Threads[i], NULL) == 0);
    }
}

/* Mutexes */

static pthread_mutex_t g_Mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_Recursive = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
static long g_Counter;

static void* MutexHammer(void* Arg)
{
    pthread_mutex_t* Mutex = Arg;

    for (int i = 0; i < 100000; i++)
    {
        pthread_mutex_lock(Mutex);
        if (Mutex == &g_Recursive)
        {
            pthread_mutex_lock(Mutex);
            g_Counter++;
            pthread_mutex_unlock(Mutex);
        } else
        {
            g_Counter++;
        }
        pthread_mutex_unlock(Mutex);
    }
    return NULL;
}

static void* TryLockOther(void* Arg)
{
    return (void*)(intptr_t)pthread_mutex_trylock(Arg);
}

static void* HoldMutex(void* Arg)
{
    pthread_mutex_lock(Arg);
    SleepMs(200);
    pthread_mutex_unlock(Arg);
    return NULL;
}

static void* UnlockOther(void* Arg)
{
    return (void*)(intptr_t)pthread_mutex_unlock(Arg);
}

static void TestMutex(void)
{
    pthread_mutex_t Mutex;
    pthread_mutexattr_t Attr;
    pthread_t Thread;
    void* Result;
    int Type;

    g_Counter = 0;
    RunThreads(MutexHammer, &g_Mutex);
    CHECK(g_Counter == THREADS * 100000);
    g_Counter = 0;
    RunThreads(MutexHammer, &g_Recursive);
    CHECK(g_Counter == THREADS * 100000);

    /* Recursive: levels, trylock of the owner and of another thread */
    CHECK(pthread_mutex_lock(&g_Recursive) == 0);
    CHECK(pthread_mutex_lock(&g_Recursive) == 0);
    CHECK(pthread_mutex_trylock(&g_Recursive) == 0);
    CHECK(pthread_create(&Thread, NULL, TryLockOther, &g_Recursive) == 0);
    CHECK(pthread_join(Thread, &Result) == 0 && (intptr_t)Result == EBUSY);
    CHECK(pthread_create(&Thread, NULL, UnlockOther, &g_Recursive) == 0);
    CHECK(pthread_join(Thread, &Result) == 0 && (intptr_t)Result == EPERM);
    CHECK(pthread_mutex_unlock(&g_Recursive) == 0);
    CHECK(pthread_mutex_unlock(&g_Recursive) == 0);
    CHECK(pthread_mutex_destroy(&g_Recursive) == EBUSY);
    CHECK(pthread_mutex_unlock(&g_Recursive) == 0);
    CHECK(pthread_mutex_unlock(&g_Recursive) == EPERM);
    CHECK(pthread_mutex_destroy(&g_Recursive) == 0);

    /* Errorcheck by an attribute and by the initializer */
    CHECK(pthread_mutexattr_init(&Attr) == 0);
    CHECK(pthread_mutexattr_gettype(&Attr, &Type) == 0 && Type == PTHREAD_MUTEX_DEFAULT);
    CHECK(pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_ERRORCHECK) == 0);
    CHECK(pthread_mutexattr_settype(&Attr, 7) == EINVAL);
    CHECK(pthread_mutexattr_gettype(&Attr, &Type) == 0 && Type == PTHREAD_MUTEX_ERRORCHECK);
    CHECK(pthread_mutexattr_setpshared(&Attr, PTHREAD_PROCESS_SHARED) == ENOTSUP);
    CHECK(pthread_mutex_init(&Mutex, &Attr) == 0);
    CHECK(pthread_mutexattr_destroy(&Attr) == 0);
    for (int Pass = 0; Pass < 2; Pass++)
    {
        CHECK(pthread_mutex_unlock(&Mutex) == EPERM);
        CHECK(pthread_mutex_lock(&Mutex) == 0);
        CHECK(pthread_mutex_lock(&Mutex) == EDEADLK);
        CHECK(pthread_mutex_trylock(&Mutex) == EBUSY);
        CHECK(pthread_create(&Thread, NULL, UnlockOther, &Mutex) == 0);
        CHECK(pthread_join(Thread, &Result) == 0 && (intptr_t)Result == EPERM);
        CHECK(pthread_mutex_unlock(&Mutex) == 0);
        Mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER;
    }

    /* Normal: trylock of the owner fails, timedlock times out */
    Mutex = PTHREAD_MUTEX_INITIALIZER;
    CHECK(pthread_mutex_trylock(&Mutex) == 0);
    CHECK(pthread_mutex_trylock(&Mutex) == EBUSY);
    CHECK(pthread_mutex_unlock(&Mutex) == 0);
    CHECK(pthread_create(&Thread, NULL, HoldMutex, &Mutex) == 0);
    SleepMs(50);
    {
        struct timespec Time = RealtimeIn(50);
        struct timespec Bad = { 0, 1000000000 };
        long long Start = NowMs();

        CHECK(pthread_mutex_timedlock(&Mutex, &Bad) == EINVAL);
        CHECK(pthread_mutex_timedlock(&Mutex, &Time) == ETIMEDOUT);
        CHECK(NowMs() - Start >= 40);
        Time = RealtimeIn(5000);
        CHECK(pthread_mutex_timedlock(&Mutex, &Time) == 0);
        CHECK(NowMs() - Start < 4000);
        CHECK(pthread_mutex_unlock(&Mutex) == 0);
    }
    CHECK(pthread_join(Thread, NULL) == 0);
    /* Zeroed is unlocked normal */
    memset(&Mutex, 0, sizeof(Mutex));
    CHECK(pthread_mutex_lock(&Mutex) == 0);
    CHECK(pthread_mutex_unlock(&Mutex) == 0);
}

/* Condition variables: a queue of producers and consumers, timeouts */

static pthread_cond_t g_NotEmpty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_NotFull = PTHREAD_COND_INITIALIZER;
static int g_Queue[16];
static int g_QueueCount;
static long g_Consumed;
static long g_ConsumedSum;

#define ITEMS 20000

static void* Producer(void* Arg)
{
    (void)Arg;
    for (int i = 1; i <= ITEMS; i++)
    {
        pthread_mutex_lock(&g_Mutex);
        while (g_QueueCount == 16)
        {
            pthread_cond_wait(&g_NotFull, &g_Mutex);
        }
        g_Queue[g_QueueCount++] = i;
        pthread_cond_signal(&g_NotEmpty);
        pthread_mutex_unlock(&g_Mutex);
    }
    return NULL;
}

static void* Consumer(void* Arg)
{
    (void)Arg;
    for (;;)
    {
        int Item;

        pthread_mutex_lock(&g_Mutex);
        while (g_QueueCount == 0)
        {
            pthread_cond_wait(&g_NotEmpty, &g_Mutex);
        }
        Item = g_Queue[--g_QueueCount];
        if (Item != 0)
        {
            g_Consumed++;
            g_ConsumedSum += Item;
        }
        pthread_cond_broadcast(&g_NotFull);
        pthread_mutex_unlock(&g_Mutex);
        if (Item == 0)
        {
            return NULL;
        }
    }
}

static volatile int g_FarSignaled;

static void* FarSignal(void* Arg)
{
    SleepMs(100);
    pthread_mutex_lock(&g_Mutex);
    g_FarSignaled = 1;
    pthread_cond_signal(&g_NotEmpty);
    pthread_mutex_unlock(&g_Mutex);
    return Arg;
}

/*
 * POSIX lets a thread destroy a condition once no thread is blocked on it: the first waiter that a broadcast woke
 * destroys it and decommits its page while the others may still be in pthread_cond_wait
 */
#define FREED_WAITERS 6
#define FREED_ROUNDS  4000

static pthread_cond_t* volatile g_FreedCond;
static volatile LONG g_FreedRound, g_FreedDone, g_FreedWaiting, g_FreedArrived, g_FreedBy;

static void* FreedWaiter(void* Arg)
{
    (void)Arg;
    for (LONG Round = 1; Round <= FREED_ROUNDS; Round++)
    {
        pthread_cond_t* Cond;

        while (g_FreedRound < Round)
        {
            YieldProcessor();
        }
        Cond = g_FreedCond;
        pthread_mutex_lock(&g_Mutex);
        g_FreedWaiting++;
        while (g_FreedDone != Round)
        {
            pthread_cond_wait(Cond, &g_Mutex);
        }
        pthread_mutex_unlock(&g_Mutex);
        if (InterlockedCompareExchange(&g_FreedBy, Round, Round - 1) == Round - 1)
        {
            CHECK(pthread_cond_destroy(Cond) == 0);
            VirtualFree(Cond, 4096, MEM_DECOMMIT);
        }
        InterlockedIncrement(&g_FreedArrived);
    }
    return NULL;
}

static void* BlockedWaiter(void* Arg)
{
    pthread_mutex_lock(&g_Mutex);
    g_FreedWaiting = 1;
    while (g_FreedDone == 0)
    {
        pthread_cond_wait((pthread_cond_t*)Arg, &g_Mutex);
    }
    pthread_mutex_unlock(&g_Mutex);
    return NULL;
}

static void TestCondFreed(void)
{
    pthread_t Threads[FREED_WAITERS];
    pthread_cond_t Cond = PTHREAD_COND_INITIALIZER;
    char* Pages = (char*)VirtualAlloc(NULL, 64 * 4096, MEM_RESERVE, PAGE_READWRITE);

    for (int i = 0; i < FREED_WAITERS; i++)
    {
        CHECK(pthread_create(&Threads[i], NULL, FreedWaiter, NULL) == 0);
    }
    for (LONG Round = 1; Round <= FREED_ROUNDS; Round++)
    {
        pthread_cond_t* Fresh = (pthread_cond_t*)VirtualAlloc(Pages + Round % 64 * 4096, 4096, MEM_COMMIT,
                                                              PAGE_READWRITE);

        pthread_cond_init(Fresh, NULL);
        g_FreedCond = Fresh;
        g_FreedArrived = 0;
        g_FreedRound = Round;
        /* Each waiter counted itself under the mutex, which it released in pthread_cond_wait: all are blocked */
        for (;;)
        {
            pthread_mutex_lock(&g_Mutex);
            if (g_FreedWaiting == FREED_WAITERS)
            {
                break;
            }
            pthread_mutex_unlock(&g_Mutex);
            YieldProcessor();
        }
        g_FreedWaiting = 0;
        g_FreedDone = Round;
        pthread_cond_broadcast(Fresh);
        pthread_mutex_unlock(&g_Mutex);
        while (g_FreedArrived != FREED_WAITERS)
        {
            YieldProcessor();
        }
    }
    for (int i = 0; i < FREED_WAITERS; i++)
    {
        CHECK(pthread_join(Threads[i], NULL) == 0);
    }
    VirtualFree(Pages, 0, MEM_RELEASE);

    /* A thread blocked on the condition: EBUSY */
    g_FreedWaiting = 0;
    g_FreedDone = 0;
    CHECK(pthread_create(&Threads[0], NULL, BlockedWaiter, &Cond) == 0);
    for (;;)
    {
        pthread_mutex_lock(&g_Mutex);
        if (g_FreedWaiting)
        {
            break;
        }
        pthread_mutex_unlock(&g_Mutex);
        SleepMs(1);
    }
    pthread_mutex_unlock(&g_Mutex);
    SleepMs(50);
    CHECK(pthread_cond_destroy(&Cond) == EBUSY);
    pthread_mutex_lock(&g_Mutex);
    g_FreedDone = 1;
    pthread_cond_signal(&Cond);
    pthread_mutex_unlock(&g_Mutex);
    CHECK(pthread_join(Threads[0], NULL) == 0);
    CHECK(pthread_cond_destroy(&Cond) == 0);
}

static void TestCond(void)
{
    pthread_t Producers[4], Consumers[4];
    pthread_cond_t Cond;
    pthread_condattr_t Attr;
    clockid_t Clock;

    for (int i = 0; i < 4; i++)
    {
        CHECK(pthread_create(&Producers[i], NULL, Producer, NULL) == 0);
        CHECK(pthread_create(&Consumers[i], NULL, Consumer, NULL) == 0);
    }
    for (int i = 0; i < 4; i++)
    {
        CHECK(pthread_join(Producers[i], NULL) == 0);
    }
    /* A 0 for each consumer ends it */
    for (int i = 0; i < 4; i++)
    {
        pthread_mutex_lock(&g_Mutex);
        while (g_QueueCount == 16)
        {
            pthread_cond_wait(&g_NotFull, &g_Mutex);
        }
        g_Queue[g_QueueCount++] = 0;
        pthread_cond_signal(&g_NotEmpty);
        pthread_mutex_unlock(&g_Mutex);
    }
    for (int i = 0; i < 4; i++)
    {
        CHECK(pthread_join(Consumers[i], NULL) == 0);
    }
    CHECK(g_Consumed == 4L * ITEMS);
    CHECK(g_ConsumedSum == 4L * ITEMS * (ITEMS + 1) / 2);

    /* Timeouts of CLOCK_REALTIME, of CLOCK_MONOTONIC and relative */
    pthread_mutex_lock(&g_Mutex);
    {
        struct timespec Time = RealtimeIn(60);
        struct timespec Bad = { 0, -1 };
        long long Start = NowMs();

        CHECK(pthread_cond_timedwait(&g_NotEmpty, &g_Mutex, &Bad) == EINVAL);
        while (pthread_cond_timedwait(&g_NotEmpty, &g_Mutex, &Time) == 0)
        {
        }
        CHECK(NowMs() - Start >= 50);
        CHECK(pthread_mutex_trylock(&g_Mutex) == EBUSY); /* Held again */

        CHECK(pthread_condattr_init(&Attr) == 0);
        CHECK(pthread_condattr_setclock(&Attr, CLOCK_MONOTONIC) == 0);
        CHECK(pthread_condattr_setclock(&Attr, CLOCK_PROCESS_CPUTIME_ID) == EINVAL);
        CHECK(pthread_condattr_getclock(&Attr, &Clock) == 0 && Clock == CLOCK_MONOTONIC);
        CHECK(pthread_cond_init(&Cond, &Attr) == 0);
        clock_gettime(CLOCK_MONOTONIC, &Time);
        Time.tv_nsec += 60000000;
        if (Time.tv_nsec >= 1000000000)
        {
            Time.tv_sec++;
            Time.tv_nsec -= 1000000000;
        }
        Start = NowMs();
        while (pthread_cond_timedwait(&Cond, &g_Mutex, &Time) == 0)
        {
        }
        CHECK(NowMs() - Start >= 50);
        CHECK(NowMs() - Start < 3000);
        CHECK(pthread_cond_destroy(&Cond) == 0);

        Time.tv_sec = 0;
        Time.tv_nsec = 60000000;
        Start = NowMs();
        CHECK(pthread_cond_timedwait_relative_np(&g_NotEmpty, &g_Mutex, &Time) == ETIMEDOUT);
        CHECK(NowMs() - Start >= 50);
        /* The longest relative time short of none (29000 years, 68 with a 32-bit time_t) waits for the signal */
        {
            pthread_t Signaler;

            g_FarSignaled = 0;
            Start = NowMs();
            CHECK(pthread_create(&Signaler, NULL, FarSignal, NULL) == 0);
            while (!g_FarSignaled)
            {
                Time.tv_sec = sizeof(time_t) == 8 ? (time_t)922337203683LL : (time_t)0x7FFFFFFF;
                Time.tv_nsec = 999999999;
                CHECK(pthread_cond_timedwait_relative_np(&g_NotEmpty, &g_Mutex, &Time) == 0);
            }
            CHECK(NowMs() - Start >= 50);
            pthread_mutex_unlock(&g_Mutex);
            CHECK(pthread_join(Signaler, NULL) == 0);
            pthread_mutex_lock(&g_Mutex);
        }

        /* A time past */
        Time.tv_sec = 1;
        CHECK(pthread_cond_timedwait(&g_NotEmpty, &g_Mutex, &Time) == ETIMEDOUT);
    }
    pthread_mutex_unlock(&g_Mutex);
    TestCondFreed();
}

/* Read-write locks */

static pthread_rwlock_t g_Rwlock = PTHREAD_RWLOCK_INITIALIZER;
static long g_RwA, g_RwB;
static volatile long g_RwBad;

static void* RwHammer(void* Arg)
{
    intptr_t Index = (intptr_t)Arg;

    for (int i = 0; i < 20000; i++)
    {
        if ((i + Index) % 4 == 0)
        {
            pthread_rwlock_wrlock(&g_Rwlock);
            g_RwA++;
            g_RwB++;
            pthread_rwlock_unlock(&g_Rwlock);
        } else
        {
            pthread_rwlock_rdlock(&g_Rwlock);
            if (g_RwA != g_RwB)
            {
                g_RwBad = 1;
            }
            pthread_rwlock_unlock(&g_Rwlock);
        }
    }
    return NULL;
}

static void* RwWriter(void* Arg)
{
    (void)Arg;
    pthread_rwlock_wrlock(&g_Rwlock);
    pthread_rwlock_unlock(&g_Rwlock);
    return NULL;
}

static void* RwTryWrite(void* Arg)
{
    struct timespec Time = RealtimeIn(50);

    (void)Arg;
    if (pthread_rwlock_trywrlock(&g_Rwlock) != EBUSY)
    {
        return (void*)1;
    }
    return (void*)(intptr_t)pthread_rwlock_timedwrlock(&g_Rwlock, &Time);
}

static void TestRwlock(void)
{
    pthread_t Threads[THREADS];
    pthread_t Writer;
    void* Result;

    for (intptr_t i = 0; i < THREADS; i++)
    {
        CHECK(pthread_create(&Threads[i], NULL, RwHammer, (void*)i) == 0);
    }
    for (int i = 0; i < THREADS; i++)
    {
        CHECK(pthread_join(Threads[i], NULL) == 0);
    }
    CHECK(g_RwBad == 0 && g_RwA == THREADS * 5000);

    /* A read lock taken again while a writer waits */
    CHECK(pthread_rwlock_rdlock(&g_Rwlock) == 0);
    CHECK(pthread_create(&Writer, NULL, RwWriter, NULL) == 0);
    SleepMs(50);
    CHECK(pthread_rwlock_rdlock(&g_Rwlock) == 0);
    CHECK(pthread_rwlock_tryrdlock(&g_Rwlock) == 0);
    CHECK(pthread_create(&Threads[0], NULL, RwTryWrite, NULL) == 0);
    CHECK(pthread_join(Threads[0], &Result) == 0 && (intptr_t)Result == ETIMEDOUT);
    for (int i = 0; i < 3; i++)
    {
        CHECK(pthread_rwlock_unlock(&g_Rwlock) == 0);
    }
    CHECK(pthread_join(Writer, NULL) == 0);
    CHECK(pthread_rwlock_unlock(&g_Rwlock) == EPERM);

    CHECK(pthread_rwlock_wrlock(&g_Rwlock) == 0);
    CHECK(pthread_rwlock_wrlock(&g_Rwlock) == EDEADLK);
    CHECK(pthread_rwlock_rdlock(&g_Rwlock) == EDEADLK);
    CHECK(pthread_rwlock_trywrlock(&g_Rwlock) == EBUSY);
    CHECK(pthread_rwlock_tryrdlock(&g_Rwlock) == EBUSY);
    CHECK(pthread_rwlock_destroy(&g_Rwlock) == EBUSY);
    CHECK(pthread_rwlock_unlock(&g_Rwlock) == 0);
    CHECK(pthread_rwlock_destroy(&g_Rwlock) == 0);
}

/* Spin locks, barriers and semaphores */

static pthread_spinlock_t g_Spin = PTHREAD_SPINLOCK_INITIALIZER;
static pthread_barrier_t g_Barrier;
static volatile long g_Serial;
static volatile long g_Phase[THREADS];
static volatile long g_BarrierBad;

static void* SpinHammer(void* Arg)
{
    (void)Arg;
    for (int i = 0; i < 100000; i++)
    {
        pthread_spin_lock(&g_Spin);
        g_Counter++;
        pthread_spin_unlock(&g_Spin);
    }
    return NULL;
}

static void* BarrierHammer(void* Arg)
{
    intptr_t Index = (intptr_t)Arg;

    for (int Round = 0; Round < 1000; Round++)
    {
        g_Phase[Index] = Round;
        if (pthread_barrier_wait(&g_Barrier) == PTHREAD_BARRIER_SERIAL_THREAD)
        {
            __sync_fetch_and_add(&g_Serial, 1);
        }
        for (int i = 0; i < THREADS; i++)
        {
            if (g_Phase[i] < Round)
            {
                g_BarrierBad = 1;
            }
        }
        pthread_barrier_wait(&g_Barrier);
    }
    return NULL;
}

static sem_t g_Sem;
static volatile long g_SemTaken;

static void* SemConsumer(void* Arg)
{
    (void)Arg;
    for (int i = 0; i < 10000; i++)
    {
        sem_wait(&g_Sem);
        __sync_fetch_and_add(&g_SemTaken, 1);
    }
    return NULL;
}

static void* SemProducer(void* Arg)
{
    (void)Arg;
    for (int i = 0; i < 10000; i++)
    {
        sem_post(&g_Sem);
    }
    return NULL;
}

/*
 * A barrier of 2 for 3 threads: rounds complete while a woken thread has not looked yet. A thread left alone at the
 * end is let go by a helper.
 */
static pthread_barrier_t g_Pairs;
static volatile long g_PairRounds;

static void* BarrierPairs(void* Arg)
{
    (void)Arg;
    while (g_PairRounds < 30000)
    {
        if (pthread_barrier_wait(&g_Pairs) == PTHREAD_BARRIER_SERIAL_THREAD)
        {
            __sync_fetch_and_add(&g_PairRounds, 1);
        }
    }
    return NULL;
}

static void* BarrierHelper(void* Arg)
{
    (void)Arg;
    pthread_barrier_wait(&g_Pairs);
    return NULL;
}

/*
 * A barrier of 2 for 3 threads on CPU 0: one released from a round does not run (idle priority) while the others
 * (time critical) would complete 2^20 - 1 rounds, which brought the generation of the word back to its own
 */
static sem_t g_LateGate;
static volatile long g_LateDone;

static void* BarrierLate(void* Arg)
{
    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
    pthread_barrier_wait(&g_Pairs);
    g_LateDone = 1;
    return Arg;
}

static void* BarrierRounds(void* Arg)
{
    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    if (Arg != NULL)
    {
        /* The round of BarrierLate */
        pthread_barrier_wait(&g_Pairs);
        sem_post(&g_LateGate);
    } else
    {
        sem_wait(&g_LateGate);
    }
    for (int i = 0; i < (1 << 20) - 1; i++)
    {
        pthread_barrier_wait(&g_Pairs);
    }
    return NULL;
}

/* The serial thread destroys and initializes the barrier again while the others may not have looked yet */
static pthread_barrier_t g_Reuse;
static pthread_barrier_t g_Fence;
static volatile long g_ReuseBad;

static void* BarrierReuse(void* Arg)
{
    for (int i = 0; i < 1000; i++)
    {
        int Result = pthread_barrier_wait(&g_Reuse);

        if (Result == PTHREAD_BARRIER_SERIAL_THREAD)
        {
            if (pthread_barrier_destroy(&g_Reuse) != 0 || pthread_barrier_init(&g_Reuse, NULL, THREADS) != 0)
            {
                g_ReuseBad = 1;
            }
        } else if (Result != 0)
        {
            g_ReuseBad = 1;
        }
        pthread_barrier_wait(&g_Fence);
    }
    return Arg;
}

/*
 * No wait of NtCRT is alertable: an APC queued to a thread that waits in sem_wait or pthread_cond_wait runs at the
 * next alertable wait of the thread itself (SleepEx). Arg2 of an APC is what it does: count, take a mutex that the
 * thread that queued it holds, or end the thread.
 */
NTSTATUS NTAPI NtQueueApcThread(HANDLE Thread, void(NTAPI* Routine)(void*, void*, void*), void* Arg1, void* Arg2,
                                void* Arg3);
NTSTATUS NTAPI NtImpersonateAnonymousToken(HANDLE Thread);
NTSTATUS NTAPI NtSetInformationThread(HANDLE Thread, int Class, void* Information, ULONG Length);

static volatile long g_ApcRan;
static volatile int g_ApcStarted;
static int g_ApcReady;
static pthread_cond_t g_ApcCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_ApcMutex = PTHREAD_MUTEX_INITIALIZER;

static void NTAPI CountApc(void* Arg1, void* Arg2, void* Arg3)
{
    (void)Arg1;
    (void)Arg3;
    if (Arg2 == (void*)1)
    {
        pthread_mutex_lock(&g_ApcMutex);
        pthread_mutex_unlock(&g_ApcMutex);
    } else if (Arg2 == (void*)2)
    {
        pthread_exit((void*)42);
    }
    __sync_fetch_and_add(&g_ApcRan, 1);
}

/* Waits on the semaphore Arg (or the condition if NULL), returns 10 * the APCs run before + those after SleepEx */
static void* WaitApc(void* Arg)
{
    long Before;

    g_ApcStarted = 1;
    if (Arg != NULL)
    {
        CHECK(sem_wait(Arg) == 0);
    } else
    {
        pthread_mutex_lock(&g_Mutex);
        while (!g_ApcReady)
        {
            pthread_cond_wait(&g_ApcCond, &g_Mutex);
        }
        pthread_mutex_unlock(&g_Mutex);
    }
    Before = g_ApcRan;
    SleepEx(0, TRUE);
    return (void*)(intptr_t)(Before * 10 + g_ApcRan);
}

/* Starts WaitApc: an APC queued before the thread runs would run at its start, the thread then waits */
static void StartWaitApc(pthread_t* Thread, void* Arg)
{
    g_ApcStarted = 0;
    CHECK(pthread_create(Thread, NULL, WaitApc, Arg) == 0);
    while (!g_ApcStarted)
    {
        SleepMs(1);
    }
    SleepMs(30);
}

/* Overwrites the stack that an ended thread left */
static void* FillStack(void* Arg)
{
    volatile char Fill[65536];

    memset((char*)Fill, 0xCC, sizeof(Fill));
    return Arg;
}

static void TestSync(void)
{
    pthread_t Threads[THREADS];
    int Value;

    g_Counter = 0;
    RunThreads(SpinHammer, NULL);
    CHECK(g_Counter == THREADS * 100000);
    CHECK(pthread_spin_trylock(&g_Spin) == 0);
    CHECK(pthread_spin_trylock(&g_Spin) == EBUSY);
    CHECK(pthread_spin_unlock(&g_Spin) == 0);

    CHECK(pthread_barrier_init(&g_Barrier, NULL, 0) == EINVAL);
    CHECK(pthread_barrier_init(&g_Barrier, NULL, THREADS) == 0);
    for (intptr_t i = 0; i < THREADS; i++)
    {
        CHECK(pthread_create(&Threads[i], NULL, BarrierHammer, (void*)i) == 0);
    }
    for (int i = 0; i < THREADS; i++)
    {
        CHECK(pthread_join(Threads[i], NULL) == 0);
    }
    CHECK(g_Serial == 1000 && g_BarrierBad == 0);
    CHECK(pthread_barrier_destroy(&g_Barrier) == 0);
    /* Destroyed: no barrier, as one of count 0 */
    CHECK(pthread_barrier_destroy(&g_Barrier) == EINVAL);
    CHECK(pthread_barrier_wait(&g_Barrier) == EINVAL);
    CHECK(pthread_barrier_init(&g_Pairs, NULL, 2) == 0);
    for (int i = 0; i < 3; i++)
    {
        CHECK(pthread_create(&Threads[i], NULL, BarrierPairs, NULL) == 0);
    }
    {
        long long Start = NowMs();
        int Joined = 0;
        bool Helped = false;

        while (Joined < 3 && NowMs() - Start < 20000)
        {
            for (int i = 0; i < 3; i++)
            {
                if (Threads[i] != 0 && _pthread_tryjoin(Threads[i], NULL) == 0)
                {
                    Threads[i] = 0;
                    Joined++;
                }
            }
            if (Joined == 2 && !Helped && NowMs() - Start > 100 && g_PairRounds >= 30000)
            {
                pthread_t Helper;

                CHECK(pthread_create(&Helper, NULL, BarrierHelper, NULL) == 0);
                pthread_detach(Helper);
                Helped = true;
            }
            SleepMs(5);
        }
        CHECK(Joined == 3);
    }
    CHECK(pthread_barrier_init(&g_Pairs, NULL, 2) == 0 && sem_init(&g_LateGate, 0, 0) == 0);
    CHECK(pthread_create(&Threads[0], NULL, BarrierRounds, NULL) == 0);
    CHECK(pthread_create(&Threads[1], NULL, BarrierLate, NULL) == 0);
    while ((uintptr_t)__atomic_load_n(&g_Pairs, __ATOMIC_RELAXED) == 2)
    {
        SleepMs(1);
    }
    SleepMs(100);
    CHECK(pthread_create(&Threads[2], NULL, BarrierRounds, (void*)1) == 0);
    for (int i = 0; i < 3; i++)
    {
        CHECK(pthread_join(Threads[i], NULL) == 0);
    }
    CHECK(g_LateDone);
    CHECK(pthread_barrier_init(&g_Reuse, NULL, THREADS) == 0 && pthread_barrier_init(&g_Fence, NULL, THREADS) == 0);
    RunThreads(BarrierReuse, NULL);
    CHECK(g_ReuseBad == 0);

    CHECK(sem_init(&g_Sem, 0, 0) == 0);
    CHECK(sem_trywait(&g_Sem) == -1 && errno == EAGAIN);
    {
        struct timespec Time = RealtimeIn(50);
        long long Start = NowMs();

        CHECK(sem_timedwait(&g_Sem, &Time) == -1 && errno == ETIMEDOUT);
        CHECK(NowMs() - Start >= 40);
    }
    for (int i = 0; i < THREADS; i++)
    {
        CHECK(pthread_create(&Threads[i], NULL, i % 2 ? SemProducer : SemConsumer, NULL) == 0);
    }
    for (int i = 0; i < THREADS; i++)
    {
        CHECK(pthread_join(Threads[i], NULL) == 0);
    }
    CHECK(g_SemTaken == THREADS / 2 * 10000);
    CHECK(sem_getvalue(&g_Sem, &Value) == 0 && Value == 0);
    CHECK(sem_post_multiple(&g_Sem, 3) == 0);
    CHECK(sem_getvalue(&g_Sem, &Value) == 0 && Value == 3);
    CHECK(sem_destroy(&g_Sem) == 0);
    CHECK(sem_init(&g_Sem, 1, 0) == -1 && errno == ENOSYS);
    {
        void* Result;

        HANDLE None = NULL;

        /* The APC waits for the semaphore or the condition, then runs in SleepEx */
        CHECK(sem_init(&g_Sem, 0, 0) == 0);
        for (int i = 0; i < 2; i++)
        {
            g_ApcRan = 0;
            StartWaitApc(&Threads[0], i == 0 ? &g_Sem : NULL);
            CHECK(NtQueueApcThread(pthread_gethandle(Threads[0]), CountApc, NULL, NULL, NULL) == 0);
            SleepMs(30);
            CHECK(g_ApcRan == 0);
            if (i == 0)
            {
                CHECK(sem_post(&g_Sem) == 0);
            } else
            {
                pthread_mutex_lock(&g_Mutex);
                g_ApcReady = 1;
                pthread_cond_signal(&g_ApcCond);
                pthread_mutex_unlock(&g_Mutex);
            }
            CHECK(pthread_join(Threads[0], &Result) == 0 && Result == (void*)1);
        }
        /* An APC that ends its thread, which waits in sem_wait (it left a node on a stack that was gone) */
        StartWaitApc(&Threads[0], &g_Sem);
        CHECK(NtQueueApcThread(pthread_gethandle(Threads[0]), CountApc, NULL, (void*)2, NULL) == 0);
        SleepMs(30);
        CHECK(sem_post(&g_Sem) == 0);
        CHECK(pthread_join(Threads[0], &Result) == 0 && Result == (void*)42);
        for (int i = 0; i < 8; i++)
        {
            CHECK(pthread_create(&Threads[i], NULL, FillStack, NULL) == 0);
            CHECK(pthread_join(Threads[i], NULL) == 0);
        }
        CHECK(sem_post(&g_Sem) == 0 && sem_wait(&g_Sem) == 0);
        /*
         * A waker impersonating the anonymous token (which may not open the thread) while the APC waits for a mutex
         * it holds: the post wakes the thread, the APC runs after
         */
        g_ApcRan = 0;
        StartWaitApc(&Threads[0], &g_Sem);
        pthread_mutex_lock(&g_ApcMutex);
        CHECK(NtQueueApcThread(pthread_gethandle(Threads[0]), CountApc, NULL, (void*)1, NULL) == 0);
        SleepMs(30);
        CHECK(NtImpersonateAnonymousToken(GetCurrentThread()) == 0);
        CHECK(sem_post(&g_Sem) == 0);
        CHECK(NtSetInformationThread(GetCurrentThread(), 5 /* ThreadImpersonationToken */, &None, sizeof(None)) == 0);
        SleepMs(30);
        pthread_mutex_unlock(&g_ApcMutex);
        CHECK(pthread_join(Threads[0], &Result) == 0 && Result == (void*)1);
        CHECK(sem_destroy(&g_Sem) == 0);
    }
}

/* Keys: destructors at the return of the start function, at pthread_exit, again for a value set again */

static pthread_key_t g_Key;
static pthread_key_t g_KeyAgain;
static int g_KeyValue;
static volatile long g_Destroyed;
static volatile long g_DestroyedAgain;

static void KeyDestructor(void* Value)
{
    CHECK(Value == (void*)&g_KeyValue);
    CHECK(pthread_getspecific(g_Key) == NULL);
    __sync_fetch_and_add(&g_Destroyed, 1);
}

static void KeyDestructorAgain(void* Value)
{
    /* Set again twice, the destructor runs three times */
    if (__sync_add_and_fetch(&g_DestroyedAgain, 1) % 3 != 0)
    {
        pthread_setspecific(g_KeyAgain, Value);
    }
}

static void* KeyReturn(void* Arg)
{
    CHECK(pthread_getspecific(g_Key) == NULL);
    CHECK(pthread_setspecific(g_Key, &g_KeyValue) == 0);
    CHECK(pthread_getspecific(g_Key) == &g_KeyValue);
    CHECK(pthread_setspecific(g_KeyAgain, &g_KeyValue) == 0);
    if (Arg != NULL)
    {
        pthread_exit(Arg);
    }
    return NULL;
}

/* A destructor runs with cancellation disabled and deferred, a pending cancellation does not act in it */
static pthread_key_t g_KeyCancel;
static volatile long g_CancelOff;

static void KeyCancelDestructor(void* Value)
{
    int State, Type;

    (void)Value;
    CHECK(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &State) == 0 && State == PTHREAD_CANCEL_DISABLE);
    CHECK(pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &Type) == 0 && Type == PTHREAD_CANCEL_DEFERRED);
    CHECK(pthread_setcancelstate(State, NULL) == 0);
    pthread_testcancel();
    __sync_fetch_and_add(&g_CancelOff, 1);
}

static void* KeyCancel(void* Arg)
{
    CHECK(pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL) == 0);
    CHECK(pthread_setspecific(g_KeyCancel, &g_KeyValue) == 0);
    if (Arg == (void*)1)
    {
        pthread_exit(Arg);
    }
    if (Arg == (void*)2)
    {
        /* Pending, with no cancellation point before the return */
        CHECK(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) == 0);
        CHECK(pthread_cancel(pthread_self()) == 0);
        CHECK(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) == 0);
    }
    return Arg;
}

/*
 * The destructors of a thread of pthread_create run outside the loader lock, which a thread needs to start and to end:
 * one creates and joins a thread, another joins a thread that ends meanwhile
 */
static pthread_key_t g_KeyJoin;
static pthread_t g_KeyJoinEnding;
static volatile long g_KeyJoined;

static void* KeyJoinHelper(void* Arg)
{
    return Arg;
}

static void* KeyJoinLater(void* Arg)
{
    SleepMs(50);
    return Arg;
}

static void KeyJoinDestructor(void* Value)
{
    pthread_t Thread;
    void* Result = NULL;

    if (Value == (void*)1)
    {
        CHECK(pthread_create(&Thread, NULL, KeyJoinHelper, (void*)3) == 0);
        CHECK(pthread_join(Thread, &Result) == 0 && Result == (void*)3);
    } else
    {
        CHECK(pthread_join(g_KeyJoinEnding, &Result) == 0 && Result == (void*)4);
    }
    __sync_fetch_and_add(&g_KeyJoined, 1);
}

static void* KeyJoin(void* Arg)
{
    CHECK(pthread_setspecific(g_KeyJoin, Arg) == 0);
    if (Arg == (void*)2)
    {
        pthread_exit(Arg);
    }
    return Arg;
}

static unsigned __stdcall KeyBeginThread(void* Arg)
{
    KeyReturn(NULL);
    return (unsigned)(uintptr_t)Arg;
}

static DWORD WINAPI KeyCreateThread(void* Arg)
{
    KeyReturn(NULL);
    return (DWORD)(uintptr_t)Arg;
}

static void TestKeys(void)
{
    pthread_t Thread;
    void* Result;
    uintptr_t Handle;

    CHECK(pthread_key_create(&g_Key, KeyDestructor) == 0);
    CHECK(pthread_key_create(&g_KeyAgain, KeyDestructorAgain) == 0);
    CHECK(pthread_create(&Thread, NULL, KeyReturn, NULL) == 0);
    CHECK(pthread_join(Thread, NULL) == 0);
    CHECK(g_Destroyed == 1 && g_DestroyedAgain == 3);
    CHECK(pthread_create(&Thread, NULL, KeyReturn, (void*)42) == 0);
    CHECK(pthread_join(Thread, &Result) == 0 && Result == (void*)42);
    CHECK(g_Destroyed == 2 && g_DestroyedAgain == 6);
    /* A thread of _beginthreadex, by the TLS callback */
    Handle = _beginthreadex(NULL, 0, KeyBeginThread, NULL, 0, NULL);
    CHECK(Handle != 0);
    CHECK(WaitForSingleObject((HANDLE)Handle, INFINITE) == WAIT_OBJECT_0);
    CHECK(g_Destroyed == 3 && g_DestroyedAgain == 9);
    CloseHandle((HANDLE)Handle);
    /* And of CreateThread */
    Handle = (uintptr_t)CreateThread(NULL, 0, KeyCreateThread, NULL, 0, NULL);
    CHECK(Handle != 0);
    CHECK(WaitForSingleObject((HANDLE)Handle, INFINITE) == WAIT_OBJECT_0);
    CHECK(g_Destroyed == 4 && g_DestroyedAgain == 12);
    CloseHandle((HANDLE)Handle);
    /* Return, pthread_exit, return with a cancellation pending */
    CHECK(pthread_key_create(&g_KeyCancel, KeyCancelDestructor) == 0);
    for (intptr_t i = 0; i < 3; i++)
    {
        CHECK(pthread_create(&Thread, NULL, KeyCancel, (void*)i) == 0);
        CHECK(pthread_join(Thread, &Result) == 0 && Result == (void*)i);
    }
    CHECK(g_CancelOff == 3);
    CHECK(pthread_key_delete(g_KeyCancel) == 0);
    /* Destructors that create, end and join threads, at a return and at pthread_exit */
    CHECK(pthread_key_create(&g_KeyJoin, KeyJoinDestructor) == 0);
    CHECK(pthread_create(&Thread, NULL, KeyJoin, (void*)1) == 0);
    CHECK(pthread_join(Thread, NULL) == 0);
    CHECK(pthread_create(&g_KeyJoinEnding, NULL, KeyJoinLater, (void*)4) == 0);
    CHECK(pthread_create(&Thread, NULL, KeyJoin, (void*)2) == 0);
    CHECK(pthread_join(Thread, NULL) == 0);
    CHECK(g_KeyJoined == 2);
    CHECK(pthread_key_delete(g_KeyJoin) == 0);
    /* The main thread keeps its value, a deleted key runs no destructor */
    CHECK(pthread_setspecific(g_Key, &g_KeyValue) == 0);
    CHECK(pthread_key_delete(g_Key) == 0);
    CHECK(pthread_key_delete(g_KeyAgain) == 0);
    CHECK(pthread_create(&Thread, NULL, KeyReturn, NULL) == 0);
    CHECK(pthread_join(Thread, NULL) == 0);
    CHECK(g_Destroyed == 4);
}

/* Once under contention */

static pthread_once_t g_Once = PTHREAD_ONCE_INIT;
static volatile long g_OnceCalls;
static volatile long g_OnceValue;
static pthread_barrier_t g_OnceBarrier;

static void OnceInit(void)
{
    __sync_fetch_and_add(&g_OnceCalls, 1);
    SleepMs(50);
    g_OnceValue = 1234;
}

static void* OnceRace(void* Arg)
{
    (void)Arg;
    pthread_barrier_wait(&g_OnceBarrier);
    CHECK(pthread_once(&g_Once, OnceInit) == 0);
    CHECK(g_OnceValue == 1234);
    return NULL;
}

static pthread_once_t g_OnceCancel = PTHREAD_ONCE_INIT;

static void OnceSleep(void)
{
    struct timespec Time = { 10, 0 };

    nanosleep(&Time, NULL);
}

static void OnceAgain(void)
{
    g_OnceCalls += 100;
}

static void* OnceCancelled(void* Arg)
{
    (void)Arg;
    pthread_once(&g_OnceCancel, OnceSleep);
    return NULL;
}

static void TestOnce(void)
{
    pthread_t Thread;
    void* Result;

    CHECK(pthread_barrier_init(&g_OnceBarrier, NULL, THREADS) == 0);
    RunThreads(OnceRace, NULL);
    CHECK(g_OnceCalls == 1);
    CHECK(pthread_once(&g_Once, OnceInit) == 0 && g_OnceCalls == 1);
    pthread_barrier_destroy(&g_OnceBarrier);

    CHECK(pthread_create(&Thread, NULL, OnceCancelled, NULL) == 0);
    SleepMs(50);
    CHECK(pthread_cancel(Thread) == 0);
    CHECK(pthread_join(Thread, &Result) == 0 && Result == PTHREAD_CANCELED);
    CHECK(pthread_once(&g_OnceCancel, OnceAgain) == 0 && g_OnceCalls == 101);
}

/* Threads: detach, self, cleanup handlers, cancellation, names */

static volatile long g_Cleanups;
static volatile long g_DetachedDone;

static void* Detached(void* Arg)
{
    (void)Arg;
    SleepMs(20);
    __sync_fetch_and_add(&g_DetachedDone, 1);
    return NULL;
}

static void Cleanup(void* Arg)
{
    __sync_fetch_and_add(&g_Cleanups, (long)(intptr_t)Arg);
}

static void* ExitWithCleanup(void* Arg)
{
    pthread_cleanup_push(Cleanup, (void*)1);
    pthread_cleanup_push(Cleanup, (void*)10);
    pthread_cleanup_pop(1);
    pthread_cleanup_push(Cleanup, (void*)100);
    if (Arg != NULL)
    {
        pthread_exit(Arg);
    }
    pthread_cleanup_pop(0);
    pthread_cleanup_pop(0);
    return NULL;
}

static void UnlockCleanup(void* Arg)
{
    pthread_mutex_unlock(Arg);
    __sync_fetch_and_add(&g_Cleanups, 1000);
}

static void* CancelCondWait(void* Arg)
{
    (void)Arg;
    pthread_mutex_lock(&g_Mutex);
    pthread_cleanup_push(UnlockCleanup, &g_Mutex);
    for (;;)
    {
        pthread_cond_wait(&g_NotEmpty, &g_Mutex);
    }
    pthread_cleanup_pop(0);
    return NULL;
}

/* A wait of 10 s (or for ever) at each cancellation point but pthread_cond_wait and pthread_testcancel */
#define CANCEL_POINTS 10
static sem_t g_Never;
static pthread_t g_NeverEnds;

static void* WaitNever(void* Arg)
{
    sem_wait(&g_Never);
    return Arg;
}

static void* CancelSleep(void* Arg)
{
    struct timespec Time = { 10, 0 };
    struct timespec Abs = RealtimeIn(10000);
    sem_t Sem;

    switch ((intptr_t)Arg)
    {
    case 0:
        nanosleep(&Time, NULL);
        break;
    case 1:
        sleep(10);
        break;
    case 2:
        usleep(10000000);
        break;
    case 3:
        clock_nanosleep(CLOCK_MONOTONIC, 0, &Time, NULL);
        break;
    case 4:
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &Abs, NULL);
        break;
    case 5:
        pthread_delay_np(&Time);
        break;
    case 6:
        sem_init(&Sem, 0, 0);
        sem_wait(&Sem);
        break;
    case 7:
        sem_init(&Sem, 0, 0);
        sem_timedwait(&Sem, &Abs);
        break;
    case 8:
        pthread_mutex_lock(&g_Mutex);
        pthread_cleanup_push(UnlockCleanup, &g_Mutex);
        pthread_cond_timedwait(&g_NotEmpty, &g_Mutex, &Abs);
        pthread_cleanup_pop(1);
        break;
    default:
        pthread_join(g_NeverEnds, NULL);
        break;
    }
    return NULL;
}

/* With cancellation disabled, pthread_cancel does not end a wait: the post does, then the cancellation acts */
static sem_t g_DisabledSem;
static volatile long g_DisabledPosted;

static void* CancelDisabledWait(void* Arg)
{
    (void)Arg;
    CHECK(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) == 0);
    CHECK(sem_wait(&g_DisabledSem) == 0);
    CHECK(g_DisabledPosted == 1);
    CHECK(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) == 0);
    pthread_testcancel();
    return NULL;
}

/* Detached, ends at the request of pthread_cancel while the call still runs */
static volatile long g_SpinsEnded;

static void SpinEnded(void* Arg)
{
    (void)Arg;
    __sync_fetch_and_add(&g_SpinsEnded, 1);
}

/*
 * Detached, cancelled at once while it starts and uses the heap, as others do: a cancel must not alert a thread in
 * a wait of ntdll (the lock of the heap, a critical section, deadlocked on such an alert)
 */
static volatile long g_HeapEnded, g_HeapStop;

static void HeapEnded(void* Arg)
{
    (void)Arg;
    __sync_fetch_and_add(&g_HeapEnded, 1);
}

static void* CancelHeap(void* Arg)
{
    pthread_cleanup_push(HeapEnded, NULL);
    for (int i = 0; i < 50; i++)
    {
        /* Through a volatile pointer, that the compiler keeps the pair */
        void* volatile Block = malloc(64 + i);

        free(Block);
    }
    for (;;)
    {
        pthread_testcancel();
    }
    pthread_cleanup_pop(0);
    return Arg;
}

static void* HeapHammer(void* Arg)
{
    while (!g_HeapStop)
    {
        void* volatile Block = malloc(128);

        free(Block);
    }
    return Arg;
}

static void* CancelSpin(void* Arg)
{
    (void)Arg;
    pthread_cleanup_push(SpinEnded, NULL);
    for (;;)
    {
        pthread_testcancel();
    }
    pthread_cleanup_pop(0);
    return NULL;
}

static void* CancelDisabled(void* Arg)
{
    int Old;

    (void)Arg;
    CHECK(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &Old) == 0 && Old == PTHREAD_CANCEL_ENABLE);
    SleepMs(100);
    CHECK(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &Old) == 0 && Old == PTHREAD_CANCEL_DISABLE);
    pthread_testcancel();
    return NULL;
}

static void* JoinSelf(void* Arg)
{
    (void)Arg;
    return (void*)(intptr_t)pthread_join(pthread_self(), NULL);
}

static void* OwnPriority(void* Arg)
{
    struct sched_param Param;
    int Policy;

    (void)Arg;
    if (pthread_getschedparam(pthread_self(), &Policy, &Param) != 0)
    {
        return (void*)(intptr_t)-100;
    }
    return (void*)(intptr_t)Param.sched_priority;
}

/* Blocks the thread detach of BlockedEnd until g_BlockRelease, after the callback of NtCRT (.CRT$XLF) */
static volatile DWORD g_BlockTid;
static HANDLE g_BlockEntered, g_BlockRelease;
static volatile int g_Joining;

static void NTAPI BlockDetach(PVOID Module, DWORD Reason, PVOID Reserved)
{
    (void)Module;
    (void)Reserved;
    if (Reason == DLL_THREAD_DETACH && g_BlockTid == GetCurrentThreadId())
    {
        SetEvent(g_BlockEntered);
        WaitForSingleObject(g_BlockRelease, INFINITE);
    }
}

__attribute__((section(".CRT$XLG"), used)) static const PIMAGE_TLS_CALLBACK BlockDetachPtr = BlockDetach;

static void* BlockedEnd(void* Gate)
{
    g_BlockTid = GetCurrentThreadId();
    sem_wait(Gate);
    return Gate;
}

static volatile long long g_JoinCancelledNs;

/* Whether a thread waits in NtWaitForMultipleObjects (its instruction pointer is in the stub of ntdll), x64 */
static bool InMultipleWait(pthread_t Thread)
{
    static uintptr_t Stub;
    HANDLE Handle = pthread_gethandle(Thread);
    CONTEXT Context;
    bool In;

    if (Stub == 0)
    {
        Stub = (uintptr_t)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtWaitForMultipleObjects");
    }
    memset(&Context, 0, sizeof(Context));
    Context.ContextFlags = CONTEXT_CONTROL;
    if (SuspendThread(Handle) == (DWORD)-1)
    {
        return false;
    }
#ifdef _WIN64
    In = GetThreadContext(Handle, &Context) && Context.Rip - Stub < 32;
#else
    In = GetThreadContext(Handle, &Context) && Context.Eip - Stub < 32;
#endif
    ResumeThread(Handle);
    return In;
}

static void JoinCancelled(void* Arg)
{
    (void)Arg;
    g_JoinCancelledNs = NowNs();
}

static void* JoinBlocked(void* Thread)
{
    void* Result = NULL;

    pthread_cleanup_push(JoinCancelled, NULL);
    g_Joining = 1;
    pthread_join((pthread_t)Thread, &Result);
    pthread_cleanup_pop(0);
    return Result;
}

static void TestThreads(void)
{
    pthread_t Thread;
    pthread_attr_t Attr;
    void* Result;
    int Value;
    size_t Size;
    char Name[32];

    CHECK(pthread_equal(pthread_self(), pthread_self()));
    CHECK(pthread_attr_init(&Attr) == 0);
    CHECK(pthread_attr_getdetachstate(&Attr, &Value) == 0 && Value == PTHREAD_CREATE_JOINABLE);
    CHECK(pthread_attr_setdetachstate(&Attr, PTHREAD_CREATE_DETACHED) == 0);
    CHECK(pthread_attr_setstacksize(&Attr, 1 << 20) == 0);
    CHECK(pthread_attr_getstacksize(&Attr, &Size) == 0 && Size == 1 << 20);
    for (int i = 0; i < 4; i++)
    {
        CHECK(pthread_create(&Thread, &Attr, Detached, NULL) == 0);
    }
    CHECK(pthread_join(Thread, NULL) == EINVAL);
    CHECK(pthread_attr_destroy(&Attr) == 0);
    CHECK(pthread_create(&Thread, NULL, Detached, NULL) == 0);
    CHECK(pthread_detach(Thread) == 0);
    CHECK(pthread_create(&Thread, NULL, Detached, NULL) == 0);
    SleepMs(100);
    CHECK(pthread_detach(Thread) == 0); /* Exited */
    while (g_DetachedDone != 6)
    {
        SleepMs(1);
    }

    CHECK(pthread_create(&Thread, NULL, JoinSelf, NULL) == 0);
    CHECK(pthread_join(Thread, &Result) == 0 && (intptr_t)Result == EDEADLK);

    CHECK(pthread_create(&Thread, NULL, ExitWithCleanup, NULL) == 0);
    CHECK(pthread_join(Thread, NULL) == 0 && g_Cleanups == 10);
    CHECK(pthread_create(&Thread, NULL, ExitWithCleanup, (void*)7) == 0);
    CHECK(pthread_join(Thread, &Result) == 0 && Result == (void*)7 && g_Cleanups == 121);

    /* Cancellation of a condition wait (the cleanup unlocks the mutex), of a sleep, after enabling it */
    g_Cleanups = 0;
    CHECK(pthread_create(&Thread, NULL, CancelCondWait, NULL) == 0);
    SleepMs(50);
    CHECK(pthread_cancel(Thread) == 0);
    CHECK(pthread_join(Thread, &Result) == 0 && Result == PTHREAD_CANCELED && g_Cleanups == 1000);
    CHECK(pthread_mutex_trylock(&g_Mutex) == 0 && pthread_mutex_unlock(&g_Mutex) == 0);
    {
        long long Start = NowMs();

        CHECK(sem_init(&g_Never, 0, 0) == 0);
        CHECK(pthread_create(&g_NeverEnds, NULL, WaitNever, NULL) == 0);
        for (intptr_t i = 0; i < CANCEL_POINTS; i++)
        {
            CHECK(pthread_create(&Thread, NULL, CancelSleep, (void*)i) == 0);
            SleepMs(30);
            CHECK(pthread_cancel(Thread) == 0);
            CHECK(pthread_join(Thread, &Result) == 0 && Result == PTHREAD_CANCELED);
        }
        CHECK(NowMs() - Start < 5000);
        CHECK(pthread_mutex_trylock(&g_Mutex) == 0 && pthread_mutex_unlock(&g_Mutex) == 0);
        /* A cancelled join leaves the thread joinable */
        CHECK(sem_post(&g_Never) == 0);
        CHECK(pthread_join(g_NeverEnds, NULL) == 0);
        CHECK(sem_init(&g_DisabledSem, 0, 0) == 0);
        CHECK(pthread_create(&Thread, NULL, CancelDisabledWait, NULL) == 0);
        SleepMs(30);
        CHECK(pthread_cancel(Thread) == 0);
        SleepMs(50);
        g_DisabledPosted = 1;
        CHECK(sem_post(&g_DisabledSem) == 0);
        CHECK(pthread_join(Thread, &Result) == 0 && Result == PTHREAD_CANCELED);
        Start = NowMs();
        CHECK(pthread_create(&Thread, NULL, CancelDisabled, NULL) == 0);
        SleepMs(20);
        CHECK(pthread_cancel(Thread) == 0);
        CHECK(pthread_join(Thread, &Result) == 0 && Result == PTHREAD_CANCELED);
        CHECK(NowMs() - Start >= 90);
    }
    {
        pthread_attr_t Detached;

        CHECK(pthread_attr_init(&Detached) == 0);
        CHECK(pthread_attr_setdetachstate(&Detached, PTHREAD_CREATE_DETACHED) == 0);
        for (int i = 0; i < 200; i++)
        {
            CHECK(pthread_create(&Thread, &Detached, CancelSpin, NULL) == 0);
            CHECK(pthread_cancel(Thread) == 0);
        }
        CHECK(pthread_attr_destroy(&Detached) == 0);
        for (long long Start = NowMs(); g_SpinsEnded != 200 && NowMs() - Start < 10000;)
        {
            SleepMs(1);
        }
        CHECK(g_SpinsEnded == 200);

        pthread_t Hammers[4];
        long long Start = NowMs();

        CHECK(pthread_attr_init(&Detached) == 0);
        CHECK(pthread_attr_setdetachstate(&Detached, PTHREAD_CREATE_DETACHED) == 0);
        for (int i = 0; i < 4; i++)
        {
            CHECK(pthread_create(&Hammers[i], NULL, HeapHammer, NULL) == 0);
        }
        /* The stacks of the threads not yet ended fill the address space of x86: EAGAIN until some end */
        for (int i = 0; i < 3000; i++)
        {
            int Error;

            while ((Error = pthread_create(&Thread, &Detached, CancelHeap, NULL)) == EAGAIN)
            {
                SleepMs(1);
            }
            CHECK(Error == 0);
            CHECK(pthread_cancel(Thread) == 0);
        }
        while (g_HeapEnded != 3000 && NowMs() - Start < 30000)
        {
            SleepMs(1);
        }
        CHECK(g_HeapEnded == 3000);
        CHECK(pthread_attr_destroy(&Detached) == 0);
        g_HeapStop = 1;
        for (int i = 0; i < 4; i++)
        {
            CHECK(pthread_join(Hammers[i], NULL) == 0);
        }
    }

    /* Names (Windows 10 1607 and later) */
    if (pthread_setname_np(pthread_self(), "main thread") == 0)
    {
        CHECK(pthread_getname_np(pthread_self(), Name, sizeof(Name)) == 0 && strcmp(Name, "main thread") == 0);
        CHECK(pthread_getname_np(pthread_self(), Name, 4) == ERANGE);
        /* Longer than the buffer on the stack */
        {
            char Long[301], Back[400];

            memset(Long, 'n', 300);
            Long[300] = 0;
            CHECK(pthread_setname_np(pthread_self(), Long) == 0);
            CHECK(pthread_getname_np(pthread_self(), Back, sizeof(Back)) == 0 && strcmp(Back, Long) == 0);
            CHECK(pthread_getname_np(pthread_self(), Back, 300) == ERANGE);
        }
    }
    CHECK(pthread_num_processors_np() >= 1);
    {
        struct sched_param Param;
        int Policy;

        CHECK(pthread_getschedparam(pthread_self(), &Policy, &Param) == 0 && Policy == SCHED_OTHER);
        CHECK(Param.sched_priority == 0);
        Param.sched_priority = THREAD_PRIORITY_IDLE - 1;
        CHECK(pthread_setschedparam(pthread_self(), SCHED_OTHER, &Param) == EINVAL);
        Param.sched_priority = THREAD_PRIORITY_BELOW_NORMAL;
        CHECK(pthread_setschedparam(pthread_self(), SCHED_FIFO, &Param) == ENOTSUP);
        /* A thread of PTHREAD_INHERIT_SCHED starts at the priority of its creator */
        CHECK(pthread_setschedparam(pthread_self(), SCHED_OTHER, &Param) == 0);
        CHECK(pthread_attr_init(&Attr) == 0);
        CHECK(pthread_attr_setinheritsched(&Attr, PTHREAD_INHERIT_SCHED) == 0);
        CHECK(pthread_create(&Thread, &Attr, OwnPriority, NULL) == 0);
        CHECK(pthread_join(Thread, &Result) == 0 && (intptr_t)Result == THREAD_PRIORITY_BELOW_NORMAL);
        CHECK(pthread_create(&Thread, NULL, OwnPriority, NULL) == 0);
        CHECK(pthread_join(Thread, &Result) == 0 && (intptr_t)Result == 0);
        CHECK(pthread_attr_destroy(&Attr) == 0);
        Param.sched_priority = 0;
        CHECK(pthread_setschedparam(pthread_self(), SCHED_OTHER, &Param) == 0);
        CHECK(sched_get_priority_min(SCHED_OTHER) == THREAD_PRIORITY_IDLE);
        CHECK(sched_yield() == 0);
    }
    /*
     * A TLS callback of the program after that of NtCRT that waits, when the thread is marked ended: a try join is
     * EBUSY, a join (in a thread that waits in it before, then for the handle) is cancelled at once, while the
     * callback still waits (the joiner then waits for the loader lock to end)
     */
    {
        sem_t Gate;
        pthread_t Ended, Joiner;
        long long Start;
        void* Result;

        g_BlockEntered = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_BlockRelease = CreateEventW(NULL, TRUE, FALSE, NULL);
        CHECK(g_BlockEntered != NULL && g_BlockRelease != NULL && sem_init(&Gate, 0, 0) == 0);
        CHECK(pthread_create(&Ended, NULL, BlockedEnd, &Gate) == 0);
        CHECK(sem_post(&Gate) == 0);
        CHECK(WaitForSingleObject(g_BlockEntered, 10000) == WAIT_OBJECT_0);
        CHECK(_pthread_tryjoin(Ended, NULL) == EBUSY);
        SetEvent(g_BlockRelease);
        CHECK(pthread_join(Ended, &Result) == 0 && Result == &Gate);
        ResetEvent(g_BlockRelease);

        g_Joining = 0;
        g_JoinCancelledNs = 0;
        CHECK(pthread_create(&Ended, NULL, BlockedEnd, &Gate) == 0);
        CHECK(pthread_create(&Joiner, NULL, JoinBlocked, (void*)Ended) == 0);
        while (!g_Joining)
        {
            SleepMs(1);
        }
        SleepMs(30);
        CHECK(sem_post(&Gate) == 0);
        CHECK(WaitForSingleObject(g_BlockEntered, 10000) == WAIT_OBJECT_0);
        Start = NowNs();
        while (!InMultipleWait(Joiner) && NowNs() - Start < 5000000000LL)
        {
            SleepMs(1);
        }
        CHECK(InMultipleWait(Joiner));
        Start = NowNs();
        CHECK(pthread_cancel(Joiner) == 0);
        while (g_JoinCancelledNs == 0 && NowNs() - Start < 5000000000LL)
        {
            SleepMs(1);
        }
        CHECK(g_JoinCancelledNs != 0 && g_JoinCancelledNs - Start < 1000000000LL);
        SetEvent(g_BlockRelease);
        CHECK(pthread_join(Joiner, &Result) == 0 && Result == PTHREAD_CANCELED);
        CHECK(pthread_join(Ended, &Result) == 0 && Result == &Gate);
        g_BlockTid = 0;
        CloseHandle(g_BlockEntered);
        CloseHandle(g_BlockRelease);
        CHECK(sem_destroy(&Gate) == 0);
    }
}

/* Clocks and sleeps */

static void TestClocks(void)
{
    struct timespec Time, Res, Real;
    long long Start;

    CHECK(clock_gettime(CLOCK_MONOTONIC, &Time) == 0 && Time.tv_nsec >= 0 && Time.tv_nsec < 1000000000);
    CHECK(clock_getres(CLOCK_MONOTONIC, &Res) == 0 && Res.tv_sec == 0 && Res.tv_nsec > 0);
    CHECK(clock_getres(CLOCK_REALTIME, &Res) == 0 && Res.tv_nsec > 0);
    CHECK(clock_gettime(CLOCK_REALTIME, &Real) == 0 && llabs((long long)Real.tv_sec - (long long)time(NULL)) <= 1);
    CHECK(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &Time) == 0);
    CHECK(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &Time) == 0);
    CHECK(clock_gettime(99, &Time) == -1 && errno == EINVAL);
    /* Past the NT time and before 1601, rejected before NtSetSystemTime (a 32-bit time_t holds neither) */
    if (sizeof(time_t) == 8)
    {
        Time.tv_sec = (time_t)922337203685LL;
        Time.tv_nsec = 0;
        CHECK(clock_settime(CLOCK_REALTIME, &Time) == -1 && errno == EINVAL);
        Time.tv_sec = (time_t)-20000000000LL;
        CHECK(clock_settime(CLOCK_REALTIME, &Time) == -1 && errno == EINVAL);
    }

    Start = NowMs();
    Time.tv_sec = 0;
    Time.tv_nsec = 30000000;
    CHECK(nanosleep(&Time, NULL) == 0);
    CHECK(NowMs() - Start >= 25);
    Time.tv_nsec = 1000000000;
    CHECK(nanosleep(&Time, NULL) == -1 && errno == EINVAL);

    Start = NowMs();
    clock_gettime(CLOCK_MONOTONIC, &Time);
    Time.tv_nsec += 30000000;
    if (Time.tv_nsec >= 1000000000)
    {
        Time.tv_sec++;
        Time.tv_nsec -= 1000000000;
    }
    CHECK(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &Time, NULL) == 0);
    CHECK(NowMs() - Start >= 25);
    Time = RealtimeIn(30);
    Start = NowMs();
    CHECK(clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &Time, NULL) == 0);
    CHECK(NowMs() - Start >= 15);
    CHECK(clock_nanosleep(CLOCK_THREAD_CPUTIME_ID, 0, &Time, NULL) == ENOTSUP);

    /*
     * With a timer resolution of 0.5 ms a relative wait of the system may end early, these take their time. Windows 11
     * ignores the resolution of a process without a visible window unless it opts out.
     */
    {
        NTSTATUS NTAPI NtSetTimerResolution(ULONG DesiredTime, BOOLEAN SetResolution, PULONG ActualTime);
        NTSTATUS NTAPI NtSetInformationProcess(HANDLE Process, int Class, void* Information, ULONG Length);
        ULONG Throttling[3] = { 1, 4 /* PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION */, 0 };
        struct timespec Two = { 0, 2000000 };
        long long Least[3] = { LLONG_MAX, LLONG_MAX, LLONG_MAX };
        ULONG Actual;

        NtSetInformationProcess(GetCurrentProcess(), 77 /* ProcessPowerThrottlingState */, Throttling,
                                sizeof(Throttling));
        NtSetTimerResolution(5000, TRUE, &Actual);
        pthread_mutex_lock(&g_Mutex);
        for (int i = 0; i < 50; i++)
        {
            long long Times[4];

            Times[0] = NowNs();
            nanosleep(&Two, NULL);
            Times[1] = NowNs();
            clock_nanosleep(CLOCK_MONOTONIC, 0, &Two, NULL);
            Times[2] = NowNs();
            pthread_cond_timedwait_relative_np(&g_NotEmpty, &g_Mutex, &Two);
            Times[3] = NowNs();
            for (int k = 0; k < 3; k++)
            {
                Least[k] = Times[k + 1] - Times[k] < Least[k] ? Times[k + 1] - Times[k] : Least[k];
            }
        }
        pthread_mutex_unlock(&g_Mutex);
        NtSetTimerResolution(5000, FALSE, &Actual);
        CHECK(Least[0] >= 2000000 && Least[1] >= 2000000 && Least[2] >= 2000000);
    }
}

/* The first pthread use, in a thread of _beginthreadex: a once whose routine cancels the thread is reset */
static pthread_once_t g_OnceForeign = PTHREAD_ONCE_INIT;
static int g_OnceForeignCalls;

static void OnceCancelSelf(void)
{
    g_OnceForeignCalls++;
    pthread_cancel(pthread_self());
    pthread_testcancel();
}

static void OnceForeignAgain(void)
{
    g_OnceForeignCalls += 10;
}

static unsigned __stdcall OnceForeign(void* Arg)
{
    (void)Arg;
    pthread_once(&g_OnceForeign, OnceCancelSelf);
    return 0;
}

static volatile LONG g_BeginGo, g_BeginDone;
static volatile ULONG_PTR g_BeginCommitted;

static void __cdecl BeginThread(void* Arg)
{
    while (!g_BeginGo)
    {
        SleepMs(1);
    }
    g_BeginDone = 1;
    if (Arg != NULL)
    {
        _endthread();
    }
}

/* The committed stack, from the guard page up */
static unsigned __stdcall BeginStack(void* Arg)
{
    NT_TIB* Tib = (NT_TIB*)NtCurrentTeb();

    (void)Arg;
    g_BeginCommitted = (ULONG_PTR)Tib->StackBase - (ULONG_PTR)Tib->StackLimit;
    return 0;
}

static void TestForeign(void)
{
    uintptr_t Handle = _beginthreadex(NULL, 0, OnceForeign, NULL, 0, NULL);

    CHECK(Handle != 0);
    CHECK(WaitForSingleObject((HANDLE)Handle, INFINITE) == WAIT_OBJECT_0);
    CloseHandle((HANDLE)Handle);
    CHECK(pthread_once(&g_OnceForeign, OnceForeignAgain) == 0 && g_OnceForeignCalls == 11);

    /* The handle of _beginthread is valid until the thread returns or calls _endthread, which close it */
    for (int i = 0; i < 2; i++)
    {
        g_BeginGo = 0;
        g_BeginDone = 0;
        Handle = _beginthread(BeginThread, 0, i != 0 ? (void*)1 : NULL);
        CHECK(Handle != (uintptr_t)-1);
        CHECK(WaitForSingleObject((HANDLE)Handle, 0) == WAIT_TIMEOUT);
        CHECK(GetThreadPriority((HANDLE)Handle) == THREAD_PRIORITY_NORMAL);
        g_BeginGo = 1;
        while (!g_BeginDone)
        {
            SleepMs(1);
        }
    }

    /* STACK_SIZE_PARAM_IS_A_RESERVATION: 64 MB reserved, the default committed */
    Handle = _beginthreadex(NULL, 64 << 20, BeginStack, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    CHECK(Handle != 0);
    CHECK(WaitForSingleObject((HANDLE)Handle, INFINITE) == WAIT_OBJECT_0);
    CloseHandle((HANDLE)Handle);
    CHECK(g_BeginCommitted != 0 && g_BeginCommitted < (1 << 20));
}

int main(void)
{
    static const struct
    {
        const char* Name;
        void (*Func)(void);
    } Tests[] = { { "foreign", TestForeign }, { "mutex", TestMutex },     { "cond", TestCond },
                  { "rwlock", TestRwlock },   { "sync", TestSync },       { "keys", TestKeys },
                  { "once", TestOnce },       { "threads", TestThreads }, { "clocks", TestClocks } };

    /* A test that hangs is the last one shown */
    setvbuf(stdout, NULL, _IONBF, 0);
    for (size_t i = 0; i < sizeof(Tests) / sizeof(Tests[0]); i++)
    {
        printf("%s ", Tests[i].Name);
        Tests[i].Func();
    }
    printf("\n");
    if (g_Failures != 0)
    {
        printf("pthread: %d failures\n", g_Failures);
        return 1;
    }
    printf("pthread: ok\n");
    return 0;
}
