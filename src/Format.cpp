/*
 * Conversions between narrow and wide characters of printf, see NTCRT_FORMAT_ARGS in NtCRT.h. The printf
 * functions of mingw-w64 are wrapped by the linker (--wrap in ntcrt.specs), so these from the headers of
 * mingw-w64 and C++ are converted as well as those of msvcrt implemented by Stdio.cpp.
 *
 * The printf of mingw-w64 converts %ls by wcrtomb a wchar_t at a time: the precision counts wchar_t instead of
 * bytes, a surrogate pair or an encoding error ends the output silently. Its wide printf converts %s by mbrtowc
 * with a new state for every character and takes an invalid byte as a character. The arguments of these
 * conversions are converted here as C requires, and the format passes them as strings of the other width.
 *
 * The printf of mingw-w64 also prints a space for the space flag of %u, which C defines for signed conversions
 * only, pads with zeros when the 0 flag comes with the - flag or a negative width (%a) or with the precision of
 * an integer (%#o), which C ignores then, and
 * prints nothing for %#.0o of 0, where the # flag requires a 0. These flags are removed from the format, and the
 * precision of such a %#o. Its %g loses the width when it removes many trailing zeros (%20.17g of 1e13), a %g is
 * changed to the %e or %f it selects with the precision left after the zeros.
 */

#include "NtCRT.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* An argument takes a slot of 8 bytes in the va_list of x64, a long double is passed by address */
static_assert(sizeof(va_list) == sizeof(char*), "va_list is a pointer to the arguments");

typedef struct _NTCRT_FORMAT_SPEC
{
    size_t Start;     /* Offset of '%' */
    size_t FlagsPos;  /* Offset of the flags */
    size_t WidthPos;  /* Offset after the flags */
    size_t PrecisionPos; /* Offset of '.', of the length modifier if none */
    size_t LengthPos; /* Offset of the length modifier */
    size_t ConvPos;   /* Offset of the conversion */
    int ValueSlot;
    int WidthSlot;     /* -1 if not '*' */
    int PrecisionSlot; /* -1 if not '*' */
    int Precision;     /* -1 if none, or from PrecisionSlot */
    BOOL Long;
    BOOL Short;
} NTCRT_FORMAT_SPEC;

static
int
_NtCRT_Max(
    _In_ int a,
    _In_ int b)
{
    return a > b ? a : b;
}

template<typename TChar>
static
BOOL
_NtCRT_IsDigit(
    _In_ TChar c)
{
    return c >= '0' && c <= '9';
}

/* Parse "n$", returns n - 1 or -1 */
template<typename TChar>
static
int
_NtCRT_ParsePosition(
    _Inout_ const TChar** p)
{
    const TChar* q = *p;
    int n = 0;

    while (_NtCRT_IsDigit(*q))
    {
        n = n * 10 + (*q++ - '0');
    }
    if (q == *p || *q != '$' || n == 0)
    {
        return -1;
    }
    *p = q + 1;
    return n - 1;
}

