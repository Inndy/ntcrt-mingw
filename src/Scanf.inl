/*
 * The scanning of the scanf family, narrow and wide, for the functions of msvcrt, their _s and _l forms and the
 * __mingw_*scanf of the headers of mingw-w64 (wrapped by the specs, defined under their names for lld).
 *
 * C (7.21.6.2, 7.29.2.2): an input item is the longest prefix of a matching sequence within the width, read with one
 * character of pushback at most. A prefix that is not a matching sequence ("1e", "0x", "-") is a matching failure,
 * the characters read are consumed. An integer is the result of strtoll (signed conversions) or strtoull, stored in
 * the size of its type, the low bits as UCRT. A floating point number is converted by FloatParse.cpp at the precision
 * of its type. %ls, %lc and %l[ of the narrow functions and the narrow strings of the wide functions convert by the
 * locale (mbrtowc, wcrtomb). The width counts the characters of the input: bytes, multibyte characters for a wide
 * string of a narrow function, wchar_t in the wide functions.
 *
 * msvcrt: the length modifiers I, I32, I64 and w, %S and %C (the other width than the function), %P, the width,
 * * and the length modifiers in any order (%h02d), an unknown conversion character matches itself, the size argument
 * of %c, %s and %[ with NTCRT_SCANF_SECURE (the _s functions) and, with NTCRT_SCANF_LEGACY_WIDE, the %s, %c and %[
 * of the wide functions, which are wide (%hs narrow). POSIX: %n$ and the m of an allocated string.
 *
 * Every argument is a pointer (the size of the _s functions an unsigned int): a slot of the va_list, of 8 bytes on
 * x64 and 4 on x86, which makes a positional argument an index.
 *
 * The engine is a template of the character type, instantiated for char by Scanf.cpp and for wchar_t by
 * ScanfWide.cpp: ld links whole objects, a program with sscanf does not link the wide engine nor the streams. The
 * items are functions of their own (noinline), inlined in the loop of the format they take more space.
 */

#pragma once

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include <type_traits>

static_assert(sizeof(va_list) == sizeof(char*), "va_list is a pointer to the arguments");

#pragma region Input

/* The next character, EOF at the end of the input: a stream is not read again after its end */
template<typename TChar>
static
int
_NtCRT_ScanGet(
    _Inout_ NTCRT_SCANF_INPUT<TChar>* In)
{
    int c = EOF;

    if (In->Eof)
    {
        return EOF;
    }
    if (In->Stream != NULL)
    {
        c = In->Get(In->Stream);
    } else if (In->Left != 0 && *In->Next != 0)
    {
        c = (int)(typename std::make_unsigned<TChar>::type)*In->Next++;
        In->Left--;
    }
    if (c == EOF)
    {
        In->Eof = TRUE;
        return EOF;
    }
    In->Count++;
    return c;
}

/* Push back the character read last, none for EOF */
template<typename TChar>
static
void
_NtCRT_ScanUnget(
    _Inout_ NTCRT_SCANF_INPUT<TChar>* In,
    _In_ int c)
{
    if (c == EOF)
    {
        return;
    }
    In->Count--;
    if (In->Stream != NULL)
    {
        In->Unget(c, In->Stream);
    } else
    {
        In->Next--;
        In->Left++;
    }
}

/* The next character of an input item with *Left characters more at most, EOF past them */
template<typename TChar>
static
int
_NtCRT_ScanItemGet(
    _Inout_ NTCRT_SCANF_INPUT<TChar>* In,
    _Inout_ size_t* Left)
{
    if (*Left == 0)
    {
        return EOF;
    }
    (*Left)--;
    return _NtCRT_ScanGet(In);
}

/* White space: of the "C" locale for bytes (also in "C.UTF-8"), iswspace for wchar_t */
template<typename TChar>
static
BOOL
_NtCRT_ScanIsSpace(
    _In_ int c)
{
    if (sizeof(TChar) == sizeof(char))
    {
        return c == ' ' || (c >= '\t' && c <= '\r');
    }
    return c >= 0 && iswspace((wint_t)c);
}

