/*
 * The wrappers of the scanf of mingw-w64.
 */

#include "NtCRT.h"

#include <stdio.h>

/*
 * The scanf of mingw-w64 fails the I64 length modifier of msvcrt after a width (%4I64d). The specs wrap its functions
 * (--wrap), which change I64, I32 and I to ll, nothing and ll. lld loads every object of a __wrap_ function: this one
 * refers to __real_ only, which does not load the scanf of mingw-w64. Buffer holds a format of up to Size characters, a
 * longer one is allocated.
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