/* Parse the specification after '%', FALSE if not understood */
template<typename TChar>
static
BOOL
_NtCRT_ParseSpec(
    _In_z_ const TChar* Format,
    _Inout_ const TChar** Cursor,
    _Inout_ int* NextSlot,
    _Out_ NTCRT_FORMAT_SPEC* Spec)
{
    const TChar* p = *Cursor;
    int Position = _NtCRT_ParsePosition(&p);

    Spec->Start = (size_t)(*Cursor - Format) - 1;
    Spec->WidthSlot = -1;
    Spec->PrecisionSlot = -1;
    Spec->Precision = -1;
    Spec->Long = Spec->Short = FALSE;
    Spec->FlagsPos = (size_t)(p - Format);
    while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0' || *p == '\'')
    {
        p++;
    }
    Spec->WidthPos = (size_t)(p - Format);
    if (*p == '*')
    {
        p++;
        Spec->WidthSlot = _NtCRT_ParsePosition(&p);
        if (Spec->WidthSlot < 0)
        {
            Spec->WidthSlot = (*NextSlot)++;
        }
    } else
    {
        while (_NtCRT_IsDigit(*p))
        {
            p++;
        }
    }
    Spec->PrecisionPos = (size_t)(p - Format);
    if (*p == '.')
    {
        p++;
        if (*p == '*')
        {
            p++;
            Spec->PrecisionSlot = _NtCRT_ParsePosition(&p);
            if (Spec->PrecisionSlot < 0)
            {
                Spec->PrecisionSlot = (*NextSlot)++;
            }
        } else
        {
            Spec->Precision = 0;
            while (_NtCRT_IsDigit(*p))
            {
                Spec->Precision = Spec->Precision < INT_MAX / 10 ? Spec->Precision * 10 + (*p - '0') : INT_MAX;
                p++;
            }
        }
    }
    Spec->LengthPos = (size_t)(p - Format);
    for (;;)
    {
        if (*p == 'h')
        {
            Spec->Short = TRUE;
            p++;
        } else if (*p == 'l' || *p == 'w')
        {
            Spec->Long = TRUE;
            p++;
        } else if (*p == 'I')
        {
            p++;
            if ((p[0] == '6' && p[1] == '4') || (p[0] == '3' && p[1] == '2'))
            {
                p += 2;
            }
        } else if (*p == 'L' || *p == 'q' || *p == 'j' || *p == 'z' || *p == 't')
        {
            p++;
        } else
        {
            break;
        }
    }
    Spec->ConvPos = (size_t)(p - Format);
    if (*p == '\0' || (*p < 0x80 && strchr("diouxXeEfFgGaAcspnCS", (char)*p) == NULL))
    {
        return FALSE;
    }
    Spec->ValueSlot = Position >= 0 ? Position : (*NextSlot)++;
    *Cursor = p + 1;
    return TRUE;
}

/* Whether the argument of a conversion is a string or character of the other width than the format */
template<typename TChar>
static
BOOL
_NtCRT_IsOtherWidth(
    _In_ const NTCRT_FORMAT_SPEC* Spec,
    _In_ TChar Conv)
{
    if (sizeof(TChar) == sizeof(char))
    {
        return ((Conv == 's' || Conv == 'c') && Spec->Long) || ((Conv == 'S' || Conv == 'C') && !Spec->Short);
    }
    /* The wide format is translated to C99, where %s and %c are narrow without l */
    return (Conv == 's' || Conv == 'c') && !Spec->Long;
}

