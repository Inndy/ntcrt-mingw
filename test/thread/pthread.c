// The pthread functions of NtCRT: races of several threads, the mutex types, timeouts, keys, once, cancellation

#include <errno.h>
#include <stdbool.h>
#include <process.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

        /* A time past */
        Time.tv_sec = 1;
        CHECK(pthread_cond_timedwait(&g_NotEmpty, &g_Mutex, &Time) == ETIMEDOUT);
    }
    pthread_mutex_unlock(&g_Mutex);
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

/* An APC that posts the semaphore its thread waits for */
NTSTATUS NTAPI NtQueueApcThread(HANDLE Thread, void(NTAPI* Routine)(void*, void*, void*), void* Arg1, void* Arg2,
                                void* Arg3);

static void NTAPI PostApc(void* Sem, void* Arg2, void* Arg3)
{
    (void)Arg2;
    (void)Arg3;
    sem_post(Sem);
}

/* Two APCs that each post the semaphore the other thread waits for, once both run */
static volatile long g_InApc;

static void NTAPI PairPostApc(void* Sem, void* Arg2, void* Arg3)
{
    (void)Arg2;
    (void)Arg3;
    __sync_fetch_and_add(&g_InApc, 1);
    while (g_InApc < 2)
    {
        sched_yield();
    }
    sem_post(Sem);
}

static void* SemWaitOne(void* Arg)
{
    return (void*)(intptr_t)sem_wait(Arg);
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

        CHECK(sem_init(&g_Sem, 0, 0) == 0);
        CHECK(pthread_create(&Threads[0], NULL, SemWaitOne, &g_Sem) == 0);
        SleepMs(50);
        CHECK(NtQueueApcThread(pthread_gethandle(Threads[0]), PostApc, &g_Sem, NULL, NULL) == 0);
        CHECK(pthread_join(Threads[0], &Result) == 0 && Result == NULL);
        CHECK(pthread_create(&Threads[0], NULL, SemWaitOne, &g_Sem) == 0);
        CHECK(pthread_create(&Threads[1], NULL, SemWaitOne, &g_Sem) == 0);
        SleepMs(50);
        CHECK(NtQueueApcThread(pthread_gethandle(Threads[0]), PairPostApc, &g_Sem, NULL, NULL) == 0);
        CHECK(NtQueueApcThread(pthread_gethandle(Threads[1]), PairPostApc, &g_Sem, NULL, NULL) == 0);
        CHECK(pthread_join(Threads[0], &Result) == 0 && Result == NULL);
        CHECK(pthread_join(Threads[1], &Result) == 0 && Result == NULL);
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

static void* CancelSleep(void* Arg)
{
    struct timespec Time = { 10, 0 };

    (void)Arg;
    nanosleep(&Time, NULL);
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

        CHECK(pthread_create(&Thread, NULL, CancelSleep, NULL) == 0);
        SleepMs(50);
        CHECK(pthread_cancel(Thread) == 0);
        CHECK(pthread_join(Thread, &Result) == 0 && Result == PTHREAD_CANCELED);
        CHECK(NowMs() - Start < 5000);
        Start = NowMs();
        CHECK(pthread_create(&Thread, NULL, CancelDisabled, NULL) == 0);
        SleepMs(20);
        CHECK(pthread_cancel(Thread) == 0);
        CHECK(pthread_join(Thread, &Result) == 0 && Result == PTHREAD_CANCELED);
        CHECK(NowMs() - Start >= 90);
    }

    /* Names (Windows 10 1607 and later) */
    if (pthread_setname_np(pthread_self(), "main thread") == 0)
    {
        CHECK(pthread_getname_np(pthread_self(), Name, sizeof(Name)) == 0 && strcmp(Name, "main thread") == 0);
        CHECK(pthread_getname_np(pthread_self(), Name, 4) == ERANGE);
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
    /* Past the NT time and before 1601, rejected before NtSetSystemTime */
    Time.tv_sec = 922337203685LL;
    Time.tv_nsec = 0;
    CHECK(clock_settime(CLOCK_REALTIME, &Time) == -1 && errno == EINVAL);
    Time.tv_sec = -20000000000LL;
    CHECK(clock_settime(CLOCK_REALTIME, &Time) == -1 && errno == EINVAL);

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

static void TestForeign(void)
{
    uintptr_t Handle = _beginthreadex(NULL, 0, OnceForeign, NULL, 0, NULL);

    CHECK(Handle != 0);
    CHECK(WaitForSingleObject((HANDLE)Handle, INFINITE) == WAIT_OBJECT_0);
    CloseHandle((HANDLE)Handle);
    CHECK(pthread_once(&g_OnceForeign, OnceForeignAgain) == 0 && g_OnceForeignCalls == 11);
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
