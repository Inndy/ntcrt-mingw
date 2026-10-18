/*
 * msvcrt formatted I/O variants: "_s" (no %n, sizes of string arguments of scanf), "_c" (count), "_p" (positional
 * parameters) and "_snscanf" (limited input)
 *
 * On x64 every variadic argument takes an 8-byte slot (larger ones are passed by reference), so a va_list is a
 * pointer to slots. Positional parameters and scanf_s are implemented by rebuilding the format and the slots.
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
EXTERN_C int __cdecl _NtCRT__vsnprintf(char* buffer, size_t count, const char* format, va_list ap);
EXTERN_C int __cdecl _NtCRT__vsnwprintf(wchar_t* buffer, size_t count, const wchar_t* format, va_list ap);
EXTERN_C int __cdecl _NtCRT__vscprintf(const char* format, va_list ap);
EXTERN_C int __cdecl _NtCRT__vscwprintf(const wchar_t* format, va_list ap);
EXTERN_C int __cdecl _NtCRT__vcprintf(const char* format, va_list ap);
EXTERN_C int __cdecl _NtCRT__vcwprintf(const wchar_t* format, va_list ap);
EXTERN_C int __cdecl _NtCRT_vfscanf(FILE* stream, const char* format, va_list ap);
EXTERN_C int __cdecl _NtCRT_vsscanf(const char* str, const char* format, va_list ap);
EXTERN_C int __cdecl _NtCRT_vfwscanf(FILE* stream, const wchar_t* format, va_list ap);
EXTERN_C int __cdecl _NtCRT_vswscanf(const wchar_t* str, const wchar_t* format, va_list ap);
EXTERN_C BOOL _NtCRT_ConsoleScanLineA(char* Line, DWORD Size);
EXTERN_C BOOL _NtCRT_ConsoleScanLineW(PWSTR Line, DWORD Count);

#define NTCRT_ARGMAX 100 /* _ARGMAX */
#define NTCRT_SCAN_LINE 1024

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
    ULONG_PTR Args[NTCRT_ARGMAX * 3];
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
    NewFormat = (TChar*)RtlAllocateHeap(NTCRT_HEAP, 0, (Length + 1) * sizeof(TChar));
    if (NewFormat == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return -1;
    }
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
    RtlFreeHeap(NTCRT_HEAP, 0, NewFormat);
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

static int __cdecl _NtCRT_vsnprintfT(char* b, size_t n, const char* f, va_list a)
{
    return _NtCRT__vsnprintf(b, n, f, a);
}
static int __cdecl _NtCRT_vsnprintfT(wchar_t* b, size_t n, const wchar_t* f, va_list a)
{
    return _NtCRT__vsnwprintf(b, n, f, a);
}

/*
 * _vsnprintf_s: at most Count characters, the whole buffer or _TRUNCATE. Exceeding Count or _TRUNCATE truncates
 * and returns -1, exceeding the buffer otherwise empties it. msvcrt sets errno to ERANGE on truncation in the
 * narrow functions only.
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
        if (Ret < 0 || (size_t)Ret > Count)
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
    if (Ret < 0 || (size_t)Ret >= Size)
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
    if (Ret < 0 || (size_t)Ret >= Count)
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

    if (Ret < 0 || (size_t)Ret >= Size)
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

#pragma region "_s" scanf

/*
 * Scan a "_s" format by Scan(Format, ap, Consumed), where Consumed is the count of characters scanned so far.
 * Each conversion is scanned on its own followed by %n, so a string that does not fit its buffer fails without
 * overflowing, like msvcrt it is emptied, errno is ENOMEM and scanning stops. Wide is true for the format of the
 * wide functions, where %s and %c are wide.
 */
