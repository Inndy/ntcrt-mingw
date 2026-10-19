/* pthread keys on the TLS slots of the TEB */

#include "NtCRT.h"

#include <pthread.h>

/*
 * A key is a TLS index (TlsAlloc). The destructors run in pthread_exit (a return from the start routine of
 * pthread_create or thrd_create is one) after the cleanup handlers, outside the loader lock, and at the exit of every
 * thread (also of CreateThread or _beginthreadex) from the TLS callback for the values set since: after the
 * thread_local destructors of C++, under the loader lock. The lock keeps a destructor from running on a slot freed by
 * pthread_key_delete and allocated again by TlsAlloc.
 */
#define KEY_SLOTS (TLS_MINIMUM_AVAILABLE + TLS_EXPANSION_SLOTS)

static void (*g_KeyDestructors[KEY_SLOTS])(void*);
static ULONG g_KeyLimit; /* Above the keys with a destructor */
static RTL_SRWLOCK g_KeyLock;

extern "C"
{
    void (*_NtCRT_PthreadExit)(void) = NULL;
}

EXTERN_C void _NtCRT_KeyDestructors(void)
{
    for (int Round = 0; Round < PTHREAD_DESTRUCTOR_ITERATIONS; Round++)
    {
        bool Called = false;

        for (ULONG Index = 0; Index < __atomic_load_n(&g_KeyLimit, __ATOMIC_RELAXED); Index++)
        {
            void (*Destructor)(void*);
            PVOID Value = NULL;

            RtlAcquireSRWLockShared(&g_KeyLock);
            Destructor = g_KeyDestructors[Index];
            if (Destructor != NULL)
            {
                Value = _Inline_TlsGetValue2(Index);
                if (Value != NULL)
                {
                    _Inline_TlsSetValue(Index, NULL);
                }
            }
            RtlReleaseSRWLockShared(&g_KeyLock);
            if (Value != NULL)
            {
                Destructor(Value);
                Called = true;
            }
        }
        if (!Called)
        {
            break;
        }
    }
}

EXTERN_C void _NtCRT_KeyExit(void)
{
    _NtCRT_KeyDestructors();
    /* A destructor may ask for pthread_self */
    if (_NtCRT_PthreadExit != NULL)
    {
        _NtCRT_PthreadExit();
    }
}

NTCRT_API int pthread_key_create(pthread_key_t* key, void (*dest)(void*))
{
    DWORD Index = _Inline_TlsAlloc();

    if (Index == TLS_OUT_OF_INDEXES)
    {
        return EAGAIN;
    }
    if (dest != NULL)
    {
        RtlAcquireSRWLockExclusive(&g_KeyLock);
        g_KeyDestructors[Index] = dest;
        if (Index >= g_KeyLimit)
        {
            __atomic_store_n(&g_KeyLimit, Index + 1, __ATOMIC_RELAXED);
        }
        RtlReleaseSRWLockExclusive(&g_KeyLock);
        __atomic_store_n(&_NtCRT_ThreadExit, &_NtCRT_KeyExit, __ATOMIC_RELEASE);
    }
    *key = Index;
    return 0;
}

NTCRT_API int pthread_key_delete(pthread_key_t key)
{
    if (key >= KEY_SLOTS)
    {
        return EINVAL;
    }
    RtlAcquireSRWLockExclusive(&g_KeyLock);
    g_KeyDestructors[key] = NULL;
    RtlReleaseSRWLockExclusive(&g_KeyLock);
    return _Inline_TlsFree(key) ? 0 : EINVAL;
}

NTCRT_API void* pthread_getspecific(pthread_key_t key)
{
    return _Inline_TlsGetValue2(key);
}

NTCRT_API int pthread_setspecific(pthread_key_t key, const void* value)
{
    return _Inline_TlsSetValue(key, (PVOID)value) ? 0 : (key < KEY_SLOTS ? ENOMEM : EINVAL);
}

/* The pointers that WINPTHREADS_USE_DLLIMPORT refers to (__declspec(dllimport) of the winpthreads headers) */
NTCRT_IMPORT_ONLY(pthread_key_create, pthread_key_create);
NTCRT_IMPORT_ONLY(pthread_key_delete, pthread_key_delete);
NTCRT_IMPORT_ONLY(pthread_getspecific, pthread_getspecific);
NTCRT_IMPORT_ONLY(pthread_setspecific, pthread_setspecific);