template<typename TChar>
static
void
_NtCRT_ScanSkipSpace(
    _Inout_ NTCRT_SCANF_INPUT<TChar>* In)
{
    int c;

    do
    {
        c = _NtCRT_ScanGet(In);
    } while (c != EOF && _NtCRT_ScanIsSpace<TChar>(c));
    _NtCRT_ScanUnget(In, c);
}

#pragma endregion

#pragma region Numbers

static
unsigned
_NtCRT_ScanDigit(
    _In_ int c)
{
    if (c >= '0' && c <= '9')
    {
        return (unsigned)(c - '0');
    }
    c |= 0x20;
    return c >= 'a' && c <= 'z' ? (unsigned)(c - 'a' + 10) : 36;
}

/*
 * An integer of Base (0 for the prefixes of %i) as strtoll (Signed) or strtoull: FALSE if the item is not a
 * matching sequence. Out of range is ERANGE and the limit of the type as theirs.
 */
template<typename TChar>
static __attribute__((noinline))
BOOL
_NtCRT_ScanInteger(
    _Inout_ NTCRT_SCANF_INPUT<TChar>* In,
    _In_ size_t Left,
    _In_ unsigned Base,
    _In_ BOOL Signed,
    _Out_ ULONG64* Value)
{
    int c = _NtCRT_ScanItemGet(In, &Left);
    BOOL Neg = FALSE, Digits = FALSE, Overflow = FALSE;
    ULONG64 v = 0;

    if (c == '+' || c == '-')
    {
        Neg = c == '-';
        c = _NtCRT_ScanItemGet(In, &Left);
    }
    if ((Base == 0 || Base == 16) && c == '0')
    {
        Digits = TRUE;
        c = _NtCRT_ScanItemGet(In, &Left);
        if ((c | 0x20) == 'x')
        {
            /* "0x" needs a hex digit */
            Base = 16;
            Digits = FALSE;
            c = _NtCRT_ScanItemGet(In, &Left);
        } else if (Base == 0)
        {
            Base = 8;
        }
    }
    if (Base == 0)
    {
        Base = 10;
    }
    for (unsigned d; c != EOF && (d = _NtCRT_ScanDigit(c)) < Base; c = _NtCRT_ScanItemGet(In, &Left))
    {
        Digits = TRUE;
        if (v > (ULLONG_MAX - d) / Base)
        {
            Overflow = TRUE;
        } else
        {
            v = v * Base + d;
        }
    }
    _NtCRT_ScanUnget(In, c);
    *Value = 0;
    if (!Digits)
    {
        return FALSE;
    }
    if (Signed && (Overflow || v > (ULONG64)LLONG_MAX + Neg))
    {
        NTCRT_ERRNO = ERANGE;
        v = Neg ? (ULONG64)LLONG_MIN : (ULONG64)LLONG_MAX;
    } else if (Overflow)
    {
        NTCRT_ERRNO = ERANGE;
        v = ULLONG_MAX;
    } else if (Neg)
    {
        v = 0 - v;
    }
    *Value = v;
    return TRUE;
}

/* The text of a floating point number, in Stack or grown on the heap */
typedef struct _NTCRT_SCANF_TEXT
{
    char* Buffer;
    size_t Length;
    size_t Size;
    char Stack[64];
} NTCRT_SCANF_TEXT;

/* Append c, FALSE with ENOMEM. A character more always fits for the terminator. */
static
BOOL
_NtCRT_ScanTextPut(
    _Inout_ NTCRT_SCANF_TEXT* t,
    _In_ int c)
{
    if (t->Length + 2 > t->Size)
    {
        size_t Size = t->Size * 2;
        char* New = (char*)(t->Buffer == t->Stack ? RtlAllocateHeap(NTCRT_HEAP, 0, Size)
                                                  : RtlReAllocateHeap(NTCRT_HEAP, 0, t->Buffer, Size));

        if (New == NULL)
        {
            NTCRT_ERRNO = ENOMEM;
            return FALSE;
        }
        if (t->Buffer == t->Stack)
        {
            memcpy(New, t->Stack, t->Length);
        }
        t->Buffer = New;
        t->Size = Size;
    }
    t->Buffer[t->Length++] = (char)c;
    t->Buffer[t->Length] = '\0';
    return TRUE;
}

