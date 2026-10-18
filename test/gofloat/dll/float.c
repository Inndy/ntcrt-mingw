/*
 * The float conversions of NtCRT in a DLL for the Go tests (float_test.go). Go calls by syscall, which returns
 * integer registers only: the results are stored as bits.
 */

#include <errno.h>
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

/* wcstod skips the characters iswspace takes */
__declspec(dllexport) int is_wspace(int c)
{
    return iswspace((wint_t)c) != 0;
}
