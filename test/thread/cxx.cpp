// The threads of libstdc++ on the pthread functions of NtCRT

#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <latch>
#include <mutex>
#include <pthread.h>
#include <semaphore>
#include <shared_mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

/*
 * libstdc++ of MinGW-w64 has no pthread_cond_clockwait: try_lock_for and wait_for wait to a deadline of system_clock,
 * which on Windows 7 moves by the tick of 15.625 ms and may come a tick early by steady_clock
 */
constexpr auto RealtimeTick = 16ms;

static int g_Failures;

static void check(bool Ok, const char* What)
{
    if (!Ok)
    {
        std::printf("failed: %s\n", What);
        g_Failures++;
    }
}

static std::atomic<int> g_Destroyed;

struct Local
{
    int Value = 1;
    ~Local() { g_Destroyed++; }
};

static thread_local Local t_Local;

static void Mutexes()
{
    std::mutex Mutex;
    std::recursive_mutex Recursive;
    long Counter = 0;
    std::vector<std::thread> Threads;

    for (int i = 0; i < 8; i++)
    {
        Threads.emplace_back([&] {
            for (int j = 0; j < 50000; j++)
            {
                std::lock_guard<std::mutex> Lock(Mutex);
                std::lock_guard<std::recursive_mutex> Outer(Recursive);
                std::lock_guard<std::recursive_mutex> Inner(Recursive);
                Counter++;
            }
            t_Local.Value++;
        });
    }
    for (auto& Thread : Threads)
    {
        Thread.join();
    }
    check(Counter == 8 * 50000, "std::mutex and std::recursive_mutex");
    check(g_Destroyed == 8 && t_Local.Value == 1, "thread_local destructors");

    std::timed_mutex Timed;
    Timed.lock();
    auto Start = std::chrono::steady_clock::now();
    std::thread([&] { check(!Timed.try_lock_for(50ms), "std::timed_mutex::try_lock_for times out"); }).join();
    check(std::chrono::steady_clock::now() - Start >= 50ms - RealtimeTick, "std::timed_mutex waits");
    Timed.unlock();
    std::thread([&] { check(Timed.try_lock_for(50ms), "std::timed_mutex::try_lock_for"); Timed.unlock(); }).join();
}

/*
 * The thread_local destructors (the TLS callback of libmingw32) and the key destructors of a thread find its
 * thread_local variables alive, the emulated variables are freed after them: asking for one again gives the same
 * object, not a new one.
 */
struct Tracked
{
    int Value = 1;
    ~Tracked();
};

static thread_local Tracked t_Tracked;
static std::atomic<int> g_TrackedBad;
static std::atomic<int> g_TrackedDestroyed;
static pthread_key_t g_TrackedKey;

Tracked::~Tracked()
{
    /* The other threads construct it too, with the other thread_local variables of the file */
    if (Value == 1)
    {
        return;
    }
    if (Value != 7 || &t_Tracked != this)
    {
        g_TrackedBad++;
    }
    g_TrackedDestroyed++;
}

static void TrackedKeyDestructor(void* Value)
{
    if (t_Tracked.Value != 7 || &t_Tracked != Value)
    {
        g_TrackedBad++;
    }
    g_TrackedDestroyed++;
}

static void* TrackedThread(void* Arg)
{
    t_Tracked.Value = 7;
    pthread_setspecific(g_TrackedKey, &t_Tracked);
    if (Arg != nullptr)
    {
        pthread_exit(Arg);
    }
    return nullptr;
}

static void ThreadLocals()
{
    pthread_t Thread;

    /* The key of emutls of libgcc would precede this one, its destructor would free the variables first */
    t_Tracked.Value = 7;
    check(pthread_key_create(&g_TrackedKey, TrackedKeyDestructor) == 0, "pthread_key_create");
    std::thread([] { TrackedThread(nullptr); }).join();
    for (int i = 0; i < 2; i++)
    {
        check(pthread_create(&Thread, nullptr, TrackedThread, i != 0 ? &Thread : nullptr) == 0, "pthread_create");
        check(pthread_join(Thread, nullptr) == 0, "pthread_join");
    }
    check(g_TrackedDestroyed == 6 && g_TrackedBad == 0, "thread_local variables alive in their and key destructors");
    pthread_key_delete(g_TrackedKey);
}

static void Conditions()
{
    std::mutex Mutex;
    std::condition_variable Cond;
    std::condition_variable_any CondAny;
    std::vector<int> Queue;
    long Sum = 0;
    bool Done = false;

    std::thread Consumer([&] {
        std::unique_lock<std::mutex> Lock(Mutex);
        for (;;)
        {
            Cond.wait(Lock, [&] { return !Queue.empty() || Done; });
            if (Queue.empty())
            {
                return;
            }
            Sum += Queue.back();
            Queue.pop_back();
        }
    });
    for (int i = 1; i <= 10000; i++)
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        Queue.push_back(i);
        Cond.notify_one();
    }
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        Done = true;
    }
    Cond.notify_all();
    Consumer.join();
    check(Sum == 10000L * 10001 / 2, "std::condition_variable");

    std::unique_lock<std::mutex> Lock(Mutex);
    auto Start = std::chrono::steady_clock::now();
    check(Cond.wait_for(Lock, 50ms) == std::cv_status::timeout, "std::condition_variable::wait_for");
    check(std::chrono::steady_clock::now() - Start >= 50ms - RealtimeTick, "std::condition_variable::wait_for waits");
    check(!CondAny.wait_for(Lock, 10ms, [] { return false; }), "std::condition_variable_any::wait_for");
}

