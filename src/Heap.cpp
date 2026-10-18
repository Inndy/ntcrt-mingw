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

EXTERN_C size_t __cdecl _NtCRT__msize(void* block)
{
    if (block == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return (size_t)-1;
    }
    return RtlSizeHeap(NTCRT_HEAP, 0, block);
}

EXTERN_C void* __cdecl _NtCRT__expand(void* block, size_t size)
{
    if (block == NULL || size > _HEAP_MAXREQ)
    {
        NTCRT_ERRNO = block == NULL ? EINVAL : ENOMEM;
        return NULL;
    }
    block = RtlReAllocateHeap(NTCRT_HEAP, HEAP_REALLOC_IN_PLACE_ONLY, block, size != 0 ? size : 1);
    if (block == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
    }
    return block;
}

EXTERN_C void* __cdecl _NtCRT__recalloc(void* block, size_t count, size_t size)
{
    size_t OldSize, NewSize;
    void* NewBlock;

    if (size != 0 && count > _HEAP_MAXREQ / size)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    NewSize = count * size;
    OldSize = block != NULL ? RtlSizeHeap(NTCRT_HEAP, 0, block) : 0;
    NewBlock = _NtCRT_realloc(block, NewSize);
    if (NewBlock != NULL && NewSize > OldSize)
    {
        memset((char*)NewBlock + OldSize, 0, NewSize - OldSize);
    }
    return NewBlock;
}

EXTERN_C void* __cdecl _NtCRT__calloc_crt(size_t count, size_t size)
{
    return _NtCRT_calloc(count, size);
}

EXTERN_C intptr_t __cdecl _NtCRT__get_heap_handle(void)
{
    return (intptr_t)NTCRT_HEAP;
}

EXTERN_C int __cdecl _NtCRT__heapchk(void)
{
    return RtlValidateHeap(NTCRT_HEAP, 0, NULL) ? _HEAPOK : _HEAPBADNODE;
}

EXTERN_C int __cdecl _NtCRT__heapmin(void)
{
    RtlCompactHeap(NTCRT_HEAP, 0);
    return 0;
}

/* Aligned allocation, the original pointer is stored before the aligned block as msvcrt does */

EXTERN_C void* __cdecl _NtCRT__aligned_offset_malloc(size_t size, size_t alignment, size_t offset)
{
    uintptr_t Raw, Aligned;
    size_t Overhead;

    if ((alignment & (alignment - 1)) != 0 || (offset != 0 && offset >= size))
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    if (alignment < sizeof(void*))
    {
        alignment = sizeof(void*);
    }
    Overhead = alignment - 1 + sizeof(void*);
    if (size > _HEAP_MAXREQ - Overhead)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    Raw = (uintptr_t)_NtCRT_malloc(size + Overhead);
    if (Raw == 0)
    {
        return NULL;
    }
    Aligned = ((Raw + sizeof(void*) + offset + alignment - 1) & ~(alignment - 1)) - offset;
    ((uintptr_t*)Aligned)[-1] = Raw;
    return (void*)Aligned;
}

EXTERN_C void* __cdecl _NtCRT__aligned_malloc(size_t size, size_t alignment)
{
    return _NtCRT__aligned_offset_malloc(size, alignment, 0);
}

EXTERN_C void __cdecl _NtCRT__aligned_free(void* block)
{
    if (block != NULL)
    {
        _NtCRT_free((void*)((uintptr_t*)block)[-1]);
    }
}

EXTERN_C size_t __cdecl _NtCRT__aligned_msize(void* block, size_t alignment, size_t offset)
{
    uintptr_t Raw;

    UNREFERENCED_PARAMETER(alignment);
    UNREFERENCED_PARAMETER(offset);
    if (block == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return (size_t)-1;
    }
    Raw = ((uintptr_t*)block)[-1];
    return _NtCRT__msize((void*)Raw) - ((uintptr_t)block - Raw);
}

EXTERN_C void* __cdecl _NtCRT__aligned_offset_realloc(void* block, size_t size, size_t alignment, size_t offset)
{
    void* NewBlock;
    size_t OldSize;

    if (block == NULL)
    {
        return _NtCRT__aligned_offset_malloc(size, alignment, offset);
    }
    if (size == 0)
    {
        _NtCRT__aligned_free(block);
        return NULL;
    }
    OldSize = _NtCRT__aligned_msize(block, alignment, offset);
    NewBlock = _NtCRT__aligned_offset_malloc(size, alignment, offset);
    if (NewBlock != NULL)
    {
        memcpy(NewBlock, block, OldSize < size ? OldSize : size);
        _NtCRT__aligned_free(block);
    }
    return NewBlock;
}

EXTERN_C void* __cdecl _NtCRT__aligned_realloc(void* block, size_t size, size_t alignment)
{
    return _NtCRT__aligned_offset_realloc(block, size, alignment, 0);
}

/* String duplication */

EXTERN_C char* __cdecl _NtCRT__strdup(const char* str)
{
    size_t Size;
    char* Copy;

    if (str == NULL)
    {
        return NULL;
    }
    Size = strlen(str) + 1;
    Copy = (char*)_NtCRT_malloc(Size);
    if (Copy != NULL)
    {
        memcpy(Copy, str, Size);
    }
    return Copy;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wcsdup(const wchar_t* str)
{
    size_t Size;
    wchar_t* Copy;

    if (str == NULL)
    {
        return NULL;
    }
    Size = (wcslen(str) + 1) * sizeof(wchar_t);
    Copy = (wchar_t*)_NtCRT_malloc(Size);
    if (Copy != NULL)
    {
        memcpy(Copy, str, Size);
    }
    return Copy;
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
NTCRT_IMPORT(_msize, _NtCRT__msize);
NTCRT_IMPORT(_expand, _NtCRT__expand);
NTCRT_IMPORT(_recalloc, _NtCRT__recalloc);
NTCRT_IMPORT(_calloc_crt, _NtCRT__calloc_crt);
NTCRT_IMPORT(_get_heap_handle, _NtCRT__get_heap_handle);
NTCRT_IMPORT(_heapchk, _NtCRT__heapchk);
NTCRT_IMPORT(_heapmin, _NtCRT__heapmin);
NTCRT_IMPORT(_aligned_offset_malloc, _NtCRT__aligned_offset_malloc);
NTCRT_IMPORT(_aligned_malloc, _NtCRT__aligned_malloc);
NTCRT_IMPORT(_aligned_free, _NtCRT__aligned_free);
NTCRT_IMPORT(_aligned_msize, _NtCRT__aligned_msize);
NTCRT_IMPORT(_aligned_offset_realloc, _NtCRT__aligned_offset_realloc);
NTCRT_IMPORT(_aligned_realloc, _NtCRT__aligned_realloc);
NTCRT_IMPORT(_strdup, _NtCRT__strdup);
NTCRT_IMPORT(strdup, _NtCRT__strdup);
NTCRT_IMPORT(_wcsdup, _NtCRT__wcsdup);
NTCRT_IMPORT(wcsdup, _NtCRT__wcsdup);
NTCRT_IMPORT(_set_new_handler, _NtCRT__set_new_handler);
NTCRT_IMPORT(_query_new_handler, _NtCRT__query_new_handler);
NTCRT_IMPORT(_set_new_mode, _NtCRT__set_new_mode);
NTCRT_IMPORT(_query_new_mode, _NtCRT__query_new_mode);
NTCRT_IMPORT(_callnewh, _NtCRT__callnewh);
