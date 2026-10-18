/* The wide functions of the printf engine (Printf.inl): to a buffer and a stream */

#include "NtCRT.h"
#include "Printf.inl"

EXTERN_C int _NtCRT_FormatStringW(wchar_t* Buffer, size_t Count, const wchar_t* Format, va_list ap, ULONG Options)
{
    return _NtCRT_FormatBuffer(Buffer, Count, Format, ap, Options, FALSE);
}

EXTERN_C int _NtCRT_FormatLegacyW(wchar_t* Buffer, size_t Count, const wchar_t* Format, va_list ap, ULONG Options)
{
    return _NtCRT_FormatBuffer(Buffer, Count, Format, ap, Options, TRUE);
}

EXTERN_C int _NtCRT_FormatStreamW(FILE* Stream, const wchar_t* Format, va_list ap, ULONG Options)
{
    wchar_t Stack[256];
    NTCRT_PRINTF_OUT<wchar_t> o = {};
    int Ret;

    o.Buffer = Stack;
    o.Count = ARRAYSIZE(Stack);
    o.Grow = TRUE;

    if (Stream == NULL || Format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_Format(&o, Format, (const ULONG64*)ap, Options);
    Ret = _NtCRT_FormatResult(&o);
    if (Ret >= 0)
    {
        _NtCRT_lock_file(Stream);
        for (size_t i = 0; i < o.Length; i++)
        {
            if (_NtCRT_putwc_nolock(o.Buffer[i], Stream) == WEOF)
            {
                Ret = -1;
                break;
            }
        }
        _NtCRT_unlock_file(Stream);
    }
    if (o.Heap)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, o.Buffer);
    }
    return Ret;
}
