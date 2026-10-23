/*
 * msvcrt miscellaneous functions: floating-point to string conversion, bit rotation, searching and sorting with
 * context, path composition, drives, heap walking, file region locking, DLL loading and legacy runtime services
 */

#include "NtCRT.h"

#include <dos.h>
#include <errno.h>
#include <malloc.h>
#include <math.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/locking.h>
#include <time.h>

#include "SplitPath.inl"

EXTERN_C int __cdecl _NtCRT__access(const char* path, int mode);
EXTERN_C int __cdecl _NtCRT__waccess(const wchar_t* path, int mode);
EXTERN_C char* __cdecl _NtCRT__fullpath(char* absPath, const char* relPath, size_t maxLength);
EXTERN_C wchar_t* __cdecl _NtCRT__wfullpath(wchar_t* absPath, const wchar_t* relPath, size_t maxLength);
EXTERN_C char* __cdecl _NtCRT__getcwd(char* buffer, int maxlen);
EXTERN_C wchar_t* __cdecl _NtCRT__wgetcwd(wchar_t* buffer, int maxlen);
EXTERN_C char* __cdecl _NtCRT_getenv(const char* name);
EXTERN_C wchar_t* __cdecl _NtCRT__wgetenv(const wchar_t* name);
EXTERN_C int __cdecl _NtCRT__heapchk(void);
EXTERN_C size_t __cdecl _NtCRT_strftime(char* s, size_t max, const char* format, const struct tm* tm);
EXTERN_C __attribute__((noreturn)) void __cdecl _NtCRT__exit(int code);
EXTERN_C VOID WINAPI _K32_Sleep(DWORD dwMilliseconds);
EXTERN_C DWORD WINAPI _K32_GetLastError(VOID);
EXTERN_C HMODULE WINAPI _K32_LoadLibraryA(LPCSTR lpLibFileName);
EXTERN_C FARPROC WINAPI _K32_GetProcAddress(HMODULE hModule, LPCSTR lpProcName);
EXTERN_C BOOL WINAPI _K32_FreeLibrary(HMODULE hLibModule);
EXTERN_C VOID WINAPI _K32_GetLocalTime(LPSYSTEMTIME lpSystemTime);
EXTERN_C DWORD WINAPI _K32_GetTimeZoneInformation(LPTIME_ZONE_INFORMATION lpTimeZoneInformation);

#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif

#ifndef SE_SYSTEMTIME_PRIVILEGE
#define SE_SYSTEMTIME_PRIVILEGE 12
#endif

#pragma region Floating-point to string

#define NTCRT_CVTBUFSIZE 349 /* _CVTBUFSIZE */

/*
 * Significant digits of Value as msvcrt has them: 17 digits correctly rounded, none for zero, or "1#INF",
 * "1#IND", "1#QNAN" and "1#SNAN" for the special values. Decpt is the position of the decimal point.
 */
