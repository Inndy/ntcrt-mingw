/*
 * The narrow functions of the scanf engine (Scanf.inl) that scan a string, and the engine for the streams of
 * StdioScanf.cpp. The headers of mingw-w64 call __mingw_vsscanf, __mingw_sscanf (C++, __USE_MINGW_ANSI_STDIO) and
 * __ms_vsscanf (vsscanf of msvcrt) of libmingwex: the specs wrap them, libntcrt defines them under their names too
 * for lld, which takes them from libntcrt (it precedes libmingwex). The scanf of libmingwex is not linked.
 */

#include "NtCRT.h"
#include "Scanf.inl"

EXTERN_C int _NtCRT_Scan(NTCRT_SCANF_INPUT<char>* In, const char* Format, va_list ap, ULONG Options)
{
    return _NtCRT_ScanFormat(In, Format, (const ULONG64*)ap, Options);
}

/* Count characters of Input at most, up to a null character */
EXTERN_C int _NtCRT_ScanString(const char* Input, size_t Count, const char* Format, va_list ap, ULONG Options)
{
    NTCRT_SCANF_INPUT<char> In = {};

    if (Input == NULL || Format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    In.Next = Input;
    In.Left = Count;
    return _NtCRT_ScanFormat(&In, Format, (const ULONG64*)ap, Options);
}

EXTERN_C int __cdecl _NtCRT_vsscanf(const char* str, const char* format, va_list ap)
{
    return _NtCRT_ScanString(str, SIZE_MAX, format, ap, 0);
}

EXTERN_C int __cdecl _NtCRT_vsscanf_s(const char* str, const char* format, va_list ap)
{
    return _NtCRT_ScanString(str, SIZE_MAX, format, ap, NTCRT_SCANF_SECURE);
}

NTCRT_WRAP_VARIADIC(_NtCRT_sscanf,
                    _NtCRT_vsscanf,
                    (const char* str, const char* format, ...),
                    format,
                    (str, format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT_sscanf_s,
                    _NtCRT_vsscanf_s,
                    (const char* str, const char* format, ...),
                    format,
                    (str, format, ap))

/* Length characters of the input at most */
NTCRT_WRAP_VARIADIC(_NtCRT__snscanf,
                    _NtCRT_ScanString,
                    (const char* input, size_t length, const char* format, ...),
                    format,
                    (input, length, format, ap, 0))
NTCRT_WRAP_VARIADIC(_NtCRT__snscanf_s,
                    _NtCRT_ScanString,
                    (const char* input, size_t length, const char* format, ...),
                    format,
                    (input, length, format, ap, NTCRT_SCANF_SECURE))

/* The narrow functions of msvcrt and C99 are the same */
EXTERN_C int __cdecl __wrap___mingw_vsscanf(const char*, const char*, va_list) __attribute__((alias("_NtCRT_vsscanf")));
EXTERN_C int __cdecl __wrap___mingw_sscanf(const char*, const char*, ...) __attribute__((alias("_NtCRT_sscanf")));
EXTERN_C int __cdecl __wrap___ms_vsscanf(const char*, const char*, va_list) __attribute__((alias("_NtCRT_vsscanf")));
EXTERN_C int __cdecl __mingw_vsscanf(const char*, const char*, va_list) __attribute__((alias("_NtCRT_vsscanf")));
EXTERN_C int __cdecl __mingw_sscanf(const char*, const char*, ...) __attribute__((alias("_NtCRT_sscanf")));
EXTERN_C int __cdecl __ms_vsscanf(const char*, const char*, va_list) __attribute__((alias("_NtCRT_vsscanf")));

NTCRT_IMPORT_ONLY(vsscanf, _NtCRT_vsscanf);
NTCRT_IMPORT(sscanf, _NtCRT_sscanf);
NTCRT_IMPORT(vsscanf_s, _NtCRT_vsscanf_s);
NTCRT_IMPORT(sscanf_s, _NtCRT_sscanf_s);
NTCRT_IMPORT(_snscanf, _NtCRT__snscanf);
NTCRT_IMPORT(_snscanf_s, _NtCRT__snscanf_s);
