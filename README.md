# ntcrt-mingw

A C runtime for MinGW-w64 (x64, GCC) that imports `ntdll.dll` only. C and C++ programs link the msvcrt
functions from this library instead of `msvcrt.dll` and `kernel32.dll`.

```sh
make            # build/libntcrt.a, the support libraries and build/ntcrt.specs
make test       # build and run test/upstream, the fuzz test and the console test
make compare    # build test/compare with NtCRT and with UCRT (cl of Visual Studio) and show the differences
g++ -static -specs=build/ntcrt.specs -o hello.exe hello.cpp
```

## Layout

- `src/` the runtime, `NtCRT.h` is the internal header
- `polyfill/` code page and console helpers built on ntdll
- `include/KNSoft/NDK/` the part of KNSoft.NDK used by the sources, on top of MinGW-w64 headers
- `test/upstream/` the KNSoft.NtCRT test suite
- `test/fuzz/` differential fuzz of the UTF-8 codec against kernel32 (`make test-fuzz`, `FUZZ_ITERATIONS=n`)
- `test/console/` console reads and writes in a pseudo console (`make test-console`), `conpty.exe` types the
  input and checks the screen
- `test/compare/` tests whose output is compared with UCRT, `run.sh` finds `vcvars64.bat` by vswhere or `VCVARS`

## Behavior

The functions follow msvcrt of Windows 11 where its behavior is sound. Bugs and legacy quirks of msvcrt are
not reproduced, the runtime aims at correct semantics and the C standard instead.

### Locales

Two locales: "C" and "C.UTF-8". Each category of `setlocale` is one of them.

- Accepted names: "C", "POSIX", "C.UTF-8", ".UTF-8", ".UTF8", ".65001" (the codeset is case-insensitive, "C" or
  "POSIX" may precede it), the composite name `LC_COLLATE=...;LC_CTYPE=...;...` returned by `setlocale`, and ""
  which is "C.UTF-8" when the ANSI code page is UTF-8 (65001) and "C" otherwise. A name with a language or
  another code page (`en-US.UTF-8`, `.1252`) fails with `NULL`.
- In the "C" locale a multibyte character is one byte, byte N is the wide character U+00NN, `MB_CUR_MAX` is 1.
- In "C.UTF-8" (`LC_CTYPE`) multibyte characters are UTF-8, `MB_CUR_MAX` is 4 and `_pctype` marks C2..F4 as
  `_LEADBYTE`. Overlong forms, surrogates and code points above U+10FFFF are invalid (`EILSEQ`).
- `wchar_t` is UTF-16. `mbrtowc` and `mbtowc` return a character above U+FFFF in two calls: the high surrogate
  when the 4-byte sequence has been read up to its third byte (returns 3), and the low surrogate after the
  fourth byte (returns 1). `mbstowcs`/`mbsrtowcs` produce the pair. `wcrtomb` and `wctomb` store a high
  surrogate and return 0, the low surrogate writes the 4 bytes. A lone surrogate is `EILSEQ`. `mbstowcs` and
  `wcstombs` never stop in the middle of a character or a pair.
- `mbrtoc16`, `c16rtomb`, `mbrtoc32` and `c32rtomb` use the encoding of the locale as the C standard requires,
  bytes in the "C" locale. The `<uchar.h>` of MinGW-w64 declares them for UCRT only.
- `towupper`, `towlower`, `_wcsupr`, `_wcslwr`, `_wcsicmp` and `_wcsnicmp` map wide characters by Unicode simple
  case mapping in "C.UTF-8", ASCII only in "C". Narrow case mapping (`toupper`, `_strupr`, `_stricmp`) is ASCII
  only and collation (`strcoll`, `wcscoll`) is code point order in both locales.
- `_l` functions use the code page of the locale object, `_create_locale` does not change the current locale.
- `_setmbcp` (the `_mbs*` functions) is independent of `setlocale` and cannot select UTF-8 (65001) or 54936
  (GB18030, which has no NLS table).

### printf and scanf

MinGW-w64 formats through `__mingw_*printf` in C++, with `__USE_MINGW_ANSI_STDIO` and for its inline `snprintf`.
The specs wrap these (`--wrap`) so that the conversions between narrow and wide strings are done by NtCRT
(`src/Format.cpp`):

- `%ls`/`%lc` of the narrow functions and `%hs`/`%hc` (`%s`/`%c` in C99 mode) of the wide functions convert by
  the current locale, an unconvertible character fails the call with -1 and `EILSEQ`
- the precision of a narrow `%ls` counts bytes and never cuts a character, the precision of a wide `%hs` counts
  `wchar_t` and never splits a surrogate pair

`btowc` and `wctob` of libmingwex are wrapped to NtCRT the same way.

### File names, console and streams

- The narrow file name functions (`fopen`, `_open`, `_stat`, `_mkdir`, `_findfirst`, `_fullpath`, `rename`,
  ...) convert names by UTF-8 when `LC_CTYPE` is "C.UTF-8" and by the ANSI code page otherwise. An invalid name
  fails with `EILSEQ`, there is no best-fit conversion. `argv`, `environ` and `_acmdln` are in the ANSI code
  page, which is UTF-8 with the `activeCodePage` manifest setting.
- When `LC_CTYPE` is "C.UTF-8", a text mode console fd writes and reads UTF-8 through `WriteConsoleW` and
  `ReadConsoleW` whatever the console code page is: a character split between writes is kept until it is
  complete, reads translate CRLF to LF and a Ctrl+Z at the start of a line is the end of the file. The console
  functions of `<conio.h>` (`_cputs`, `_getch`, ...) use UTF-8 likewise.