/*
 * The subject sequence of strtod into t: a sign, then inf, infinity, nan, nan(n-char-sequence), a hex number 0x...
 * with an optional binary exponent p..., or a decimal number with an optional exponent e.... FALSE if the item is
 * not a matching sequence.
 */
template<typename TChar>
static __attribute__((noinline))
BOOL
_NtCRT_ScanFloat(
    _Inout_ NTCRT_SCANF_INPUT<TChar>* In,
    _In_ size_t Left,
    _Inout_ NTCRT_SCANF_TEXT* t)
{
    int c = _NtCRT_ScanItemGet(In, &Left);
    BOOL Hex = FALSE, Point = FALSE, Digits = FALSE;

    /* Take c and read the next one */
    auto Take = [&]() {
        if (!_NtCRT_ScanTextPut(t, c))
        {
            return FALSE;
        }
        c = _NtCRT_ScanItemGet(In, &Left);
        return TRUE;
    };

    if ((c == '+' || c == '-') && !Take())
    {
        return FALSE;
    }
    if ((c | 0x20) == 'i' || (c | 0x20) == 'n')
    {
        const char* Word = (c | 0x20) == 'i' ? "infinity" : "nan";
        size_t n = 0;

        for (; Word[n] != '\0' && c != EOF && (c | 0x20) == Word[n]; n++)
        {
            if (!Take())
            {
                return FALSE;
            }
        }
        /* inf and infinity, not what is between */
        if (n != 3 && Word[n] != '\0')
        {
            _NtCRT_ScanUnget(In, c);
            return FALSE;
        }
        if (Word[0] == 'n' && c == '(')
        {
            do
            {
                if (!Take())
                {
                    return FALSE;
                }
            } while ((c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') || c == '_');
            if (c != ')')
            {
                _NtCRT_ScanUnget(In, c);
                return FALSE;
            }
            return _NtCRT_ScanTextPut(t, c);
        }
        _NtCRT_ScanUnget(In, c);
        return TRUE;
    }
    if (c == '0')
    {
        Digits = TRUE;
        if (!Take())
        {
            return FALSE;
        }
        if ((c | 0x20) == 'x')
        {
            /* "0x" needs a hex digit */
            Hex = TRUE;
            Digits = FALSE;
            if (!Take())
            {
                return FALSE;
            }
        }
    }
    for (;;)
    {
        if (c != EOF && _NtCRT_ScanDigit(c) < (Hex ? 16u : 10u))
        {
            Digits = TRUE;
        } else if (c == '.' && !Point)
        {
            Point = TRUE;
        } else
        {
            break;
        }
        if (!Take())
        {
            return FALSE;
        }
    }
    if (Digits && (c | 0x20) == (Hex ? 'p' : 'e'))
    {
        if (!Take() || ((c == '+' || c == '-') && !Take()))
        {
            return FALSE;
        }
        Digits = c >= '0' && c <= '9';
        while (c >= '0' && c <= '9')
        {
            if (!Take())
            {
                return FALSE;
            }
        }
    }
    _NtCRT_ScanUnget(In, c);
    return Digits;
}

#pragma endregion

#pragma region Strings and characters

/* The array of a string or characters: Size elements fit (the terminator of a string apart), grown if Alloc (%m) */
typedef struct _NTCRT_SCANF_SINK
{
    void* Buffer; /* NULL if suppressed */
    size_t Size;
    size_t Length;
    BOOL Wide;  /* of wchar_t */
    BOOL Alloc; /* malloc, grown by realloc */
    BOOL Full;  /* failed: Size elements did not hold the input */
    size_t Terminator;
    mbstate_t State;
} NTCRT_SCANF_SINK;

static
BOOL
_NtCRT_ScanSinkPut(
    _Inout_ NTCRT_SCANF_SINK* s,
    _In_ unsigned Unit)
{
    if (s->Length == s->Size)
    {
        size_t Count = (s->Size + s->Terminator) * 2;
        void* New = s->Alloc ? _NtCRT_realloc(s->Buffer, Count * (s->Wide ? sizeof(wchar_t) : 1)) : NULL;

        if (New == NULL)
        {
            s->Full = TRUE;
            NTCRT_ERRNO = ENOMEM;
            return FALSE;
        }
        s->Buffer = New;
        s->Size = Count - s->Terminator;
    }
    if (s->Buffer == NULL)
    {
    } else if (s->Wide)
    {
        ((wchar_t*)s->Buffer)[s->Length] = (wchar_t)Unit;
    } else
    {
        ((char*)s->Buffer)[s->Length] = (char)Unit;
    }
    s->Length++;
    return TRUE;
}

/* A null character at Index, which the array holds after Size */
static
void
_NtCRT_ScanSinkEnd(
    _Inout_ NTCRT_SCANF_SINK* s,
    _In_ size_t Index)
{
    if (s->Wide)
    {
        ((wchar_t*)s->Buffer)[Index] = L'\0';
    } else
    {
        ((char*)s->Buffer)[Index] = '\0';
    }
}

/*
 * A character of the input c to the array, converted if it is of the other width: 1 if it completes a character of
 * the input, 0 if it is a part of a multibyte one, -1 on an error (EILSEQ, ENOMEM)
 */
template<typename TChar>
static
int
_NtCRT_ScanSinkChar(
    _Inout_ NTCRT_SCANF_SINK* s,
    _In_ int c)
{
    if (sizeof(TChar) == sizeof(char) && s->Wide)
    {
        /* A character above U+FFFF is a surrogate pair, the high one before the last byte */
        char Byte = (char)c;
        wchar_t Char;
        size_t n = mbrtowc(&Char, &Byte, 1, &s->State);

        if (n == (size_t)-2)
        {
            return 0;
        }
        if (n == (size_t)-1 || !_NtCRT_ScanSinkPut(s, Char))
        {
            return -1;
        }
        return mbsinit(&s->State) ? 1 : 0;
    }
    if (sizeof(TChar) != sizeof(char) && !s->Wide)
    {
        /* A high surrogate stores nothing, the low one the bytes of the pair */
        char Bytes[MB_LEN_MAX];
        size_t n = wcrtomb(Bytes, (wchar_t)c, &s->State);

        if (n == (size_t)-1)
        {
            return -1;
        }
        for (size_t i = 0; i < n; i++)
        {
            if (!_NtCRT_ScanSinkPut(s, (unsigned char)Bytes[i]))
            {
                return -1;
            }
        }
        return 1;
    }
    return _NtCRT_ScanSinkPut(s, (unsigned)c) ? 1 : -1;
}

/*
 * c in the scan set Set..End: ']' first is a member, a '-' between two characters is the range from the one before
 * to the one after if they are in order, else and at the ends it is a member
 */
template<typename TChar>
static
BOOL
_NtCRT_ScanInSet(
    _In_reads_(End - Set) const TChar* Set,
    _In_ const TChar* End,
    _In_ int c)
{
    typedef typename std::make_unsigned<TChar>::type UChar;
    UChar u = (UChar)c;

    for (const TChar* p = Set; p < End; p++)
    {
        BOOL Range = *p == '-' && p != Set && p + 1 < End && (UChar)p[-1] <= (UChar)p[1];

        if (Range ? (UChar)p[-1] <= u && u <= (UChar)p[1] : (UChar)*p == u)
        {
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * %c (Conv 'c', exactly Width characters), %s or %[ (Set..SetEnd, its complement if Negate, Width characters at
 * most) into the array: FALSE if the item is not a matching sequence or does not convert
 */
template<typename TChar>
static __attribute__((noinline))
BOOL
_NtCRT_ScanText(
    _Inout_ NTCRT_SCANF_INPUT<TChar>* In,
    _In_ int Conv,
    _In_ size_t Width,
    _In_ const TChar* Set,
    _In_ const TChar* SetEnd,
    _In_ BOOL Negate,
    _Inout_ NTCRT_SCANF_SINK* s)
{
    size_t n = 0;

    while (n < Width)
    {
        int c = _NtCRT_ScanGet(In), r;

        if (c == EOF)
        {
            break;
        }
        if ((Conv == 's' && _NtCRT_ScanIsSpace<TChar>(c)) ||
            (Conv == '[' && _NtCRT_ScanInSet(Set, SetEnd, c) == Negate))
        {
            _NtCRT_ScanUnget(In, c);
            break;
        }
        r = _NtCRT_ScanSinkChar<TChar>(s, c);
        if (r < 0)
        {
            return FALSE;
        }
        n += (size_t)r;
    }
    if (!mbsinit(&s->State))
    {
        /* A multibyte character or a surrogate pair cut by the end of the item */
        NTCRT_ERRNO = EILSEQ;
        return FALSE;
    }
    return n != 0 && (Conv != 'c' || n == Width);
}

#pragma endregion

/* Store the low Size bytes of v (x64 is little endian), by memcpy as the object may be an int, a long or a void* */
static
void
_NtCRT_ScanStore(
    _Out_ void* p,
    _In_ int Size,
    _In_ ULONG64 v)
{
    switch (Size)
    {
    case 1:
        memcpy(p, &v, 1);
        break;
    case 2:
        memcpy(p, &v, 2);
        break;
    case 4:
        memcpy(p, &v, 4);
        break;
    default:
        memcpy(p, &v, 8);
        break;
    }
}

typedef enum _NTCRT_SCANF_LENGTH
{
    NtCRTScanNone,
    NtCRTScanChar,     /* hh */
    NtCRTScanShort,    /* h */
    NtCRTScanLong,     /* l: 32 bits, double */
    NtCRTScanLongLong, /* ll, q: long double; j, I64: 64 bits */
    NtCRTScanInt32,    /* I32 */
    NtCRTScanDouble    /* L: long double, 64 bits for __mingw_*scanf (ignored by msvcrt) */
} NTCRT_SCANF_LENGTH;

/* Scan Format, returns the count of assignments, EOF for an input failure before the first conversion */
template<typename TChar>
static
int
_NtCRT_ScanFormat(
    _Inout_ NTCRT_SCANF_INPUT<TChar>* In,
    _In_z_ const TChar* f,
    _In_ const ULONG_PTR* Args,
    _In_ ULONG Options)
{
    typedef typename std::make_unsigned<TChar>::type UChar;
    int Assigned = 0, Next = 0;
    BOOL Converted = FALSE, InputFailure = FALSE;
    BOOL Wide = sizeof(TChar) != sizeof(char);

    auto Arg = [&](int Position) { return Position >= 0 ? Args[Position] : Args[Next++]; };

    while (*f != '\0')
    {
        NTCRT_SCANF_LENGTH Length = NtCRTScanNone;
        BOOL Suppress = FALSE, Alloc = FALSE, Ok;
        int Position = -1, Text = 0, Conv;
        size_t Width = 0, Start;
        const TChar* q;
        void* Target = NULL;

        if (_NtCRT_ScanIsSpace<TChar>((UChar)*f))
        {
            while (_NtCRT_ScanIsSpace<TChar>((UChar)*f))
            {
                f++;
            }
            _NtCRT_ScanSkipSpace(In);
            continue;
        }
        if (*f != '%')
        {
            int c = _NtCRT_ScanGet(In);

            if (c != (UChar)*f)
            {
                InputFailure = c == EOF;
                _NtCRT_ScanUnget(In, c);
                break;
            }
            f++;
            continue;
        }

        /* n$, then the width, * and the length modifiers in any order as msvcrt */
        f++;
        for (q = f; *q >= '0' && *q <= '9' && Position < 100000; q++)
        {
            Position = (Position < 0 ? 0 : Position * 10) + (*q - '0');
        }
        if (q != f && *q == '$' && Position > 0)
        {
            f = q + 1;
            Position--;
        } else
        {
            Position = -1;
        }
        for (;; f++)
        {
            if (*f >= '0' && *f <= '9')
            {
                Width = Width < (SIZE_MAX - 9) / 10 ? Width * 10 + (size_t)(*f - '0') : SIZE_MAX;
            } else if (*f == '*')
            {
                Suppress = TRUE;
            } else if (*f == 'm')
            {
                Alloc = TRUE;
            } else if (*f == 'h')
            {
                Length = Length == NtCRTScanShort ? NtCRTScanChar : NtCRTScanShort;
                Text = 1;
            } else if (*f == 'l')
            {
                Length = Length == NtCRTScanLong ? NtCRTScanLongLong : NtCRTScanLong;
                Text = 2;
            } else if (*f == 'w')
            {
                Text = 2;
            } else if (*f == 'L')
            {
                Length = NtCRTScanDouble;
            } else if (*f == 'q' || *f == 'j')
            {
                Length = NtCRTScanLongLong;
            } else if (*f == 'z' || *f == 't')
            {
                Length = sizeof(void*) == 8 ? NtCRTScanLongLong : NtCRTScanLong;
            } else if (*f == 'I')
            {
                if (f[1] == '3' && f[2] == '2')
                {
                    f += 2;
                    Length = NtCRTScanInt32;
                } else
                {
                    BOOL Is64 = f[1] == '6' && f[2] == '4';

                    f += Is64 ? 2 : 0;
                    Length = Is64 || sizeof(void*) == 8 ? NtCRTScanLongLong : NtCRTScanLong;
                }
            } else if (*f != '\'')
            {
                break;
            }
        }
        Conv = (UChar)*f;
        if (Conv == '\0')
        {
            break;
        }
        f++;

        /* The size of an integer: L is ignored by msvcrt, 64 bits for __mingw_*scanf */
        BOOL L64 = Length == NtCRTScanDouble && (Options & NTCRT_SCANF_LONG_DOUBLE);
        int IntSize = Length == NtCRTScanChar    ? 1
                      : Length == NtCRTScanShort ? 2
                      : Length == NtCRTScanLongLong || L64 ? 8
                                                           : 4;

        if (Conv == 'n')
        {
            /* No input is read, the count is no assignment */
            if (!Suppress)
            {
                _NtCRT_ScanStore((void*)Arg(Position), IntSize, In->Count);
            }
            Converted = TRUE;
            continue;
        }
        switch (Conv)
        {
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
        case 'p':
        case 'P':
        {
            ULONG64 v;
            int Size = (Conv | 0x20) == 'p' && IntSize > 2 ? (int)sizeof(void*) : IntSize;

            if (!Suppress)
            {
                Target = (void*)Arg(Position);
            }
            _NtCRT_ScanSkipSpace(In);
            Start = In->Count;
            Ok = _NtCRT_ScanInteger(In, Width != 0 ? Width : SIZE_MAX,
                                    Conv == 'd' || Conv == 'u' ? 10u : Conv == 'i' ? 0u : Conv == 'o' ? 8u : 16u,
                                    Conv == 'd' || Conv == 'i', &v);
            if (Ok && !Suppress)
            {
                _NtCRT_ScanStore(Target, Size, v);
                Assigned++;
            }
            break;
        }
        case 'a':
        case 'A':
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        {
            NTCRT_SCANF_TEXT t;

            t.Buffer = t.Stack;
            t.Length = 0;
            t.Size = sizeof(t.Stack);
            if (!Suppress)
            {
                Target = (void*)Arg(Position);
            }
            _NtCRT_ScanSkipSpace(In);
            Start = In->Count;
            Ok = _NtCRT_ScanFloat(In, Width != 0 ? Width : SIZE_MAX, &t);
            if (Ok && !Suppress)
            {
                /* A long double is a double for the functions of msvcrt, as for MSVC */
                int Long = Options & NTCRT_SCANF_LONG_DOUBLE ? 2 : 1;
                int Kind = Length == NtCRTScanLong                                   ? 1
                           : Length == NtCRTScanLongLong || Length == NtCRTScanDouble ? Long
                                                                                      : 0;

                if (_NtCRT_ScanFloatText(t.Buffer, Kind, Target))
                {
                    NTCRT_ERRNO = ERANGE;
                }
                Assigned++;
            }
            if (t.Buffer != t.Stack)
            {
                RtlFreeHeap(NTCRT_HEAP, 0, t.Buffer);
            }
            break;
        }
        case 'c':
        case 'C':
        case 's':
        case 'S':
        case '[':
        {
            /* The array is wide by l and w, narrow by h, else %S and %C are of the other width than %s and %c */
            BOOL Upper = Conv == 'S' || Conv == 'C', Negate = FALSE;
            const TChar* Set = f;
            NTCRT_SCANF_SINK s = {};
            size_t UserSize = 0;

            s.Wide = Text != 0 ? Text == 2 : (Wide && (Options & NTCRT_SCANF_LEGACY_WIDE) ? !Upper : Upper);
            Conv = Conv == '[' ? '[' : Conv | 0x20;
            if (Conv == '[')
            {
                if (*f == '^')
                {
                    Negate = TRUE;
                    f++;
                }
                Set = f;
                if (*f == ']')
                {
                    f++;
                }
                while (*f != '\0' && *f != ']')
                {
                    f++;
                }
                if (*f == '\0')
                {
                    /* No end of the scan set */
                    goto End;
                }
            }
            s.Size = SIZE_MAX;
            s.Terminator = Conv != 'c';
            if (!Suppress)
            {
                s.Buffer = Target = (void*)Arg(Position);
                if (Alloc)
                {
                    s.Alloc = TRUE;
                    s.Size = 32 - s.Terminator;
                    s.Buffer = _NtCRT_malloc(32 * (s.Wide ? sizeof(wchar_t) : 1));
                    if (s.Buffer == NULL)
                    {
                        /* An error, EOF before the first conversion as POSIX */
                        NTCRT_ERRNO = ENOMEM;
                        InputFailure = TRUE;
                        goto End;
                    }
                } else if (Options & NTCRT_SCANF_SECURE)
                {
                    /* The size of the array in elements as unsigned int, the terminator included */
                    UserSize = (unsigned)Arg(Position >= 0 ? Position + 1 : -1);
                    s.Size = UserSize > s.Terminator ? UserSize - s.Terminator : 0;
                    if (Target == NULL)
                    {
                        /* A null array stops as UCRT, msvcrt crashes */
                        NTCRT_ERRNO = EINVAL;
                        goto End;
                    }
                }
            }
            if (Conv == 's')
            {
                _NtCRT_ScanSkipSpace(In);
            }
            Start = In->Count;
            Ok = _NtCRT_ScanText(In, Conv, Width != 0 ? Width : Conv == 'c' ? 1 : SIZE_MAX, Set, f, Negate, &s);
            if (Conv == '[')
            {
                f++;
            }
            if (Suppress)
            {
                break;
            }
            if (!Ok)
            {
                if (Alloc)
                {
                    _NtCRT_free(s.Buffer);
                    if (s.Full)
                    {
                        /* realloc failed */
                        InputFailure = TRUE;
                        goto End;
                    }
                } else if (s.Full && UserSize != 0)
                {
                    /* The array of a _s function is emptied as msvcrt */
                    _NtCRT_ScanSinkEnd(&s, 0);
                }
                break;
            }
            if (s.Terminator)
            {
                _NtCRT_ScanSinkEnd(&s, s.Length);
            }
            if (Alloc)
            {
                /* The pointer is a char* or a wchar_t* */
                memcpy(Target, &s.Buffer, sizeof(s.Buffer));
            }
            Assigned++;
            break;
        }
        default:
            /* %% and, as msvcrt, any other character match the character after white space */
            _NtCRT_ScanSkipSpace(In);
            {
                int c = _NtCRT_ScanGet(In);

                if (c != Conv)
                {
                    InputFailure = c == EOF;
                    _NtCRT_ScanUnget(In, c);
                    goto End;
                }
            }
            continue;
        }
        if (!Ok)
        {
            /* Nothing read at the end of the input is an input failure, else a matching failure */
            InputFailure = In->Count == Start && In->Eof;
            break;
        }
        Converted = TRUE;
    }
End:
    return InputFailure && !Converted ? EOF : Assigned;
}
