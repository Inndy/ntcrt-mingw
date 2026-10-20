/* The slot of pthread_self in the expansion slots of TLS, and pthread_create when they cannot be allocated */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <windows.h>

typedef PVOID(NTAPI* ALLOC)(PVOID, ULONG, SIZE_T);

/* The slot of RtlAllocateHeap in the import table, of a stdcall function on x86 */
#ifdef _WIN64
extern ALLOC __imp_RtlAllocateHeap;
#else
extern ALLOC __imp_RtlAllocateHeap __asm__("__imp__RtlAllocateHeap@12");
#endif

static ALLOC g_Alloc;
static volatile LONG g_NoSlots;
static pthread_t g_Created;
static volatile LONG g_Same;

/* No expansion slots, TLS_EXPANSION_SLOTS pointers zeroed */
static PVOID NTAPI Alloc(PVOID Heap, ULONG Flags, SIZE_T Size)
{
    if (g_NoSlots && Size == 1024 * sizeof(PVOID) && (Flags & HEAP_ZERO_MEMORY) != 0)
    {
        return NULL;
    }
    return g_Alloc(Heap, Flags, Size);
}

static void* Child(void* Arg)
{
    g_Same = pthread_equal(pthread_self(), g_Created);
    pthread_testcancel();
    return Arg;
}

static void* Spin(void* Arg)
{
    for (;;)
    {
        pthread_testcancel();
    }
    return Arg;
}

int main(void)
{
    pthread_t Thread;
    void* Result;
    DWORD Protect;

    /* Before the first pthread_create: the slot of pthread_self is past the 64 of the TEB */
    while (TlsAlloc() < 70)
    {
    }
    if (pthread_create(&g_Created, NULL, Child, (void*)1) != 0 || pthread_join(g_Created, &Result) != 0 ||
        Result != (void*)1 || !g_Same)
    {
        printf("tlsslot: no pthread_self in the expansion slot\n");
        return 1;
    }
    if (pthread_create(&Thread, NULL, Spin, NULL) != 0 || pthread_cancel(Thread) != 0 ||
        pthread_join(Thread, &Result) != 0 || Result != PTHREAD_CANCELED)
    {
        printf("tlsslot: no cancellation\n");
        return 1;
    }
    g_Alloc = __imp_RtlAllocateHeap;
    if (!VirtualProtect(&__imp_RtlAllocateHeap, sizeof(PVOID), PAGE_READWRITE, &Protect))
    {
        printf("tlsslot: VirtualProtect\n");
        return 1;
    }
    __imp_RtlAllocateHeap = Alloc;
    g_NoSlots = 1;
    if (pthread_create(&g_Created, NULL, Child, NULL) != EAGAIN)
    {
        printf("tlsslot: a thread without its pthread_self\n");
        return 1;
    }
    g_NoSlots = 0;
    if (pthread_create(&g_Created, NULL, Child, (void*)2) != 0 || pthread_join(g_Created, &Result) != 0 ||
        Result != (void*)2 || !g_Same)
    {
        printf("tlsslot: no thread after EAGAIN\n");
        return 1;
    }
    printf("tlsslot: ok\n");
    return 0;
}