template<typename TChar>
static
BOOL
_NtCRT_HasFlag(
    _In_z_ const TChar* Format,
    _In_ const NTCRT_FORMAT_SPEC* Spec,
    _In_ TChar Flag)
{
    for (size_t i = Spec->FlagsPos; i < Spec->WidthPos; i++)
    {
        if (Format[i] == Flag)
        {
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * Whether a flag has no effect: a sign flag of %u, the 0 flag with the - flag, a negative width or an integer
 * conversion with a precision
 */
template<typename TChar>
static
BOOL
_NtCRT_IsIgnoredFlag(
    _In_z_ const TChar* Format,
    _In_ const NTCRT_FORMAT_SPEC* Spec,
    _In_ const ULONG64* Slots,
    _In_ TChar Flag)
{
    if (Flag == ' ' || Flag == '+')
    {
        return Format[Spec->ConvPos] == 'u';
    }
    if (Flag == '0')
    {
        TChar Conv = Format[Spec->ConvPos];
        BOOL Integer = Conv == 'd' || Conv == 'i' || Conv == 'o' || Conv == 'u' || Conv == 'x' || Conv == 'X';
        int Precision = Spec->PrecisionSlot >= 0 ? (int)Slots[Spec->PrecisionSlot] : Spec->Precision;

        return _NtCRT_HasFlag(Format, Spec, (TChar)'-') || (Spec->WidthSlot >= 0 && (int)Slots[Spec->WidthSlot] < 0) ||
               (Integer && Precision >= 0);
    }
    return FALSE;
}

/* Whether a %#o with a precision of 0 converts 0 */
template<typename TChar>
static
BOOL
_NtCRT_IsZeroOctal(
    _In_z_ const TChar* Format,
    _In_ const NTCRT_FORMAT_SPEC* Spec,
    _In_ const ULONG64* Slots)
{
    const TChar* Length = Format + Spec->LengthPos;
    ULONG64 Value = Slots[Spec->ValueSlot];
    int Precision = Spec->PrecisionSlot >= 0 ? (int)Slots[Spec->PrecisionSlot] : Spec->Precision;

    if (Format[Spec->ConvPos] != 'o' || Precision != 0 || !_NtCRT_HasFlag(Format, Spec, (TChar)'#'))
    {
        return FALSE;
    }
    if (Length[0] == 'h')
    {
        Value = Length[1] == 'h' ? (UCHAR)Value : (USHORT)Value;
    } else if (Spec->ConvPos == Spec->LengthPos || (Length[0] == 'l' && Length[1] != 'l') ||
               (Length[0] == 'I' && Length[1] == '3'))
    {
        Value = (ULONG)Value;
    }
    return Value == 0;
}

/*
 * The %e or %f a %g of a finite double without the # flag selects, and its precision without the trailing zeros:
 * with P significant digits and the exponent X of %e, %f if P > X >= -4 with P - 1 - X digits after the point.
 */
template<typename TChar>
static
BOOL
_NtCRT_GeneralFloat(
    _In_z_ const TChar* Format,
    _In_ const NTCRT_FORMAT_SPEC* Spec,
    _In_ const ULONG64* Slots,
    _Out_opt_ char* Conv,
    _Out_opt_ int* Precision)
{
    TChar c = Format[Spec->ConvPos];
    int P = Spec->PrecisionSlot >= 0 ? (int)Slots[Spec->PrecisionSlot] : Spec->Precision;
    int Zeros, Exponent, Fraction;
    double Value;

    if ((c != 'g' && c != 'G') || (Spec->LengthPos != Spec->ConvPos && Format[Spec->LengthPos] == 'L') ||
        _NtCRT_HasFlag(Format, Spec, (TChar)'#'))
    {
        return FALSE;
    }
    memcpy(&Value, &Slots[Spec->ValueSlot], sizeof(Value));
    P = P < 0 ? 6 : P == 0 ? 1 : P;
    if (!isfinite(Value))
    {
        return FALSE;
    }
    if (Conv == NULL)
    {
        return TRUE;
    }
    Exponent = _NtCRT_DecimalExponent(Value, P, &Zeros);
    if (P > Exponent && Exponent >= -4)
    {
        *Conv = c == 'g' ? 'f' : 'F';
        Fraction = P - 1 - Exponent;
    } else
    {
        *Conv = c == 'g' ? 'e' : 'E';
        Fraction = P - 1;
    }
    *Precision = Fraction - (Zeros < Fraction ? Zeros : Fraction);
    return TRUE;
}

template<typename TChar>
static
BOOL
_NtCRT_NeedsFix(
    _In_z_ const TChar* Format,
    _In_ const NTCRT_FORMAT_SPEC* Spec,
    _In_ const ULONG64* Slots)
{
    for (size_t i = Spec->FlagsPos; i < Spec->WidthPos; i++)
    {
        if (_NtCRT_IsIgnoredFlag(Format, Spec, Slots, Format[i]))
        {
            return TRUE;
        }
    }
    return _NtCRT_IsZeroOctal(Format, Spec, Slots) || _NtCRT_GeneralFloat(Format, Spec, Slots, NULL, NULL);
}

/* Convert a wide string to multibyte characters, at most Precision bytes if not negative */
static
char*
_NtCRT_FormatNarrow(
    _In_z_ const wchar_t* Str,
    _In_ int Precision)
{
    size_t Size = 64, Length = 0;
    char* Out = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, Size);
    mbstate_t State = 0;

    for (; Out != NULL; Str++)
    {
        char Buffer[MB_LEN_MAX];
        size_t n = wcrtomb(Buffer, *Str, &State);

        if (n == (size_t)-1)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, Out);
            return NULL;
        }
        if (*Str == L'\0' || (Precision >= 0 && Length + n > (size_t)Precision))
        {
            Out[Length] = '\0';
            return Out;
        }
        if (Length + n + 1 > Size)
        {
            char* New = (char*)RtlReAllocateHeap(NTCRT_HEAP, 0, Out, Size * 2);

            if (New == NULL)
            {
                RtlFreeHeap(NTCRT_HEAP, 0, Out);
                NTCRT_ERRNO = ENOMEM;
                return NULL;
            }
            Out = New;
            Size *= 2;
        }
        memcpy(Out + Length, Buffer, n);
        Length += n;
    }
    NTCRT_ERRNO = ENOMEM;
    return NULL;
}

