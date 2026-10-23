# Edits run.sh makes to the tests of libc-test, parts of POSIX left out of tests that check more of C:
# sigsetjmp and the signal mask of setjmp
/^\tsigjmp_buf sjb;/,/^\tsigset_t set, set2;/d
/^\tsigemptyset(&set);/,/siglongjmp incorrectly restored mask/d
# the stack limit of sscanf_long (setrlimit), the stack of Windows is 1 MB and the input 8 MB anyway
/^#include <sys\/resource\.h>/d
/t_setrlim(RLIMIT_STACK/d
# strlcpy and strlcat of string (BSD, POSIX 2024)
/strlcpy(b, "abc", sizeof b - 1)/,/^\treturn t_status;/{/return t_status/!d}
# the TZ of strftime by _putenv of Windows (setenv is POSIX)
s/setenv("TZ", "UTC0", 1);/_putenv("TZ=UTC0");/
# mbc: wchar_t is UTF-16, mbrtowc returns the high surrogate of a character above U+FFFF after its third byte, as
# Cygwin (README.md "Locales"; 4 and U+10000 do not fit, mbrtoc32 converts it in one call)
s/), 4, "failed to accept U+10FFF\([EF]\)");/), 3, "failed to accept U+10FFF\1");/
s/), 4, "wrong length");/), 3, "wrong length");/
s/), 0x10000, "wrong char");/), 0xd800, "wrong char");/
# clocale_mbfuncs: wctob returns EOF (C 7.29.6.1.2), not WEOF, which is 0xFFFF on Windows (wint_t of 16 bits). The
# bytes 0x80..0xFF of the "C" locale are U+0080..U+00FF, as msvcrt (README.md "Locales"), whose classes are those of
# Latin-1, not characters of no class (musl maps them to U+DF80..U+DFFF)
s/(c=wctob(i)) != WEOF/(c=wctob(i)) != EOF/
/^\tfor (i=128; i<256; i++) {/,/^\t}/d
# fwscanf and fgetwc-buffering: the wide functions read a binary stream (tmpfile, the pipe of compat.h) as UTF-16
# units on Windows (README.md "C, msvcrt and UCRT"), multibyte characters of the locale in a text stream
/^#include <wchar\.h>/,/= tmpfile();/s/tmpfile()/fopen("fwscanf.tmp", "w+D")/
s/^\tA(pipe(p) == 0);/\tA(_pipe(p, 4096, _O_TEXT) == 0);/
# fpclassify-invalid-ld80: the invalid encodings of the x87 are undefined in C. fpclassify is the inline of the
# <math.h> of MinGW-w64, not NtCRT's; %La writes the 4 high bits before the point (0x8p-16385, as glibc), the same
# value as 0x1p-16382. What NtCRT is tested for: an unnormal and a pseudo-infinity are "nan".
s/^\tif (c != cwant) \\$/\tif (0 \&\& c != cwant) \\/
s/, "0x1p-16382");/, "0x8p-16385");/
s/, "0x1.0000000000000002p-16382");/, "0x8.000000000000001p-16385");/
