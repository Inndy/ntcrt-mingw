/*
 * The narrow scanf family on streams, by the engine of Scanf.cpp under the lock of the stream: the functions of
 * msvcrt, their _s forms, and __mingw_vfscanf, __mingw_fscanf, __mingw_vscanf, __mingw_scanf, __ms_vfscanf and
 * __ms_vscanf of the headers of mingw-w64 (wrapped by the specs, under their names too for lld).
 */

#include "NtCRT.h"
#include "Stdio.inl"

#include <errno.h>

static int _NtCRT_ScanGetc(FILE* Stream)
{
    return _NtCRT_getc_nolock(Stream);
}

static void _NtCRT_ScanUngetc(int Ch, FILE* Stream)
{
    _NtCRT_ungetc_nolock(Ch, Stream);
}

EXTERN_C int _NtCRT_ScanStream(FILE* Stream, const char* Format, va_list ap, ULONG Options)
{
    NTCRT_SCANF_INPUT<char> In = {};
    int Ret;

    if (Stream == NULL || Format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    NTCRT_CHECK_STREAM_ANSI(Stream, EOF);
    In.Stream = Stream;
    In.Get = _NtCRT_ScanGetc;
    In.Unget = _NtCRT_ScanUngetc;
    _NtCRT_lock_file(Stream);
    Ret = _NtCRT_Scan(&In, Format, ap, Options);
    _NtCRT_unlock_file(Stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vfscanf(FILE* stream, const char* format, va_list ap)
{
    return _NtCRT_ScanStream(stream, format, ap, 0);
}

EXTERN_C int __cdecl _NtCRT_vscanf(const char* format, va_list ap)
{
    return _NtCRT_ScanStream(&_NtCRT_iob[0], format, ap, 0);
}

EXTERN_C int __cdecl _NtCRT_vfscanf_s(FILE* stream, const char* format, va_list ap)
{
    return _NtCRT_ScanStream(stream, format, ap, NTCRT_SCANF_SECURE);
}

EXTERN_C int __cdecl _NtCRT_vscanf_s(const char* format, va_list ap)
{
    return _NtCRT_ScanStream(&_NtCRT_iob[0], format, ap, NTCRT_SCANF_SECURE);
}

NTCRT_WRAP_VARIADIC(_NtCRT_fscanf,
                    _NtCRT_vfscanf,
                    (FILE * stream, const char* format, ...),
                    format,
                    (stream, format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT_scanf, _NtCRT_vscanf, (const char* format, ...), format, (format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT_fscanf_s,
                    _NtCRT_vfscanf_s,
                    (FILE * stream, const char* format, ...),
                    format,
                    (stream, format, ap))
NTCRT_WRAP_VARIADIC(_NtCRT_scanf_s, _NtCRT_vscanf_s, (const char* format, ...), format, (format, ap))

/* Those of mingw-w64 take the long double of GCC for %Lf */
EXTERN_C int __cdecl __wrap___mingw_vfscanf(FILE* stream, const char* format, va_list ap)
{
    return _NtCRT_ScanStream(stream, format, ap, NTCRT_SCANF_LONG_DOUBLE);
}

EXTERN_C int __cdecl __wrap___mingw_vscanf(const char* format, va_list ap)
{
    return _NtCRT_ScanStream(&_NtCRT_iob[0], format, ap, NTCRT_SCANF_LONG_DOUBLE);
}

NTCRT_WRAP_VARIADIC(__wrap___mingw_fscanf,
                    __wrap___mingw_vfscanf,
                    (FILE * stream, const char* format, ...),
                    format,
                    (stream, format, ap))
NTCRT_WRAP_VARIADIC(__wrap___mingw_scanf, __wrap___mingw_vscanf, (const char* format, ...), format, (format, ap))


EXTERN_C int __cdecl __wrap___ms_vfscanf(FILE*, const char*, va_list) __attribute__((alias("_NtCRT_vfscanf")));
EXTERN_C int __cdecl __wrap___ms_vscanf(const char*, va_list) __attribute__((alias("_NtCRT_vscanf")));
EXTERN_C int __cdecl __mingw_vfscanf(FILE*, const char*, va_list) __attribute__((alias("__wrap___mingw_vfscanf")));
EXTERN_C int __cdecl __mingw_vscanf(const char*, va_list) __attribute__((alias("__wrap___mingw_vscanf")));
EXTERN_C int __cdecl __mingw_fscanf(FILE*, const char*, ...) __attribute__((alias("__wrap___mingw_fscanf")));
EXTERN_C int __cdecl __mingw_scanf(const char*, ...) __attribute__((alias("__wrap___mingw_scanf")));
EXTERN_C int __cdecl __ms_vfscanf(FILE*, const char*, va_list) __attribute__((alias("_NtCRT_vfscanf")));
EXTERN_C int __cdecl __ms_vscanf(const char*, va_list) __attribute__((alias("_NtCRT_vscanf")));

NTCRT_IMPORT_ONLY(vfscanf, _NtCRT_vfscanf);
NTCRT_IMPORT(fscanf, _NtCRT_fscanf);
NTCRT_IMPORT_ONLY(vscanf, _NtCRT_vscanf);
NTCRT_IMPORT(scanf, _NtCRT_scanf);
NTCRT_IMPORT(vfscanf_s, _NtCRT_vfscanf_s);
NTCRT_IMPORT(fscanf_s, _NtCRT_fscanf_s);
NTCRT_IMPORT(vscanf_s, _NtCRT_vscanf_s);
NTCRT_IMPORT(scanf_s, _NtCRT_scanf_s);