/* Convert a multibyte string to wide characters, at most Precision wchar_t if not negative */
static
wchar_t*
_NtCRT_FormatWide(
    _In_z_ const char* Str,
    _In_ int Precision)
{
    size_t Size = 64, Length = 0;
    wchar_t* Out = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, Size * sizeof(wchar_t));
    mbstate_t State = 0;

    while (Out != NULL)
    {
        wchar_t Char;
        size_t n = mbrtowc(&Char, Str, MB_LEN_MAX, &State);

        if (n == (size_t)-1 || n == (size_t)-2)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, Out);
            NTCRT_ERRNO = EILSEQ;
            return NULL;
        }
        /* A surrogate pair is not split by the precision */
        if (n == 0 ||
            (Precision >= 0 && Length + (Char >= 0xD800 && Char <= 0xDBFF ? 2 : 1) > (size_t)Precision))
        {
            Out[Length] = L'\0';
            return Out;
        }
        if (Length + 2 > Size)
        {
            wchar_t* New = (wchar_t*)RtlReAllocateHeap(NTCRT_HEAP, 0, Out, Size * 2 * sizeof(wchar_t));

            if (New == NULL)
            {
                RtlFreeHeap(NTCRT_HEAP, 0, Out);
                NTCRT_ERRNO = ENOMEM;
                return NULL;
            }
            Out = New;
            Size *= 2;
        }
        Out[Length++] = Char;
        Str += n;
    }
    NTCRT_ERRNO = ENOMEM;
    return NULL;
}