static
void
_NtCRT_CvtDigits(
    _In_ double Value,
    _Out_writes_z_(18) char* Digits,
    _Out_ int* Decpt)
{
    unsigned long long Bits;
    char Text[NTCRT_FLOAT_DIGITS];
    int Exponent, nd;

    memcpy(&Bits, &Value, sizeof(Bits));
    if ((Bits & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL)
    {
        unsigned long long Mantissa = Bits & 0x000FFFFFFFFFFFFFULL;

        if (Mantissa == 0)
        {
            strcpy(Digits, "1#INF");
        } else if ((Mantissa & 0x0008000000000000ULL) == 0)
        {
            strcpy(Digits, "1#SNAN");
        } else
        {
            strcpy(Digits, (Bits >> 63) != 0 && Mantissa == 0x0008000000000000ULL ? "1#IND" : "1#QNAN");
        }
        *Decpt = 1;
        return;
    }
    if ((Bits << 1) == 0)
    {
        Digits[0] = '\0';
        *Decpt = 0;
        return;
    }

    /* The digits of %.16e */
    Exponent = (int)(Bits >> 52) & 0x7FF;
    nd = _NtCRT_FloatDigits(Exponent == 0 ? Bits & 0x000FFFFFFFFFFFFFULL : (Bits & 0x000FFFFFFFFFFFFFULL) | 1ULL << 52,
                            Exponent == 0 ? -1074 : Exponent - 1075,
                            'e',
                            16,
                            Text,
                            sizeof(Text),
                            Decpt);
    memcpy(Digits, Text, (size_t)nd);
    memset(Digits + nd, '0', (size_t)(17 - nd));
    Digits[17] = '\0';
}

/*
 * Round Digits to Count characters into Buffer, padded with '0'. The special values are rounded like digits,
 * as msvcrt does. Returns true if the carry overflowed, Decpt is incremented and Buffer is "1" and zeros then.
 */
static
bool
_NtCRT_CvtRound(
    _In_z_ const char* Digits,
    _In_ int Count,
    _Out_writes_z_(Count + 1) char* Buffer,
    _Inout_ int* Decpt)
{
    int Length = (int)strlen(Digits);
    int i;

    for (i = 0; i < Count; i++)
    {
        Buffer[i] = i < Length ? Digits[i] : '0';
    }
    Buffer[Count] = '\0';
    if (Count >= Length || Digits[Count] < '5')
    {
        return false;
    }
    for (i = Count - 1; i >= 0 && Buffer[i] == '9'; i--)
    {
        Buffer[i] = '0';
    }
    if (i >= 0)
    {
        Buffer[i]++;
        return false;
    }
    if (Count > 0)
    {
        Buffer[0] = '1';
    }
    (*Decpt)++;
    return true;
}

/* Count significant digits, Buffer has Count + 1 characters */
static
void
_NtCRT_Ecvt(
    _In_ double Value,
    _In_ int Count,
    _Out_writes_z_(Count + 1) char* Buffer,
    _Out_ int* Decpt,
    _Out_ int* Sign)
{
    char Digits[18];

    _NtCRT_CvtDigits(Value, Digits, Decpt);
    *Sign = signbit(Value) ? 1 : 0;
    _NtCRT_CvtRound(Digits, Count, Buffer, Decpt);
}

/* Count digits after the decimal point, Buffer has NTCRT_CVTBUFSIZE characters */
static
void
_NtCRT_Fcvt(
    _In_ double Value,
    _In_ int Count,
    _Out_writes_z_(NTCRT_CVTBUFSIZE) char* Buffer,
    _Out_ int* Decpt,
    _Out_ int* Sign)
{
    char Digits[18];
    long long Total;

    _NtCRT_CvtDigits(Value, Digits, Decpt);
    *Sign = signbit(Value) ? 1 : 0;
    Total = (long long)*Decpt + Count;
    if (Total < 0)
    {
        Buffer[0] = '\0';
        return;
    }
    if (Total > NTCRT_CVTBUFSIZE - 2)
    {
        Total = NTCRT_CVTBUFSIZE - 2;
    }
    if (_NtCRT_CvtRound(Digits, (int)Total, Buffer, Decpt))
    {
        /* One more digit before the decimal point */
        memset(Buffer, '0', (size_t)Total + 1);
        Buffer[0] = '1';
        Buffer[Total + 1] = '\0';
    }
}

/* Fixed notation if it fits in Count significant digits, exponential otherwise, without trailing zeros */
static
void
_NtCRT_Gcvt(
    _In_ double Value,
    _In_ int Count,
    _Out_writes_z_(NTCRT_CVTBUFSIZE + 16) char* Buffer)
{
    char Digits[18];
    char Rounded[NTCRT_CVTBUFSIZE];
    char* p = Buffer;
    char* Stop;
    int Decpt;
    int i;

    _NtCRT_CvtDigits(Value, Digits, &Decpt);
    if (signbit(Value))
    {
        *p++ = '-';
    }
    if (Decpt - 1 < -1 || Decpt - 1 > Count - 1)
    {
        int Exponent;

        _NtCRT_CvtRound(Digits, Count, Rounded, &Decpt);
        if (Count > 0)
        {
            *p++ = Rounded[0];
            if (Count > 1)
            {
                *p++ = '.';
                memcpy(p, Rounded + 1, Count - 1);
                p += Count - 1;
            }
        }
        Exponent = Digits[0] == '\0' ? 0 : Decpt - 1;
        *p++ = 'e';
        *p++ = Exponent < 0 ? '-' : '+';
        Exponent = Exponent < 0 ? -Exponent : Exponent;
        *p++ = (char)('0' + Exponent / 100);
        *p++ = (char)('0' + Exponent / 10 % 10);
        *p++ = (char)('0' + Exponent % 10);
    } else
    {
        int Decimals = Count - Decpt;

        if (_NtCRT_CvtRound(Digits, Count, Rounded, &Decpt))
        {
            memset(Rounded, '0', Count + 1);
            Rounded[0] = '1';
            Rounded[Count + 1] = '\0';
        }
        if (Decpt <= 0)
        {
            *p++ = '0';
        } else
        {
            memcpy(p, Rounded, Decpt);
            p += Decpt;
        }
        if (Decimals > 0)
        {
            *p++ = '.';
            for (i = 0; i < Decimals; i++)
            {
                *p++ = Decpt + i < 0 ? '0' : Rounded[Decpt + i];
            }
        }
    }
    *p = '\0';

    /* Remove the trailing zeros of the fraction, keeping the decimal point */
    p = strchr(Buffer, '.');
    if (p != NULL)
    {
        for (Stop = p + 1; *Stop != '\0' && *Stop != 'e'; Stop++)
        {
        }
        for (p = Stop - 1; *p == '0'; p--)
        {
        }
        memmove(p + 1, Stop, strlen(Stop) + 1);
    }
}

static
char*
_NtCRT_CvtBuffer(void)
{
    return (char*)_NtCRT_PtdBuffer((void**)&_NtCRT_GetPtd()->CvtBuffer, NTCRT_CVTBUFSIZE, 0);
}

static
int
_NtCRT_CvtCount(
    _In_ int Count)
{
    return Count < 0 ? 0 : Count > NTCRT_CVTBUFSIZE - 1 ? NTCRT_CVTBUFSIZE - 1 : Count;
}

EXTERN_C char* __cdecl _NtCRT__ecvt(double value, int count, int* dec, int* sign)
{
    char* Buffer = _NtCRT_CvtBuffer();

    if (Buffer == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    _NtCRT_Ecvt(value, _NtCRT_CvtCount(count), Buffer, dec, sign);
    return Buffer;
}

EXTERN_C char* __cdecl _NtCRT__fcvt(double value, int count, int* dec, int* sign)
{
    char* Buffer = _NtCRT_CvtBuffer();

    if (Buffer == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    _NtCRT_Fcvt(value, count, Buffer, dec, sign);
    return Buffer;
}

EXTERN_C char* __cdecl _NtCRT__gcvt(double value, int digits, char* buffer)
{
    char Text[NTCRT_CVTBUFSIZE + 16];

    if (buffer == NULL || digits < 0)
    {
        _NtCRT_InvalidParameter(buffer == NULL ? EINVAL : ERANGE);
        return NULL;
    }
    _NtCRT_Gcvt(value, _NtCRT_CvtCount(digits), Text);
    strcpy(buffer, Text);
    return buffer;
}

EXTERN_C errno_t __cdecl _NtCRT__ecvt_s(char* buffer, size_t sizeInBytes, double value, int count, int* dec, int* sign)
{
    if (buffer == NULL || sizeInBytes == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    buffer[0] = '\0';
    if (dec == NULL || sign == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    count = _NtCRT_CvtCount(count);
    if ((size_t)count >= sizeInBytes)
    {
        return _NtCRT_InvalidParameter(ERANGE);
    }
    _NtCRT_Ecvt(value, count, buffer, dec, sign);
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT__fcvt_s(char* buffer, size_t sizeInBytes, double value, int count, int* dec, int* sign)
{
    char Text[NTCRT_CVTBUFSIZE];

    if (buffer == NULL || sizeInBytes == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    buffer[0] = '\0';
    if (dec == NULL || sign == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    _NtCRT_Fcvt(value, count, Text, dec, sign);
    if (strlen(Text) >= sizeInBytes)
    {
        return _NtCRT_InvalidParameter(ERANGE);
    }
    strcpy(buffer, Text);
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT__gcvt_s(char* buffer, size_t sizeInBytes, double value, int digits)
{
    char Text[NTCRT_CVTBUFSIZE + 16];

    if (buffer == NULL || sizeInBytes == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    buffer[0] = '\0';
    _NtCRT_Gcvt(value, _NtCRT_CvtCount(digits), Text);
    if (strlen(Text) >= sizeInBytes)
    {
        return _NtCRT_InvalidParameter(ERANGE);
    }
    strcpy(buffer, Text);
    return 0;
}

#pragma endregion

#pragma region Bit rotation

EXTERN_C unsigned int __cdecl _NtCRT__rotl(unsigned int value, int shift)
{
    shift &= 31;
    return (value << shift) | (value >> ((32 - shift) & 31));
}

EXTERN_C unsigned int __cdecl _NtCRT__rotr(unsigned int value, int shift)
{
    shift &= 31;
    return (value >> shift) | (value << ((32 - shift) & 31));
}

EXTERN_C unsigned long long __cdecl _NtCRT__rotl64(unsigned long long value, int shift)
{
    shift &= 63;
    return (value << shift) | (value >> ((64 - shift) & 63));
}

EXTERN_C unsigned long long __cdecl _NtCRT__rotr64(unsigned long long value, int shift)
{
    shift &= 63;
    return (value >> shift) | (value << ((64 - shift) & 63));
}

#pragma endregion

#pragma region Searching and sorting

EXTERN_C void* __cdecl _NtCRT__lfind(const void* key,
                                     const void* base,
                                     unsigned int* num,
                                     unsigned int width,
                                     int(__cdecl* compare)(const void*, const void*))
{
    const char* p = (const char*)base;

    for (unsigned int i = 0; i < *num; i++, p += width)
    {
        if (compare(key, p) == 0)
        {
            return (void*)p;
        }
    }
    return NULL;
}

EXTERN_C void* __cdecl _NtCRT__lsearch(
    const void* key, void* base, unsigned int* num, unsigned int width, int(__cdecl* compare)(const void*, const void*))
{
    char* p = (char*)_NtCRT__lfind(key, base, num, width, compare);

    if (p == NULL)
    {
        p = (char*)base + (size_t)*num * width;
        memcpy(p, key, width);
        (*num)++;
    }
    return p;
}

EXTERN_C void* __cdecl _NtCRT__lfind_s(const void* key,
                                       const void* base,
                                       unsigned int* num,
                                       size_t width,
                                       int(__cdecl* compare)(void*, const void*, const void*),
                                       void* context)
{
    const char* p = (const char*)base;

    if (key == NULL || num == NULL || compare == NULL || width == 0 || (base == NULL && *num != 0))
    {
        _NtCRT_InvalidParameter(EINVAL);
        return NULL;
    }
    for (unsigned int i = 0; i < *num; i++, p += width)
    {
        if (compare(context, key, p) == 0)
        {
            return (void*)p;
        }
    }
    return NULL;
}

EXTERN_C void* __cdecl _NtCRT__lsearch_s(const void* key,
                                         void* base,
                                         unsigned int* num,
                                         size_t width,
                                         int(__cdecl* compare)(void*, const void*, const void*),
                                         void* context)
{
    char* p;

    if (key == NULL || num == NULL || compare == NULL || width == 0 || base == NULL)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return NULL;
    }
    p = (char*)_NtCRT__lfind_s(key, base, num, width, compare, context);
    if (p == NULL)
    {
        p = (char*)base + *num * width;
        memcpy(p, key, width);
        (*num)++;
    }
    return p;
}

static
void
_NtCRT_SwapBytes(
    _Inout_updates_bytes_(Width) char* a,
    _Inout_updates_bytes_(Width) char* b,
    _In_ size_t Width)
{
    while (Width-- > 0)
    {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* Heap sort, not stable like the qsort of msvcrt */
EXTERN_C void __cdecl _NtCRT_qsort_s(
    void* base, size_t num, size_t width, int(__cdecl* compare)(void*, const void*, const void*), void* context)
{
    char* Base = (char*)base;
    size_t Start;
    size_t End;
    size_t Root;
    size_t Child;

    if ((base == NULL && num != 0) || width == 0 || compare == NULL)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return;
    }
    if (num < 2)
    {
        return;
    }
    for (Start = num / 2; ; )
    {
        if (Start > 0)
        {
            Root = --Start;
            End = num;
        } else
        {
            End = num - 1;
            if (End == 0)
            {
                break;
            }
            _NtCRT_SwapBytes(Base, Base + End * width, width);
            num = End;
            Root = 0;
        }
        while ((Child = Root * 2 + 1) < End)
        {
            if (Child + 1 < End && compare(context, Base + Child * width, Base + (Child + 1) * width) < 0)
            {
                Child++;
            }
            if (compare(context, Base + Root * width, Base + Child * width) >= 0)
            {
                break;
            }
            _NtCRT_SwapBytes(Base + Root * width, Base + Child * width, width);
            Root = Child;
        }
    }
}

EXTERN_C void* __cdecl _NtCRT_bsearch_s(const void* key,
                                        const void* base,
                                        size_t num,
                                        size_t width,
                                        int(__cdecl* compare)(void*, const void*, const void*),
                                        void* context)
{
    size_t Low = 0;
    size_t High = num;

    if (key == NULL || (base == NULL && num != 0) || width == 0 || compare == NULL)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return NULL;
    }
    while (Low < High)
    {
        size_t Middle = Low + (High - Low) / 2;
        const char* p = (const char*)base + Middle * width;
        int Result = compare(context, key, p);

        if (Result == 0)
        {
            return (void*)p;
        } else if (Result < 0)
        {
            High = Middle;
        } else
        {
            Low = Middle + 1;
        }
    }
    return NULL;
}

#pragma endregion

#pragma region Paths

static size_t _NtCRT_TStrLen(const char* s) { return strlen(s); }
static size_t _NtCRT_TStrLen(const wchar_t* s) { return wcslen(s); }
static int _NtCRT_TAccess(const char* s) { return _NtCRT__access(s, 0); }
static int _NtCRT_TAccess(const wchar_t* s) { return _NtCRT__waccess(s, 0); }
static const char* _NtCRT_TGetenv(const char* s) { return _NtCRT_getenv(s); }
static const wchar_t* _NtCRT_TGetenv(const wchar_t* s) { return _NtCRT__wgetenv(s); }
static char* _NtCRT_TFullpath(char* a, const char* r, size_t n) { return _NtCRT__fullpath(a, r, n); }
static wchar_t* _NtCRT_TFullpath(wchar_t* a, const wchar_t* r, size_t n) { return _NtCRT__wfullpath(a, r, n); }

/* Compose a path into Path, or only measure it if Path is NULL, returns the size with the terminator */
template<typename TChar>
static
size_t
_NtCRT_MakePath(
    _Out_writes_opt_z_(_Inexpressible_(return)) TChar* Path,
    _In_opt_z_ const TChar* Drive,
    _In_opt_z_ const TChar* Dir,
    _In_opt_z_ const TChar* Fname,
    _In_opt_z_ const TChar* Ext)
{
    size_t n = 0;
    auto Put = [&](TChar c)
    {
        if (Path != NULL)
        {
            Path[n] = c;
        }
        n++;
    };

    if (Drive != NULL && *Drive != 0)
    {
        Put(*Drive);
        Put(':');
    }
    if (Dir != NULL && *Dir != 0)
    {
        const TChar* p;

        for (p = Dir; *p != 0; p++)
        {
            Put(*p);
        }
        if (p[-1] != '/' && p[-1] != '\\')
        {
            Put('\\');
        }
    }
    if (Fname != NULL)
    {
        for (const TChar* p = Fname; *p != 0; p++)
        {
            Put(*p);
        }
    }
    if (Ext != NULL && *Ext != 0)
    {
        if (*Ext != '.')
        {
            Put('.');
        }
        for (const TChar* p = Ext; *p != 0; p++)
        {
            Put(*p);
        }
    }
    Put(0);
    return n;
}

template<typename TChar>
static
errno_t
_NtCRT_MakePathS(
    _Out_writes_z_(Size) TChar* Path,
    _In_ size_t Size,
    _In_opt_z_ const TChar* Drive,
    _In_opt_z_ const TChar* Dir,
    _In_opt_z_ const TChar* Fname,
    _In_opt_z_ const TChar* Ext)
{
    if (Path == NULL || Size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (_NtCRT_MakePath<TChar>(NULL, Drive, Dir, Fname, Ext) > Size)
    {
        Path[0] = 0;
        return _NtCRT_InvalidParameter(ERANGE);
    }
    _NtCRT_MakePath(Path, Drive, Dir, Fname, Ext);
    return 0;
}

EXTERN_C void __cdecl _NtCRT__makepath(
    char* path, const char* drive, const char* dir, const char* fname, const char* ext)
{
    _NtCRT_MakePath(path, drive, dir, fname, ext);
}

EXTERN_C void __cdecl _NtCRT__wmakepath(
    wchar_t* path, const wchar_t* drive, const wchar_t* dir, const wchar_t* fname, const wchar_t* ext)
{
    _NtCRT_MakePath(path, drive, dir, fname, ext);
}

EXTERN_C errno_t __cdecl _NtCRT__makepath_s(
    char* path, size_t size, const char* drive, const char* dir, const char* fname, const char* ext)
{
    return _NtCRT_MakePathS(path, size, drive, dir, fname, ext);
}

EXTERN_C errno_t __cdecl _NtCRT__wmakepath_s(
    wchar_t* path, size_t size, const wchar_t* drive, const wchar_t* dir, const wchar_t* fname, const wchar_t* ext)
{
    return _NtCRT_MakePathS(path, size, drive, dir, fname, ext);
}

EXTERN_C void __cdecl _NtCRT__wsplitpath(
    const wchar_t* path, wchar_t* drive, wchar_t* dir, wchar_t* fname, wchar_t* ext)
{
    _NtCRT_SplitPath(path, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT, true, NULL);
}

EXTERN_C errno_t __cdecl _NtCRT__wsplitpath_s(const wchar_t* path,
                                              wchar_t* drive,
                                              size_t driveSize,
                                              wchar_t* dir,
                                              size_t dirSize,
                                              wchar_t* fname,
                                              size_t fnameSize,
                                              wchar_t* ext,
                                              size_t extSize)
{
    return _NtCRT_SplitPathS(path, drive, driveSize, dir, dirSize, fname, fnameSize, ext, extSize, NULL);
}

/* Search FileName in the current directory, then in the directories listed by the environment variable */
template<typename TChar>
static
errno_t
_NtCRT_SearchEnv(
    _In_z_ const TChar* FileName,
    _In_z_ const TChar* VarName,
    _Out_writes_z_(Size) TChar* PathName,
    _In_ size_t Size)
{
    const TChar* List;
    TChar* Candidate;
    size_t NameLength;
    int SavedErrno;

    if (PathName == NULL || Size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    PathName[0] = 0;
    if (FileName == NULL || VarName == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }

    SavedErrno = NTCRT_ERRNO;
    if (_NtCRT_TAccess(FileName) == 0)
    {
        NTCRT_ERRNO = SavedErrno;
        return _NtCRT_TFullpath(PathName, FileName, Size) == NULL ? NTCRT_ERRNO : 0;
    }
    List = _NtCRT_TGetenv(VarName);
    if (List == NULL)
    {
        NTCRT_ERRNO = ENOENT;
        return ENOENT;
    }

    NameLength = _NtCRT_TStrLen(FileName);
    Candidate = (TChar*)RtlAllocateHeap(NTCRT_HEAP, 0, (_NtCRT_TStrLen(List) + NameLength + 2) * sizeof(TChar));
    if (Candidate == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return ENOMEM;
    }
    while (*List != 0)
    {
        size_t n = 0;

        /* "dir1;dir2", an entry may be quoted to contain ';' */
        for (bool Quoted = false; *List != 0 && (Quoted || *List != ';'); List++)
        {
            if (*List == '"')
            {
                Quoted = !Quoted;
            } else
            {
                Candidate[n++] = *List;
            }
        }
        if (*List == ';')
        {
            List++;
        }
        if (n == 0)
        {
            continue;
        }
        if (Candidate[n - 1] != '\\' && Candidate[n - 1] != '/')
        {
            Candidate[n++] = '\\';
        }
        memcpy(Candidate + n, FileName, (NameLength + 1) * sizeof(TChar));
        if (_NtCRT_TAccess(Candidate) == 0)
        {
            errno_t Ret = 0;

            NTCRT_ERRNO = SavedErrno;
            if (_NtCRT_TStrLen(Candidate) >= Size)
            {
                Ret = _NtCRT_InvalidParameter(ERANGE);
            } else
            {
                memcpy(PathName, Candidate, (_NtCRT_TStrLen(Candidate) + 1) * sizeof(TChar));
            }
            RtlFreeHeap(NTCRT_HEAP, 0, Candidate);
            return Ret;
        }
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Candidate);
    NTCRT_ERRNO = ENOENT;
    return ENOENT;
}

EXTERN_C errno_t __cdecl _NtCRT__searchenv_s(const char* filename, const char* varname, char* pathname, size_t size)
{
    return _NtCRT_SearchEnv(filename, varname, pathname, size);
}

EXTERN_C errno_t __cdecl _NtCRT__wsearchenv_s(const wchar_t* filename,
                                              const wchar_t* varname,
                                              wchar_t* pathname,
                                              size_t size)
{
    return _NtCRT_SearchEnv(filename, varname, pathname, size);
}

EXTERN_C void __cdecl _NtCRT__searchenv(const char* filename, const char* varname, char* pathname)
{
    _NtCRT_SearchEnv(filename, varname, pathname, _MAX_PATH);
}

EXTERN_C void __cdecl _NtCRT__wsearchenv(const wchar_t* filename, const wchar_t* varname, wchar_t* pathname)
{
    _NtCRT_SearchEnv(filename, varname, pathname, _MAX_PATH);
}

/* "nameXXXXXX": a letter from 'a' to 'z' that makes the name unused, and the last 5 digits of the thread id */
template<typename TChar>
static
TChar*
_NtCRT_MkTemp(
    _Inout_z_ TChar* Template)
{
    TChar* X;
    size_t Length;
    unsigned int Number;
    int SavedErrno;
    int i;

    if (Template == NULL || (Length = _NtCRT_TStrLen(Template)) < 6)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return NULL;
    }
    X = Template + Length - 6;
    for (i = 0; i < 6; i++)
    {
        if (X[i] != 'X')
        {
            _NtCRT_InvalidParameter(EINVAL);
            return NULL;
        }
    }
    Number = (unsigned int)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;
    for (i = 5; i > 0; i--)
    {
        X[i] = (TChar)('0' + Number % 10);
        Number /= 10;
    }
    SavedErrno = NTCRT_ERRNO;
    for (TChar Letter = 'a'; Letter <= 'z'; Letter++)
    {
        X[0] = Letter;
        if (_NtCRT_TAccess(Template) != 0 && NTCRT_ERRNO != EACCES)
        {
            NTCRT_ERRNO = SavedErrno;
            return Template;
        }
    }
    NTCRT_ERRNO = EEXIST;
    return NULL;
}

EXTERN_C char* __cdecl _NtCRT__mktemp(char* nameTemplate)
{
    return _NtCRT_MkTemp(nameTemplate);
}

EXTERN_C wchar_t* __cdecl _NtCRT__wmktemp(wchar_t* nameTemplate)
{
    return _NtCRT_MkTemp(nameTemplate);
}

/* The template is emptied on failure */
template<typename TChar>
static
errno_t
_NtCRT_MkTempS(
    _Inout_updates_z_(Size) TChar* Template,
    _In_ size_t Size)
{
    size_t Length;

    if (Template == NULL || Size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    for (Length = 0; Length < Size && Template[Length] != 0; Length++)
    {
    }
    if (Length >= Size)
    {
        Template[0] = 0;
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (_NtCRT_MkTemp(Template) == NULL)
    {
        Template[0] = 0;
        return NTCRT_ERRNO;
    }
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT__mktemp_s(char* nameTemplate, size_t sizeInChars)
{
    return _NtCRT_MkTempS(nameTemplate, sizeInChars);
}

EXTERN_C errno_t __cdecl _NtCRT__wmktemp_s(wchar_t* nameTemplate, size_t sizeInChars)
{
    return _NtCRT_MkTempS(nameTemplate, sizeInChars);
}

#pragma endregion

#pragma region Drives

static
void
_NtCRT_InvalidDrive(void)
{
    _NtCRT_GetPtd()->Doserrno = ERROR_INVALID_DRIVE;
    NTCRT_ERRNO = EACCES;
}

EXTERN_C unsigned long __cdecl _NtCRT__getdrives(void)
{
    PROCESS_DEVICEMAP_INFORMATION Info;

    if (!NT_SUCCESS(
            NtQueryInformationProcess(NtCurrentProcess(), ProcessDeviceMap, &Info.Query, sizeof(Info.Query), NULL)))
    {
        return 0;
    }
    return Info.Query.DriveMap;
}

static
bool
_NtCRT_IsValidDrive(
    _In_ int Drive)
{
    return Drive >= 1 && Drive <= 26 && (_NtCRT__getdrives() & (1UL << (Drive - 1))) != 0;
}

/* 1 for A: and so on, 0 if the current directory is not on a drive */
EXTERN_C int __cdecl _NtCRT__getdrive(void)
{
    wchar_t* Path = _NtCRT__wgetcwd(NULL, 0);
    int Drive = 0;

    if (Path == NULL)
    {
        return 0;
    }
    if (Path[0] != L'\0' && Path[1] == L':')
    {
        Drive = (Path[0] | 0x20) - L'a' + 1;
        if (Drive < 1 || Drive > 26)
        {
            Drive = 0;
        }
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Path);
    return Drive;
}

EXTERN_C int __cdecl _NtCRT__chdrive(int drive)
{
    wchar_t Path[3] = { (wchar_t)(L'A' + drive - 1), L':', L'\0' };
    UNICODE_STRING String;

    if (drive < 1 || drive > 26)
    {
        _NtCRT_InvalidDrive();
        return -1;
    }
    RtlInitUnicodeString(&String, Path);
    if (!NT_SUCCESS(RtlSetCurrentDirectory_U(&String)))
    {
        _NtCRT_InvalidDrive();
        return -1;
    }
    return 0;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wgetdcwd(int drive, wchar_t* buffer, int maxlen)
{
    wchar_t Path[4] = { (wchar_t)(L'A' + drive - 1), L':', L'.', L'\0' };

    if (drive == 0)
    {
        return _NtCRT__wgetcwd(buffer, maxlen);
    }
    if (buffer != NULL && maxlen <= 0)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return NULL;
    }
    if (!_NtCRT_IsValidDrive(drive))
    {
        _NtCRT_InvalidDrive();
        return NULL;
    }
    return _NtCRT__wfullpath(buffer, Path, buffer == NULL ? 0 : maxlen);
}

EXTERN_C char* __cdecl _NtCRT__getdcwd(int drive, char* buffer, int maxlen)
{
    char Path[4] = { (char)('A' + drive - 1), ':', '.', '\0' };

    if (drive == 0)
    {
        return _NtCRT__getcwd(buffer, maxlen);
    }
    if (buffer != NULL && maxlen <= 0)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return NULL;
    }
    if (!_NtCRT_IsValidDrive(drive))
    {
        _NtCRT_InvalidDrive();
        return NULL;
    }
    return _NtCRT__fullpath(buffer, Path, buffer == NULL ? 0 : maxlen);
}

/* Like GetDiskFreeSpaceW, returns 0 or the Win32 error */
EXTERN_C unsigned __cdecl _NtCRT__getdiskfree(unsigned drive, struct _diskfree_t* driveinfo)
{
    wchar_t Root[4] = { (wchar_t)(L'A' + drive - 1), L':', L'\\', L'\0' };
    FILE_FS_FULL_SIZE_INFORMATION Info;
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Error;

    if (drive > 26 || driveinfo == NULL)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return ERROR_INVALID_PARAMETER;
    }
    Error = _NtCRT_CreateFile(&Handle,
                              drive == 0 ? L"\\" : Root,
                              FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              FILE_OPEN,
                              FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_FREE_SPACE_QUERY,
                              0,
                              NULL);
    if (Error != ERROR_SUCCESS)
    {
        _NtCRT_SetErrnoFromWin32(Error);
        return Error;
    }
    Status = NtQueryVolumeInformationFile(Handle, &Iosb, &Info, sizeof(Info), FileFsFullSizeInformation);
    NtClose(Handle);
    if (!NT_SUCCESS(Status))
    {
        Error = RtlNtStatusToDosError(Status);
        _NtCRT_SetErrnoFromWin32(Error);
        return Error;
    }
    driveinfo->total_clusters = Info.TotalAllocationUnits.HighPart != 0 ? MAXULONG : Info.TotalAllocationUnits.LowPart;
    driveinfo->avail_clusters =
        Info.CallerAvailableAllocationUnits.HighPart != 0 ? MAXULONG : Info.CallerAvailableAllocationUnits.LowPart;
    driveinfo->sectors_per_cluster = Info.SectorsPerAllocationUnit;
    driveinfo->bytes_per_sector = Info.BytesPerSector;
    return 0;
}

#pragma endregion

#pragma region Heap

/* Next block of the heap after Entry, skipping segments and uncommitted ranges */
static
NTSTATUS
_NtCRT_HeapWalkNext(
    _Inout_ PRTL_HEAP_WALK_ENTRY Entry)
{
    NTSTATUS Status;

    do
    {
        Status = RtlWalkHeap(NTCRT_HEAP, Entry);
    } while (NT_SUCCESS(Status) && (Entry->Flags & (RTL_HEAP_SEGMENT | RTL_HEAP_UNCOMMITTED_RANGE)));
    return Status;
}

/*
 * The CRT heap is the process heap. RtlWalkHeap needs the whole entry it returned to continue, which _HEAPINFO
 * does not hold, so the last entry is kept per thread and the heap is walked again up to an unknown _pentry.
 */
EXTERN_C int __cdecl _NtCRT__heapwalk(_HEAPINFO* entryinfo)
{
    PRTL_HEAP_WALK_ENTRY Entry;
    NTSTATUS Status;

    if (entryinfo == NULL)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return _HEAPBADPTR;
    }
    Entry = &_NtCRT_GetPtd()->HeapWalkEntry;
    if (entryinfo->_pentry == NULL || entryinfo->_pentry != Entry->DataAddress)
    {
        if (entryinfo->_pentry != NULL && entryinfo->_useflag == _USEDENTRY &&
            !RtlValidateHeap(NTCRT_HEAP, 0, entryinfo->_pentry))
        {
            return _HEAPBADNODE;
        }
        RtlZeroMemory(Entry, sizeof(*Entry));
        if (entryinfo->_pentry != NULL)
        {
            do
            {
                Status = _NtCRT_HeapWalkNext(Entry);
                if (!NT_SUCCESS(Status))
                {
                    RtlZeroMemory(Entry, sizeof(*Entry));
                    return _HEAPBADNODE;
                }
            } while (Entry->DataAddress != entryinfo->_pentry);
        }
    }
    Status = _NtCRT_HeapWalkNext(Entry);
    if (!NT_SUCCESS(Status))
    {
        RtlZeroMemory(Entry, sizeof(*Entry));
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            return _HEAPEND;
        }
        return entryinfo->_pentry == NULL ? _HEAPBADBEGIN : _HEAPBADNODE;
    }
    entryinfo->_pentry = (int*)Entry->DataAddress;
    entryinfo->_size = Entry->DataSize;
    entryinfo->_useflag = (Entry->Flags & RTL_HEAP_BUSY) ? _USEDENTRY : _FREEENTRY;
    return _HEAPOK;
}

/* msvcrt only checks the heap */
EXTERN_C int __cdecl _NtCRT__heapset(unsigned int fill)
{
    UNREFERENCED_PARAMETER(fill);
    return _NtCRT__heapchk();
}

EXTERN_C int __cdecl _NtCRT__heapused(size_t* used, size_t* commit)
{
    UNREFERENCED_PARAMETER(used);
    UNREFERENCED_PARAMETER(commit);
    NTCRT_ERRNO = ENOSYS;
    return 0;
}

EXTERN_C int __cdecl _NtCRT__heapadd(void* memblock, size_t size)
{
    UNREFERENCED_PARAMETER(memblock);
    UNREFERENCED_PARAMETER(size);
    NTCRT_ERRNO = ENOSYS;
    return -1;
}

/* No small-block heap, as msvcrt on the system heap */
EXTERN_C size_t __cdecl _NtCRT__get_sbh_threshold(void)
{
    return 0;
}

EXTERN_C int __cdecl _NtCRT__set_sbh_threshold(size_t size)
{
    UNREFERENCED_PARAMETER(size);
    return 0;
}

#pragma endregion

#pragma region File region locking

/* Lock or unlock nbytes from the current position of fd */
EXTERN_C int __cdecl _NtCRT__locking(int fd, int mode, long nbytes)
{
    HANDLE Handle;
    LARGE_INTEGER Offset;
    LARGE_INTEGER Length;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    int Retry;

    if (nbytes < 0)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return -1;
    }
    Handle = (HANDLE)_NtCRT_get_osfhandle(fd);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        return -1;
    }
    Offset.QuadPart = _NtCRT_lseeki64(fd, 0, SEEK_CUR);
    if (Offset.QuadPart < 0)
    {
        return -1;
    }
    Length.QuadPart = nbytes;

    switch (mode)
    {
        case _LK_UNLCK:
            Status = NtUnlockFile(Handle, &Iosb, &Offset, &Length, 0);
            if (!NT_SUCCESS(Status))
            {
                _NtCRT_SetErrnoFromNtStatus(Status);
                return -1;
            }
            return 0;

        case _LK_LOCK:
        case _LK_RLCK:
        case _LK_NBLCK:
        case _LK_NBRLCK:
            /* The blocking modes retry 10 times a second apart */
            for (Retry = (mode == _LK_LOCK || mode == _LK_RLCK) ? 10 : 1; ; )
            {
                Status = NtLockFile(Handle, NULL, NULL, NULL, &Iosb, &Offset, &Length, 0, TRUE, TRUE);
                if (NT_SUCCESS(Status))
                {
                    return 0;
                }
                if (--Retry == 0)
                {
                    break;
                }
                _K32_Sleep(1000);
            }
            _NtCRT_SetErrnoFromNtStatus(Status);
            if (mode == _LK_LOCK || mode == _LK_RLCK)
            {
                NTCRT_ERRNO = EDEADLOCK;
            }
            return -1;

        default:
            _NtCRT_InvalidParameter(EINVAL);
            return -1;
    }
}

#pragma endregion

#pragma region DLL loading

EXTERN_C intptr_t __cdecl _NtCRT__loaddll(char* name)
{
    return (intptr_t)_K32_LoadLibraryA(name);
}

EXTERN_C int __cdecl _NtCRT__unloaddll(intptr_t handle)
{
    return _K32_FreeLibrary((HMODULE)handle) ? 0 : (int)_K32_GetLastError();
}

/* By name with ordinal -1, or by ordinal with no name */
EXTERN_C void* __cdecl _NtCRT__getdllprocaddr(intptr_t handle, char* name, intptr_t ordinal)
{
    if (name == NULL)
    {
        if (ordinal >= 0 && ordinal <= 0xFFFF)
        {
            return (void*)_K32_GetProcAddress((HMODULE)handle, (LPCSTR)ordinal);
        }
    } else if (ordinal == -1)
    {
        return (void*)_K32_GetProcAddress((HMODULE)handle, name);
    }
    return NULL;
}

typedef void(__cdecl* _PVFV)(void);

/* The table of a DLL, which runs it at its detach: *pbegin from malloc, grown by realloc, the functions not encoded */
EXTERN_C _onexit_t __cdecl _NtCRT___dllonexit(_onexit_t func, _PVFV** pbegin, _PVFV** pend)
{
    _onexit_t Result = NULL;
    size_t Count;

    _NtCRT_lock(_EXIT_LOCK1);
    Count = *pend - *pbegin;
    if (RtlSizeHeap(NTCRT_HEAP, 0, *pbegin) < (Count + 1) * sizeof(_PVFV))
    {
        _PVFV* Table = (_PVFV*)RtlReAllocateHeap(NTCRT_HEAP, 0, *pbegin, (Count + 32) * sizeof(_PVFV));

        if (Table == NULL)
        {
            goto _Exit;
        }
        *pbegin = Table;
        *pend = Table + Count;
    }
    *(*pend)++ = (_PVFV)func;
    Result = func;

_Exit:
    _NtCRT_unlock(_EXIT_LOCK1);
    return Result;
}

#pragma endregion

#pragma region Runtime services

static int g_ErrorMode = _OUT_TO_DEFAULT;

EXTERN_C int __cdecl _NtCRT__set_error_mode(int mode)
{
    int Old = g_ErrorMode;

    switch (mode)
    {
        case _OUT_TO_DEFAULT:
        case _OUT_TO_STDERR:
        case _OUT_TO_MSGBOX:
            g_ErrorMode = mode;
            return Old;
        case _REPORT_ERRMODE:
            return Old;
        default:
            _NtCRT_InvalidParameter(EINVAL);
            return -1;
    }
}

/* SetErrorMode */
EXTERN_C void __cdecl _NtCRT__seterrormode(int mode)
{
    ULONG HardErrorMode = (ULONG)mode ^ SEM_FAILCRITICALERRORS;

    NtSetInformationProcess(NtCurrentProcess(), ProcessDefaultHardErrorMode, &HardErrorMode, sizeof(HardErrorMode));
}

EXTERN_C errno_t __cdecl _NtCRT__set_doserrno(unsigned long value)
{
    _NtCRT_GetPtd()->Doserrno = value;
    return 0;
}

/* A zero duration still yields like msvcrt */
EXTERN_C void __cdecl _NtCRT__sleep(unsigned long duration)
{
    _K32_Sleep(duration == 0 ? 1 : duration);
}

EXTERN_C void __cdecl _NtCRT__beep(unsigned frequency, unsigned duration)
{
    typedef BOOL(WINAPI* PFN_BEEP)(DWORD, DWORD);
    PFN_BEEP Beep = (PFN_BEEP)_NtCRT_GetKernel32Function(NTCRT_K32_Beep);

    if (Beep != NULL)
    {
        Beep(frequency, duration);
    }
}

EXTERN_C void __cdecl _NtCRT__purecall(void)
{
    _NtCRT_amsg_exit(25); /* _RT_PUREVIRT */
}

EXTERN_C unsigned long __cdecl _NtCRT___threadid(void)
{
    return (unsigned long)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;
}

EXTERN_C uintptr_t __cdecl _NtCRT___threadhandle(void)
{
    return (uintptr_t)NtCurrentThread();
}

EXTERN_C int* __cdecl _NtCRT___fpecode(void)
{
    return &_NtCRT_GetPtd()->Fpecode;
}

EXTERN_C void** __cdecl _NtCRT___pxcptinfoptrs(void)
{
    return &_NtCRT_GetPtd()->PxcptInfoPtrs;
}

/* Restore the guard page below the stack pointer after a stack overflow, returns 0 on failure */
EXTERN_C int __cdecl _NtCRT__resetstkoflw(void)
{
    MEMORY_BASIC_INFORMATION Info;
    PUCHAR Stack = (PUCHAR)__builtin_frame_address(0);
    PUCHAR Base;
    PUCHAR Page;
    PVOID Guard;
    SIZE_T Size;
    ULONG OldProtect;

    if (!NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(), Stack, MemoryBasicInformation, &Info, sizeof(Info), NULL)))
    {
        return 0;
    }
    Base = (PUCHAR)Info.AllocationBase;

    /* Nothing to do while a guard page remains */
    for (Page = Base; Page < Stack; Page = (PUCHAR)Info.BaseAddress + Info.RegionSize)
    {
        if (!NT_SUCCESS(
                NtQueryVirtualMemory(NtCurrentProcess(), Page, MemoryBasicInformation, &Info, sizeof(Info), NULL)))
        {
            return 0;
        }
        if (Info.State == MEM_COMMIT && (Info.Protect & PAGE_GUARD) != 0)
        {
            return 1;
        }
    }

    Page = (PUCHAR)((ULONG_PTR)Stack & ~(ULONG_PTR)(PAGE_SIZE - 1)) - PAGE_SIZE;
    if (Page < Base + PAGE_SIZE)
    {
        return 0;
    }
    Guard = Page;
    Size = PAGE_SIZE;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &Guard, 0, &Size, MEM_COMMIT, PAGE_READWRITE)))
    {
        return 0;
    }
    return NT_SUCCESS(
        NtProtectVirtualMemory(NtCurrentProcess(), &Guard, &Size, PAGE_READWRITE | PAGE_GUARD, &OldProtect));
}

static const int g_DaysBeforeMonth[2][13] = {
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
    { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 },
};

/* Local time into *tp, returns the milliseconds */
EXTERN_C unsigned __cdecl _NtCRT__getsystime(struct tm* tp)
{
    SYSTEMTIME Time;
    TIME_ZONE_INFORMATION Tz;
    DWORD TzId = _K32_GetTimeZoneInformation(&Tz);
    int Leap;

    _K32_GetLocalTime(&Time);
    Leap = (Time.wYear % 4 == 0 && Time.wYear % 100 != 0) || Time.wYear % 400 == 0;
    tp->tm_isdst = TzId == TIME_ZONE_ID_INVALID ? -1 : TzId == TIME_ZONE_ID_DAYLIGHT;
    tp->tm_year = Time.wYear - 1900;
    tp->tm_mon = Time.wMonth - 1;
    tp->tm_mday = Time.wDay;
    tp->tm_hour = Time.wHour;
    tp->tm_min = Time.wMinute;
    tp->tm_sec = Time.wSecond;
    tp->tm_wday = Time.wDayOfWeek;
    tp->tm_yday = g_DaysBeforeMonth[Leap][tp->tm_mon] + Time.wDay - 1;
    return Time.wMilliseconds;
}

/* Set the local time like SetLocalTime, returns 0 or the Win32 error */
EXTERN_C unsigned __cdecl _NtCRT__setsystime(struct tm* tp, unsigned milliseconds)
{
    TIME_FIELDS Fields;
    LARGE_INTEGER LocalTime;
    LARGE_INTEGER SystemTime;
    BOOLEAN WasEnabled;
    NTSTATUS Status;
    ULONG Error;

    Fields.Year = (CSHORT)(tp->tm_year + 1900);
    Fields.Month = (CSHORT)(tp->tm_mon + 1);
    Fields.Day = (CSHORT)tp->tm_mday;
    Fields.Hour = (CSHORT)tp->tm_hour;
    Fields.Minute = (CSHORT)tp->tm_min;
    Fields.Second = (CSHORT)tp->tm_sec;
    Fields.Milliseconds = (CSHORT)milliseconds;
    Fields.Weekday = 0;
    if (!RtlTimeFieldsToTime(&Fields, &LocalTime))
    {
        Status = STATUS_INVALID_PARAMETER;
    } else
    {
        Status = RtlLocalTimeToSystemTime(&LocalTime, &SystemTime);
        if (NT_SUCCESS(Status))
        {
            Status = RtlAdjustPrivilege(SE_SYSTEMTIME_PRIVILEGE, TRUE, FALSE, &WasEnabled);
            if (NT_SUCCESS(Status))
            {
                Status = NtSetSystemTime(&SystemTime, NULL);
                if (!WasEnabled)
                {
                    RtlAdjustPrivilege(SE_SYSTEMTIME_PRIVILEGE, FALSE, FALSE, &WasEnabled);
                }
            }
        }
    }
    if (NT_SUCCESS(Status))
    {
        return 0;
    }
    Error = RtlNtStatusToDosError(Status);
    _NtCRT_SetErrnoFromWin32(Error);
    return Error;
}

#pragma endregion

#pragma region Locale time names

static const char g_Days[] = ":Sun:Sunday:Mon:Monday:Tue:Tuesday:Wed:Wednesday:Thu:Thursday:Fri:Friday:Sat:Saturday";
static const char g_Months[] = ":Jan:January:Feb:February:Mar:March:Apr:April:May:May:Jun:June:Jul:July:Aug:August"
                               ":Sep:September:Oct:October:Nov:November:Dec:December";

/* Names of the "C" locale, ":abbreviation:name" for each, allocated by malloc */
static
char*
_NtCRT_DupNames(
    _In_reads_z_(Size) const char* Names,
    _In_ size_t Size)
{
    char* p = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, Size);

    if (p != NULL)
    {
        memcpy(p, Names, Size);
    }
    return p;
}

EXTERN_C char* __cdecl _NtCRT__Getdays(void)
{
    return _NtCRT_DupNames(g_Days, sizeof(g_Days));
}

EXTERN_C char* __cdecl _NtCRT__Getmonths(void)
{
    return _NtCRT_DupNames(g_Months, sizeof(g_Months));
}

/* __lc_time_data of msvcrt */
typedef struct _NTCRT_TIME_NAMES
{
    char* wday_abbr[7];
    char* wday[7];
    char* month_abbr[12];
    char* month[12];
    char* ampm[2];
    char* ww_sdatefmt;
    char* ww_ldatefmt;
    char* ww_timefmt;
    LCID ww_lcid;
    int ww_caltype;
    int refcount;
} NTCRT_TIME_NAMES;

static const char* const g_TimeNames[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December",
    "AM", "PM", "MM/dd/yy", "dddd, MMMM dd, yyyy", "HH:mm:ss"
};

/* The time names of the "C" locale and their strings in one block, allocated by malloc */
EXTERN_C void* __cdecl _NtCRT__Gettnames(void)
{
    size_t Size = sizeof(NTCRT_TIME_NAMES);
    NTCRT_TIME_NAMES* Names;
    char** Field;
    char* p;

    static_assert(ARRAYSIZE(g_TimeNames) == FIELD_OFFSET(NTCRT_TIME_NAMES, ww_lcid) / sizeof(char*), "_Gettnames");
    for (const char* Name : g_TimeNames)
    {
        Size += strlen(Name) + 1;
    }
    Names = (NTCRT_TIME_NAMES*)RtlAllocateHeap(NTCRT_HEAP, 0, Size);
    if (Names == NULL)
    {
        return NULL;
    }
    Field = Names->wday_abbr;
    p = (char*)(Names + 1);
    for (const char* Name : g_TimeNames)
    {
        size_t Length = strlen(Name) + 1;

        memcpy(p, Name, Length);
        *Field++ = p;
        p += Length;
    }
    Names->ww_lcid = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);
    Names->ww_caltype = CAL_GREGORIAN;
    Names->refcount = 0;
    return Names;
}

