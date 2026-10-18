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

EXTERN_C long long __cdecl _NtCRT_strtoll(const char* str, char** end, int base);
EXTERN_C unsigned long long __cdecl _NtCRT_strtoull(const char* str, char** end, int base);

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

/* msvcrt: INT_MAX and EINVAL for a NULL source or destination */
EXTERN_C size_t __cdecl _NtCRT_strxfrm(char* dst, const char* src, size_t n)
{
    size_t Length;

    if (src == NULL || (dst == NULL && n != 0))
    {
        NTCRT_ERRNO = EINVAL;
        return INT_MAX;
    }
    Length = strlen(src);
    if (Length < n)
    {
        memcpy(dst, src, Length + 1);
    }
    return Length;
}

EXTERN_C size_t __cdecl _NtCRT_wcsxfrm(wchar_t* dst, const wchar_t* src, size_t n)
{
    size_t Length;

    if (src == NULL || (dst == NULL && n != 0))
    {
        NTCRT_ERRNO = EINVAL;
        return INT_MAX;
    }
    Length = wcslen(src);
    if (Length < n)
    {
        memcpy(dst, src, (Length + 1) * sizeof(wchar_t));
    }
    return Length;
}

extern "C" unsigned short* _NtCRT_pctype;
extern "C" unsigned short* _NtCRT_pwctype;
EXTERN_C unsigned short* __cdecl _NtCRT___pctype_func(void);

EXTERN_C int __cdecl _NtCRT__isctype(int c, int mask)
{
    if (c >= -1 && c <= 255)
    {
        return _NtCRT___pctype_func()[c] & mask;
    }
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
    return (c >= 0 && c <= 255 && (_NtCRT___pctype_func()[c] & (_ALPHA | _DIGIT))) || c == '_';
}

EXTERN_C int __cdecl _NtCRT___iscsymf(int c)
{
    return (c >= 0 && c <= 255 && (_NtCRT___pctype_func()[c] & _ALPHA)) || c == '_';
}

EXTERN_C int __cdecl _NtCRT__tolower(int c)
{
    return c - 'A' + 'a';
}

EXTERN_C int __cdecl _NtCRT__toupper(int c)
{
    return c - 'a' + 'A';
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
 * Case mapping and character classes. ntdll maps case by the ANSI code page and classifies wide characters up
 * to 0xFF only. The "C" locale maps ASCII only as msvcrt, "C.UTF-8" maps wide characters by Unicode simple case
 * mapping, both classify wide characters by GetStringTypeW.
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
    if (c < 0x80)
    {
        return c >= L'a' && c <= L'z' ? c - (L'a' - L'A') : c;
    }
    return c != WEOF && _NtCRT_CurrentCodePage() == CP_UTF8 ? RtlUpcaseUnicodeChar((WCHAR)c) : c;
}

EXTERN_C wint_t __cdecl _NtCRT_towlower(wint_t c)
{
    if (c < 0x80)
    {
        return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c;
    }
    return c != WEOF && _NtCRT_CurrentCodePage() == CP_UTF8 ? RtlDowncaseUnicodeChar((WCHAR)c) : c;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wcsupr(wchar_t* str)
{
    for (wchar_t* p = str; *p != L'\0'; p++)
    {
        *p = (wchar_t)_NtCRT_towupper(*p);
    }
    return str;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wcslwr(wchar_t* str)
{
    for (wchar_t* p = str; *p != L'\0'; p++)
    {
        *p = (wchar_t)_NtCRT_towlower(*p);
    }
    return str;
}

EXTERN_C int __cdecl _NtCRT__wcsnicmp(const wchar_t* s1, const wchar_t* s2, size_t n)
{
    for (; n > 0; n--, s1++, s2++)
    {
        int Diff = (int)_NtCRT_towlower(*s1) - (int)_NtCRT_towlower(*s2);

        if (Diff != 0 || *s1 == L'\0')
        {
            return Diff;
        }
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT__wcsicmp(const wchar_t* s1, const wchar_t* s2)
{
    return _NtCRT__wcsnicmp(s1, s2, SIZE_MAX);
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
NTCRT_IMPORT(_wcsupr, _NtCRT__wcsupr);
NTCRT_IMPORT(_wcslwr, _NtCRT__wcslwr);
NTCRT_IMPORT(_wcsicmp, _NtCRT__wcsicmp);
NTCRT_IMPORT(_wcsnicmp, _NtCRT__wcsnicmp);
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
NTCRT_IMPORT(strtoimax, _NtCRT_strtoimax);
NTCRT_IMPORT(strtoumax, _NtCRT_strtoumax);
NTCRT_IMPORT(imaxabs, _NtCRT_imaxabs);

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
NTCRT_IMPORT(wcsicmp, _NtCRT__wcsicmp);
NTCRT_IMPORT(wcscmpi, _NtCRT__wcsicmp);
NTCRT_IMPORT(wcsnicmp, _NtCRT__wcsnicmp);
NTCRT_IMPORT(wcslwr, _NtCRT__wcslwr);
NTCRT_IMPORT(wcsupr, _NtCRT__wcsupr);
