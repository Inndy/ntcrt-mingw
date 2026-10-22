/*
 * swprintf and vswprintf of C (a buffer size, a negative value if the output does not fit, C11 7.29.2.3). The
 * swprintf.inl of the headers of mingw-w64 before 13, which <stdio.h> and <wchar.h> include, defines them by
 * vsnwprintf, whose result is the length of the whole output as snprintf: there the call is to those of NtCRT, in the
 * conventions of C with __USE_MINGW_ANSI_STDIO, else of msvcrt (%s wide). From 13 the headers declare the functions
 * of C, which NtCRT has under their names (NTCRT_SWPRINTF_ISO).
 */

#if __MINGW64_VERSION_MAJOR < 13 && !defined(_NTCRT_SWPRINTF_INL)
#define _NTCRT_SWPRINTF_INL

#ifdef __cplusplus
extern "C" {
#endif
int __cdecl _NtCRT_vswprintf_iso(wchar_t* buffer, size_t count, const wchar_t* format, __builtin_va_list ap);
int __cdecl _NtCRT___mingw_vswprintf_iso(wchar_t* buffer, size_t count, const wchar_t* format, __builtin_va_list ap);
#ifdef __cplusplus
}
#endif

#pragma push_macro("vsnwprintf")
#undef vsnwprintf
#if __USE_MINGW_ANSI_STDIO
#define vsnwprintf _NtCRT___mingw_vswprintf_iso
#else
#define vsnwprintf _NtCRT_vswprintf_iso
#endif
#include_next <swprintf.inl>
#pragma pop_macro("vsnwprintf")

#else
#include_next <swprintf.inl>
#endif
