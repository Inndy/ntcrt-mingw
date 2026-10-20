/* The wide functions of the printf engine (Printf.inl): to a buffer, a growing buffer and a stream */

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

EXTERN_C int _NtCRT_FormatSwprintf(wchar_t* Buffer, size_t Count, const wchar_t* Format, va_list ap, ULONG Options)
{
    int Ret = _NtCRT_FormatBuffer(Buffer, Count, Format, ap, Options, FALSE);

    return Ret >= 0 && (size_t)Ret >= Count ? -1 : Ret;
}

/*
 * The whole output, formatted once: in *Buffer of Count characters, or in a buffer of NTCRT_HEAP that replaces *Buffer
 * and the caller frees, also on an error
 */
EXTERN_C __attribute__((noinline)) int
_NtCRT_FormatGrowW(wchar_t** Buffer, size_t Count, const wchar_t* Format, va_list ap, ULONG Options)
{
    NTCRT_PRINTF_OUT<wchar_t> o = {};

    o.Buffer = *Buffer;
    o.Count = Count;
    o.Grow = TRUE;
    _NtCRT_Format(&o, Format, ap, Options);
    *Buffer = o.Buffer;
    return _NtCRT_FormatResult(&o);
}

EXTERN_C int _NtCRT_FormatStreamW(FILE* Stream, const wchar_t* Format, va_list ap, ULONG Options)
{
    wchar_t Stack[256], *Buffer = Stack;
    int Ret;

    if (Stream == NULL || Format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Ret = _NtCRT_FormatGrowW(&Buffer, ARRAYSIZE(Stack), Format, ap, Options);
    if (Ret >= 0)
    {
        _NtCRT_lock_file(Stream);
        for (int i = 0; i < Ret; i++)
        {
            if (_NtCRT_putwc_nolock(Buffer[i], Stream) == WEOF)
            {
                Ret = -1;
                break;
            }
        }
        _NtCRT_unlock_file(Stream);
    }
    if (Buffer != Stack)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Buffer);
    }
    return Ret;
}
