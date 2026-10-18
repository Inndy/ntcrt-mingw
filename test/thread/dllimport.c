// The winpthreads headers with WINPTHREADS_USE_DLLIMPORT: calls through the __imp_ pointers of the functions

#define WINPTHREADS_USE_DLLIMPORT
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>

static pthread_mutex_t g_Mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_Once = PTHREAD_ONCE_INIT;
static int g_Calls;

static void Once(void)
{
    g_Calls++;
}

static void* Run(void* Arg)
{
    pthread_once(&g_Once, Once);
    return Arg;
}

int main(void)
{
    pthread_t Thread;
    pthread_condattr_t Attr;
    clockid_t Clock;
    sem_t Sem;
    void* Result;
    int Value;

    if (pthread_mutex_lock(&g_Mutex) != 0 || pthread_mutex_unlock(&g_Mutex) != 0 ||
        pthread_create(&Thread, NULL, Run, (void*)3) != 0 || pthread_join(Thread, &Result) != 0 || Result != (void*)3 ||
        pthread_once(&g_Once, Once) != 0 || g_Calls != 1 || sem_init(&Sem, 0, 1) != 0 || sem_wait(&Sem) != 0 ||
        sem_getvalue(&Sem, &Value) != 0 || Value != 0 || sem_destroy(&Sem) != 0 || sched_yield() != 0 ||
        pthread_condattr_init(&Attr) != 0 || pthread_condattr_getclock(&Attr, &Clock) != 0 || Clock != CLOCK_REALTIME)
    {
        printf("dllimport: failed\n");
        return 1;
    }
    printf("dllimport: ok\n");
    return 0;
}
