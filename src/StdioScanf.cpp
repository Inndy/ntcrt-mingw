/*
 * The scanf family, see Stdio.cpp.
 */

#include "NtCRT.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/*
 * The scanf of mingw-w64 fails the I64 length modifier of msvcrt after a width (%4I64d). The specs wrap its
 * functions (--wrap), which change I64, I32 and I to ll, nothing and ll. Buffer holds a format of up to Size
 * characters, a longer one is allocated.
 */
template<typename TChar>
static
const TChar*
_NtCRT_ScanFormat(
    _In_z_ const TChar* Format,
    _Out_writes_(Size) TChar* Buffer,
    _In_ size_t Size,
    _Outptr_result_maybenull_ TChar** Allocated)
{
    size_t Length = 0, Capacity;
    const TChar* f = Format;
    TChar* o;
    BOOL Found = FALSE;

    *Allocated = NULL;
    for (; Format[Length] != '\0'; Length++)
    {
        Found |= Format[Length] == 'I';
    }
    if (!Found)
    {
        return Format;
    }
    Capacity = Length * 2 + 1;
    if (Capacity > Size)
    {
        Buffer = (TChar*)RtlAllocateHeap(NTCRT_HEAP, 0, Capacity * sizeof(TChar));
        if (Buffer == NULL)
        {
            return Format;
        }
        *Allocated = Buffer;
    }
    o = Buffer;
    while (*f != '\0')
    {
        if (*f != '%')
        {
            *o++ = *f++;
            continue;
        }
        *o++ = *f++;
        while (*f == '*' || (*f >= '0' && *f <= '9'))
        {
            *o++ = *f++;
        }
        if (*f == 'I')
        {
            f++;
            if (f[0] == '3' && f[1] == '2')
            {
                f += 2;
            } else
            {
                f += f[0] == '6' && f[1] == '4' ? 2 : 0;
                *o++ = 'l';
                *o++ = 'l';
            }
        }
        while (*f == 'h' || *f == 'l' || *f == 'L' || *f == 'j' || *f == 'z' || *f == 't' || *f == 'w')
        {
            *o++ = *f++;
        }
        if (*f == '[')
        {
            /* The scan set, ']' first is a member */
            *o++ = *f++;
            if (*f == '^')
            {
                *o++ = *f++;
            }
            if (*f == ']')
            {
                *o++ = *f++;
            }
            while (*f != '\0' && *f != ']')
            {
                *o++ = *f++;
            }
        } else if (*f != '\0')
        {
            *o++ = *f++;
        }
    }
    *o = '\0';
    return Buffer;
}

#define NTCRT_WRAP_SCANF(Name, TChar, TSource)                                                                   \
    EXTERN_C int __cdecl __real_##Name(TSource Source, const TChar* Format, va_list ap);                         \
    EXTERN_C int __cdecl __wrap_##Name(TSource Source, const TChar* Format, va_list ap)                          \
    {                                                                                                          \
        TChar Buffer[256];                                                                                     \
        TChar* Allocated;                                                                                      \
        int Ret;                                                                                               \
                                                                                                               \
        if (Format == NULL)                                                                                    \
        {                                                                                                      \
            return __real_##Name(Source, Format, ap);                                                          \
        }                                                                                                      \
        Ret = __real_##Name(Source, _NtCRT_ScanFormat(Format, Buffer, ARRAYSIZE(Buffer), &Allocated), ap);     \
        if (Allocated != NULL)                                                                                 \
        {                                                                                                      \
            RtlFreeHeap(NTCRT_HEAP, 0, Allocated);                                                             \
        }                                                                                                      \
        return Ret;                                                                                            \
    }

NTCRT_WRAP_SCANF(__mingw_vfscanf, char, FILE*)
NTCRT_WRAP_SCANF(__mingw_vsscanf, char, const char*)
NTCRT_WRAP_SCANF(__mingw_vfwscanf, wchar_t, FILE*)
NTCRT_WRAP_SCANF(__mingw_vswscanf, wchar_t, const wchar_t*)

/* Narrow scanf */

EXTERN_C int __cdecl _NtCRT_vfscanf(FILE* stream, const char* format, va_list ap)
{
    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    return __mingw_vfscanf(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT_fscanf(FILE* stream, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfscanf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vscanf(const char* format, va_list ap)
{
    return _NtCRT_vfscanf(&_NtCRT_iob[0], format, ap);
}

EXTERN_C int __cdecl _NtCRT_scanf(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfscanf(&_NtCRT_iob[0], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vsscanf(const char* str, const char* format, va_list ap)
{
    if (str == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    return __mingw_vsscanf(str, format, ap);
}

EXTERN_C int __cdecl _NtCRT_sscanf(const char* str, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vsscanf(str, format, ap);
    va_end(ap);
    return Ret;
}

/* Wide scanf */

EXTERN_C int __cdecl _NtCRT_vfwscanf(FILE* stream, const wchar_t* format, va_list ap)
{
    int Ret;

    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    NTCRT_WIDE_FORMAT(format, TRUE);
    Ret = __mingw_vfwscanf(stream, _Format, ap);
    NTCRT_WIDE_FORMAT_FREE();
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_fwscanf(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwscanf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vwscanf(const wchar_t* format, va_list ap)
{
    return _NtCRT_vfwscanf(&_NtCRT_iob[0], format, ap);
}

EXTERN_C int __cdecl _NtCRT_wscanf(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwscanf(&_NtCRT_iob[0], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vswscanf(const wchar_t* str, const wchar_t* format, va_list ap)
{
    int Ret;

    if (str == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    NTCRT_WIDE_FORMAT(format, TRUE);
    Ret = __mingw_vswscanf(str, _Format, ap);
    NTCRT_WIDE_FORMAT_FREE();
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_swscanf(const wchar_t* str, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vswscanf(str, format, ap);
    va_end(ap);
    return Ret;
}

NTCRT_IMPORT_ONLY(vfscanf, _NtCRT_vfscanf);
NTCRT_IMPORT(fscanf, _NtCRT_fscanf);
NTCRT_IMPORT_ONLY(vscanf, _NtCRT_vscanf);
NTCRT_IMPORT(scanf, _NtCRT_scanf);
NTCRT_IMPORT_ONLY(vsscanf, _NtCRT_vsscanf);
NTCRT_IMPORT(sscanf, _NtCRT_sscanf);
NTCRT_IMPORT_ONLY(vfwscanf, _NtCRT_vfwscanf);
NTCRT_IMPORT(fwscanf, _NtCRT_fwscanf);
NTCRT_IMPORT_ONLY(vwscanf, _NtCRT_vwscanf);
NTCRT_IMPORT(wscanf, _NtCRT_wscanf);
NTCRT_IMPORT_ONLY(vswscanf, _NtCRT_vswscanf);
NTCRT_IMPORT(swscanf, _NtCRT_swscanf);
