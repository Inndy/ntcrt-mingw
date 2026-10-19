/*
 * msvcrt printf variants: "_s" (no %n), "_c" (count) and "_p" (positional parameters). The scanf ones are of the
 * scanf engine (Scanf.inl).
 *
 * On x64 every variadic argument takes an 8-byte slot (larger ones are passed by reference), so a va_list is a
 * pointer to slots. Positional parameters are implemented by rebuilding the format and the slots.
 */

#include "NtCRT.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static_assert(sizeof(va_list) == sizeof(void*), "va_list must be a pointer to the argument slots");

extern "C" FILE _NtCRT_iob[];

EXTERN_C int __cdecl _NtCRT_vfprintf(FILE* stream, const char* format, va_list ap);
EXTERN_C int __cdecl _NtCRT_vfwprintf(FILE* stream, const wchar_t* format, va_list ap);
EXTERN_C int __cdecl _NtCRT__vscprintf(const char* format, va_list ap);
EXTERN_C int __cdecl _NtCRT__vscwprintf(const wchar_t* format, va_list ap);
EXTERN_C int __cdecl _NtCRT__vcprintf(const char* format, va_list ap);
EXTERN_C int __cdecl _NtCRT__vcwprintf(const wchar_t* format, va_list ap);

#define NTCRT_ARGMAX 100 /* _ARGMAX */

static size_t _NtCRT_TLen(const char* s) { return strlen(s); }
static size_t _NtCRT_TLen(const wchar_t* s) { return wcslen(s); }

static
va_list
_NtCRT_SlotList(
    _In_ ULONG_PTR* Slots)
{
    return (va_list)(void*)Slots;
}

/* Allocate a copy of the first Length characters of Str */
template<typename TChar>
static
TChar*
_NtCRT_TDup(
    _In_reads_(Length) const TChar* Str,
    _In_ size_t Length)
{
    TChar* p = (TChar*)RtlAllocateHeap(NTCRT_HEAP, 0, (Length + 1) * sizeof(TChar));

    if (p != NULL)
    {
        memcpy(p, Str, Length * sizeof(TChar));
        p[Length] = 0;
    }
    return p;
}

static
bool
_NtCRT_IsDigit(
    _In_ unsigned int c)
{
    return c >= '0' && c <= '9';
}

/* A printf conversion specification after '%' */
typedef struct _NTCRT_PRINTF_SPEC
{
    size_t Start;       /* Offset of '%' */
    size_t End;         /* Offset after the conversion character */
    size_t FlagsStart;  /* Offset after "n$" */
    int Position;       /* n of "%n$", 0 if not positional */
    int WidthPosition;  /* m of "*m$", -1 for '*', 0 for none */
    int PrecisionPosition;
    size_t WidthStart, WidthEnd;         /* Width text, "*m$" included */
    size_t PrecisionStart, PrecisionEnd; /* Precision text after '.', "*m$" included */
    unsigned int Conversion;
} NTCRT_PRINTF_SPEC, *PNTCRT_PRINTF_SPEC;

/* Parse "n$" at p, returns n or 0 */
template<typename TChar>
static
int
_NtCRT_ParsePosition(
    _In_z_ const TChar* Format,
    _Inout_ size_t* i)
{
    size_t j = *i;
    int n = 0;

    while (_NtCRT_IsDigit(Format[j]) && n < 10000)
    {
        n = n * 10 + (Format[j++] - '0');
    }
    if (j > *i && Format[j] == '$' && n > 0)
    {
        *i = j + 1;
        return n;
    }
    return 0;
}