static void Futures()
{
    std::promise<int> Promise;
    std::future<int> Future = Promise.get_future();
    std::thread Thread([&] {
        std::this_thread::sleep_for(20ms);
        Promise.set_value(42);
    });
    check(Future.get() == 42, "std::promise and std::future");
    Thread.join();

    auto Async = std::async(std::launch::async, [] { return 7; });
    check(Async.get() == 7, "std::async");
    auto Throws = std::async(std::launch::async, []() -> int { throw std::runtime_error("x"); });
    try
    {
        Throws.get();
        check(false, "std::future rethrows");
    } catch (const std::runtime_error&)
    {
    }
    std::packaged_task<int(int)> Task([](int x) { return x * 2; });
    auto TaskFuture = Task.get_future();
    std::thread(std::move(Task), 21).join();
    check(TaskFuture.get() == 42, "std::packaged_task");
}

static void SharedMutexes()
{
    std::shared_mutex Shared;
    std::shared_timed_mutex SharedTimed;
    long A = 0, B = 0;
    std::atomic<bool> Bad = false;
    std::vector<std::thread> Threads;

    for (int i = 0; i < 8; i++)
    {
        Threads.emplace_back([&, i] {
            for (int j = 0; j < 20000; j++)
            {
                if ((i + j) % 4 == 0)
                {
                    std::unique_lock<std::shared_mutex> Lock(Shared);
                    A++;
                    B++;
                } else
                {
                    std::shared_lock<std::shared_mutex> Lock(Shared);
                    if (A != B)
                    {
                        Bad = true;
                    }
                }
            }
        });
    }
    for (auto& Thread : Threads)
    {
        Thread.join();
    }
    check(!Bad && A == 8 * 5000, "std::shared_mutex");

    SharedTimed.lock_shared();
    std::thread([&] { check(!SharedTimed.try_lock_for(30ms), "std::shared_timed_mutex::try_lock_for"); }).join();
    std::thread([&] {
        check(SharedTimed.try_lock_shared_for(30ms), "std::shared_timed_mutex::try_lock_shared_for");
        SharedTimed.unlock_shared();
    }).join();
    SharedTimed.unlock_shared();
}

static void Once()
{
    static std::once_flag Flag;
    std::atomic<int> Calls = 0;
    std::vector<std::thread> Threads;

    /* An exception leaves the flag unset for another call, it goes through pthread_once of NtCRT */
#ifndef NTCRT_NO_UNWIND
    try
    {
        std::call_once(Flag, [] { throw std::runtime_error("once"); });
        check(false, "std::call_once rethrows");
    } catch (const std::runtime_error&)
    {
    }
#endif
    for (int i = 0; i < 8; i++)
    {
        Threads.emplace_back([&] {
            std::call_once(Flag, [&] {
                std::this_thread::sleep_for(20ms);
                Calls++;
            });
        });
    }
    for (auto& Thread : Threads)
    {
        Thread.join();
    }
    check(Calls == 1, "std::call_once");
}

static void Cxx20()
{
    std::atomic<int> Stopped = 0;
    {
        std::jthread Thread([&](std::stop_token Token) {
            while (!Token.stop_requested())
            {
                std::this_thread::yield();
            }
            Stopped++;
        });
        std::this_thread::sleep_for(10ms);
    }
    check(Stopped == 1, "std::jthread");

    std::counting_semaphore<8> Semaphore(0);
    std::atomic<int> Acquired = 0;
    std::vector<std::thread> Threads;
    for (int i = 0; i < 4; i++)
    {
        Threads.emplace_back([&] {
            for (int j = 0; j < 1000; j++)
            {
                Semaphore.acquire();
                Acquired++;
            }
        });
    }
    for (int i = 0; i < 4000; i++)
    {
        Semaphore.release();
    }
    for (auto& Thread : Threads)
    {
        Thread.join();
    }
    check(Acquired == 4000, "std::counting_semaphore");
    check(!Semaphore.try_acquire_for(10ms), "std::counting_semaphore::try_acquire_for");

    std::latch Latch(4);
    std::barrier Barrier(4);
    std::atomic<int> Phase = 0;
    Threads.clear();
    for (int i = 0; i < 4; i++)
    {
        Threads.emplace_back([&] {
            Latch.arrive_and_wait();
            for (int j = 0; j < 100; j++)
            {
                Phase++;
                Barrier.arrive_and_wait();
            }
        });
    }
    for (auto& Thread : Threads)
    {
        Thread.join();
    }
    check(Phase == 400, "std::latch and std::barrier");

    std::atomic<int> Value = 0;
    std::thread Waiter([&] { Value.wait(0); });
    std::this_thread::sleep_for(10ms);
    Value = 1;
    Value.notify_one();
    Waiter.join();
}

/*
 * "last": the main thread, the last one, ends by pthread_exit, the process ends with it. Its thread_local destructors
 * run with the atexit functions, one constructs another thread_local variable whose destructor runs too: "AB".
 */
struct LastB
{
    ~LastB() { std::printf("B"); }
};

static LastB& GetLastB()
{
    static thread_local LastB B;
    return B;
}

struct LastA
{
    ~LastA()
    {
        GetLastB();
        std::printf("A");
    }
};

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        static thread_local LastA A;

        (void)A;
        pthread_exit(nullptr);
    }
    check(std::thread::hardware_concurrency() >= 1, "std::thread::hardware_concurrency");
    check(std::this_thread::get_id() == std::this_thread::get_id(), "std::this_thread::get_id");
    Mutexes();
    ThreadLocals();
    Conditions();
    Futures();
    SharedMutexes();
    Once();
    Cxx20();
    if (g_Failures != 0)
    {
        std::printf("libstdc++ threads: %d failures\n", g_Failures);
        return 1;
    }
    std::printf("libstdc++ threads: ok\n");
    return 0;
}
