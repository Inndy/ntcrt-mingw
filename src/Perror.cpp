/*
 * perror, apart from the strings (String.cpp) and the streams (Stdio.cpp) that it needs: the linker loads them
 * for it, not the streams for any string function.
 */

#include "NtCRT.h"

#include <stdio.h>
#include <string.h>

EXTERN_C char* __cdecl _NtCRT_strerror(int errnum);

EXTERN_C void __cdecl _NtCRT_perror(const char* str)
{
    const char* Message = _NtCRT_strerror(NTCRT_ERRNO);
    FILE* Stream = &_NtCRT_iob[2];

    _NtCRT_lock_file(Stream);
    _NtCRT__fflush_nolock(Stream);
    if (str != NULL && *str != '\0')
    {
        _NtCRT_write(2, str, (unsigned int)strlen(str));
        _NtCRT_write(2, ": ", 2);
    }
    _NtCRT_write(2, Message, (unsigned int)strlen(Message));
    _NtCRT_write(2, "\n", 1);
    _NtCRT_unlock_file(Stream);
}

/* The prefix is converted to the file code page */
EXTERN_C void __cdecl _NtCRT__wperror(const wchar_t* str)
{
    char* Narrow = NULL;

    if (str != NULL && *str != L'\0')
    {
        Narrow = _NtCRT_WideToFile(str);
    }
    _NtCRT_perror(Narrow);
    RtlFreeHeap(NTCRT_HEAP, 0, Narrow);
}

NTCRT_IMPORT(perror, _NtCRT_perror);
NTCRT_IMPORT(_wperror, _NtCRT__wperror);
