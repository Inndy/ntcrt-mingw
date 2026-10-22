/*
 * The wide functions of the scanf engine (Scanf.inl) that scan a string, and the engine for the streams of
 * StdioWscanf.cpp. Those of msvcrt take %s, %c and %[ wide, the C99 __mingw_vswscanf and __mingw_swscanf of the
 * headers of mingw-w64 narrow (wrapped by the specs, defined under their names for lld as Scanf.cpp).
 */

#include "NtCRT.h"
#include "Scanf.inl"

EXTERN_C int _NtCRT_ScanW(NTCRT_SCANF_INPUT<wchar_t>* In, const wchar_t* Format, va_list ap, ULONG Options)
{
    return _NtCRT_ScanFormat(In, Format, (const ULONG_PTR*)ap, Options);
}

EXTERN_C int _NtCRT_ScanStringW(const wchar_t* Input, size_t Count, const wchar_t* Format, va_list ap, ULONG Options)
{
    NTCRT_SCANF_INPUT<wchar_t> In = {};

    if (Input == NULL || Format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    In.Next = Input;
    In.Left = Count;
    return _NtCRT_ScanFormat(&In, Format, (const ULONG_PTR*)ap, Options);
}

EXTERN_C int __cdecl _NtCRT_vswscanf(const wchar_t* str, const wchar_t* format, va_list ap)
{
    return _NtCRT_ScanStringW(str, SIZE_MAX, format, ap, NTCRT_SCANF_LEGACY_WIDE);
}

EXTERN_C int __cdecl _NtCRT_vswscanf_s(const wchar_t* str, const wchar_t* format, va_list ap)
{
    return _NtCRT_ScanStringW(str, SIZE_MAX, format, ap, NTCRT_SCANF_LEGACY_WIDE | NTCRT_SCANF_SECURE);
}

NTCRT_WRAP_VARIADIC(_NtCRT_swscanf,
                    _NtCRT_vswscanf,
                    (const wchar_t* str, const wchar_t* format, ...),
                    format,
                    (str, format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT_swscanf_s,
                    _NtCRT_vswscanf_s,
                    (const wchar_t* str, const wchar_t* format, ...),
                    format,
                    (str, format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT__snwscanf,
                    _NtCRT_ScanStringW,
                    (const wchar_t* input, size_t length, const wchar_t* format, ...),
                    format,
                    (input, length, format, ap, NTCRT_SCANF_LEGACY_WIDE))
NTCRT_WRAP_VARIADIC(_NtCRT__snwscanf_s,
                    _NtCRT_ScanStringW,
                    (const wchar_t* input, size_t length, const wchar_t* format, ...),
                    format,
                    (input, length, format, ap, NTCRT_SCANF_LEGACY_WIDE | NTCRT_SCANF_SECURE))

EXTERN_C int __cdecl __wrap___mingw_vswscanf(const wchar_t* str, const wchar_t* format, va_list ap)
{
    return _NtCRT_ScanStringW(str, SIZE_MAX, format, ap, NTCRT_SCANF_LONG_DOUBLE);
}

NTCRT_WRAP_VARIADIC(__wrap___mingw_swscanf,
                    __wrap___mingw_vswscanf,
                    (const wchar_t* str, const wchar_t* format, ...),
                    format,
                    (str, format, ap))

EXTERN_C int __cdecl __mingw_vswscanf(const wchar_t*, const wchar_t*, va_list)
    __attribute__((alias("__wrap___mingw_vswscanf")));
EXTERN_C int __cdecl __mingw_swscanf(const wchar_t*, const wchar_t*, ...)
    __attribute__((alias("__wrap___mingw_swscanf")));
EXTERN_C int __cdecl __wrap___ms_vswscanf(const wchar_t*, const wchar_t*, va_list)
    __attribute__((alias("_NtCRT_vswscanf")));
EXTERN_C int __cdecl __ms_vswscanf(const wchar_t*, const wchar_t*, va_list) __attribute__((alias("_NtCRT_vswscanf")));

NTCRT_IMPORT_ONLY(vswscanf, _NtCRT_vswscanf);
NTCRT_IMPORT(swscanf, _NtCRT_swscanf);
NTCRT_IMPORT(__ms_swscanf, _NtCRT_swscanf);
NTCRT_IMPORT(vswscanf_s, _NtCRT_vswscanf_s);
NTCRT_IMPORT(swscanf_s, _NtCRT_swscanf_s);
NTCRT_IMPORT(_snwscanf, _NtCRT__snwscanf);
NTCRT_IMPORT(_snwscanf_s, _NtCRT__snwscanf_s);
