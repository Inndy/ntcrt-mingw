/*
 * The strtold and wcstold of gdtoa in libmingwex, which NtCRT replaces, as the reference of strtold.c. Built with the
 * default runtime of MinGW-w64.
 */

#include <stdlib.h>
#include <wchar.h>

__declspec(dllexport) long double gdtoa_strtold(const char* s, char** end)
{
    return __mingw_strtold(s, end);
}

__declspec(dllexport) long double gdtoa_wcstold(const wchar_t* s, wchar_t** end)
{
    return __mingw_wcstold(s, end);
}
