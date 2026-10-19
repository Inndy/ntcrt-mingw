/*
 * The emulated thread-local variables of GCC and clang (-femulated-tls, the thread_local of C++ and _Thread_local):
 * __emutls_get_address and __emutls_register_common in place of emutls.o of libgcc. libgcc frees the variables of a
 * thread in a pthread key destructor, which pthread_exit runs before the thread ends, so the thread_local destructors
 * of C++ (the TLS callback of libmingw32, at the exit of the thread) would run on freed memory. These are freed by a
 * TLS callback after that of libmingw32 and that of NtCRT (.CRT$XLF): thread_local and key destructors that run at
 * the exit of the thread use live variables.
 *
 * libntcrt_start.a precedes libgcc.a in the link, see the specs in the Makefile.
 */

#include "../NtCRT.h"

#include <stdlib.h>
#include <string.h>

/* An emulated variable, as the compilers emit it (__emutls_v.<name>) */
typedef struct _NTCRT_EMUTLS_OBJECT
{
    ULONG_PTR Size;
    ULONG_PTR Align;
    ULONG_PTR Index; /* 1 and up, 0 until first used */
    void* Template;  /* Its initial value, NULL for zeros */
} NTCRT_EMUTLS_OBJECT;

/* The variables of a thread by index - 1, each a block of the heap with its address before it */
typedef struct _NTCRT_EMUTLS_ARRAY
{
    ULONG_PTR Count;
    void* Data[];
} NTCRT_EMUTLS_ARRAY;

static DWORD g_EmutlsSlot = TLS_OUT_OF_INDEXES;
static ULONG_PTR g_EmutlsCount;
static RTL_SRWLOCK g_EmutlsLock;

static ULONG_PTR EmutlsIndex(NTCRT_EMUTLS_OBJECT* Object)
{
    ULONG_PTR Index = __atomic_load_n(&Object->Index, __ATOMIC_ACQUIRE);

    if (Index != 0)
    {
        return Index;
    }
    RtlAcquireSRWLockExclusive(&g_EmutlsLock);
    Index = Object->Index;
    if (Index == 0)
    {
        if (g_EmutlsSlot == TLS_OUT_OF_INDEXES)
        {
            DWORD Slot = _Inline_TlsAlloc();

            if (Slot == TLS_OUT_OF_INDEXES)
            {
                abort();
            }
            __atomic_store_n(&g_EmutlsSlot, Slot, __ATOMIC_RELEASE);
        }
        Index = ++g_EmutlsCount;
        __atomic_store_n(&Object->Index, Index, __ATOMIC_RELEASE);
    }
    RtlReleaseSRWLockExclusive(&g_EmutlsLock);
    return Index;
}

EXTERN_C void* __emutls_get_address(NTCRT_EMUTLS_OBJECT* Object)
{
    ULONG_PTR Index = EmutlsIndex(Object);
    DWORD Slot = __atomic_load_n(&g_EmutlsSlot, __ATOMIC_RELAXED);
    NTCRT_EMUTLS_ARRAY* Array = (NTCRT_EMUTLS_ARRAY*)_Inline_TlsGetValue2(Slot);
    void* Variable;

    if (Array == NULL || Array->Count < Index)
    {
        ULONG_PTR Count = Index + 32;
        SIZE_T Bytes = sizeof(*Array) + Count * sizeof(void*);

        Array = (NTCRT_EMUTLS_ARRAY*)(Array == NULL ? RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, Bytes)
                                                    : RtlReAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, Array, Bytes));
        if (Array == NULL || !_Inline_TlsSetValue(Slot, Array))
        {
            abort();
        }
        Array->Count = Count;
    }
    Variable = Array->Data[Index - 1];
    if (Variable == NULL)
    {
        /* The heap aligns to 16 bytes, the address of the block takes 8 */
        ULONG_PTR Align = Object->Align > sizeof(void*) ? Object->Align : sizeof(void*);
        char* Block = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, Object->Size + Align - 1 + sizeof(void*));

        if (Block == NULL)
        {
            abort();
        }
        Variable = (void*)(((ULONG_PTR)Block + sizeof(void*) + Align - 1) & ~(Align - 1));
        ((void**)Variable)[-1] = Block;
        if (Object->Template != NULL)
        {
            memcpy(Variable, Object->Template, Object->Size);
        } else
        {
            memset(Variable, 0, Object->Size);
        }
        Array->Data[Index - 1] = Variable;
    }
    return Variable;
}

/* A common symbol: the largest size and alignment, the template of a definition */
EXTERN_C void __emutls_register_common(NTCRT_EMUTLS_OBJECT* Object, ULONG_PTR Size, ULONG_PTR Align, void* Template)
{
    if (Object->Size < Size)
    {
        Object->Size = Size;
        Object->Template = NULL;
    }
    if (Object->Align < Align)
    {
        Object->Align = Align;
    }
    if (Template != NULL && Size == Object->Size)
    {
        Object->Template = Template;
    }
}

static VOID NTAPI EmutlsTlsCallback(PVOID DllHandle, DWORD Reason, PVOID Reserved)
{
    DWORD Slot = __atomic_load_n(&g_EmutlsSlot, __ATOMIC_ACQUIRE);
    NTCRT_EMUTLS_ARRAY* Array;

    UNREFERENCED_PARAMETER(DllHandle);
    UNREFERENCED_PARAMETER(Reserved);
    if (Reason != DLL_THREAD_DETACH || Slot == TLS_OUT_OF_INDEXES)
    {
        return;
    }
    Array = (NTCRT_EMUTLS_ARRAY*)_Inline_TlsGetValue2(Slot);
    if (Array == NULL)
    {
        return;
    }
    _Inline_TlsSetValue(Slot, NULL);
    for (ULONG_PTR i = 0; i < Array->Count; i++)
    {
        if (Array->Data[i] != NULL)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, ((void**)Array->Data[i])[-1]);
        }
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Array);
}

/* After the callbacks of libmingw32 (.CRT$XLB to .CRT$XLD) and of NtCRT (.CRT$XLF), before the end (.CRT$XLZ) */
extern "C" __attribute__((section(".CRT$XLY"), used)) const PIMAGE_TLS_CALLBACK _NtCRT_EmutlsCallbackEntry =
    EmutlsTlsCallback;
