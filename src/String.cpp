/* msvcrt string, conversion and character class functions not exported by ntdll */

#include "NtCRT.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

/* Integer conversion */

template<typename TChar>
static
bool
_NtCRT_IsSpace(
    _In_ TChar c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

template<typename TChar>
static
int
_NtCRT_DigitValue(
    _In_ TChar c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    } else if (c >= 'a' && c <= 'z')
    {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z')
    {
        return c - 'A' + 10;
    }
    return 99;
}

/* Parse the magnitude, returns the limit with ERANGE on overflow */
template<typename TChar>
static
unsigned long long
_NtCRT_ParseInteger(
    _In_ const TChar* Str,
    _Out_opt_ TChar** End,
    _In_ int Base,
    _In_ unsigned long long PositiveLimit,
    _In_ unsigned long long NegativeLimit,
    _Out_ bool* Negative)
{
    const TChar* p = Str;
    unsigned long long Value = 0, Limit;
    bool Any = false, Overflow = false;

    *Negative = false;
    if (End != NULL)
    {
        *End = (TChar*)Str;
    }
    if (Str == NULL || Base < 0 || Base == 1 || Base > 36)
    {
        NTCRT_ERRNO = EINVAL;
        return 0;
    }
    while (_NtCRT_IsSpace(*p))
    {
        p++;
    }
    if (*p == '-')
    {
        *Negative = true;
        p++;
    } else if (*p == '+')
    {
        p++;
    }
    if ((Base == 0 || Base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && _NtCRT_DigitValue(p[2]) < 16)
    {
        p += 2;
        Base = 16;
    } else if (Base == 0)
    {
        Base = p[0] == '0' ? 8 : 10;
    }
    Limit = *Negative ? NegativeLimit : PositiveLimit;
    for (;; p++)
    {
        int Digit = _NtCRT_DigitValue(*p);
        if (Digit >= Base)
        {
            break;
        }
        Any = true;
        if (!Overflow)
        {
            if (Value > (Limit - Digit) / Base)
            {
                Overflow = true;
            } else
            {
                Value = Value * Base + Digit;
            }
        }
    }
    if (!Any)
    {
        *Negative = false;
        return 0;
    }
    if (End != NULL)
    {
        *End = (TChar*)p;
    }
    if (Overflow)
    {
        NTCRT_ERRNO = ERANGE;
        return Limit;
    }
    return Value;
}

template<typename TChar, typename TSigned>
static
TSigned
_NtCRT_StrToSigned(
    _In_ const TChar* Str,
    _Out_opt_ TChar** End,
    _In_ int Base,
    _In_ TSigned Max)
{
    bool Negative;
    unsigned long long Value = _NtCRT_ParseInteger(Str,
                                                   End,
                                                   Base,
                                                   (unsigned long long)Max,
                                                   (unsigned long long)Max + 1,
                                                   &Negative);
    return Negative ? (TSigned)(0 - Value) : (TSigned)Value;
}

template<typename TChar, typename TUnsigned>
static
TUnsigned
_NtCRT_StrToUnsigned(
    _In_ const TChar* Str,
    _Out_opt_ TChar** End,
    _In_ int Base,
    _In_ TUnsigned Max)
{
    bool Negative;
    int OldErrno = NTCRT_ERRNO;
    unsigned long long Value;

    NTCRT_ERRNO = 0;
    Value = _NtCRT_ParseInteger(Str, End, Base, Max, Max, &Negative);
    if (NTCRT_ERRNO == ERANGE)
    {
        return Max;
    }
    if (NTCRT_ERRNO == 0)
    {
        NTCRT_ERRNO = OldErrno;
    }
    return Negative ? (TUnsigned)(0 - Value) : (TUnsigned)Value;
}

EXTERN_C long __cdecl _NtCRT_strtol(const char* str, char** end, int base)
{
    return _NtCRT_StrToSigned<char, long>(str, end, base, LONG_MAX);
}

EXTERN_C unsigned long __cdecl _NtCRT_strtoul(const char* str, char** end, int base)
{
    return _NtCRT_StrToUnsigned<char, unsigned long>(str, end, base, ULONG_MAX);
}

EXTERN_C long long __cdecl _NtCRT_strtoll(const char* str, char** end, int base)
{
    return _NtCRT_StrToSigned<char, long long>(str, end, base, LLONG_MAX);
}

EXTERN_C unsigned long long __cdecl _NtCRT_strtoull(const char* str, char** end, int base)
{
    return _NtCRT_StrToUnsigned<char, unsigned long long>(str, end, base, ULLONG_MAX);
}

EXTERN_C long __cdecl _NtCRT_wcstol(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_StrToSigned<wchar_t, long>(str, end, base, LONG_MAX);
}

EXTERN_C unsigned long __cdecl _NtCRT_wcstoul(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_StrToUnsigned<wchar_t, unsigned long>(str, end, base, ULONG_MAX);
}

EXTERN_C long long __cdecl _NtCRT_wcstoll(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_StrToSigned<wchar_t, long long>(str, end, base, LLONG_MAX);
}

EXTERN_C unsigned long long __cdecl _NtCRT_wcstoull(const wchar_t* str, wchar_t** end, int base)
{
    return _NtCRT_StrToUnsigned<wchar_t, unsigned long long>(str, end, base, ULLONG_MAX);
}

EXTERN_C long long __cdecl _NtCRT_atoll(const char* str)
{
    return _NtCRT_strtoll(str, NULL, 10);
}

EXTERN_C long long __cdecl _NtCRT__wtoll(const wchar_t* str)
{
    return _NtCRT_wcstoll(str, NULL, 10);
}

/* Floating point conversion from mingw-w64 */

EXTERN_C double __cdecl __mingw_wcstod(const wchar_t* str, wchar_t** end);

EXTERN_C double __cdecl _NtCRT_strtod(const char* str, char** end)
{
    return __mingw_strtod(str, end);
}

EXTERN_C double __cdecl _NtCRT_wcstod(const wchar_t* str, wchar_t** end)
{
    return __mingw_wcstod(str, end);
}

EXTERN_C double __cdecl _NtCRT_atof(const char* str)
{
    return __mingw_strtod(str, NULL);
}

EXTERN_C double __cdecl _NtCRT__wtof(const wchar_t* str)
{
    return __mingw_wcstod(str, NULL);
}

/* Integer arithmetic */

EXTERN_C long long __cdecl _NtCRT_llabs(long long x)
{
    return x < 0 ? -x : x;
}

EXTERN_C div_t __cdecl _NtCRT_div(int numer, int denom)
{
    div_t Result;
    Result.quot = numer / denom;
    Result.rem = numer % denom;
    return Result;
}

EXTERN_C ldiv_t __cdecl _NtCRT_ldiv(long numer, long denom)
{
    ldiv_t Result;
    Result.quot = numer / denom;
    Result.rem = numer % denom;
    return Result;
}

/* Error messages of msvcrt */

static const char* const g_ErrorList[] = {
    "No error",
    "Operation not permitted",
    "No such file or directory",
    "No such process",
    "Interrupted function call",
    "Input/output error",
    "No such device or address",
    "Arg list too long",
    "Exec format error",
    "Bad file descriptor",
    "No child processes",
    "Resource temporarily unavailable",
    "Not enough space",
    "Permission denied",
    "Bad address",
    "Unknown error",
    "Resource device",
    "File exists",
    "Improper link",
    "No such device",
    "Not a directory",
    "Is a directory",
    "Invalid argument",
    "Too many open files in system",
    "Too many open files",
    "Inappropriate I/O control operation",
    "Unknown error",
    "File too large",
    "No space left on device",
    "Invalid seek",
    "Read-only file system",
    "Too many links",
    "Broken pipe",
    "Domain error",
    "Result too large",
    "Unknown error",
    "Resource deadlock avoided",
    "Unknown error",
    "Filename too long",
    "No locks available",
    "Function not implemented",
    "Directory not empty",
    "Illegal byte sequence",
    "Unknown error",
};

#define NTCRT_SYS_NERR ((int)ARRAYSIZE(g_ErrorList) - 1)

extern "C"
{
    char* _NtCRT_sys_errlist[ARRAYSIZE(g_ErrorList)];
    int _NtCRT_sys_nerr = NTCRT_SYS_NERR;
}

static
const char*
_NtCRT_ErrorMessage(
    _In_ int ErrNum)
{
    if ((unsigned int)ErrNum > (unsigned int)NTCRT_SYS_NERR)
    {
        ErrNum = NTCRT_SYS_NERR;
    }
    return g_ErrorList[ErrNum];
}

EXTERN_C char** __cdecl _NtCRT___sys_errlist(void)
{
    if (_NtCRT_sys_errlist[0] == NULL)
    {
        for (unsigned int i = 0; i < ARRAYSIZE(g_ErrorList); i++)
        {
            _NtCRT_sys_errlist[i] = (char*)g_ErrorList[i];
        }
    }
    return _NtCRT_sys_errlist;
}

EXTERN_C int* __cdecl _NtCRT___sys_nerr(void)
{
    return &_NtCRT_sys_nerr;
}

EXTERN_C char* __cdecl _NtCRT_strerror(int errnum)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();
    const char* Message = _NtCRT_ErrorMessage(errnum);

    strncpy(Ptd->StrerrorBuffer, Message, sizeof(Ptd->StrerrorBuffer) - 1);
    Ptd->StrerrorBuffer[sizeof(Ptd->StrerrorBuffer) - 1] = '\0';
    return Ptd->StrerrorBuffer;
}

EXTERN_C int __cdecl _NtCRT_strerror_s(char* buffer, size_t size, int errnum)
{
    const char* Message = _NtCRT_ErrorMessage(errnum);
    size_t Length = strlen(Message);

    if (buffer == NULL || size == 0)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    if (Length >= size)
    {
        Length = size - 1;
    }
    memcpy(buffer, Message, Length);
    buffer[Length] = '\0';
    return 0;
}

/* "str: message\n", or "message\n" without str */
EXTERN_C char* __cdecl _NtCRT__strerror(const char* str)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();
    char* Buffer = Ptd->StrerrorBuffer;
    size_t Size = sizeof(Ptd->StrerrorBuffer), Used = 0;
    const char* Message = _NtCRT_ErrorMessage(NTCRT_ERRNO);

    if (str != NULL && *str != '\0')
    {
        size_t Length = strnlen(str, 94);
        memcpy(Buffer, str, Length);
        Used = Length;
        Buffer[Used++] = ':';
        Buffer[Used++] = ' ';
    }
    for (; *Message != '\0' && Used < Size - 2; Message++)
    {
        Buffer[Used++] = *Message;
    }
    Buffer[Used++] = '\n';
    Buffer[Used] = '\0';
    return Buffer;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wcserror(int errnum)
{
    /* Messages are ASCII, widen into a per-thread buffer after the narrow one */
    static_assert(sizeof(((PNTCRT_PTD)0)->StrerrorBuffer) >= 64, "buffer");
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();
    const char* Message = _NtCRT_ErrorMessage(errnum);
    wchar_t* Buffer = (wchar_t*)Ptd->StrerrorBuffer;
    size_t Count = sizeof(Ptd->StrerrorBuffer) / sizeof(wchar_t), i;

    for (i = 0; Message[i] != '\0' && i < Count - 1; i++)
    {
        Buffer[i] = (unsigned char)Message[i];
    }
    Buffer[i] = L'\0';
    return Buffer;
}

EXTERN_C int __cdecl _NtCRT__wcserror_s(wchar_t* buffer, size_t count, int errnum)
{
    const char* Message = _NtCRT_ErrorMessage(errnum);
    size_t i;

    if (buffer == NULL || count == 0)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    for (i = 0; Message[i] != '\0' && i < count - 1; i++)
    {
        buffer[i] = (unsigned char)Message[i];
    }
    buffer[i] = L'\0';
    return 0;
}

/* "str: message" of errno, or "message" without str, followed by a newline if requested, truncated to Size */
template<typename TChar>
static
void
_NtCRT_FormatStrerror(
    _Out_writes_z_(Size) TChar* Buffer,
    _In_ size_t Size,
    _In_opt_z_ const TChar* str,
    _In_ bool NewLine)
{
    const char* Message = _NtCRT_ErrorMessage(NTCRT_ERRNO);
    size_t Used = 0;

    if (str != NULL && *str != 0)
    {
        for (size_t i = 0; str[i] != 0 && i < 94 && Used < Size; i++)
        {
            Buffer[Used++] = str[i];
        }
        for (const char* p = ": "; *p != '\0' && Used < Size; p++)
        {
            Buffer[Used++] = *p;
        }
    }
    for (; *Message != '\0' && Used < Size; Message++)
    {
        Buffer[Used++] = (unsigned char)*Message;
    }
    if (NewLine && Used < Size)
    {
        Buffer[Used++] = '\n';
    }
    Buffer[Used < Size ? Used : Size - 1] = 0;
}

EXTERN_C errno_t __cdecl _NtCRT__strerror_s(char* buffer, size_t size, const char* str)
{
    if (buffer == NULL || size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    _NtCRT_FormatStrerror(buffer, size, str, false);
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT___wcserror_s(wchar_t* buffer, size_t count, const wchar_t* str)
{
    if (buffer == NULL || count == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    _NtCRT_FormatStrerror(buffer, count, str, false);
    return 0;
}

#define NTCRT_WCSERROR_SIZE 256

EXTERN_C wchar_t* __cdecl _NtCRT___wcserror(const wchar_t* str)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();

    if (Ptd->WcserrorBuffer == NULL)
    {
        Ptd->WcserrorBuffer = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, NTCRT_WCSERROR_SIZE * sizeof(wchar_t));
        if (Ptd->WcserrorBuffer == NULL)
        {
            NTCRT_ERRNO = ENOMEM;
            return NULL;
        }
    }
    _NtCRT_FormatStrerror(Ptd->WcserrorBuffer, NTCRT_WCSERROR_SIZE, str, true);
    return Ptd->WcserrorBuffer;
}

/* Tokenizers */

template<typename TChar>
static
TChar*
_NtCRT_Tokenize(
    _In_opt_ TChar* Str,
    _In_ const TChar* Delimiters,
    _Inout_ TChar** Context)
{
    TChar* Token;

    if (Str == NULL)
    {
        Str = *Context;
        if (Str == NULL)
        {
            return NULL;
        }
    }
    /* Skip leading delimiters */
    for (; *Str != 0; Str++)
    {
        const TChar* d = Delimiters;
        while (*d != 0 && *d != *Str)
        {
            d++;
        }
        if (*d == 0)
        {
            break;
        }
    }
    if (*Str == 0)
    {
        *Context = Str;
        return NULL;
    }
    Token = Str;
    for (; *Str != 0; Str++)
    {
        const TChar* d = Delimiters;
        while (*d != 0 && *d != *Str)
        {
            d++;
        }
        if (*d != 0)
        {
            *Str++ = 0;
            break;
        }
    }
    *Context = Str;
    return Token;
}

EXTERN_C char* __cdecl _NtCRT_strtok_s(char* str, const char* delim, char** context)
{
    if (delim == NULL || context == NULL || (str == NULL && *context == NULL))
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    return _NtCRT_Tokenize(str, delim, context);
}

EXTERN_C char* __cdecl _NtCRT_strtok(char* str, const char* delim)
{
    return _NtCRT_Tokenize(str, delim, &_NtCRT_GetPtd()->StrtokContext);
}

EXTERN_C wchar_t* __cdecl _NtCRT_wcstok_s(wchar_t* str, const wchar_t* delim, wchar_t** context)
{
    if (delim == NULL || context == NULL || (str == NULL && *context == NULL))
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    return _NtCRT_Tokenize(str, delim, context);
}

EXTERN_C wchar_t* __cdecl _NtCRT_wcstok(wchar_t* str, const wchar_t* delim)
{
    return _NtCRT_Tokenize(str, delim, &_NtCRT_GetPtd()->WcstokContext);
}

/* Collation of the "C" locale */

EXTERN_C int __cdecl _NtCRT_strcoll(const char* s1, const char* s2)
{
    return strcmp(s1, s2);
}

EXTERN_C int __cdecl _NtCRT__stricoll(const char* s1, const char* s2)
{
    return _stricmp(s1, s2);
}

EXTERN_C int __cdecl _NtCRT__strncoll(const char* s1, const char* s2, size_t n)
{
    return strncmp(s1, s2, n);
}

EXTERN_C int __cdecl _NtCRT__strnicoll(const char* s1, const char* s2, size_t n)
{
    return _strnicmp(s1, s2, n);
}

EXTERN_C int __cdecl _NtCRT_wcscoll(const wchar_t* s1, const wchar_t* s2)
{
    return wcscmp(s1, s2);
}

EXTERN_C int __cdecl _NtCRT__wcsicoll(const wchar_t* s1, const wchar_t* s2)
{
    return _wcsicmp(s1, s2);
}

EXTERN_C int __cdecl _NtCRT__wcsncoll(const wchar_t* s1, const wchar_t* s2, size_t n)
{
    return wcsncmp(s1, s2, n);
}

EXTERN_C int __cdecl _NtCRT__wcsnicoll(const wchar_t* s1, const wchar_t* s2, size_t n)
{
    return _wcsnicmp(s1, s2, n);
}

EXTERN_C size_t __cdecl _NtCRT_strxfrm(char* dst, const char* src, size_t n)
{
    size_t Length = strlen(src);

    if (Length < n)
    {
        memcpy(dst, src, Length + 1);
    }
    return Length;
}

EXTERN_C size_t __cdecl _NtCRT_wcsxfrm(wchar_t* dst, const wchar_t* src, size_t n)
{
    size_t Length = wcslen(src);

    if (Length < n)
    {
        memcpy(dst, src, (Length + 1) * sizeof(wchar_t));
    }
    return Length;
}

/* Character class tables of the "C" locale */

struct NTCRT_CTYPE_TABLE
{
    unsigned short Table[257];
};

static
constexpr
unsigned short
_NtCRT_CtypeOf(
    unsigned int c)
{
    if (c >= '\t' && c <= '\r')
    {
        return _SPACE | _CONTROL;
    } else if (c < 0x20 || c == 0x7F)
    {
        return _CONTROL;
    } else if (c == ' ')
    {
        return _SPACE | _BLANK;
    } else if (c >= '0' && c <= '9')
    {
        return _DIGIT | _HEX;
    } else if (c >= 'A' && c <= 'Z')
    {
        return c <= 'F' ? _UPPER | _HEX : _UPPER;
    } else if (c >= 'a' && c <= 'z')
    {
        return c <= 'f' ? _LOWER | _HEX : _LOWER;
    } else if (c < 0x7F)
    {
        return _PUNCT;
    }
    return 0;
}

/* Latin-1 classes of the wide table, a tab is blank in the wide table only */
static
constexpr
unsigned short
_NtCRT_WctypeOf(
    unsigned int c)
{
    if (c == '\t')
    {
        return _SPACE | _CONTROL | _BLANK;
    } else if (c < 0x80)
    {
        return _NtCRT_CtypeOf(c);
    } else if (c < 0xA0)
    {
        return _CONTROL;
    } else if (c == 0xA0)
    {
        return _SPACE | _BLANK;
    } else if (c == 0xAA || c == 0xB5 || c == 0xBA || c == 0xDF || (c >= 0xE0 && c != 0xF7))
    {
        return _LOWER | _ALPHA;
    } else if (c >= 0xC0 && c != 0xD7 && c <= 0xDE)
    {
        return _UPPER | _ALPHA;
    } else if (c == 0xB2 || c == 0xB3 || c == 0xB9)
    {
        return _DIGIT | _PUNCT;
    }
    return _PUNCT;
}

static
constexpr
NTCRT_CTYPE_TABLE
_NtCRT_MakeCtype(
    bool Wide)
{
    NTCRT_CTYPE_TABLE t {};
    t.Table[0] = 0;
    for (unsigned int c = 0; c < 256; c++)
    {
        t.Table[c + 1] = Wide ? _NtCRT_WctypeOf(c) : _NtCRT_CtypeOf(c);
    }
    return t;
}

static constexpr NTCRT_CTYPE_TABLE g_Ctype = _NtCRT_MakeCtype(false);
static constexpr NTCRT_CTYPE_TABLE g_Wctype = _NtCRT_MakeCtype(true);

extern "C"
{
    unsigned short _NtCRT_ctype[257];
    unsigned short* _NtCRT_pctype = _NtCRT_ctype + 1;
    extern const NTCRT_CTYPE_TABLE _NtCRT_wctype;
    const NTCRT_CTYPE_TABLE _NtCRT_wctype = g_Wctype;
    unsigned short* _NtCRT_pwctype = (unsigned short*)_NtCRT_wctype.Table + 1;
}

/* _ctype is writable data in msvcrt, copy the constant table at startup */
static
void
__cdecl
_NtCRT_InitCtype(void)
{
    memcpy(_NtCRT_ctype, g_Ctype.Table, sizeof(_NtCRT_ctype));
}

extern "C" __attribute__((section(".CRT$XIC"), used)) void(__cdecl* const _NtCRT_InitCtypeEntry)(void) =
    _NtCRT_InitCtype;

EXTERN_C unsigned short* __cdecl _NtCRT___pctype_func(void)
{
    if (_NtCRT_ctype['A' + 1] == 0)
    {
        _NtCRT_InitCtype();
    }
    return _NtCRT_pctype;
}

EXTERN_C unsigned short* __cdecl _NtCRT___pwctype_func(void)
{
    return _NtCRT_pwctype;
}

EXTERN_C unsigned short** __cdecl _NtCRT___p__pctype(void)
{
    _NtCRT___pctype_func();
    return &_NtCRT_pctype;
}

EXTERN_C unsigned short** __cdecl _NtCRT___p__pwctype(void)
{
    return &_NtCRT_pwctype;
}

EXTERN_C int __cdecl _NtCRT__isctype(int c, int mask)
{
    if (c >= -1 && c <= 255)
    {
        return g_Ctype.Table[c + 1] & mask;
    }
    /* No multibyte character in the "C" locale */
    return 0;
}

EXTERN_C int __cdecl _NtCRT_isblank(int c)
{
    return c == ' ' || c == '\t';
}

EXTERN_C int __cdecl _NtCRT_iswblank(wint_t c)
{
    return c == L' ' || c == L'\t' || c == 0xA0 || c == 0x3000;
}

EXTERN_C int __cdecl _NtCRT___isascii(int c)
{
    return (unsigned int)c < 0x80;
}

EXTERN_C int __cdecl _NtCRT___toascii(int c)
{
    return c & 0x7F;
}

EXTERN_C int __cdecl _NtCRT___iscsym(int c)
{
    return (c >= 0 && c <= 255 && (g_Ctype.Table[c + 1] & (_ALPHA | _DIGIT))) || c == '_';
}

EXTERN_C int __cdecl _NtCRT___iscsymf(int c)
{
    return (c >= 0 && c <= 255 && (g_Ctype.Table[c + 1] & _ALPHA)) || c == '_';
}

EXTERN_C int __cdecl _NtCRT__tolower(int c)
{
    return c - 'A' + 'a';
}

EXTERN_C int __cdecl _NtCRT__toupper(int c)
{
    return c - 'a' + 'A';
}

/* As msvcrt, a single byte valid in the ANSI code page, even if mbtowc of the "C" locale takes any byte */
EXTERN_C int __cdecl _NtCRT_mblen(const char* s, size_t n)
{
    if (s == NULL || n == 0 || *s == '\0')
    {
        return 0;
    }
    return _NtCRT_MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, s, 1, NULL, 0) == 0 ? -1 : 1;
}

/* String manipulation, byte or wchar_t wise as msvcrt */

template<typename TChar>
static
TChar*
_NtCRT_StrRev(
    _Inout_z_ TChar* str)
{
    TChar *Left = str, *Right = str;

    if (*Right != 0)
    {
        while (Right[1] != 0)
        {
            Right++;
        }
        for (; Left < Right; Left++, Right--)
        {
            TChar c = *Left;
            *Left = *Right;
            *Right = c;
        }
    }
    return str;
}

template<typename TChar>
static
TChar*
_NtCRT_StrNSet(
    _Inout_z_ TChar* str,
    _In_ TChar c,
    _In_ size_t count)
{
    for (TChar* p = str; count > 0 && *p != 0; p++, count--)
    {
        *p = c;
    }
    return str;
}

EXTERN_C char* __cdecl _NtCRT__strrev(char* str)
{
    return _NtCRT_StrRev(str);
}

EXTERN_C wchar_t* __cdecl _NtCRT__wcsrev(wchar_t* str)
{
    return _NtCRT_StrRev(str);
}

EXTERN_C char* __cdecl _NtCRT__strset(char* str, int c)
{
    return _NtCRT_StrNSet(str, (char)c, SIZE_MAX);
}

EXTERN_C wchar_t* __cdecl _NtCRT__wcsset(wchar_t* str, wchar_t c)
{
    return _NtCRT_StrNSet(str, c, SIZE_MAX);
}

EXTERN_C char* __cdecl _NtCRT__strnset(char* str, int c, size_t count)
{
    return _NtCRT_StrNSet(str, (char)c, count);
}

EXTERN_C wchar_t* __cdecl _NtCRT__wcsnset(wchar_t* str, wchar_t c, size_t count)
{
    return _NtCRT_StrNSet(str, c, count);
}

EXTERN_C void __cdecl _NtCRT__swab(char* src, char* dest, int n)
{
    for (; n > 1; n -= 2)
    {
        char c = *src++;
        *dest++ = *src++;
        *dest++ = c;
    }
}

/*
 * Case mapping and character classes of the "C" locale. ntdll maps case by the ANSI code page and classifies
 * wide characters up to 0xFF only, msvcrt maps ASCII only and classifies the others by GetStringTypeW.
 */

EXTERN_C BOOL WINAPI _CRT_GetStringTypeW(DWORD dwInfoType, LPCWCH lpSrcStr, int cchSrc, LPWORD lpCharType);

EXTERN_C int __cdecl _NtCRT_toupper(int c)
{
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

EXTERN_C int __cdecl _NtCRT_tolower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

EXTERN_C wint_t __cdecl _NtCRT_towupper(wint_t c)
{
    return c >= L'a' && c <= L'z' ? c - (L'a' - L'A') : c;
}

EXTERN_C wint_t __cdecl _NtCRT_towlower(wint_t c)
{
    return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c;
}

EXTERN_C int __cdecl _NtCRT_iswctype(wint_t c, wctype_t mask)
{
    WORD Type;

    if (c == WEOF)
    {
        return 0;
    }
    if (c < 256)
    {
        return _NtCRT_pwctype[c] & mask;
    }
    return _CRT_GetStringTypeW(CT_CTYPE1, (LPCWCH)&c, 1, &Type) ? Type & mask : 0;
}

EXTERN_C int __cdecl _NtCRT_iswalpha(wint_t c)
{
    return _NtCRT_iswctype(c, _ALPHA);
}

EXTERN_C int __cdecl _NtCRT_iswalnum(wint_t c)
{
    return _NtCRT_iswctype(c, _ALPHA | _DIGIT);
}

EXTERN_C int __cdecl _NtCRT_iswdigit(wint_t c)
{
    return _NtCRT_iswctype(c, _DIGIT);
}

EXTERN_C int __cdecl _NtCRT_iswxdigit(wint_t c)
{
    return _NtCRT_iswctype(c, _HEX);
}

EXTERN_C int __cdecl _NtCRT_iswlower(wint_t c)
{
    return _NtCRT_iswctype(c, _LOWER);
}

EXTERN_C int __cdecl _NtCRT_iswupper(wint_t c)
{
    return _NtCRT_iswctype(c, _UPPER);
}

EXTERN_C int __cdecl _NtCRT_iswspace(wint_t c)
{
    return _NtCRT_iswctype(c, _SPACE);
}

EXTERN_C int __cdecl _NtCRT_iswpunct(wint_t c)
{
    return _NtCRT_iswctype(c, _PUNCT);
}

EXTERN_C int __cdecl _NtCRT_iswcntrl(wint_t c)
{
    return _NtCRT_iswctype(c, _CONTROL);
}

EXTERN_C int __cdecl _NtCRT_iswgraph(wint_t c)
{
    return _NtCRT_iswctype(c, _PUNCT | _ALPHA | _DIGIT);
}

EXTERN_C int __cdecl _NtCRT_iswprint(wint_t c)
{
    return _NtCRT_iswctype(c, _BLANK | _PUNCT | _ALPHA | _DIGIT);
}

EXTERN_C int __cdecl _NtCRT_iswascii(wint_t c)
{
    return c < 0x80;
}

EXTERN_C int __cdecl _NtCRT_isleadbyte(int c)
{
    return _NtCRT_pctype[(unsigned char)c] & _LEADBYTE;
}

/* Multibyte characters of the "C" locale are bytes, as msvcrt */

EXTERN_C int __cdecl _NtCRT_mbtowc(wchar_t* pwc, const char* s, size_t n)
{
    if (s == NULL || n == 0)
    {
        return 0;
    }
    if (pwc != NULL)
    {
        *pwc = (unsigned char)*s;
    }
    return *s != '\0';
}

EXTERN_C int __cdecl _NtCRT_wctomb(char* s, wchar_t wc)
{
    if (s == NULL)
    {
        return 0;
    }
    if (wc > 0xFF)
    {
        NTCRT_ERRNO = EILSEQ;
        return -1;
    }
    *s = (char)wc;
    return 1;
}

EXTERN_C size_t __cdecl _NtCRT_mbstowcs(wchar_t* dest, const char* src, size_t count)
{
    size_t i;

    if (src == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return (size_t)-1;
    }
    if (dest == NULL)
    {
        return strlen(src);
    }
    for (i = 0; i < count; i++)
    {
        dest[i] = (unsigned char)src[i];
        if (src[i] == 0)
        {
            break;
        }
    }
    return i;
}

EXTERN_C size_t __cdecl _NtCRT_wcstombs(char* dest, const wchar_t* src, size_t count)
{
    size_t i;

    if (src == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return (size_t)-1;
    }
    for (i = 0; dest == NULL || i < count; i++)
    {
        if (src[i] > 0xFF)
        {
            NTCRT_ERRNO = EILSEQ;
            return (size_t)-1;
        }
        if (dest != NULL)
        {
            dest[i] = (char)src[i];
        }
        if (src[i] == 0)
        {
            break;
        }
    }
    return i;
}

EXTERN_C errno_t __cdecl _NtCRT_wctomb_s(int* pRetValue, char* mbchar, size_t sizeInBytes, wchar_t wchar)
{
    if (mbchar == NULL && sizeInBytes > 0)
    {
        if (pRetValue != NULL)
        {
            *pRetValue = -1;
        }
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (mbchar == NULL)
    {
        if (pRetValue != NULL)
        {
            *pRetValue = 0;
        }
        return 0;
    }
    if (wchar > 0xFF)
    {
        *mbchar = '\0';
        if (pRetValue != NULL)
        {
            *pRetValue = -1;
        }
        NTCRT_ERRNO = EILSEQ;
        return EILSEQ;
    }
    if (sizeInBytes < 1)
    {
        if (pRetValue != NULL)
        {
            *pRetValue = -1;
        }
        return _NtCRT_InvalidParameter(ERANGE);
    }
    *mbchar = (char)wchar;
    if (pRetValue != NULL)
    {
        *pRetValue = 1;
    }
    return 0;
}

/* <inttypes.h> */

EXTERN_C intmax_t __cdecl _NtCRT_strtoimax(const char* str, char** end, int base)
{
    return _NtCRT_strtoll(str, end, base);
}

EXTERN_C uintmax_t __cdecl _NtCRT_strtoumax(const char* str, char** end, int base)
{
    return _NtCRT_strtoull(str, end, base);
}

EXTERN_C intmax_t __cdecl _NtCRT_imaxabs(intmax_t n)
{
    return n < 0 ? -(uintmax_t)n : n;
}

/* Returns _OVERFLOW or _UNDERFLOW of <math.h> on range errors */
EXTERN_C int __cdecl _NtCRT__atodbl(_CRT_DOUBLE* value, char* str)
{
    int Saved = NTCRT_ERRNO, Ret = 0;

    NTCRT_ERRNO = 0;
    value->x = __mingw_strtod(str, NULL);
    if (NTCRT_ERRNO == ERANGE)
    {
        Ret = value->x == 0 ? _UNDERFLOW : _OVERFLOW;
    }
    NTCRT_ERRNO = Saved;
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__atoldbl(_LDOUBLE* value, char* str)
{
    int Saved = NTCRT_ERRNO, Ret = 0;
    long double x;

    NTCRT_ERRNO = 0;
    x = __mingw_strtold(str, NULL);
    if (NTCRT_ERRNO == ERANGE)
    {
        Ret = x == 0 ? _UNDERFLOW : _OVERFLOW;
    }
    memcpy(value->ld, &x, sizeof(value->ld));
    NTCRT_ERRNO = Saved;
    return Ret;
}

/* Flags of __STRINGTOLD */
#define NTCRT_SLD_UNDERFLOW 1
#define NTCRT_SLD_OVERFLOW 2
#define NTCRT_SLD_NODIGITS 4

/* The conversion of _atoldbl with the end of the number, mult12 is not used by msvcrt */
EXTERN_C unsigned int __cdecl _NtCRT___STRINGTOLD(_LDOUBLE* pld, char** end, const char* str, int mult12)
{
    int Saved = NTCRT_ERRNO;
    unsigned int Ret = 0;
    char* End;
    long double x;

    UNREFERENCED_PARAMETER(mult12);
    NTCRT_ERRNO = 0;
    x = __mingw_strtold(str, &End);
    if (End == str)
    {
        Ret = NTCRT_SLD_NODIGITS;
    } else if (NTCRT_ERRNO == ERANGE)
    {
        Ret = x == 0 ? NTCRT_SLD_UNDERFLOW : NTCRT_SLD_OVERFLOW;
    }
    memcpy(pld->ld, &x, sizeof(pld->ld));
    if (end != NULL)
    {
        *end = End;
    }
    NTCRT_ERRNO = Saved;
    return Ret;
}

/* Random numbers */

EXTERN_C void __cdecl _NtCRT_srand(unsigned int seed)
{
    /* The seed is stored xor 1, a zeroed per-thread data starts with seed 1 */
    _NtCRT_GetPtd()->RandSeed = seed ^ 1;
}

EXTERN_C int __cdecl _NtCRT_rand(void)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();
    unsigned int Seed = (Ptd->RandSeed ^ 1) * 214013 + 2531011;

    Ptd->RandSeed = Seed ^ 1;
    return (Seed >> 16) & RAND_MAX;
}

/* \Device\KsecDD, IOCTL_KSEC_RNG */
#define NTCRT_IOCTL_KSEC_RNG CTL_CODE(0x39, 1, METHOD_BUFFERED, FILE_ANY_ACCESS)

static
BOOL
_NtCRT_GenRandom(
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    static HANDLE KsecDD;
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;

    if (KsecDD == NULL)
    {
        static WCHAR Name[] = L"\\Device\\KsecDD";
        UNICODE_STRING DeviceName;
        OBJECT_ATTRIBUTES ObjectAttributes;
        HANDLE Handle;

        _Inline_RtlInitUnicodeString(&DeviceName, Name);
        InitializeObjectAttributes(&ObjectAttributes, &DeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);
        Status = NtOpenFile(&Handle,
                            FILE_READ_DATA | SYNCHRONIZE,
                            &ObjectAttributes,
                            &IoStatusBlock,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            FILE_SYNCHRONOUS_IO_NONALERT);
        if (!NT_SUCCESS(Status))
        {
            return FALSE;
        }
        if (_InterlockedCompareExchangePointer(&KsecDD, Handle, NULL) != NULL)
        {
            NtClose(Handle);
        }
    }
    Status = NtDeviceIoControlFile(KsecDD,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   NTCRT_IOCTL_KSEC_RNG,
                                   NULL,
                                   0,
                                   Buffer,
                                   Length);
    return NT_SUCCESS(Status) && IoStatusBlock.Information == Length;
}

EXTERN_C int __cdecl _NtCRT_rand_s(unsigned int* value)
{
    if (value == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    if (!_NtCRT_GenRandom(value, sizeof(*value)))
    {
        *value = 0;
        NTCRT_ERRNO = ENOMEM;
        return ENOMEM;
    }
    return 0;
}

NTCRT_IMPORT(strtol, _NtCRT_strtol);
NTCRT_IMPORT(strtoul, _NtCRT_strtoul);
NTCRT_IMPORT(strtoll, _NtCRT_strtoll);
NTCRT_IMPORT(_strtoi64, _NtCRT_strtoll);
NTCRT_IMPORT(strtoull, _NtCRT_strtoull);
NTCRT_IMPORT(_strtoui64, _NtCRT_strtoull);
NTCRT_IMPORT(wcstol, _NtCRT_wcstol);
NTCRT_IMPORT(wcstoul, _NtCRT_wcstoul);
NTCRT_IMPORT_ONLY(wcstoll, _NtCRT_wcstoll);
NTCRT_IMPORT(_wcstoi64, _NtCRT_wcstoll);
NTCRT_IMPORT_ONLY(wcstoull, _NtCRT_wcstoull);
NTCRT_IMPORT(_wcstoui64, _NtCRT_wcstoull);
NTCRT_IMPORT(atoll, _NtCRT_atoll);
NTCRT_IMPORT(_wtoll, _NtCRT__wtoll);
NTCRT_IMPORT(strtod, _NtCRT_strtod);
NTCRT_IMPORT(wcstod, _NtCRT_wcstod);
NTCRT_IMPORT(atof, _NtCRT_atof);
NTCRT_IMPORT(_wtof, _NtCRT__wtof);
NTCRT_IMPORT(llabs, _NtCRT_llabs);
NTCRT_IMPORT(_abs64, _NtCRT_llabs);
NTCRT_IMPORT(div, _NtCRT_div);
NTCRT_IMPORT(ldiv, _NtCRT_ldiv);
NTCRT_IMPORT_DATA(_sys_errlist, _NtCRT_sys_errlist);
NTCRT_IMPORT_DATA(_sys_nerr, _NtCRT_sys_nerr);
NTCRT_IMPORT(__sys_errlist, _NtCRT___sys_errlist);
NTCRT_IMPORT(__sys_nerr, _NtCRT___sys_nerr);
NTCRT_IMPORT(strerror, _NtCRT_strerror);
NTCRT_IMPORT(strerror_s, _NtCRT_strerror_s);
NTCRT_IMPORT(_strerror, _NtCRT__strerror);
NTCRT_IMPORT(_wcserror, _NtCRT__wcserror);
NTCRT_IMPORT(_wcserror_s, _NtCRT__wcserror_s);
NTCRT_IMPORT(_strerror_s, _NtCRT__strerror_s);
NTCRT_IMPORT(__wcserror_s, _NtCRT___wcserror_s);
NTCRT_IMPORT(__wcserror, _NtCRT___wcserror);
NTCRT_IMPORT(strtok, _NtCRT_strtok);
NTCRT_IMPORT(strtok_s, _NtCRT_strtok_s);
NTCRT_IMPORT(wcstok, _NtCRT_wcstok);
NTCRT_IMPORT(wcstok_s, _NtCRT_wcstok_s);
NTCRT_IMPORT(strcoll, _NtCRT_strcoll);
NTCRT_IMPORT(_stricoll, _NtCRT__stricoll);
NTCRT_IMPORT(_strncoll, _NtCRT__strncoll);
NTCRT_IMPORT(_strnicoll, _NtCRT__strnicoll);
NTCRT_IMPORT(wcscoll, _NtCRT_wcscoll);
NTCRT_IMPORT(_wcsicoll, _NtCRT__wcsicoll);
NTCRT_IMPORT(_wcsncoll, _NtCRT__wcsncoll);
NTCRT_IMPORT(_wcsnicoll, _NtCRT__wcsnicoll);
NTCRT_IMPORT(strxfrm, _NtCRT_strxfrm);
NTCRT_IMPORT(wcsxfrm, _NtCRT_wcsxfrm);
NTCRT_IMPORT_DATA(_ctype, _NtCRT_ctype);
NTCRT_IMPORT_DATA(_pctype, _NtCRT_pctype);
NTCRT_IMPORT_DATA(_pwctype, _NtCRT_pwctype);
NTCRT_IMPORT_DATA(_wctype, _NtCRT_wctype);
NTCRT_IMPORT(__pctype_func, _NtCRT___pctype_func);
NTCRT_IMPORT(__pwctype_func, _NtCRT___pwctype_func);
NTCRT_IMPORT(__p__pctype, _NtCRT___p__pctype);
NTCRT_IMPORT(__p__pwctype, _NtCRT___p__pwctype);
NTCRT_IMPORT(_isctype, _NtCRT__isctype);
NTCRT_IMPORT_ONLY(isblank, _NtCRT_isblank);
NTCRT_IMPORT_ONLY(iswblank, _NtCRT_iswblank);
NTCRT_IMPORT(__isascii, _NtCRT___isascii);
NTCRT_IMPORT(isascii, _NtCRT___isascii);
NTCRT_IMPORT(__toascii, _NtCRT___toascii);
NTCRT_IMPORT(toascii, _NtCRT___toascii);
NTCRT_IMPORT(__iscsym, _NtCRT___iscsym);
NTCRT_IMPORT(__iscsymf, _NtCRT___iscsymf);
NTCRT_IMPORT(_tolower, _NtCRT__tolower);
NTCRT_IMPORT(_toupper, _NtCRT__toupper);
NTCRT_IMPORT(mblen, _NtCRT_mblen);
NTCRT_IMPORT(srand, _NtCRT_srand);
NTCRT_IMPORT(rand, _NtCRT_rand);
NTCRT_IMPORT(rand_s, _NtCRT_rand_s);
NTCRT_IMPORT(_strrev, _NtCRT__strrev);
NTCRT_IMPORT(_wcsrev, _NtCRT__wcsrev);
NTCRT_IMPORT(_strset, _NtCRT__strset);
NTCRT_IMPORT(_wcsset, _NtCRT__wcsset);
NTCRT_IMPORT(_strnset, _NtCRT__strnset);
NTCRT_IMPORT(_wcsnset, _NtCRT__wcsnset);
NTCRT_IMPORT(_swab, _NtCRT__swab);
NTCRT_IMPORT(toupper, _NtCRT_toupper);
NTCRT_IMPORT(tolower, _NtCRT_tolower);
NTCRT_IMPORT(towupper, _NtCRT_towupper);
NTCRT_IMPORT(towlower, _NtCRT_towlower);
NTCRT_IMPORT(iswctype, _NtCRT_iswctype);
NTCRT_IMPORT(iswalpha, _NtCRT_iswalpha);
NTCRT_IMPORT(iswalnum, _NtCRT_iswalnum);
NTCRT_IMPORT(iswdigit, _NtCRT_iswdigit);
NTCRT_IMPORT(iswxdigit, _NtCRT_iswxdigit);
NTCRT_IMPORT(iswlower, _NtCRT_iswlower);
NTCRT_IMPORT(iswupper, _NtCRT_iswupper);
NTCRT_IMPORT(iswspace, _NtCRT_iswspace);
NTCRT_IMPORT(iswpunct, _NtCRT_iswpunct);
NTCRT_IMPORT(iswcntrl, _NtCRT_iswcntrl);
NTCRT_IMPORT(iswgraph, _NtCRT_iswgraph);
NTCRT_IMPORT(iswprint, _NtCRT_iswprint);
NTCRT_IMPORT(iswascii, _NtCRT_iswascii);
NTCRT_IMPORT(isleadbyte, _NtCRT_isleadbyte);
NTCRT_IMPORT(mbtowc, _NtCRT_mbtowc);
NTCRT_IMPORT(mbstowcs, _NtCRT_mbstowcs);
NTCRT_IMPORT(wcstombs, _NtCRT_wcstombs);
NTCRT_IMPORT(wctomb, _NtCRT_wctomb);
NTCRT_IMPORT(wctomb_s, _NtCRT_wctomb_s);
NTCRT_IMPORT(strtoimax, _NtCRT_strtoimax);
NTCRT_IMPORT(strtoumax, _NtCRT_strtoumax);
NTCRT_IMPORT(imaxabs, _NtCRT_imaxabs);
NTCRT_IMPORT(_atodbl, _NtCRT__atodbl);
NTCRT_IMPORT(_atoldbl, _NtCRT__atoldbl);
NTCRT_IMPORT(__STRINGTOLD, _NtCRT___STRINGTOLD);

/* POSIX names */
NTCRT_IMPORT(strrev, _NtCRT__strrev);
NTCRT_IMPORT(wcsrev, _NtCRT__wcsrev);
NTCRT_IMPORT(strset, _NtCRT__strset);
NTCRT_IMPORT(wcsset, _NtCRT__wcsset);
NTCRT_IMPORT(strnset, _NtCRT__strnset);
NTCRT_IMPORT(wcsnset, _NtCRT__wcsnset);
NTCRT_IMPORT(swab, _NtCRT__swab);
NTCRT_IMPORT(stricoll, _NtCRT__stricoll);
NTCRT_IMPORT(wcsicoll, _NtCRT__wcsicoll);
NTCRT_IMPORT(iscsym, _NtCRT___iscsym);
NTCRT_IMPORT(iscsymf, _NtCRT___iscsymf);
NTCRT_IMPORT(is_wctype, _NtCRT_iswctype);
