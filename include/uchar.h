/*
 * <uchar.h> with the functions of C11 that the <uchar.h> of MinGW-w64 declares for UCRT only. The ntcrt.specs and
 * ntcrt.cfg drivers add this directory to the system include path. mbrtoc32 and c32rtomb convert a character above
 * U+FFFF in one call, where the UTF-16 wchar_t of mbrtowc takes two (README.md "Locales").
 */

#ifndef _NTCRT_UCHAR_H
#define _NTCRT_UCHAR_H

#include_next <uchar.h>

#ifndef _UCRT
#ifdef __cplusplus
extern "C" {
#endif

size_t mbrtoc16(char16_t* __restrict__ pc16, const char* __restrict__ s, size_t n, mbstate_t* __restrict__ ps);
size_t c16rtomb(char* __restrict__ s, char16_t c16, mbstate_t* __restrict__ ps);
size_t mbrtoc32(char32_t* __restrict__ pc32, const char* __restrict__ s, size_t n, mbstate_t* __restrict__ ps);
size_t c32rtomb(char* __restrict__ s, char32_t c32, mbstate_t* __restrict__ ps);

#ifdef __cplusplus
}
#endif
#endif

#endif
