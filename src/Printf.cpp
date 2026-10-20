/* The narrow functions of the printf engine (Printf.inl): to a buffer, an allocation and a stream */

#include "NtCRT.h"
#include "Printf.inl"

/* C snprintf: up to Count - 1 characters and the terminator, returns the length of the whole output */
EXTERN_C int _NtCRT_FormatString(char* Buffer, size_t Count, const char* Format, va_list ap, ULONG Options)
{
    return _NtCRT_FormatBuffer(Buffer, Count, Format, ap, Options, FALSE);
}

/* _vsnprintf of msvcrt: the terminator only if it fits, -1 if the output does not */
EXTERN_C int _NtCRT_FormatLegacy(char* Buffer, size_t Count, const char* Format, va_list ap, ULONG Options)
{
    return _NtCRT_FormatBuffer(Buffer, Count, Format, ap, Options, TRUE);
}

/* The whole output in a buffer of malloc, for vasprintf */
EXTERN_C int _NtCRT_FormatAlloc(char** Result, const char* Format, va_list ap, ULONG Options)
{
    char Stack[256];
    NTCRT_PRINTF_OUT<char> o = {};
    int Ret;

    o.Buffer = Stack;
    o.Count = sizeof(Stack);
    o.Grow = TRUE;

    *Result = NULL;
    if (Format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_Format(&o, Format, ap, Options);
    Ret = _NtCRT_FormatResult(&o);
    if (Ret >= 0)
    {
        *Result = (char*)malloc((size_t)Ret + 1);
        if (*Result == NULL)
        {
            NTCRT_ERRNO = ENOMEM;
            Ret = -1;
        } else
        {
            memcpy(*Result, o.Buffer, (size_t)Ret);
            (*Result)[Ret] = '\0';
        }
    }
    if (o.Heap)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, o.Buffer);
    }
    return Ret;
}

/* To a stream: the whole output is formatted first, so that an encoding error writes nothing */
EXTERN_C int _NtCRT_FormatStream(FILE* Stream, const char* Format, va_list ap, ULONG Options)
{
    char Stack[512];
    NTCRT_PRINTF_OUT<char> o = {};
    int Ret;

    o.Buffer = Stack;
    o.Count = sizeof(Stack);
    o.Grow = TRUE;

    if (Stream == NULL || Format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_Format(&o, Format, ap, Options);
    Ret = _NtCRT_FormatResult(&o);
    if (Ret >= 0 && _NtCRT_StreamWrite(Stream, o.Buffer, o.Length) != 0)
    {
        Ret = -1;
    }
    if (o.Heap)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, o.Buffer);
    }
    return Ret;
}
