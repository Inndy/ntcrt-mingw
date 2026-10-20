/*
 * The pseudo handles of the current process and thread and their identifiers in the TEB (ClientId), in place of a
 * call (x64). A call by a pointer (&GetCurrentProcess) reaches the function of NtCRT.
 */

#include_next <processthreadsapi.h>

#if defined(__x86_64__) && defined(__GNUC__) && !defined(NTCRT_NO_INLINE_WIN32)
#ifndef NTCRT_PROCESSTHREADSAPI_INLINE
#define NTCRT_PROCESSTHREADSAPI_INLINE

#ifdef __cplusplus
extern "C" {
#endif

extern __inline__ __attribute__((__gnu_inline__, __always_inline__)) HANDLE WINAPI GetCurrentProcess(VOID)
{
    return (HANDLE)(LONG_PTR)-1;
}

extern __inline__ __attribute__((__gnu_inline__, __always_inline__)) HANDLE WINAPI GetCurrentThread(VOID)
{
    return (HANDLE)(LONG_PTR)-2;
}

extern __inline__ __attribute__((__gnu_inline__, __always_inline__)) DWORD WINAPI GetCurrentProcessId(VOID)
{
    DWORD Id;

    __asm__("movl %%gs:0x40, %0" : "=r"(Id));
    return Id;
}

extern __inline__ __attribute__((__gnu_inline__, __always_inline__)) DWORD WINAPI GetCurrentThreadId(VOID)
{
    DWORD Id;

    __asm__("movl %%gs:0x48, %0" : "=r"(Id));
    return Id;
}

#ifdef __cplusplus
}
#endif

#endif
#endif