/* Parse the conversion at Format[i] == '%' (not "%%"), returns false for an incomplete one */
template<typename TChar>
static
bool
_NtCRT_ParsePrintfSpec(
    _In_z_ const TChar* Format,
    _In_ size_t i,
    _Out_ PNTCRT_PRINTF_SPEC Spec)
{
    Spec->Start = i++;
    Spec->Position = _NtCRT_ParsePosition(Format, &i);
    Spec->FlagsStart = i;
    while (Format[i] == '-' || Format[i] == '+' || Format[i] == ' ' || Format[i] == '#' || Format[i] == '0')
    {
        i++;
    }
    Spec->WidthStart = i;
    Spec->WidthPosition = 0;
    if (Format[i] == '*')
    {
        i++;
        Spec->WidthPosition = _NtCRT_ParsePosition(Format, &i);
        if (Spec->WidthPosition == 0)
        {
            Spec->WidthPosition = -1;
        }
    } else
    {
        while (_NtCRT_IsDigit(Format[i]))
        {
            i++;
        }
    }
    Spec->WidthEnd = i;
    Spec->PrecisionPosition = 0;
    Spec->PrecisionStart = Spec->PrecisionEnd = 0;
    if (Format[i] == '.')
    {
        Spec->PrecisionStart = ++i;
        if (Format[i] == '*')
        {
            i++;
            Spec->PrecisionPosition = _NtCRT_ParsePosition(Format, &i);
            if (Spec->PrecisionPosition == 0)
            {
                Spec->PrecisionPosition = -1;
            }
        } else
        {
            while (_NtCRT_IsDigit(Format[i]))
            {
                i++;
            }
        }
        Spec->PrecisionEnd = i;
    }
    for (;;)
    {
        TChar c = Format[i];

        if (c == 'h' || c == 'l' || c == 'L' || c == 'w' || c == 'j' || c == 'z' || c == 't' || c == 'q')
        {
            i++;
        } else if (c == 'I')
        {
            i++;
            if ((Format[i] == '3' && Format[i + 1] == '2') || (Format[i] == '6' && Format[i + 1] == '4'))
            {
                i += 2;
            }
        } else
        {
            break;
        }
    }
    if (Format[i] == 0)
    {
        return false;
    }
    Spec->Conversion = (unsigned int)Format[i];
    Spec->End = i + 1;
    return true;
}

/* Offset of the first %n conversion, or SIZE_MAX */
template<typename TChar>
static
size_t
_NtCRT_FindPrintfN(
    _In_z_ const TChar* Format)
{
    NTCRT_PRINTF_SPEC Spec;

    for (size_t i = 0; Format[i] != 0; i++)
    {
        if (Format[i] != '%')
        {
            continue;
        }
        if (Format[i + 1] == '%')
        {
            i++;
            continue;
        }
        if (!_NtCRT_ParsePrintfSpec(Format, i, &Spec))
        {
            break;
        }
        if (Spec.Conversion == 'n')
        {
            return i;
        }
        i = Spec.End - 1;
    }
    return SIZE_MAX;
}

/*
 * Print a "_s" format by Print(Format, ap). %n is an invalid parameter, msvcrt fails when it reaches it, after
 * printing what precedes it.
 */
template<typename TChar, typename TPrint>
static
int
_NtCRT_PrintS(
    _In_z_ const TChar* Format,
    _In_ va_list ap,
    _In_ TPrint Print)
{
    size_t Bad = _NtCRT_FindPrintfN(Format);
    TChar* Prefix;

    if (Bad == SIZE_MAX)
    {
        return Print(Format, ap);
    }
    Prefix = _NtCRT_TDup(Format, Bad);
    if (Prefix != NULL)
    {
        Print(Prefix, ap);
        RtlFreeHeap(NTCRT_HEAP, 0, Prefix);
    }
    _NtCRT_InvalidParameter(EINVAL);
    return -1;
}

/*
 * Print a format with positional parameters ("%n$", "*m$") by Print(Format, ap). All conversions must be either
 * positional or not. A format without them is printed as is.
 */