- `_getch` and `_getwch` take Alt and keypad digits as the composition of a character, which the console
  reports with the release of Alt. A pseudo console (Windows Terminal) types a character beyond U+FFFF in this
  way, one surrogate per composition.
- `getwc`, `putwc` and `ungetwc` of a narrow stream convert by UTF-8 with a state per stream in "C.UTF-8".
- `_O_U8TEXT` reads return the characters of complete UTF-8 sequences, a sequence split by the read size is
  completed by the next read and an invalid or truncated sequence is U+FFFD. `ftell` of a Unicode mode stream is
  exact, except between the two units of a surrogate pair, where no byte offset exists.

### Differences from msvcrt

- The locales above only, see "Locales"
- `tmpfile` creates the file in the temporary directory instead of the root of the current drive
- `stderr` is not buffered when it is redirected to a file
- No MSVC C++ exception handling and RTTI (`_CxxThrowException`, `__CxxFrameHandler`, `__RTDynamicCast`, ...),
  GCC programs use their own
- No `__pioinfo`, `__badioinfo` and `$I10_OUTPUT`
- Known msvcrt bugs are not reproduced, e.g. console reads in `_O_U16TEXT` mode translate CRLF to one LF
  (msvcrt returns two LFs and hangs when a CR ends the buffer), a binary fd keeps no Unicode read/write
  restriction from a previous `_setmode`, and `signal` returns `SIG_DFL` after `raise(SIGILL)`/`raise(SIGSEGV)`
- Unicode mode I/O:
  - `_O_U8TEXT` reads accept any buffer size, msvcrt reads at least 4 bytes and fails with `EINVAL` when they
    convert to more units than the buffer holds, and it drops a truncated sequence at the end of the file
  - `ftell` of a `_O_U8TEXT`/`_O_U16TEXT` read stream counts the bytes of the unread characters, msvcrt
    estimates them and `fseek` to its result can land elsewhere
  - `_O_U8TEXT`/`_O_U16TEXT` console fds read by `ReadConsoleW` and write to the handle of the fd, a surrogate
    pair split between writes is kept until it is complete
- `_getch`, `_getwch` and `_kbhit` take the character reported with the release of Alt, msvcrt reports the
  keypad digits typed with Alt as Alt+arrow keys and loses the character (UCRT too)
- In the "C" locale `mblen` is 1 for every non-NUL byte, msvcrt returns -1 for bytes 0x80..0xFF when `n` is 1
- The `printf` family returns -1 (`EILSEQ`) when a `%ls`, `%lc`, `%hs` or `%hc` argument cannot be converted,
  msvcrt writes a part of the output and returns its length

### Differences from UCRT

From `make compare` (UCRT of Visual Studio 2026, ANSI code page 950). UCRT names its UTF-8 locales by the user
locale (`setlocale(LC_ALL, ".UTF-8")` returns `Chinese (Traditional)_Taiwan.utf8`), NtCRT returns "C.UTF-8".

Where UCRT conforms to the C standard and NtCRT does not support it:
- UCRT accepts locale names with a language and other code pages, `setlocale(LC_ALL, "")` selects the user
  locale; NtCRT supports "C" and "C.UTF-8" only
- UCRT collates (`strcoll`, `wcscoll`) and maps narrow case (`_strupr`, `_strlwr`) by the locale, NtCRT by code
  point and ASCII
- `_setmbcp(65001)` succeeds in UCRT and fails in NtCRT

Where NtCRT differs on purpose (UCRT behavior in parentheses):
- "C.UTF-8" and "POSIX" are accepted (`NULL`)
- `mbtowc` converts 3-byte sequences (fails with `EILSEQ`). `mbrtowc` and `mbtowc` return a character above
  U+FFFF as a surrogate pair in two calls (`mbrtowc` returns U+FFFD, `mbtowc` fails)
- `wcrtomb`/`wctomb` accept a surrogate pair in two calls (`EILSEQ`); a lone low surrogate fails in both
- `mbrtoc16`, `c16rtomb`, `mbrtoc32` and `c32rtomb` use the locale encoding (always UTF-8)
- `wctob(0xE9)` is 233 in the "C" locale (-23, the byte is sign extended)
- `snprintf("%.4ls", L"a\x4e2d\x4e2d")` writes 4 bytes `a中` (9 bytes, the precision counts `wchar_t`)
- An unconvertible `%lc` or `%hc` fails the call with -1 (the character is skipped and the length returned)
- `sscanf("%ls")` and `swscanf("%hs")` convert UTF-8 and the "C" locale correctly (UTF-8 sequences become
  sign-extended bytes or are truncated)
- `_wcsicmp`/`_wcsnicmp` compare by Unicode case in "C.UTF-8" (ASCII only for `_wcsicmp`)
- `_mbstrlen` counts UTF-8 characters, 3 for `a中😀` (6)
- `ftell` of a `ccs=UTF-8` read stream is exact except inside a surrogate pair (wrong for every position after a
  character above U+FFFF)
- `_pctype` in "C.UTF-8" has the "C" classes and `_LEADBYTE` (UCRT adds `C1_DEFINED` to every class)

## Origin and license

Derived from [KNSoft.NtCRT](https://github.com/KNSoft/KNSoft.NtCRT) at commit
`dc8159b829f3274127225f527069a2664847052d`, under the MIT license, see `LICENSE`. `polyfill/` and
`test/upstream/` are copied from it, `include/KNSoft/NDK/` follows
[KNSoft.NDK](https://github.com/KNSoft/KNSoft.NDK) (MIT).
