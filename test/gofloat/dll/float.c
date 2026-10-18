/*
 * The float conversions of NtCRT in a DLL for the Go tests. Go calls by syscall, which returns
 * integer registers only: the results are stored as bits.
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

/* Returns the number of characters converted, *Err is errno */
__declspec(dllexport) size_t strtod_bits(const char* s, uint64_t* Bits, int* Err)
{
    char* End;
    double d;

    errno = 0;
    d = strtod(s, &End);
    *Err = errno;
    memcpy(Bits, &d, sizeof(d));
    return (size_t)(End - s);
}

__declspec(dllexport) size_t strtof_bits(const char* s, uint32_t* Bits, int* Err)
{
    char* End;
    float f;

    errno = 0;
    f = strtof(s, &End);
    *Err = errno;
    memcpy(Bits, &f, sizeof(f));
    return (size_t)(End - s);
}

__declspec(dllexport) size_t wcstod_bits(const wchar_t* s, uint64_t* Bits, int* Err)
{
    wchar_t* End;
    double d;

    errno = 0;
    d = wcstod(s, &End);
    *Err = errno;
    memcpy(Bits, &d, sizeof(d));
    return (size_t)(End - s);
}

/* The 80-bit long double of strtold and wcstold goes to Go as its 10 bytes */
__declspec(dllexport) size_t strtold_bytes(const char* s, unsigned char* Bytes, int* Err)
{
    char* End;
    long double x;

    errno = 0;
    x = strtold(s, &End);
    *Err = errno;
    memcpy(Bytes, &x, 10);
    return (size_t)(End - s);
}

__declspec(dllexport) size_t wcstold_bytes(const wchar_t* s, unsigned char* Bytes, int* Err)
{
    wchar_t* End;
    long double x;

    errno = 0;
    x = wcstold(s, &End);
    *Err = errno;
    memcpy(Bytes, &x, 10);
    return (size_t)(End - s);
}

/* wcstod skips the characters iswspace takes */
__declspec(dllexport) int is_wspace(int c)
{
    return iswspace((wint_t)c) != 0;
}

/* printf of NtCRT with one double, given as its bits, for the Go tests of printf (printf_test.go) */
__declspec(dllexport) int format_double(char* Buffer, size_t Size, const char* Format, uint64_t Bits)
{
    double d;

    memcpy(&d, &Bits, sizeof(d));
    return __mingw_snprintf(Buffer, Size, Format, d);
}

/* The same with one 80-bit long double, given as its 10 bytes */
__declspec(dllexport) int format_long_double(char* Buffer, size_t Size, const char* Format, const unsigned char* Bytes)
{
    long double x = 0;

    memcpy(&x, Bytes, 10);
    return __mingw_snprintf(Buffer, Size, Format, x);
}
