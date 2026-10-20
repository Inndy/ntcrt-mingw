/*
 * The last error of the thread in its TEB, read and written in place of a call (x64). A call by a pointer
 * (&GetLastError) reaches the function of NtCRT.
 */

#include_next <errhandlingapi.h>

#if defined(__x86_64__) && defined(__GNUC__) && !defined(NTCRT_NO_INLINE_WIN32)
#ifndef NTCRT_ERRHANDLINGAPI_INLINE
#define NTCRT_ERRHANDLINGAPI_INLINE

#ifdef __cplusplus
extern "C" {
#endif

extern __inline__ __attribute__((__gnu_inline__, __always_inline__)) DWORD WINAPI GetLastError(VOID)
{
    DWORD Error;

    __asm__ __volatile__("movl %%gs:0x68, %0" : "=r"(Error));
    return Error;
}

extern __inline__ __attribute__((__gnu_inline__, __always_inline__)) VOID WINAPI SetLastError(DWORD dwErrCode)
{
    __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(dwErrCode));
}

#ifdef __cplusplus
}
#endif

#endif
#endif