template<typename TChar, typename TScan>
static
int
_NtCRT_ScanS(
    _In_z_ const TChar* Format,
    _In_ va_list ap,
    _In_ bool Wide,
    _In_ TScan Scan)
{
    size_t Length = _NtCRT_TLen(Format);
    TChar* Piece;
    size_t PieceStart = 0;
    size_t Consumed = 0;
    int Total = 0;
    int Ret = 0;

    Piece = (TChar*)RtlAllocateHeap(NTCRT_HEAP, 0, (Length + 32) * sizeof(TChar));
    if (Piece == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return EOF;
    }
    for (size_t i = 0; ; i++)
    {
        ULONG_PTR Slots[2];
        size_t SlotCount = 0;
        TChar* o;
        int Count = -1;
        bool Suppress = false;
        bool WideArg;
        unsigned int Width = 0;
        size_t ModStart;
        size_t ModEnd;
        size_t SetEnd;
        unsigned int Conversion;
        void* Target = NULL;
        void* Temp = NULL;
        unsigned int Size = 0;
        int Result;

        if (Format[i] == '%' && Format[i + 1] == '%')
        {
            i++;
            continue;
        }
        if (Format[i] != '%' && Format[i] != 0)
        {
            continue;
        }

        /* Literal text before the conversion, then the conversion */
        o = Piece;
        memcpy(o, Format + PieceStart, (i - PieceStart) * sizeof(TChar));
        o += i - PieceStart;
        if (Format[i] == 0)
        {
            /* Match the trailing text */
            if (i > PieceStart)
            {
                *o++ = '%';
                *o++ = 'n';
                *o = 0;
                Slots[0] = (ULONG_PTR)&Count;
                Scan(Piece, _NtCRT_SlotList(Slots), Consumed);
            }
            Ret = Total;
            break;
        }
        i++;
        if (Format[i] == '*')
        {
            Suppress = true;
            i++;
        }
        while (_NtCRT_IsDigit(Format[i]))
        {
            Width = Width * 10 + (Format[i++] - '0');
        }
        ModStart = i;
        while (Format[i] == 'h' || Format[i] == 'l' || Format[i] == 'L' || Format[i] == 'w' || Format[i] == 'j' ||
               Format[i] == 'z' || Format[i] == 't' || Format[i] == 'q' || Format[i] == 'I' ||
               (Format[i] == '3' && Format[i - 1] == 'I') || (Format[i] == '6' && Format[i - 1] == 'I') ||
               (Format[i] == '2' && Format[i - 1] == '3') || (Format[i] == '4' && Format[i - 1] == '6'))
        {
            i++;
        }
        ModEnd = i;
        Conversion = (unsigned int)Format[i];
        if (Conversion == 0)
        {
            Ret = Total;
            break;
        }
        SetEnd = i + 1;
        if (Conversion == '[')
        {
            if (Format[SetEnd] == '^')
            {
                SetEnd++;
            }
            if (Format[SetEnd] == ']')
            {
                SetEnd++;
            }
            while (Format[SetEnd] != 0 && Format[SetEnd] != ']')
            {
                SetEnd++;
            }
            if (Format[SetEnd] == ']')
            {
                SetEnd++;
            }
        }

        /* Wide argument: "l" or "w" for narrow conversions, no "h" for wide ones */
        {
            bool Long = false, Short = false;

            for (size_t j = ModStart; j < ModEnd; j++)
            {
                Long |= Format[j] == 'l' || Format[j] == 'w';
                Short |= Format[j] == 'h';
            }
            if (Conversion == 'S' || Conversion == 'C')
            {
                WideArg = Wide ? Long : !Short;
            } else
            {
                WideArg = Wide ? !Short : Long;
            }
        }

        if (!Suppress)
        {
            Target = va_arg(ap, void*);
        }
        if (!Suppress && (Conversion == 'c' || Conversion == 'C' || Conversion == 's' || Conversion == 'S' ||
                          Conversion == '['))
        {
            size_t Element = WideArg ? sizeof(wchar_t) : sizeof(char);

            Size = va_arg(ap, unsigned int);
            if (Conversion == 'c' || Conversion == 'C')
            {
                if (Width == 0)
                {
                    Width = 1;
                }
                if (Width > Size)
                {
                    goto _NoMemory;
                }
            } else if (Width == 0 || Width >= Size)
            {
                if (Size == 0)
                {
                    goto _NoMemory;
                }
                /* One more character than fits tells an overflow */
                Width = Size;
                Temp = RtlAllocateHeap(NTCRT_HEAP, 0, ((size_t)Size + 1) * Element);
                if (Temp == NULL)
                {
                    goto _NoMemory;
                }
            }
        }

        *o++ = '%';
        if (Suppress)
        {
            *o++ = '*';
        }
        if (Width != 0)
        {
            TChar Digits[16];
            int n = 0;

            for (unsigned int w = Width; w != 0; w /= 10)
            {
                Digits[n++] = (TChar)('0' + w % 10);
            }
            while (n > 0)
            {
                *o++ = Digits[--n];
            }
        }
        memcpy(o, Format + ModStart, (SetEnd - ModStart) * sizeof(TChar));
        o += SetEnd - ModStart;
        *o++ = '%';
        *o++ = 'n';
        *o = 0;

        if (!Suppress)
        {
            Slots[SlotCount++] = (ULONG_PTR)(Temp != NULL ? Temp : Target);
        }
        Slots[SlotCount++] = (ULONG_PTR)&Count;
        Result = Scan(Piece, _NtCRT_SlotList(Slots), Consumed);
        if (Result == EOF || Count < 0)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, Temp);
            Ret = (Result == EOF && Total == 0) ? EOF : Total;
            break;
        }
        if (Temp != NULL)
        {
            size_t TempLength = WideArg ? wcslen((wchar_t*)Temp) : strlen((char*)Temp);

            if (TempLength >= Size)
            {
                RtlFreeHeap(NTCRT_HEAP, 0, Temp);
                goto _NoMemory;
            }
            memcpy(Target, Temp, (TempLength + 1) * (WideArg ? sizeof(wchar_t) : sizeof(char)));
            RtlFreeHeap(NTCRT_HEAP, 0, Temp);
        }
        if (Conversion == 'n' && !Suppress)
        {
            /* Count from the start of the input, not of this piece */
            size_t Mods = ModEnd - ModStart;

            if (Mods == 2 && Format[ModStart] == 'h' && Format[ModStart + 1] == 'h')
            {
                *(signed char*)Target += (signed char)Consumed;
            } else if (Mods == 1 && Format[ModStart] == 'h')
            {
                *(short*)Target += (short)Consumed;
            } else if ((Mods == 2 && Format[ModStart] == 'l' && Format[ModStart + 1] == 'l') ||
                       (Mods == 3 && Format[ModStart] == 'I'))
            {
                *(long long*)Target += (long long)Consumed;
            } else
            {
                *(int*)Target += (int)Consumed;
            }
        }
        Total += Result;
        Consumed += Count;
        PieceStart = SetEnd;
        i = SetEnd - 1;
        continue;

