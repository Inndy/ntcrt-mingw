#include <mutex>
#include <thread>

static std::mutex Mutex;
static int Count;

int main()
{
    std::thread Thread([] {
        std::lock_guard<std::mutex> Lock(Mutex);
        Count++;
    });
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        Count++;
    }
    Thread.join();
    return Count == 2 ? 0 : 1;
}
