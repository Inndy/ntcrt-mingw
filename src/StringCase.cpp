/*
 * _stricmp, _strnicmp and _memicmp instead of those of ntdll, which crash on NULL: ASCII case as the narrow case
 * mapping of NtCRT, _NLSCMPERROR and EINVAL for a NULL string as msvcrt. Any count is taken, where the _strnicmp of
 * msvcrt fails for one above INT_MAX, so that strncasecmp(s1, s2, SIZE_MAX) compares whole strings.
 */

#include "NtCRT.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

static int _NtCRT_AsciiLower(unsigned char c)
{
    return (unsigned)(c - 'A') < 26 ? c + ('a' - 'A') : c;
}

/* One loop for both: String stops at the end of s1 and takes no NULL even for count 0 */
static __attribute__((noinline)) int
_NtCRT_CaseCompare(const unsigned char* s1, const unsigned char* s2, size_t n, bool String)
{
    if ((String || n != 0) && (s1 == NULL || s2 == NULL))
    {
        NTCRT_ERRNO = EINVAL;
        return _NLSCMPERROR;
    }
    for (; n > 0; n--, s1++, s2++)
    {
        int Diff = _NtCRT_AsciiLower(*s1) - _NtCRT_AsciiLower(*s2);

        if (Diff != 0 || (String && *s1 == '\0'))
        {
            return Diff;
        }
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT__memicmp(const void* buf1, const void* buf2, size_t count)
{
    return _NtCRT_CaseCompare((const unsigned char*)buf1, (const unsigned char*)buf2, count, false);
}

EXTERN_C int __cdecl _NtCRT__strnicmp(const char* s1, const char* s2, size_t n)
{
    return _NtCRT_CaseCompare((const unsigned char*)s1, (const unsigned char*)s2, n, true);
}

EXTERN_C int __cdecl _NtCRT__stricmp(const char* s1, const char* s2)
{
    return _NtCRT__strnicmp(s1, s2, SIZE_MAX);
}

NTCRT_IMPORT(_memicmp, _NtCRT__memicmp);
NTCRT_IMPORT(_strnicmp, _NtCRT__strnicmp);
NTCRT_IMPORT(_stricmp, _NtCRT__stricmp);
NTCRT_IMPORT(_strcmpi, _NtCRT__stricmp);

/* POSIX names */
NTCRT_IMPORT(memicmp, _NtCRT__memicmp);
NTCRT_IMPORT(stricmp, _NtCRT__stricmp);
NTCRT_IMPORT(strcmpi, _NtCRT__stricmp);
NTCRT_IMPORT(strcasecmp, _NtCRT__stricmp);
NTCRT_IMPORT(strnicmp, _NtCRT__strnicmp);
NTCRT_IMPORT(strncasecmp, _NtCRT__strnicmp);
