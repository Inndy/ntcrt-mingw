/* msvcrt heap functions on the process heap */

#include "NtCRT.h"

#include <errno.h>
#include <malloc.h>
#include <new.h>

static _PNH g_NewHandler;
static int g_NewMode;

static
BOOL
_NtCRT_CallNewHandler(
    _In_ size_t Size)
{
    _PNH Handler = (_PNH)_InterlockedCompareExchangePointer((PVOID volatile*)&g_NewHandler, NULL, NULL);
    return Handler != NULL && Handler(Size) != 0;
}

EXTERN_C void* __cdecl _NtCRT_malloc(size_t size)
{
    void* Block;

    if (size > _HEAP_MAXREQ)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    for (;;)
    {
        Block = RtlAllocateHeap(NTCRT_HEAP, 0, size != 0 ? size : 1);
        if (Block != NULL || !g_NewMode || !_NtCRT_CallNewHandler(size))
        {
            break;
        }
    }
    if (Block == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
    }
    return Block;
}

EXTERN_C void* __cdecl _NtCRT_calloc(size_t count, size_t size)
{
    void* Block;
    size_t Total;

    if (size != 0 && count > _HEAP_MAXREQ / size)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    Total = count * size;
    Block = RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, Total != 0 ? Total : 1);
    if (Block == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
    }
    return Block;
}

EXTERN_C void __cdecl _NtCRT_free(void* block)
{
    if (block != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, block);
    }
}

EXTERN_C void* __cdecl _NtCRT_realloc(void* block, size_t size)
{
    void* NewBlock;

    if (block == NULL)
    {
        return _NtCRT_malloc(size);
    }
    if (size == 0)
    {
        _NtCRT_free(block);
        return NULL;
    }
    if (size > _HEAP_MAXREQ)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    NewBlock = RtlReAllocateHeap(NTCRT_HEAP, 0, block, size);
    if (NewBlock == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
    }
    return NewBlock;
}

/* New handler */

EXTERN_C _PNH __cdecl _NtCRT__set_new_handler(_PNH handler)
{
    return (_PNH)_InterlockedExchangePointer((PVOID volatile*)&g_NewHandler, (PVOID)handler);
}

EXTERN_C _PNH __cdecl _NtCRT__query_new_handler(void)
{
    return g_NewHandler;
}

EXTERN_C int __cdecl _NtCRT__set_new_mode(int mode)
{
    int Old = g_NewMode;
    g_NewMode = mode;
    return Old;
}

EXTERN_C int __cdecl _NtCRT__query_new_mode(void)
{
    return g_NewMode;
}

EXTERN_C int __cdecl _NtCRT__callnewh(size_t size)
{
    return _NtCRT_CallNewHandler(size) ? 1 : 0;
}

NTCRT_IMPORT(malloc, _NtCRT_malloc);
NTCRT_IMPORT(calloc, _NtCRT_calloc);
NTCRT_IMPORT(free, _NtCRT_free);
NTCRT_IMPORT(realloc, _NtCRT_realloc);
NTCRT_IMPORT(_set_new_handler, _NtCRT__set_new_handler);
NTCRT_IMPORT(_query_new_handler, _NtCRT__query_new_handler);
NTCRT_IMPORT(_set_new_mode, _NtCRT__set_new_mode);
NTCRT_IMPORT(_query_new_mode, _NtCRT__query_new_mode);
NTCRT_IMPORT(_callnewh, _NtCRT__callnewh);
