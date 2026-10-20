/*
 * The wide scanf family on streams, by the engine of ScanfWide.cpp under the lock of the stream: the functions of
 * msvcrt (%s, %c and %[ wide), their _s forms and __ms_vfwscanf, __ms_vwscanf, and the C99 __mingw_vfwscanf,
 * __mingw_fwscanf, __mingw_vwscanf and __mingw_wscanf (%s, %c and %[ narrow) of the headers of mingw-w64 (wrapped by
 * the specs, under their names too for lld).
 */

#include "NtCRT.h"
#include "Stdio.inl"

#include <errno.h>

static int _NtCRT_ScanGetwc(FILE* Stream)
{
    wint_t Ch = _NtCRT__fgetwc_nolock(Stream);

    return Ch == WEOF ? EOF : (int)Ch;
}

static void _NtCRT_ScanUngetwc(int Ch, FILE* Stream)
{
    _NtCRT__ungetwc_nolock((wint_t)Ch, Stream);
}

EXTERN_C int _NtCRT_ScanStreamW(FILE* Stream, const wchar_t* Format, va_list ap, ULONG Options)
{
    NTCRT_SCANF_INPUT<wchar_t> In = {};
    int Ret;

    if (Stream == NULL || Format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    In.Stream = Stream;
    In.Get = _NtCRT_ScanGetwc;
    In.Unget = _NtCRT_ScanUngetwc;
    _NtCRT_lock_file(Stream);
    Ret = _NtCRT_ScanW(&In, Format, ap, Options);
    _NtCRT_unlock_file(Stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vfwscanf(FILE* stream, const wchar_t* format, va_list ap)
{
    return _NtCRT_ScanStreamW(stream, format, ap, NTCRT_SCANF_LEGACY_WIDE);
}

EXTERN_C int __cdecl _NtCRT_vwscanf(const wchar_t* format, va_list ap)
{
    return _NtCRT_ScanStreamW(&_NtCRT_iob[0], format, ap, NTCRT_SCANF_LEGACY_WIDE);
}

EXTERN_C int __cdecl _NtCRT_vfwscanf_s(FILE* stream, const wchar_t* format, va_list ap)
{
    return _NtCRT_ScanStreamW(stream, format, ap, NTCRT_SCANF_LEGACY_WIDE | NTCRT_SCANF_SECURE);
}

EXTERN_C int __cdecl _NtCRT_vwscanf_s(const wchar_t* format, va_list ap)
{
    return _NtCRT_ScanStreamW(&_NtCRT_iob[0], format, ap, NTCRT_SCANF_LEGACY_WIDE | NTCRT_SCANF_SECURE);
}

EXTERN_C int __cdecl __wrap___mingw_vfwscanf(FILE* stream, const wchar_t* format, va_list ap)
{
    return _NtCRT_ScanStreamW(stream, format, ap, NTCRT_SCANF_LONG_DOUBLE);
}

EXTERN_C int __cdecl __wrap___mingw_vwscanf(const wchar_t* format, va_list ap)
{
    return _NtCRT_ScanStreamW(&_NtCRT_iob[0], format, ap, NTCRT_SCANF_LONG_DOUBLE);
}

NTCRT_WRAP_VARIADIC(_NtCRT_fwscanf,
                    _NtCRT_vfwscanf,
                    (FILE * stream, const wchar_t* format, ...),
                    format,
                    (stream, format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT_wscanf, _NtCRT_vwscanf, (const wchar_t* format, ...), format, (format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT_fwscanf_s,
                    _NtCRT_vfwscanf_s,
                    (FILE * stream, const wchar_t* format, ...),
                    format,
                    (stream, format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT_wscanf_s, _NtCRT_vwscanf_s, (const wchar_t* format, ...), format, (format, ap))
NTCRT_WRAP_VARIADIC(__wrap___mingw_fwscanf,
                    __wrap___mingw_vfwscanf,
                    (FILE * stream, const wchar_t* format, ...),
                    format,
                    (stream, format, ap))
NTCRT_WRAP_VARIADIC(__wrap___mingw_wscanf, __wrap___mingw_vwscanf, (const wchar_t* format, ...), format, (format, ap))

EXTERN_C int __cdecl __mingw_vfwscanf(FILE*, const wchar_t*, va_list) __attribute__((alias("__wrap___mingw_vfwscanf")));
EXTERN_C int __cdecl __mingw_vwscanf(const wchar_t*, va_list) __attribute__((alias("__wrap___mingw_vwscanf")));
EXTERN_C int __cdecl __mingw_fwscanf(FILE*, const wchar_t*, ...) __attribute__((alias("__wrap___mingw_fwscanf")));
EXTERN_C int __cdecl __mingw_wscanf(const wchar_t*, ...) __attribute__((alias("__wrap___mingw_wscanf")));
EXTERN_C int __cdecl __wrap___ms_vfwscanf(FILE*, const wchar_t*, va_list) __attribute__((alias("_NtCRT_vfwscanf")));
EXTERN_C int __cdecl __wrap___ms_vwscanf(const wchar_t*, va_list) __attribute__((alias("_NtCRT_vwscanf")));
EXTERN_C int __cdecl __ms_vfwscanf(FILE*, const wchar_t*, va_list) __attribute__((alias("_NtCRT_vfwscanf")));
EXTERN_C int __cdecl __ms_vwscanf(const wchar_t*, va_list) __attribute__((alias("_NtCRT_vwscanf")));

NTCRT_IMPORT_ONLY(vfwscanf, _NtCRT_vfwscanf);
NTCRT_IMPORT(fwscanf, _NtCRT_fwscanf);
NTCRT_IMPORT_ONLY(vwscanf, _NtCRT_vwscanf);
NTCRT_IMPORT(wscanf, _NtCRT_wscanf);
NTCRT_IMPORT(vfwscanf_s, _NtCRT_vfwscanf_s);
NTCRT_IMPORT(_winput_s, _NtCRT_vfwscanf_s);
NTCRT_IMPORT(fwscanf_s, _NtCRT_fwscanf_s);
NTCRT_IMPORT(vwscanf_s, _NtCRT_vwscanf_s);
NTCRT_IMPORT(wscanf_s, _NtCRT_wscanf_s);
