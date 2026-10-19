// C11 <threads.h> of NtCRT: threads, mutexes, condition variables, thread-specific storage, call_once

#include <stdio.h>
#include <threads.h>
#include <time.h>

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

static mtx_t g_Mutex;
static cnd_t g_Cond;
static long g_Counter;
static int g_Ready;
static thread_local int t_Local = 5;
static tss_t g_Tss;
static volatile long g_TssDestroyed;
static once_flag g_Once = ONCE_FLAG_INIT;
static volatile long g_OnceCalls;

static void OnceInit(void)
{
    g_OnceCalls++;
}

static void TssDestructor(void* Value)
{
    if (Value == &g_Counter)
    {
        __sync_fetch_and_add(&g_TssDestroyed, 1);
    }
}

static int Worker(void* Arg)
{
    call_once(&g_Once, OnceInit);
    t_Local += (int)(intptr_t)Arg;
    tss_set(g_Tss, &g_Counter);
    for (int i = 0; i < 50000; i++)
    {
        mtx_lock(&g_Mutex);
        g_Counter++;
        mtx_unlock(&g_Mutex);
    }
    mtx_lock(&g_Mutex);
    while (!g_Ready)
    {
        cnd_wait(&g_Cond, &g_Mutex);
    }
    mtx_unlock(&g_Mutex);
    return t_Local;
}

/* A destructor of a thread of thrd_create runs outside the loader lock: it may create and join a thread */
static tss_t g_TssJoin;
static volatile long g_TssJoined;

static int TssJoinHelper(void* Arg)
{
    return (int)(intptr_t)Arg;
}

static void TssJoinDestructor(void* Value)
{
    thrd_t Thread;
    int Result = 0;

    CHECK(thrd_create(&Thread, TssJoinHelper, Value) == thrd_success);
    CHECK(thrd_join(Thread, &Result) == thrd_success && Result == 3);
    __sync_fetch_and_add(&g_TssJoined, 1);
}

static int TssJoin(void* Arg)
{
    CHECK(tss_set(g_TssJoin, (void*)3) == thrd_success);
    if (Arg != NULL)
    {
        thrd_exit(1);
    }
    return 0;
}

static int Exiter(void* Arg)
{
    (void)Arg;
    thrd_exit(77);
}

int main(void)
{
    thrd_t Threads[THREADS];
    mtx_t Recursive, Timed;
    struct timespec Time;
    int Result;

    CHECK(mtx_init(&g_Mutex, mtx_plain) == thrd_success);
    CHECK(cnd_init(&g_Cond) == thrd_success);
    CHECK(tss_create(&g_Tss, TssDestructor) == thrd_success);
    for (intptr_t i = 0; i < THREADS; i++)
    {
        CHECK(thrd_create(&Threads[i], Worker, (void*)i) == thrd_success);
    }
    Time.tv_sec = 0;
    Time.tv_nsec = 50000000;
    CHECK(thrd_sleep(&Time, NULL) == 0);
    mtx_lock(&g_Mutex);
    g_Ready = 1;
    cnd_broadcast(&g_Cond);
    mtx_unlock(&g_Mutex);
    for (int i = 0; i < THREADS; i++)
    {
        CHECK(thrd_join(Threads[i], &Result) == thrd_success && Result == 5 + i);
    }
    CHECK(g_Counter == THREADS * 50000);
    CHECK(g_OnceCalls == 1);
    CHECK(g_TssDestroyed == THREADS);
    CHECK(t_Local == 5);
    CHECK(tss_get(g_Tss) == NULL);
    CHECK(tss_set(g_Tss, &Result) == thrd_success && tss_get(g_Tss) == &Result);
    tss_delete(g_Tss);

    CHECK(thrd_create(&Threads[0], Exiter, NULL) == thrd_success);
    CHECK(thrd_join(Threads[0], &Result) == thrd_success && Result == 77);
    CHECK(tss_create(&g_TssJoin, TssJoinDestructor) == thrd_success);
    for (intptr_t i = 0; i < 2; i++)
    {
        CHECK(thrd_create(&Threads[0], TssJoin, (void*)i) == thrd_success);
        CHECK(thrd_join(Threads[0], &Result) == thrd_success && Result == i);
    }
    CHECK(g_TssJoined == 2);
    tss_delete(g_TssJoin);
    CHECK(thrd_equal(thrd_current(), thrd_current()));
    thrd_yield();

    CHECK(mtx_init(&Recursive, mtx_plain | mtx_recursive) == thrd_success);
    CHECK(mtx_lock(&Recursive) == thrd_success && mtx_trylock(&Recursive) == thrd_success);
    CHECK(mtx_unlock(&Recursive) == thrd_success && mtx_unlock(&Recursive) == thrd_success);
    mtx_destroy(&Recursive);

    CHECK(mtx_init(&Timed, mtx_timed) == thrd_success);
    CHECK(mtx_lock(&Timed) == thrd_success);
    CHECK(mtx_trylock(&Timed) == thrd_busy);
    CHECK(timespec_get(&Time, TIME_UTC) == TIME_UTC);
    Time.tv_nsec += 30000000;
    if (Time.tv_nsec >= 1000000000)
    {
        Time.tv_sec++;
        Time.tv_nsec -= 1000000000;
    }
    CHECK(cnd_timedwait(&g_Cond, &Timed, &Time) == thrd_timedout);
    CHECK(mtx_unlock(&Timed) == thrd_success);
    mtx_destroy(&Timed);
    cnd_destroy(&g_Cond);
    mtx_destroy(&g_Mutex);

    if (g_Failures != 0)
    {
        printf("threads.h: %d failures\n", g_Failures);
        return 1;
    }
    printf("threads.h: ok\n");
    return 0;
}
