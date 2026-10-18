/*
 * The wide printf family, see Stdio.cpp.
 */

#include "NtCRT.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <stdio.h>
#include <wchar.h>

/* Wide printf */

EXTERN_C int __cdecl _NtCRT_vfwprintf(FILE* stream, const wchar_t* format, va_list ap)
{
    int Ret;

    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    NTCRT_WIDE_FORMAT(format, FALSE);
    Ret = __mingw_vfwprintf(stream, _Format, ap);
    NTCRT_WIDE_FORMAT_FREE();
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_fwprintf(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwprintf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vwprintf(const wchar_t* format, va_list ap)
{
    return _NtCRT_vfwprintf(&_NtCRT_iob[1], format, ap);
}

EXTERN_C int __cdecl _NtCRT_wprintf(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwprintf(&_NtCRT_iob[1], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsnwprintf(wchar_t* buffer, size_t count, const wchar_t* format, va_list ap)
{
    va_list Copy;
    int Length;

    if (format == NULL || (buffer == NULL && count != 0))
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    NTCRT_WIDE_FORMAT(format, FALSE);
    va_copy(Copy, ap);
    Length = __mingw_vsnwprintf(buffer, count, _Format, ap);
    if (Length >= 0 && (size_t)Length >= count && count != 0)
    {
        wchar_t* Full = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, ((size_t)Length + 1) * sizeof(wchar_t));
        if (Full != NULL)
        {
            __mingw_vsnwprintf(Full, (size_t)Length + 1, _Format, Copy);
            memcpy(buffer, Full, count * sizeof(wchar_t));
            RtlFreeHeap(NTCRT_HEAP, 0, Full);
        }
        if ((size_t)Length > count)
        {
            Length = -1;
        }
    } else if (Length >= 0 && count == 0 && buffer != NULL)
    {
        Length = Length == 0 ? 0 : -1;
    }
    va_end(Copy);
    NTCRT_WIDE_FORMAT_FREE();
    return Length;
}

EXTERN_C int __cdecl _NtCRT__snwprintf(wchar_t* buffer, size_t count, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vsnwprintf(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

/* Legacy swprintf of msvcrt without a buffer size */
EXTERN_C int __cdecl _NtCRT_vswprintf(wchar_t* buffer, const wchar_t* format, va_list ap)
{
    return _NtCRT__vsnwprintf(buffer, INT_MAX / sizeof(wchar_t), format, ap);
}

EXTERN_C int __cdecl _NtCRT_swprintf(wchar_t* buffer, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vswprintf(buffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vscwprintf(const wchar_t* format, va_list ap)
{
    int Ret;

    if (format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    NTCRT_WIDE_FORMAT(format, FALSE);
    Ret = __mingw_vsnwprintf(NULL, 0, _Format, ap);
    NTCRT_WIDE_FORMAT_FREE();
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__scwprintf(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vscwprintf(format, ap);
    va_end(ap);
    return Ret;
}

/*
 * mingw-w64 wide pformat writes strings to a FILE with __ms_fwprintf and "%[-]*.*s" / "%[-]*.*S",
 * so it must not call back into pformat. Only string and character conversions are supported.
 */
static
bool
_NtCRT_MsPutWideChar(
    _In_ FILE* Stream,
    _In_ wchar_t Ch)
{
    return _NtCRT_putwc_nolock(Ch, Stream) != WEOF;
}

static
int
_NtCRT_MsPutWide(
    _In_ FILE* Stream,
    _In_reads_(Count) const wchar_t* Str,
    _In_ int Count,
    _In_ int Width,
    _In_ BOOL Left)
{
    int Pad = Width > Count ? Width - Count : 0;

    for (int i = 0; !Left && i < Pad; i++)
    {
        if (!_NtCRT_MsPutWideChar(Stream, L' '))
        {
            return -1;
        }
    }
    for (int i = 0; i < Count; i++)
    {
        if (!_NtCRT_MsPutWideChar(Stream, Str[i]))
        {
            return -1;
        }
    }
    for (int i = 0; Left && i < Pad; i++)
    {
        if (!_NtCRT_MsPutWideChar(Stream, L' '))
        {
            return -1;
        }
    }
    return Count + Pad;
}

EXTERN_C int __cdecl _NtCRT___ms_vfwprintf(FILE* stream, const wchar_t* format, va_list ap)
{
    int Total = 0;

    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_lock_file(stream);
    for (const wchar_t* f = format; *f != L'\0'; f++)
    {
        BOOL Left = FALSE, Narrow;
        int Width = 0, Precision = -1, Written;
        WCHAR Modifier = 0;

        if (*f != L'%')
        {
            if (!_NtCRT_MsPutWideChar(stream, *f))
            {
                goto _Fail;
            }
            Total++;
            continue;
        }
        f++;
        if (*f == L'%')
        {
            if (!_NtCRT_MsPutWideChar(stream, L'%'))
            {
                goto _Fail;
            }
            Total++;
            continue;
        }
        if (*f == L'-')
        {
            Left = TRUE;
            f++;
        }
        if (*f == L'*')
        {
            Width = va_arg(ap, int);
            if (Width < 0)
            {
                Left = TRUE;
                Width = -Width;
            }
            f++;
        } else
        {
            while (*f >= L'0' && *f <= L'9')
            {
                Width = Width * 10 + (*f++ - L'0');
            }
        }
        if (*f == L'.')
        {
            f++;
            Precision = 0;
            if (*f == L'*')
            {
                Precision = va_arg(ap, int);
                f++;
            } else
            {
                while (*f >= L'0' && *f <= L'9')
                {
                    Precision = Precision * 10 + (*f++ - L'0');
                }
            }
        }
        if (*f == L'h' || *f == L'l' || *f == L'w')
        {
            Modifier = *f++;
        }
        switch (*f)
        {
            case L's':
            case L'S':
                Narrow = Modifier == L'h' || (*f == L'S' && Modifier == 0);
                if (Narrow)
                {
                    const char* Str = va_arg(ap, const char*);
                    int Count;
                    PWSTR Wide;

                    if (Str == NULL)
                    {
                        Str = "(null)";
                    }
                    /* Bytes of the "C" locale */
                    Count = Precision >= 0 ? (int)strnlen(Str, Precision) : (int)strlen(Str);
                    Wide = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, (Count + 1) * sizeof(WCHAR));
                    if (Wide == NULL)
                    {
                        goto _Fail;
                    }
                    for (int i = 0; i < Count; i++)
                    {
                        Wide[i] = (unsigned char)Str[i];
                    }
                    Written = _NtCRT_MsPutWide(stream, Wide, Count, Width, Left);
                    RtlFreeHeap(NTCRT_HEAP, 0, Wide);
                } else
                {
                    const wchar_t* Str = va_arg(ap, const wchar_t*);
                    if (Str == NULL)
                    {
                        Str = L"(null)";
                    }
                    Written = _NtCRT_MsPutWide(stream,
                                               Str,
                                               Precision >= 0 ? (int)wcsnlen(Str, Precision) : (int)wcslen(Str),
                                               Width,
                                               Left);
                }
                break;
            case L'c':
            case L'C':
            {
                WCHAR Ch;
                if (Modifier == L'h' || (*f == L'C' && Modifier == 0))
                {
                    Ch = (unsigned char)va_arg(ap, int);
                } else
                {
                    Ch = (WCHAR)va_arg(ap, int);
                }
                Written = _NtCRT_MsPutWide(stream, &Ch, 1, Width, Left);
                break;
            }
            default:
                NTCRT_ERRNO = EINVAL;
                goto _Fail;
        }
        if (Written < 0)
        {
            goto _Fail;
        }
        Total += Written;
    }
    _NtCRT_unlock_file(stream);
    return Total;

_Fail:
    _NtCRT_unlock_file(stream);
    return -1;
}

EXTERN_C int __cdecl _NtCRT___ms_fwprintf(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT___ms_vfwprintf(stream, format, ap);
    va_end(ap);
    return Ret;
}

/*
 * The internal wide output of msvcrt, UTF-16 units are put into the stream buffer regardless of the text mode,
 * and it fails on a stream without a buffer
 */
EXTERN_C int __cdecl _NtCRT__woutput_s(FILE* stream, const wchar_t* format, va_list ap)
{
    va_list Copy;
    int Length;
    wchar_t* Buffer;

    if (stream == NULL || format == NULL)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return -1;
    }
    va_copy(Copy, ap);
    Length = _NtCRT__vscwprintf(format, Copy);
    va_end(Copy);
    if (Length < 0)
    {
        return -1;
    }
    Buffer = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, ((size_t)Length + 1) * sizeof(wchar_t));
    if (Buffer == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return -1;
    }
    _NtCRT__vsnwprintf(Buffer, (size_t)Length + 1, format, ap);
    for (int i = 0; i < Length; i++)
    {
        if ((stream->_cnt -= sizeof(wchar_t)) >= 0)
        {
            memcpy(stream->_ptr, &Buffer[i], sizeof(wchar_t));
            stream->_ptr += sizeof(wchar_t);
        } else if (stream->_base == NULL || _NtCRT_FlushPut((const char*)&Buffer[i], sizeof(wchar_t), stream) != 0)
        {
            stream->_flag |= _IOERR;
            Length = -1;
            break;
        }
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Buffer);
    return Length;
}

NTCRT_IMPORT(vfwprintf, _NtCRT_vfwprintf);
NTCRT_IMPORT(fwprintf, _NtCRT_fwprintf);
NTCRT_IMPORT(__ms_fwprintf, _NtCRT___ms_fwprintf);
NTCRT_IMPORT(_woutput_s, _NtCRT__woutput_s);
NTCRT_IMPORT(__ms_vfwprintf, _NtCRT___ms_vfwprintf);
NTCRT_IMPORT(vwprintf, _NtCRT_vwprintf);
NTCRT_IMPORT(wprintf, _NtCRT_wprintf);
NTCRT_IMPORT(_vsnwprintf, _NtCRT__vsnwprintf);
NTCRT_IMPORT(_snwprintf, _NtCRT__snwprintf);
NTCRT_IMPORT(snwprintf, _NtCRT__snwprintf);
NTCRT_IMPORT(vsnwprintf, _NtCRT__vsnwprintf);
NTCRT_IMPORT(vswprintf, _NtCRT_vswprintf);
NTCRT_IMPORT(swprintf, _NtCRT_swprintf);
NTCRT_IMPORT(_vswprintf, _NtCRT_vswprintf);
NTCRT_IMPORT(_swprintf, _NtCRT_swprintf);
NTCRT_IMPORT(_vscwprintf, _NtCRT__vscwprintf);
NTCRT_IMPORT(_scwprintf, _NtCRT__scwprintf);