template<typename TChar, typename TPrint>
static
int
_NtCRT_PrintP(
    _In_z_ const TChar* Format,
    _In_ va_list ap,
    _In_ TPrint Print)
{
    NTCRT_PRINTF_SPEC Spec;
    ULONG_PTR Slots[NTCRT_ARGMAX + 1];
    ULONG_PTR* Args;
    size_t ArgCount = 0;
    size_t Length = _NtCRT_TLen(Format);
    TChar* NewFormat;
    TChar* o;
    int Mode = 0; /* 1 positional, -1 not */
    int Max = 0;
    int Ret;
    size_t i;

    /* Check the mode and find the largest position */
    for (i = 0; i < Length; i++)
    {
        int ThisMode;

        if (Format[i] != '%')
        {
            continue;
        }
        if (Format[i + 1] == '%')
        {
            i++;
            continue;
        }
        if (!_NtCRT_ParsePrintfSpec(Format, i, &Spec))
        {
            break;
        }
        ThisMode = Spec.Position > 0 ? 1 : -1;
        if (Spec.WidthPosition != 0 && (Spec.WidthPosition > 0 ? 1 : -1) != ThisMode)
        {
            ThisMode = 0;
        }
        if (Spec.PrecisionPosition != 0 && (Spec.PrecisionPosition > 0 ? 1 : -1) != ThisMode)
        {
            ThisMode = 0;
        }
        if (Mode == 0)
        {
            Mode = ThisMode;
        }
        if (ThisMode != Mode || Spec.Position > NTCRT_ARGMAX || Spec.WidthPosition > NTCRT_ARGMAX ||
            Spec.PrecisionPosition > NTCRT_ARGMAX)
        {
            /* Not positional so far, what precedes is printed like msvcrt */
            if (Mode < 0)
            {
                TChar* Prefix = _NtCRT_TDup(Format, i);

                if (Prefix != NULL)
                {
                    Print(Prefix, ap);
                    RtlFreeHeap(NTCRT_HEAP, 0, Prefix);
                }
            }
            _NtCRT_InvalidParameter(EINVAL);
            return -1;
        }
        Max = Spec.Position > Max ? Spec.Position : Max;
        Max = Spec.WidthPosition > Max ? Spec.WidthPosition : Max;
        Max = Spec.PrecisionPosition > Max ? Spec.PrecisionPosition : Max;
        /* A position may be used again, every use takes a slot of Args */
        ArgCount += 1 + (Spec.WidthPosition > 0) + (Spec.PrecisionPosition > 0);
        i = Spec.End - 1;
    }
    if (Mode <= 0)
    {
        return Print(Format, ap);
    }

    for (int n = 1; n <= Max; n++)
    {
        Slots[n] = va_arg(ap, ULONG_PTR);
    }
    /* A conversion is at least 3 characters ("%1$d") and takes 3 slots at most, the sizes cannot overflow */
    Args = (ULONG_PTR*)RtlAllocateHeap(NTCRT_HEAP, 0, ArgCount * sizeof(ULONG_PTR) + (Length + 1) * sizeof(TChar));
    if (Args == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return -1;
    }
    NewFormat = (TChar*)(Args + ArgCount);
    ArgCount = 0;
    o = NewFormat;
    for (i = 0; i < Length; i++)
    {
        if (Format[i] != '%' || Format[i + 1] == '%')
        {
            *o++ = Format[i];
            if (Format[i] == '%')
            {
                *o++ = Format[++i];
            }
            continue;
        }
        if (!_NtCRT_ParsePrintfSpec(Format, i, &Spec))
        {
            break;
        }
        *o++ = '%';
        for (size_t j = Spec.FlagsStart; j < Spec.WidthStart; j++)
        {
            *o++ = Format[j];
        }
        if (Spec.WidthPosition > 0)
        {
            *o++ = '*';
            Args[ArgCount++] = Slots[Spec.WidthPosition];
        } else
        {
            for (size_t j = Spec.WidthStart; j < Spec.WidthEnd; j++)
            {
                *o++ = Format[j];
            }
        }
        if (Spec.PrecisionStart != 0)
        {
            *o++ = '.';
            if (Spec.PrecisionPosition > 0)
            {
                *o++ = '*';
                Args[ArgCount++] = Slots[Spec.PrecisionPosition];
            } else
            {
                for (size_t j = Spec.PrecisionStart; j < Spec.PrecisionEnd; j++)
                {
                    *o++ = Format[j];
                }
            }
        }
        for (size_t j = Spec.PrecisionStart != 0 ? Spec.PrecisionEnd : Spec.WidthEnd; j < Spec.End; j++)
        {
            *o++ = Format[j];
        }
        Args[ArgCount++] = Slots[Spec.Position];
        i = Spec.End - 1;
    }
    *o = 0;
    Ret = Print(NewFormat, _NtCRT_SlotList(Args));
    RtlFreeHeap(NTCRT_HEAP, 0, Args);
    return Ret;
}

