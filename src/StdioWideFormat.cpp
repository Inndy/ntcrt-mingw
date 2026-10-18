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
 * use, a format it does not change is used as it is. printf takes the wide formats of msvcrt itself (Printf.cpp).
 */

static
void
_NtCRT_TranslateWideFormatTo(
    _Inout_ NTCRT_FORMAT_WRITER<WCHAR>& Out,
    _In_ BOOL Scan)
{
    PCWSTR f = Out.Format;

    while (*f != L'\0')
    {
        BOOL Long = FALSE;
        int Short = 0;
        WCHAR Conv;

        if (*f != L'%')
        {
            Out.Put(*f++);
            continue;
        }
        Out.Put(*f++);
        if (*f == L'%')
        {
            Out.Put(*f++);
            continue;
        }
        /* Flags, width, precision and argument suppression */
        while (*f != L'\0' && wcschr(L"-+ #0123456789.*$'", *f) != NULL)
        {
            Out.Put(*f++);
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
                    Out.Put(L'l');
                    Out.Put(L'l');
                    Long = FALSE;
                    f++;
                    continue;
                }
                Long = TRUE;
                f++;
            } else if (*f == L'I')
            {
                Out.Put(*f++);
                if ((f[0] == L'6' && f[1] == L'4') || (f[0] == L'3' && f[1] == L'2'))
                {
                    Out.Put(*f++);
                    Out.Put(*f++);
                }
            } else if (*f != L'\0' && wcschr(L"Lqjzt", *f) != NULL)
            {
                Out.Put(*f++);
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
                Out.Put(L'l');
            }
        } else if (Conv == L'S' || Conv == L'C')
        {
            /* Narrow unless l is specified */
            if (Long)
            {
                Out.Put(L'l');
            }
            Conv = Conv == L'S' ? L's' : L'c';
        } else
        {
            /* h or hh */
            for (int i = 0; i < Short && i < 2; i++)
            {
                Out.Put(L'h');
            }
            if (Long)
            {
                Out.Put(L'l');
            }
        }
        if (Conv == L'\0')
        {
            break;
        }
        Out.Put(Conv);
        f++;
        if (Scan && Conv == L'[')
        {
            /* Copy the scan set */
            if (*f == L'^')
            {
                Out.Put(*f++);
            }
            if (*f == L']')
            {
                Out.Put(*f++);
            }
            while (*f != L'\0' && *f != L']')
            {
                Out.Put(*f++);
            }
            if (*f == L']')
            {
                Out.Put(*f++);
            }
        }
    }
    Out.End();
}

EXTERN_C
PCWSTR
_NtCRT_TranslateWideFormat(
    _In_ PCWSTR Format,
    _Out_writes_(BufferCount) PWSTR Buffer,
    _In_ size_t BufferCount,
    _In_ BOOL Scan,
    _Outptr_result_maybenull_ PWSTR* Allocated)
{
    return _NtCRT_RewriteFormat(Format, Buffer, BufferCount, Allocated,
                                [Scan](NTCRT_FORMAT_WRITER<WCHAR>& Out) { _NtCRT_TranslateWideFormatTo(Out, Scan); });
}