template<typename TChar>
static
void
_NtCRT_PrepareFormat(
    _Inout_ NTCRT_FORMAT_ARGS* Args,
    _In_z_ const TChar* Format,
    _In_ va_list ap)
{
    const TChar* p;
    NTCRT_FORMAT_SPEC Spec;
    int NextSlot = 0, Slots = 0, Converts = 0, Flags = 0;
    size_t FormatLength;
    ULONG64* Src = (ULONG64*)ap;
    TChar *Out, *o;
    size_t Last = 0;

    /* Count the slots and the conversions to do, keep everything if nothing to do or not understood */
    for (p = Format; *p != '\0';)
    {
        if (*p++ != '%')
        {
            continue;
        }
        if (*p == '%')
        {
            p++;
            continue;
        }
        if (!_NtCRT_ParseSpec(Format, &p, &NextSlot, &Spec))
        {
            return;
        }
        Slots = _NtCRT_Max(Slots, NextSlot);
        Slots = _NtCRT_Max(Slots, _NtCRT_Max(Spec.ValueSlot, _NtCRT_Max(Spec.WidthSlot, Spec.PrecisionSlot)) + 1);
        Converts += _NtCRT_IsOtherWidth(&Spec, Format[Spec.ConvPos]);
        Flags += _NtCRT_NeedsFix(Format, &Spec, Src);
    }
    if (Converts == 0 && Flags == 0)
    {
        return;
    }

    FormatLength = (size_t)(p - Format);
    Args->m_Slots = (ULONG64*)RtlAllocateHeap(NTCRT_HEAP, 0, (size_t)Slots * sizeof(ULONG64));
    Args->m_Strings = (PVOID*)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, ((size_t)Converts + 1) * sizeof(PVOID));
    /* A conversion of the other width adds 'l', a %g changed to %e or %f a precision */
    Out = (TChar*)RtlAllocateHeap(NTCRT_HEAP, 0, (FormatLength + (size_t)Converts + (size_t)Flags * 12 + 1) * sizeof(TChar));
    Args->m_Format = Out;
    if (Args->m_Slots == NULL || Args->m_Strings == NULL || Out == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        Args->Failed = TRUE;
        return;
    }
    memcpy(Args->m_Slots, Src, (size_t)Slots * sizeof(ULONG64));

    /* Convert the arguments and change the conversions to the width of the format */
    NextSlot = 0;
    o = Out;
    for (p = Format; *p != '\0';)
    {
        TChar Conv;
        PVOID Converted;

        if (*p++ != '%')
        {
            continue;
        }
        if (*p == '%')
        {
            p++;
            continue;
        }
        _NtCRT_ParseSpec(Format, &p, &NextSlot, &Spec);
        Conv = Format[Spec.ConvPos];
        if (_NtCRT_NeedsFix(Format, &Spec, Args->m_Slots))
        {
            BOOL ZeroOctal = _NtCRT_IsZeroOctal(Format, &Spec, Args->m_Slots);
            char FloatConv;
            int FloatPrecision;
            BOOL General = _NtCRT_GeneralFloat(Format, &Spec, Args->m_Slots, &FloatConv, &FloatPrecision);

            /* Copy the format up to the flags, the flags that have an effect and the width */
            memcpy(o, Format + Last, (Spec.FlagsPos - Last) * sizeof(TChar));
            o += Spec.FlagsPos - Last;
            for (size_t i = Spec.FlagsPos; i < Spec.WidthPos; i++)
            {
                if (!_NtCRT_IsIgnoredFlag(Format, &Spec, Args->m_Slots, Format[i]))
                {
                    *o++ = Format[i];
                }
            }
            memcpy(o, Format + Spec.WidthPos, (Spec.PrecisionPos - Spec.WidthPos) * sizeof(TChar));
            o += Spec.PrecisionPos - Spec.WidthPos;
            /* A %#o of 0 without the precision of 0, a negative precision argument is none */
            if (ZeroOctal && Spec.PrecisionSlot >= 0)
            {
                Args->m_Slots[Spec.PrecisionSlot] = (ULONG64)(LONG64)-1;
            }
            if (General && Spec.PrecisionSlot >= 0)
            {
                Args->m_Slots[Spec.PrecisionSlot] = (ULONG64)(LONG64)FloatPrecision;
            }
            if (General && Spec.PrecisionSlot < 0)
            {
                char Number[16];
                int n = 0;

                *o++ = '.';
                do
                {
                    Number[n++] = (char)('0' + FloatPrecision % 10);
                    FloatPrecision /= 10;
                } while (FloatPrecision != 0);
                while (n > 0)
                {
                    *o++ = Number[--n];
                }
            } else if (!ZeroOctal || Spec.PrecisionSlot >= 0)
            {
                memcpy(o, Format + Spec.PrecisionPos, (Spec.LengthPos - Spec.PrecisionPos) * sizeof(TChar));
                o += Spec.LengthPos - Spec.PrecisionPos;
            }
            Last = Spec.LengthPos;
            if (General)
            {
                /* The length modifier and the conversion */
                memcpy(o, Format + Spec.LengthPos, (Spec.ConvPos - Spec.LengthPos) * sizeof(TChar));
                o += Spec.ConvPos - Spec.LengthPos;
                *o++ = FloatConv;
                Last = Spec.ConvPos + 1;
            }
        }
        if (!_NtCRT_IsOtherWidth(&Spec, Conv))
        {
            continue;
        }
        if (Spec.PrecisionSlot >= 0)
        {
            Spec.Precision = (int)Args->m_Slots[Spec.PrecisionSlot];
        }
        if (Conv == 'c' || Conv == 'C')
        {
            /* A character is a string of it, the null character keeps %c */
            if (sizeof(TChar) == sizeof(char))
            {
                wchar_t Str[2] = { (wchar_t)Args->m_Slots[Spec.ValueSlot], L'\0' };

                Converted = Str[0] != L'\0' ? _NtCRT_FormatNarrow(Str, -1) : NULL;
            } else
            {
                wint_t Char = btowc((unsigned char)Args->m_Slots[Spec.ValueSlot]);
                wchar_t Str[2] = { (wchar_t)Char, L'\0' };

                if (Char == WEOF)
                {
                    NTCRT_ERRNO = EILSEQ;
                    Args->Failed = TRUE;
                    return;
                }
                Converted = Str[0] != L'\0' ? RtlAllocateHeap(NTCRT_HEAP, 0, sizeof(Str)) : NULL;
                if (Converted != NULL)
                {
                    memcpy(Converted, Str, sizeof(Str));
                }
            }
            if (Converted == NULL && (sizeof(TChar) == sizeof(char) ? (wchar_t)Args->m_Slots[Spec.ValueSlot] != L'\0'
                                                                    : (unsigned char)Args->m_Slots[Spec.ValueSlot] != 0))
            {
                Args->Failed = TRUE;
                return;
            }
        } else if (Args->m_Slots[Spec.ValueSlot] == 0)
        {
            /* NULL is printed as "(null)" */
            Converted = NULL;
        } else
        {
            Converted = sizeof(TChar) == sizeof(char)
                            ? (PVOID)_NtCRT_FormatNarrow((const wchar_t*)Args->m_Slots[Spec.ValueSlot], Spec.Precision)
                            : (PVOID)_NtCRT_FormatWide((const char*)Args->m_Slots[Spec.ValueSlot], Spec.Precision);
            if (Converted == NULL)
            {
                Args->Failed = TRUE;
                return;
            }
        }

        /* Copy the format up to the length modifier, and the conversion of the format width */
        memcpy(o, Format + Last, (Spec.LengthPos - Last) * sizeof(TChar));
        o += Spec.LengthPos - Last;
        Last = Spec.ConvPos + 1;
        if (sizeof(TChar) != sizeof(char))
        {
            *o++ = 'l';
        }
        if ((Conv == 'c' || Conv == 'C') && Converted == NULL)
        {
            *o++ = 'c';
            Args->m_Slots[Spec.ValueSlot] = 0;
        } else
        {
            *o++ = 's';
            Args->m_Slots[Spec.ValueSlot] = (ULONG64)Converted;
        }
        Args->m_Strings[Args->m_StringCount++] = Converted;
    }
    memcpy(o, Format + Last, (FormatLength - Last + 1) * sizeof(TChar));
    Args->Format = Args->m_Format;
    Args->Args = (va_list)Args->m_Slots;
}

NTCRT_FORMAT_ARGS::NTCRT_FORMAT_ARGS(const char* Format, va_list ap)
{
    Failed = FALSE;
    this->Format = Format;
    Args = ap;
    m_Format = NULL;
    m_Slots = NULL;
    m_Strings = NULL;
    m_StringCount = 0;
    if (Format != NULL)
    {
        _NtCRT_PrepareFormat(this, Format, ap);
    }
}

NTCRT_FORMAT_ARGS::NTCRT_FORMAT_ARGS(const wchar_t* Format, va_list ap)
{
    Failed = FALSE;
    this->Format = Format;
    Args = ap;
    m_Format = NULL;
    m_Slots = NULL;
    m_Strings = NULL;
    m_StringCount = 0;
    if (Format != NULL)
    {
        _NtCRT_PrepareFormat(this, Format, ap);
    }
}

NTCRT_FORMAT_ARGS::~NTCRT_FORMAT_ARGS()
{
    for (int i = 0; i < m_StringCount; i++)
    {
        if (m_Strings[i] != NULL)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, m_Strings[i]);
        }
    }
    if (m_Strings != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, m_Strings);
    }
    if (m_Slots != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, m_Slots);
    }
    if (m_Format != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, m_Format);
    }
}
