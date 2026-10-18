/* Names of threads, the description of a thread of Windows 10 1607 and later */

#include "NtCRT.h"

#include <pthread.h>

/* The name is in the ANSI code page */
NTCRT_API int pthread_setname_np(pthread_t thread, const char* name)
{
    UNICODE_STRING Name;
    PWSTR Wide;
    NTSTATUS Status;

    if (thread == 0 || name == NULL)
    {
        return EINVAL;
    }
    Wide = _NtCRT_AnsiToWide(name);
    if (Wide == NULL)
    {
        return ENOMEM;
    }
    RtlInitUnicodeString(&Name, Wide);
    Status = NtSetInformationThread((HANDLE)pthread_gethandle(thread), ThreadNameInformation, &Name, sizeof(Name));
    RtlFreeHeap(NTCRT_HEAP, 0, Wide);
    return NT_SUCCESS(Status) ? 0 : Status == STATUS_INVALID_INFO_CLASS ? ENOTSUP : ESRCH;
}

NTCRT_API int pthread_getname_np(pthread_t thread, char* name, size_t len)
{
    /* A UNICODE_STRING and its characters, on the heap for a longer name */
    alignas(8) WCHAR Buffer[sizeof(UNICODE_STRING) / sizeof(WCHAR) + 257];
    PWSTR Info = Buffer;
    UNICODE_STRING Name;
    PWSTR Start = Buffer;
    PWSTR End = Buffer;
    char* Narrow;
    NTSTATUS Status;
    ULONG Length;
    size_t Size;

    if (thread == 0 || name == NULL || len == 0)
    {
        return EINVAL;
    }
    Status = NtQueryInformationThread((HANDLE)pthread_gethandle(thread), ThreadNameInformation, Buffer,
                                      sizeof(Buffer) - sizeof(WCHAR), &Length);
    if (Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_INFO_LENGTH_MISMATCH)
    {
        Info = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, Length + sizeof(WCHAR));
        if (Info == NULL)
        {
            return ENOMEM;
        }
        Status =
            NtQueryInformationThread((HANDLE)pthread_gethandle(thread), ThreadNameInformation, Info, Length, NULL);
    }
    if (NT_SUCCESS(Status))
    {
        memcpy(&Name, Info, sizeof(Name));
        Start = Info + (Name.Buffer - Info);
        End = Start + Name.Length / sizeof(WCHAR);
    } else if (Status != STATUS_INVALID_INFO_CLASS)
    {
        if (Info != Buffer)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, Info);
        }
        return ESRCH;
    }
    /* No name is empty */
    *End = 0;
    Narrow = _NtCRT_WideToAnsi(Start);
    if (Info != Buffer)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Info);
    }
    if (Narrow == NULL)
    {
        return ENOMEM;
    }
    Size = strlen(Narrow) + 1;
    if (Size > len)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Narrow);
        return ERANGE;
    }
    memcpy(name, Narrow, Size);
    RtlFreeHeap(NTCRT_HEAP, 0, Narrow);
    return 0;
}

/* The pointers that WINPTHREADS_USE_DLLIMPORT refers to (__declspec(dllimport) of the winpthreads headers) */
NTCRT_IMPORT_ONLY(pthread_setname_np, pthread_setname_np);
NTCRT_IMPORT_ONLY(pthread_getname_np, pthread_getname_np);