#pragma region "_s" printf

EXTERN_C int __cdecl _NtCRT_vfprintf_s(FILE* stream, const char* format, va_list ap)
{
    if (stream == NULL || format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    return _NtCRT_PrintS(format, ap, [stream](const char* f, va_list a) { return _NtCRT_vfprintf(stream, f, a); });
}

EXTERN_C int __cdecl _NtCRT_fprintf_s(FILE* stream, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfprintf_s(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vprintf_s(const char* format, va_list ap)
{
    return _NtCRT_vfprintf_s(&_NtCRT_iob[1], format, ap);
}

EXTERN_C int __cdecl _NtCRT_printf_s(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfprintf_s(&_NtCRT_iob[1], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vfwprintf_s(FILE* stream, const wchar_t* format, va_list ap)
{
    if (stream == NULL || format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    return _NtCRT_PrintS(format, ap, [stream](const wchar_t* f, va_list a) { return _NtCRT_vfwprintf(stream, f, a); });
}

EXTERN_C int __cdecl _NtCRT_fwprintf_s(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwprintf_s(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vwprintf_s(const wchar_t* format, va_list ap)
{
    return _NtCRT_vfwprintf_s(&_NtCRT_iob[1], format, ap);
}

EXTERN_C int __cdecl _NtCRT_wprintf_s(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwprintf_s(&_NtCRT_iob[1], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vcprintf_s(const char* format, va_list ap)
{
    if (format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    return _NtCRT_PrintS(format, ap, _NtCRT__vcprintf);
}

EXTERN_C int __cdecl _NtCRT__cprintf_s(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vcprintf_s(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vcwprintf_s(const wchar_t* format, va_list ap)
{
    if (format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    return _NtCRT_PrintS(format, ap, _NtCRT__vcwprintf);
}

EXTERN_C int __cdecl _NtCRT__cwprintf_s(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vcwprintf_s(format, ap);
    va_end(ap);
    return Ret;
}

/* C snprintf: the length of the whole output, negative for a formatting error only (EILSEQ, ...) */
static int __cdecl _NtCRT_vsnprintfT(char* b, size_t n, const char* f, va_list a)
{
    return _NtCRT_FormatString(b, n, f, a, 0);
}
static int __cdecl _NtCRT_vsnprintfT(wchar_t* b, size_t n, const wchar_t* f, va_list a)
{
    return _NtCRT_FormatStringW(b, n, f, a, NTCRT_PRINTF_LEGACY_WIDE);
}

/*
 * _vsnprintf_s: at most Count characters, the whole buffer or _TRUNCATE. Exceeding Count or _TRUNCATE truncates
 * and returns -1, exceeding the buffer otherwise empties it. msvcrt sets errno to ERANGE on truncation in the
 * narrow functions only. A formatting error returns -1 with its errno.
 */
template<typename TChar>
static
int
_NtCRT_VsnprintfS(
    _Out_writes_opt_(Size) TChar* Buffer,
    _In_ size_t Size,
    _In_ size_t Count,
    _In_z_ const TChar* Format,
    _In_ va_list ap)
{
    int Ret;

    if (Buffer == NULL && Size == 0 && Count == 0)
    {
        return 0;
    }
    if (Format == NULL || Buffer == NULL || Size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    if (_NtCRT_FindPrintfN(Format) != SIZE_MAX)
    {
        Buffer[0] = 0;
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    if (Count != _TRUNCATE && Count < Size)
    {
        Ret = _NtCRT_vsnprintfT(Buffer, Count + 1, Format, ap);
        Buffer[Count] = 0;
        if (Ret >= 0 && (size_t)Ret > Count)
        {
            if (sizeof(TChar) == sizeof(char))
            {
                NTCRT_ERRNO = ERANGE;
            }
            return -1;
        }
        return Ret;
    }
    Ret = _NtCRT_vsnprintfT(Buffer, Size, Format, ap);
    Buffer[Size - 1] = 0;
    if (Ret >= 0 && (size_t)Ret >= Size)
    {
        if (Count == _TRUNCATE)
        {
            if (sizeof(TChar) == sizeof(char))
            {
                NTCRT_ERRNO = ERANGE;
            }
            return -1;
        }
        Buffer[0] = 0;
        return _NtCRT_InvalidParameter(ERANGE), -1;
    }
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsnprintf_s(
    char* buffer, size_t sizeOfBuffer, size_t count, const char* format, va_list ap)
{
    return _NtCRT_VsnprintfS(buffer, sizeOfBuffer, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT__snprintf_s(char* buffer, size_t sizeOfBuffer, size_t count, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_VsnprintfS(buffer, sizeOfBuffer, count, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsnwprintf_s(
    wchar_t* buffer, size_t sizeOfBuffer, size_t count, const wchar_t* format, va_list ap)
{
    return _NtCRT_VsnprintfS(buffer, sizeOfBuffer, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT__snwprintf_s(wchar_t* buffer, size_t sizeOfBuffer, size_t count, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_VsnprintfS(buffer, sizeOfBuffer, count, format, ap);
    va_end(ap);
    return Ret;
}

/* The buffer must hold the output with the terminator */
EXTERN_C int __cdecl _NtCRT_vsprintf_s(char* buffer, size_t sizeOfBuffer, const char* format, va_list ap)
{
    if (buffer == NULL || sizeOfBuffer == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    return _NtCRT_VsnprintfS(buffer, sizeOfBuffer, sizeOfBuffer, format, ap);
}

EXTERN_C int __cdecl _NtCRT_sprintf_s(char* buffer, size_t sizeOfBuffer, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vsprintf_s(buffer, sizeOfBuffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vswprintf_s(wchar_t* buffer, size_t sizeOfBuffer, const wchar_t* format, va_list ap)
{
    if (buffer == NULL || sizeOfBuffer == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    return _NtCRT_VsnprintfS(buffer, sizeOfBuffer, sizeOfBuffer, format, ap);
}

EXTERN_C int __cdecl _NtCRT_swprintf_s(wchar_t* buffer, size_t sizeOfBuffer, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vswprintf_s(buffer, sizeOfBuffer, format, ap);
    va_end(ap);
    return Ret;
}

#pragma endregion

#pragma region "_c" printf

/* Like _vsnprintf, but output that does not fit with the terminator empties the buffer and fails */
template<typename TChar>
static
int
_NtCRT_VsnprintfC(
    _Out_writes_opt_(Count) TChar* Buffer,
    _In_ size_t Count,
    _In_z_ const TChar* Format,
    _In_ va_list ap)
{
    int Ret;

    if (Format == NULL || (Buffer == NULL && Count != 0))
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    Ret = _NtCRT_vsnprintfT(Buffer, Count, Format, ap);
    if (Ret >= 0 && (size_t)Ret >= Count)
    {
        if (Count != 0)
        {
            Buffer[0] = 0;
        }
        return _NtCRT_InvalidParameter(ERANGE), -1;
    }
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsnprintf_c(char* buffer, size_t count, const char* format, va_list ap)
{
    if (buffer == NULL && count == 0 && format != NULL)
    {
        return _NtCRT__vscprintf(format, ap);
    }
    return _NtCRT_VsnprintfC(buffer, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT__snprintf_c(char* buffer, size_t count, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vsnprintf_c(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vswprintf_c(wchar_t* buffer, size_t count, const wchar_t* format, va_list ap)
{
    if (buffer == NULL && count == 0 && format != NULL)
    {
        return _NtCRT__vscwprintf(format, ap);
    }
    return _NtCRT_VsnprintfC(buffer, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT__swprintf_c(wchar_t* buffer, size_t count, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vswprintf_c(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

#pragma endregion

#pragma region "_p" printf

/* Output that does not fit is truncated and terminated */
template<typename TChar>
static
int
_NtCRT_VsprintfP(
    _Out_writes_(Size) TChar* Buffer,
    _In_ size_t Size,
    _In_z_ const TChar* Format,
    _In_ va_list ap)
{
    int Ret = _NtCRT_vsnprintfT(Buffer, Size, Format, ap);

    if (Ret >= 0 && (size_t)Ret >= Size)
    {
        Buffer[Size - 1] = 0;
        NTCRT_ERRNO = ERANGE;
        return -1;
    }
    return Ret;
}

#define NTCRT_CHECK_FORMAT(format) \
    if ((format) == NULL) \
    { \
        return _NtCRT_InvalidParameter(EINVAL), -1; \
    }

EXTERN_C int __cdecl _NtCRT__vfprintf_p(FILE* stream, const char* format, va_list ap)
{
    if (stream == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    NTCRT_CHECK_FORMAT(format);
    return _NtCRT_PrintP(format, ap, [stream](const char* f, va_list a) { return _NtCRT_vfprintf(stream, f, a); });
}

EXTERN_C int __cdecl _NtCRT__fprintf_p(FILE* stream, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vfprintf_p(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vprintf_p(const char* format, va_list ap)
{
    return _NtCRT__vfprintf_p(&_NtCRT_iob[1], format, ap);
}

EXTERN_C int __cdecl _NtCRT__printf_p(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vfprintf_p(&_NtCRT_iob[1], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vfwprintf_p(FILE* stream, const wchar_t* format, va_list ap)
{
    if (stream == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    NTCRT_CHECK_FORMAT(format);
    return _NtCRT_PrintP(format, ap, [stream](const wchar_t* f, va_list a) { return _NtCRT_vfwprintf(stream, f, a); });
}

EXTERN_C int __cdecl _NtCRT__fwprintf_p(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vfwprintf_p(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vwprintf_p(const wchar_t* format, va_list ap)
{
    return _NtCRT__vfwprintf_p(&_NtCRT_iob[1], format, ap);
}

EXTERN_C int __cdecl _NtCRT__wprintf_p(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vfwprintf_p(&_NtCRT_iob[1], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsprintf_p(char* buffer, size_t sizeOfBuffer, const char* format, va_list ap)
{
    if (buffer == NULL || sizeOfBuffer == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    NTCRT_CHECK_FORMAT(format);
    return _NtCRT_PrintP(format, ap, [buffer, sizeOfBuffer](const char* f, va_list a) {
        return _NtCRT_VsprintfP(buffer, sizeOfBuffer, f, a);
    });
}

EXTERN_C int __cdecl _NtCRT__sprintf_p(char* buffer, size_t sizeOfBuffer, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vsprintf_p(buffer, sizeOfBuffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vswprintf_p(wchar_t* buffer, size_t sizeOfBuffer, const wchar_t* format, va_list ap)
{
    if (buffer == NULL || sizeOfBuffer == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL), -1;
    }
    NTCRT_CHECK_FORMAT(format);
    return _NtCRT_PrintP(format, ap, [buffer, sizeOfBuffer](const wchar_t* f, va_list a) {
        return _NtCRT_VsprintfP(buffer, sizeOfBuffer, f, a);
    });
}

EXTERN_C int __cdecl _NtCRT__swprintf_p(wchar_t* buffer, size_t sizeOfBuffer, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vswprintf_p(buffer, sizeOfBuffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vscprintf_p(const char* format, va_list ap)
{
    NTCRT_CHECK_FORMAT(format);
    return _NtCRT_PrintP(format, ap, _NtCRT__vscprintf);
}

EXTERN_C int __cdecl _NtCRT__scprintf_p(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vscprintf_p(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vscwprintf_p(const wchar_t* format, va_list ap)
{
    NTCRT_CHECK_FORMAT(format);
    return _NtCRT_PrintP(format, ap, _NtCRT__vscwprintf);
}

EXTERN_C int __cdecl _NtCRT__scwprintf_p(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vscwprintf_p(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vcprintf_p(const char* format, va_list ap)
{
    NTCRT_CHECK_FORMAT(format);
    return _NtCRT_PrintP(format, ap, _NtCRT__vcprintf);
}

EXTERN_C int __cdecl _NtCRT__cprintf_p(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vcprintf_p(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vcwprintf_p(const wchar_t* format, va_list ap)
{
    NTCRT_CHECK_FORMAT(format);
    return _NtCRT_PrintP(format, ap, _NtCRT__vcwprintf);
}

EXTERN_C int __cdecl _NtCRT__cwprintf_p(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vcwprintf_p(format, ap);
    va_end(ap);
    return Ret;
}

#pragma endregion

NTCRT_IMPORT(vfprintf_s, _NtCRT_vfprintf_s);
NTCRT_IMPORT(fprintf_s, _NtCRT_fprintf_s);
NTCRT_IMPORT(vprintf_s, _NtCRT_vprintf_s);
NTCRT_IMPORT(printf_s, _NtCRT_printf_s);
NTCRT_IMPORT(vfwprintf_s, _NtCRT_vfwprintf_s);
NTCRT_IMPORT(fwprintf_s, _NtCRT_fwprintf_s);
NTCRT_IMPORT(vwprintf_s, _NtCRT_vwprintf_s);
NTCRT_IMPORT(wprintf_s, _NtCRT_wprintf_s);
NTCRT_IMPORT(_vcprintf_s, _NtCRT__vcprintf_s);
NTCRT_IMPORT(_cprintf_s, _NtCRT__cprintf_s);
NTCRT_IMPORT(_vcwprintf_s, _NtCRT__vcwprintf_s);
NTCRT_IMPORT(_cwprintf_s, _NtCRT__cwprintf_s);
NTCRT_IMPORT(_vsnprintf_s, _NtCRT__vsnprintf_s);
NTCRT_IMPORT(vsnprintf_s, _NtCRT__vsnprintf_s);
NTCRT_IMPORT(_snprintf_s, _NtCRT__snprintf_s);
NTCRT_IMPORT(_vsnwprintf_s, _NtCRT__vsnwprintf_s);
NTCRT_IMPORT(_snwprintf_s, _NtCRT__snwprintf_s);
NTCRT_IMPORT(vsprintf_s, _NtCRT_vsprintf_s);
NTCRT_IMPORT(sprintf_s, _NtCRT_sprintf_s);
NTCRT_IMPORT(vswprintf_s, _NtCRT_vswprintf_s);
NTCRT_IMPORT(swprintf_s, _NtCRT_swprintf_s);
NTCRT_IMPORT(_vsnprintf_c, _NtCRT__vsnprintf_c);
NTCRT_IMPORT(_snprintf_c, _NtCRT__snprintf_c);
NTCRT_IMPORT(_vswprintf_c, _NtCRT__vswprintf_c);
NTCRT_IMPORT(_swprintf_c, _NtCRT__swprintf_c);
NTCRT_IMPORT(_vfprintf_p, _NtCRT__vfprintf_p);
NTCRT_IMPORT(_fprintf_p, _NtCRT__fprintf_p);
NTCRT_IMPORT(_vprintf_p, _NtCRT__vprintf_p);
NTCRT_IMPORT(_printf_p, _NtCRT__printf_p);
NTCRT_IMPORT(_vfwprintf_p, _NtCRT__vfwprintf_p);
NTCRT_IMPORT(_fwprintf_p, _NtCRT__fwprintf_p);
NTCRT_IMPORT(_vwprintf_p, _NtCRT__vwprintf_p);
NTCRT_IMPORT(_wprintf_p, _NtCRT__wprintf_p);
NTCRT_IMPORT(_vsprintf_p, _NtCRT__vsprintf_p);
NTCRT_IMPORT(_sprintf_p, _NtCRT__sprintf_p);
NTCRT_IMPORT(_vswprintf_p, _NtCRT__vswprintf_p);
NTCRT_IMPORT(_swprintf_p, _NtCRT__swprintf_p);
NTCRT_IMPORT(_vscprintf_p, _NtCRT__vscprintf_p);
NTCRT_IMPORT(_scprintf_p, _NtCRT__scprintf_p);
NTCRT_IMPORT(_vscwprintf_p, _NtCRT__vscwprintf_p);
NTCRT_IMPORT(_scwprintf_p, _NtCRT__scwprintf_p);
NTCRT_IMPORT(_vcprintf_p, _NtCRT__vcprintf_p);
NTCRT_IMPORT(_cprintf_p, _NtCRT__cprintf_p);
NTCRT_IMPORT(_vcwprintf_p, _NtCRT__vcwprintf_p);
NTCRT_IMPORT(_cwprintf_p, _NtCRT__cwprintf_p);