/* The names come from the "C" locale, so the time names of _Gettnames are not used */
EXTERN_C size_t __cdecl _NtCRT__Strftime(char* s, size_t max, const char* format, const struct tm* tm, void* tnames)
{
    UNREFERENCED_PARAMETER(tnames);
    return _NtCRT_strftime(s, max, format, tm);
}

#pragma endregion

#pragma region Variables

extern "C"
{
    double _NtCRT_HUGE = __builtin_huge_val();
    unsigned int _NtCRT_osplatform = VER_PLATFORM_WIN32_NT;
    int _NtCRT_fileinfo = 0;
    void(__cdecl* _NtCRT_aexit_rtn)(int) = _NtCRT__exit;
}

NTCRT_IMPORT_DATA(_HUGE, _NtCRT_HUGE);
NTCRT_IMPORT_DATA(_osplatform, _NtCRT_osplatform);
NTCRT_IMPORT_DATA(_fileinfo, _NtCRT_fileinfo);
NTCRT_IMPORT_DATA(_aexit_rtn, _NtCRT_aexit_rtn);

#pragma endregion

NTCRT_IMPORT(_ecvt, _NtCRT__ecvt);
NTCRT_IMPORT(_fcvt, _NtCRT__fcvt);
NTCRT_IMPORT(_gcvt, _NtCRT__gcvt);
NTCRT_IMPORT(_ecvt_s, _NtCRT__ecvt_s);
NTCRT_IMPORT(_fcvt_s, _NtCRT__fcvt_s);
NTCRT_IMPORT(_gcvt_s, _NtCRT__gcvt_s);
NTCRT_IMPORT(_rotl, _NtCRT__rotl);
NTCRT_IMPORT(_rotr, _NtCRT__rotr);
NTCRT_IMPORT(_lrotl, _NtCRT__rotl);
NTCRT_IMPORT(_lrotr, _NtCRT__rotr);
NTCRT_IMPORT(_rotl64, _NtCRT__rotl64);
NTCRT_IMPORT(_rotr64, _NtCRT__rotr64);
NTCRT_IMPORT(_lfind, _NtCRT__lfind);
NTCRT_IMPORT(_lsearch, _NtCRT__lsearch);
NTCRT_IMPORT(_lfind_s, _NtCRT__lfind_s);
NTCRT_IMPORT(_lsearch_s, _NtCRT__lsearch_s);
NTCRT_IMPORT(qsort_s, _NtCRT_qsort_s);
NTCRT_IMPORT(bsearch_s, _NtCRT_bsearch_s);
NTCRT_IMPORT(_makepath, _NtCRT__makepath);
NTCRT_IMPORT(_wmakepath, _NtCRT__wmakepath);
NTCRT_IMPORT(_makepath_s, _NtCRT__makepath_s);
NTCRT_IMPORT(_wmakepath_s, _NtCRT__wmakepath_s);
NTCRT_IMPORT(_wsplitpath, _NtCRT__wsplitpath);
NTCRT_IMPORT(_wsplitpath_s, _NtCRT__wsplitpath_s);
NTCRT_IMPORT(_searchenv, _NtCRT__searchenv);
NTCRT_IMPORT(_wsearchenv, _NtCRT__wsearchenv);
NTCRT_IMPORT(_searchenv_s, _NtCRT__searchenv_s);
NTCRT_IMPORT(_wsearchenv_s, _NtCRT__wsearchenv_s);
NTCRT_IMPORT(_mktemp, _NtCRT__mktemp);
NTCRT_IMPORT(_wmktemp, _NtCRT__wmktemp);
NTCRT_IMPORT(_mktemp_s, _NtCRT__mktemp_s);
NTCRT_IMPORT(_wmktemp_s, _NtCRT__wmktemp_s);
NTCRT_IMPORT(_getdrives, _NtCRT__getdrives);
NTCRT_IMPORT(_getdrive, _NtCRT__getdrive);
NTCRT_IMPORT(_chdrive, _NtCRT__chdrive);
NTCRT_IMPORT(_getdcwd, _NtCRT__getdcwd);
NTCRT_IMPORT(_wgetdcwd, _NtCRT__wgetdcwd);
NTCRT_IMPORT(_getdiskfree, _NtCRT__getdiskfree);
NTCRT_IMPORT(_heapwalk, _NtCRT__heapwalk);
NTCRT_IMPORT(_heapset, _NtCRT__heapset);
NTCRT_IMPORT(_heapused, _NtCRT__heapused);
NTCRT_IMPORT(_heapadd, _NtCRT__heapadd);
NTCRT_IMPORT(_get_sbh_threshold, _NtCRT__get_sbh_threshold);
NTCRT_IMPORT(_set_sbh_threshold, _NtCRT__set_sbh_threshold);
NTCRT_IMPORT(_locking, _NtCRT__locking);
NTCRT_IMPORT(_loaddll, _NtCRT__loaddll);
NTCRT_IMPORT(_unloaddll, _NtCRT__unloaddll);
NTCRT_IMPORT(_getdllprocaddr, _NtCRT__getdllprocaddr);
NTCRT_IMPORT(__dllonexit, _NtCRT___dllonexit);
NTCRT_IMPORT(_set_error_mode, _NtCRT__set_error_mode);
NTCRT_IMPORT(_seterrormode, _NtCRT__seterrormode);
NTCRT_IMPORT(_set_doserrno, _NtCRT__set_doserrno);
NTCRT_IMPORT(_sleep, _NtCRT__sleep);
NTCRT_IMPORT(_beep, _NtCRT__beep);
NTCRT_IMPORT(_purecall, _NtCRT__purecall);
NTCRT_IMPORT(__threadid, _NtCRT___threadid);
NTCRT_IMPORT(__threadhandle, _NtCRT___threadhandle);
NTCRT_IMPORT(__fpecode, _NtCRT___fpecode);
NTCRT_IMPORT(__pxcptinfoptrs, _NtCRT___pxcptinfoptrs);
NTCRT_IMPORT(_resetstkoflw, _NtCRT__resetstkoflw);
NTCRT_IMPORT(_getsystime, _NtCRT__getsystime);
NTCRT_IMPORT(_setsystime, _NtCRT__setsystime);
NTCRT_IMPORT(_Getdays, _NtCRT__Getdays);
NTCRT_IMPORT(_Getmonths, _NtCRT__Getmonths);
NTCRT_IMPORT(_Gettnames, _NtCRT__Gettnames);
NTCRT_IMPORT(_Strftime, _NtCRT__Strftime);

/* POSIX names */
NTCRT_IMPORT(ecvt, _NtCRT__ecvt);
NTCRT_IMPORT(fcvt, _NtCRT__fcvt);
NTCRT_IMPORT(gcvt, _NtCRT__gcvt);
NTCRT_IMPORT(lfind, _NtCRT__lfind);
NTCRT_IMPORT(lsearch, _NtCRT__lsearch);
NTCRT_IMPORT(searchenv, _NtCRT__searchenv);
NTCRT_IMPORT(mktemp, _NtCRT__mktemp);
NTCRT_IMPORT(heapwalk, _NtCRT__heapwalk);
