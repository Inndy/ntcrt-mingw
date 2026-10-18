/*
 * The wrappers of the scanf of mingw-w64.
 */

#include "NtCRT.h"

#include <stdio.h>

/*
 * The scanf of mingw-w64 fails the I64 length modifier of msvcrt after a width (%4I64d). The specs wrap its functions
 * (--wrap), which change I64, I32 and I to ll, nothing and ll. lld loads every object of a __wrap_ function: this one
 * refers to __real_ only, which does not load the scanf of mingw-w64.
 */
template<typename TChar>
static
void
_NtCRT_ScanFormatTo(
    _Inout_ NTCRT_FORMAT_WRITER<TChar>& Out)
{
    const TChar* f = Out.Format;

    while (*f != '\0')
    {
        if (*f != '%')
        {
            Out.Put(*f++);
            continue;
        }
        Out.Put(*f++);
        while (*f == '*' || (*f >= '0' && *f <= '9'))
        {
            Out.Put(*f++);
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
                Out.Put('l');
                Out.Put('l');
            }
        }
        while (*f == 'h' || *f == 'l' || *f == 'L' || *f == 'j' || *f == 'z' || *f == 't' || *f == 'w')
        {
            Out.Put(*f++);
        }
        if (*f == '[')
        {
            /* The scan set, ']' first is a member */
            Out.Put(*f++);
            if (*f == '^')
            {
                Out.Put(*f++);
            }
            if (*f == ']')
            {
                Out.Put(*f++);
            }
            while (*f != '\0' && *f != ']')
            {
                Out.Put(*f++);
            }
        } else if (*f != '\0')
        {
            Out.Put(*f++);
        }
    }
    Out.End();
}

/* A format without these modifiers is used as it is, see _NtCRT_RewriteFormat */
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
        Ret = __real_##Name(Source,                                                                            \
                            _NtCRT_RewriteFormat(Format, Buffer, ARRAYSIZE(Buffer), &Allocated,                \
                                                 _NtCRT_ScanFormatTo<TChar>),                                  \
                            ap);                                                                               \
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
