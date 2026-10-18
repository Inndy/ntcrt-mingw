/*
 * The wide formats of msvcrt rewritten for mingw-w64, see Stdio.cpp.
 */

#include "NtCRT.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <stdio.h>
#include <wchar.h>

/*
 * Formatted I/O
 *
 * The wide scanf of msvcrt treats %s and %c as wide, the format is rewritten for the scanf of mingw-w64 before
 * use. printf takes the wide formats of msvcrt itself (Printf.cpp).
 */

EXTERN_C
PCWSTR
_NtCRT_TranslateWideFormat(
    _In_ PCWSTR Format,
    _Out_writes_(BufferCount) PWSTR Buffer,
    _In_ size_t BufferCount,
    _In_ BOOL Scan,
    _Outptr_result_maybenull_ PWSTR* Allocated)
{
    size_t Length = wcslen(Format);
    size_t Capacity = Length * 2 + 1;
    PWSTR Out, o;
    PCWSTR f = Format;

    *Allocated = NULL;
    if (wcschr(Format, L'%') == NULL)
    {
        return Format;
    }
    if (Capacity <= BufferCount)
    {
        Out = Buffer;
    } else
    {
        Out = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, Capacity * sizeof(WCHAR));
        if (Out == NULL)
        {
            return Format;
        }
        *Allocated = Out;
    }
    o = Out;
    while (*f != L'\0')
    {
        BOOL Long = FALSE;
        int Short = 0;
        WCHAR Conv;

        if (*f != L'%')
        {
            *o++ = *f++;
            continue;
        }
        *o++ = *f++;
        if (*f == L'%')
        {
            *o++ = *f++;
            continue;
        }
        /* Flags, width, precision and argument suppression */
        while (*f != L'\0' && wcschr(L"-+ #0123456789.*$'", *f) != NULL)
        {
            *o++ = *f++;
        }
        /* Length modifiers */
        for (;;)
        {
            if (*f == L'h')
            {
                Short++;
                f++;
            } else if (*f == L'l' || *f == L'w')
            {
                if (Long)
                {
                    /* ll */
                    *o++ = L'l';
                    *o++ = L'l';
                    Long = FALSE;
                    f++;
                    continue;
                }
                Long = TRUE;
                f++;
            } else if (*f == L'I')
            {
                *o++ = *f++;
                if ((f[0] == L'6' && f[1] == L'4') || (f[0] == L'3' && f[1] == L'2'))
                {
                    *o++ = *f++;
                    *o++ = *f++;
                }
            } else if (*f != L'\0' && wcschr(L"Lqjzt", *f) != NULL)
            {
                *o++ = *f++;
            } else
            {
                break;
            }
        }
        Conv = *f;
        if (Conv == L's' || Conv == L'c' || (Scan && Conv == L'['))
        {
            /* Wide unless h is specified */
            if (!Short)
            {
                *o++ = L'l';
            }
        } else if (Conv == L'S' || Conv == L'C')
        {
            /* Narrow unless l is specified */
            if (Long)
            {
                *o++ = L'l';
            }
            Conv = Conv == L'S' ? L's' : L'c';
        } else
        {
            /* h or hh */
            for (int i = 0; i < Short && i < 2; i++)
            {
                *o++ = L'h';
            }
            if (Long)
            {
                *o++ = L'l';
            }
        }
        if (Conv == L'\0')
        {
            break;
        }
        *o++ = Conv;
        f++;
        if (Scan && Conv == L'[')
        {
            /* Copy the scan set */
            if (*f == L'^')
            {
                *o++ = *f++;
            }
            if (*f == L']')
            {
                *o++ = *f++;
            }
            while (*f != L'\0' && *f != L']')
            {
                *o++ = *f++;
            }
            if (*f == L']')
            {
                *o++ = *f++;
            }
        }
    }
    *o = L'\0';
    return Out;
}