_NoMemory:
        if (Target != NULL && Size != 0)
        {
            if (WideArg)
            {
                *(wchar_t*)Target = 0;
            } else
            {
                *(char*)Target = 0;
            }
        }
        NTCRT_ERRNO = ENOMEM;
        Ret = Total;
        break;
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Piece);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vfscanf_s(FILE* stream, const char* format, va_list ap)
{
    if (stream == NULL || format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), EOF;
    }
    return _NtCRT_ScanS(format, ap, false, [stream](const char* f, va_list a, size_t) {
        return _NtCRT_vfscanf(stream, f, a);
    });
}

EXTERN_C int __cdecl _NtCRT_fscanf_s(FILE* stream, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfscanf_s(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vscanf_s(const char* format, va_list ap)
{
    return _NtCRT_vfscanf_s(&_NtCRT_iob[0], format, ap);
}

EXTERN_C int __cdecl _NtCRT_scanf_s(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfscanf_s(&_NtCRT_iob[0], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vsscanf_s(const char* buffer, const char* format, va_list ap)
{
    if (buffer == NULL || format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), EOF;
    }
    return _NtCRT_ScanS(format, ap, false, [buffer](const char* f, va_list a, size_t Consumed) {
        return _NtCRT_vsscanf(buffer + Consumed, f, a);
    });
}

EXTERN_C int __cdecl _NtCRT_sscanf_s(const char* buffer, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vsscanf_s(buffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vfwscanf_s(FILE* stream, const wchar_t* format, va_list ap)
{
    if (stream == NULL || format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), EOF;
    }
    return _NtCRT_ScanS(format, ap, true, [stream](const wchar_t* f, va_list a, size_t) {
        return _NtCRT_vfwscanf(stream, f, a);
    });
}

EXTERN_C int __cdecl _NtCRT_fwscanf_s(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwscanf_s(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vwscanf_s(const wchar_t* format, va_list ap)
{
    return _NtCRT_vfwscanf_s(&_NtCRT_iob[0], format, ap);
}

EXTERN_C int __cdecl _NtCRT_wscanf_s(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwscanf_s(&_NtCRT_iob[0], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vswscanf_s(const wchar_t* buffer, const wchar_t* format, va_list ap)
{
    if (buffer == NULL || format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), EOF;
    }
    return _NtCRT_ScanS(format, ap, true, [buffer](const wchar_t* f, va_list a, size_t Consumed) {
        return _NtCRT_vswscanf(buffer + Consumed, f, a);
    });
}

EXTERN_C int __cdecl _NtCRT_swscanf_s(const wchar_t* buffer, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vswscanf_s(buffer, format, ap);
    va_end(ap);
    return Ret;
}

/* msvcrt scans the characters as they are typed, a line is read and scanned here instead */
EXTERN_C int __cdecl _NtCRT__cscanf_s(const char* format, ...)
{
    char Line[NTCRT_SCAN_LINE];
    va_list ap;
    int Ret;

    if (format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), EOF;
    }
    if (!_NtCRT_ConsoleScanLineA(Line, sizeof(Line)))
    {
        return EOF;
    }
    va_start(ap, format);
    Ret = _NtCRT_vsscanf_s(Line, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__cwscanf_s(const wchar_t* format, ...)
{
    WCHAR Line[NTCRT_SCAN_LINE / 2];
    va_list ap;
    int Ret;

    if (format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), EOF;
    }
    if (!_NtCRT_ConsoleScanLineW(Line, ARRAYSIZE(Line)))
    {
        return EOF;
    }
    va_start(ap, format);
    Ret = _NtCRT_vswscanf_s(Line, format, ap);
    va_end(ap);
    return Ret;
}

#pragma endregion

#pragma region Limited input

/* Copy at most Count characters of Input, free by RtlFreeHeap */
template<typename TChar>
static
TChar*
_NtCRT_LimitInput(
    _In_reads_or_z_(Count) const TChar* Input,
    _In_ size_t Count)
{
    size_t Length = 0;

    while (Length < Count && Input[Length] != 0)
    {
        Length++;
    }
    return _NtCRT_TDup(Input, Length);
}

template<typename TChar>
static
int
_NtCRT_SnScan(
    _In_reads_or_z_(Length) const TChar* Input,
    _In_ size_t Length,
    _In_z_ const TChar* Format,
    _In_ int(__cdecl* VScan)(const TChar*, const TChar*, va_list),
    _In_ va_list ap)
{
    TChar* Copy;
    int Ret;

    if (Input == NULL || Format == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL), EOF;
    }
    Copy = _NtCRT_LimitInput(Input, Length);
    if (Copy == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return EOF;
    }
    Ret = VScan(Copy, Format, ap);
    RtlFreeHeap(NTCRT_HEAP, 0, Copy);
    return Ret;
}

/* The locale is ignored as the other scanning functions */
#define NTCRT_COMMA ,
#define NTCRT_SNSCANF(Name, TChar, VScan, Params, Last) \
    EXTERN_C int __cdecl Name(const TChar* input, size_t length, const TChar* format Params, ...) \
    { \
        va_list ap; \
        int Ret; \
        va_start(ap, Last); \
        Ret = _NtCRT_SnScan(input, length, format, VScan, ap); \
        va_end(ap); \
        return Ret; \
    }

NTCRT_SNSCANF(_NtCRT__snscanf, char, _NtCRT_vsscanf, , format)
NTCRT_SNSCANF(_NtCRT__snscanf_s, char, _NtCRT_vsscanf_s, , format)
NTCRT_SNSCANF(_NtCRT__snwscanf, wchar_t, _NtCRT_vswscanf, , format)
NTCRT_SNSCANF(_NtCRT__snwscanf_s, wchar_t, _NtCRT_vswscanf_s, , format)
NTCRT_SNSCANF(_NtCRT__snscanf_l, char, _NtCRT_vsscanf, NTCRT_COMMA _locale_t locale, locale)
NTCRT_SNSCANF(_NtCRT__snscanf_s_l, char, _NtCRT_vsscanf_s, NTCRT_COMMA _locale_t locale, locale)
NTCRT_SNSCANF(_NtCRT__snwscanf_l, wchar_t, _NtCRT_vswscanf, NTCRT_COMMA _locale_t locale, locale)
NTCRT_SNSCANF(_NtCRT__snwscanf_s_l, wchar_t, _NtCRT_vswscanf_s, NTCRT_COMMA _locale_t locale, locale)

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
NTCRT_IMPORT(vfscanf_s, _NtCRT_vfscanf_s);
NTCRT_IMPORT(fscanf_s, _NtCRT_fscanf_s);
NTCRT_IMPORT(vscanf_s, _NtCRT_vscanf_s);
NTCRT_IMPORT(scanf_s, _NtCRT_scanf_s);
NTCRT_IMPORT(vsscanf_s, _NtCRT_vsscanf_s);
NTCRT_IMPORT(sscanf_s, _NtCRT_sscanf_s);
NTCRT_IMPORT(vfwscanf_s, _NtCRT_vfwscanf_s);
NTCRT_IMPORT(_winput_s, _NtCRT_vfwscanf_s);
NTCRT_IMPORT(fwscanf_s, _NtCRT_fwscanf_s);
NTCRT_IMPORT(vwscanf_s, _NtCRT_vwscanf_s);
NTCRT_IMPORT(wscanf_s, _NtCRT_wscanf_s);
NTCRT_IMPORT(vswscanf_s, _NtCRT_vswscanf_s);
NTCRT_IMPORT(swscanf_s, _NtCRT_swscanf_s);
NTCRT_IMPORT(_cscanf_s, _NtCRT__cscanf_s);
NTCRT_IMPORT(_cwscanf_s, _NtCRT__cwscanf_s);
NTCRT_IMPORT(_snscanf, _NtCRT__snscanf);
NTCRT_IMPORT(_snscanf_s, _NtCRT__snscanf_s);
NTCRT_IMPORT(_snwscanf, _NtCRT__snwscanf);
NTCRT_IMPORT(_snwscanf_s, _NtCRT__snwscanf_s);
NTCRT_IMPORT(_snscanf_l, _NtCRT__snscanf_l);
NTCRT_IMPORT(_snscanf_s_l, _NtCRT__snscanf_s_l);
NTCRT_IMPORT(_snwscanf_l, _NtCRT__snwscanf_l);
NTCRT_IMPORT(_snwscanf_s_l, _NtCRT__snwscanf_s_l);
