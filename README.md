# ntcrt-mingw

A C runtime for MinGW-w64 (x64, GCC or clang) that imports `ntdll.dll` only. C and C++ programs link the msvcrt
functions from this library instead of `msvcrt.dll` and `kernel32.dll`.

```sh
make            # build/libntcrt.a, the support libraries and build/ntcrt.specs
make test       # build and run test/upstream, the fuzz, console, libstdc++ and startup tests
make size       # the size of the programs of test/size
make compare    # build test/compare with NtCRT and with UCRT (cl of Visual Studio) and show the differences
g++ -static -specs=build/ntcrt.specs -o hello.exe hello.cpp

make LLVM=1     # the same with clang into build-llvm (LLVM=<dir>/ for clang in <dir>), make LLVM=1 test, ...
clang++ --config=build-llvm/ntcrt.cfg -static -o hello.exe hello.cpp
```

## Layout

- `src/` the runtime, `NtCRT.h` is the internal header
- `src/start/` replacements of libmingw32 members in `libntcrt_start.a`, linked before libmingw32
- `polyfill/` code page and console helpers built on ntdll
- `include/KNSoft/NDK/` the part of KNSoft.NDK used by the sources, on top of MinGW-w64 headers
- `test/upstream/` the KNSoft.NtCRT test suite
- `test/fuzz/` differential fuzz (`make test-fuzz`, `FUZZ_ITERATIONS=n`): the UTF-8 codec against kernel32, the
  printf and scanf families, strtod and strtol against ucrtbase.dll
- `test/console/` console reads and writes in a pseudo console (`make test-console`), `conpty.exe` types the
  input and checks the screen
- `test/cxx/` libstdc++ on NtCRT (`make test-cxx`)
- `test/start/` pseudo relocations of data auto-imported from a DLL and `_matherr` (`make test-start`)
- `test/size/` small programs measured by `make size`
- `test/compare/` tests whose output is compared with UCRT, `run.sh` finds `vcvars64.bat` by vswhere or `VCVARS`

## Size

ld cannot drop the unused functions of an x64 PE image: the linker script keeps `.pdata`, which refers to every
function, so `--gc-sections` has no effect and a program links whole objects of `libntcrt.a`. The runtime is
split so that the startup code links few of them: the `_l` functions are beside the functions they call or in
`Locale*.cpp`, locale objects, character class tables, printf, wide printf and scanf are objects of their own,
and stdio is flushed at exit by a pointer that stdio sets. `libntcrt_start.a` replaces the pseudo relocator and
`_matherr` of libmingw32, which report errors by `fprintf` and would link the printf family into every program.

lld (`make LLVM=1`) drops the unused functions: clang makes the `.pdata` and `.xdata` of a function associative to
it, so a program links the functions it reaches. The libraries of GCC (libmingwex, libstdc++) are still linked by
object. What lld keeps is what the startup code reaches and the sections that are not COMDAT in the objects it
loads, so:

- the export stubs (`__imp_Name` and the jump `Name`) are COMDAT
- lld loads the object of every `__wrap_` function of `--wrap`, used or not, and every object these refer to. They
  refer to libmingwex by `__real_` only (which loads nothing, see "LLVM"), and the objects with functions that
  call libmingwex are apart: `ScanfWrap.cpp` from `StdioScanf.cpp`, `Integer.cpp` and `Perror.cpp` from
  `String.cpp`, `_atoldbl` in `LocaleConv.cpp`
- no `.CRT$XI*` initializer for data that can be constant, `_ctype` is initialized data
- the functions the startup code refers to in every program (`_amsg_exit`, `abort`, `_matherr`) write their ASCII
  messages to the handle of fd 2 as msvcrt does, `_write` would link the console and text mode conversions

Stripped sizes in bytes (`make size`), before the UTF-8 locale work, before the split, now, and with lld:

| Program | Before UTF-8 | Before split | Now | lld | msvcrt.dll |
|---|---:|---:|---:|---:|---:|
| `return 0` | 248832 | 445440 | 114176 | 30208 | 14336 |
| `puts` | 248832 | 445440 | 143872 | 40448 | 14336 |
| `printf("%d")` | 248832 | 445440 | 240640 | 78336 | 41984 |
| `wprintf` | 248832 | 445440 | 244736 | 80896 | 41472 |
| `std::cout` | 1257472 | 1420800 | 1249792 | 783360 | 1035776 |

The msvcrt.dll column is the default MinGW-w64 runtime of GCC, which imports it. A program with printf links the
C99 printf of libmingwex (`pformat`, `gdtoa`), and with ld all of `src/Float.cpp` for the digits of `%g`.

## LLVM

`make LLVM=1` builds the runtime with clang and writes `build-llvm/ntcrt.cfg`, a clang configuration file that
does what the specs do and links by lld. clang uses the headers and libraries of the GCC found by `g++`
(libstdc++, libgcc and the msvcrt flavor of MinGW-w64), the msys2 CLANG64 toolchain works with
`LLVM=<msys2>/clang64/bin/`. Its own MinGW-w64 is built for UCRT and is not used.

- lld drops the unused functions of the runtime (`--gc-sections`), see "Size".
- lld resolves a symbol to the first library that defines it, wherever the reference is. libntcrt precedes
  libmingwex, so `btowc`, `wctob` and `ftruncate64` are those of NtCRT without `--wrap`. The math functions that
  both libmingwex and ntdll define (`sin`, `cos`, `log`, `pow`, `sqrt`, ...) are those of libmingwex for NtCRT too:
  `_j0`, `_y0` and the other Bessel functions may differ from the GCC build in the last bits.
- With `--wrap=X`, lld resolves `__real_X` to the `X` of an archive member only if something else loads that
  member (a reference to `X`); otherwise to address 0, without an error. GNU ld loads it. A `__wrap_X` reached
  only through `X` is safe, NtCRT calls no `__real_` function from elsewhere.
- The `__cpuidex` of MinGW-w64 headers before 12 clashes with `<cpuid.h>` of clang and is left out
  (`-D__INTRINSIC_DEFINED___cpuidex`).

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
- `_isctype`, `__iscsym` and `__iscsymf` classify by the current locale (`_LEADBYTE` for C2..F4 in "C.UTF-8").
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

In front of the printf of mingw-w64, `Format.cpp` rewrites the format where it does not follow C: the space and
`+` flags of `%u` are dropped, the `0` flag with `-`, a negative `*` width or the precision of an integer
conversion, `%#.0o` of 0 prints `0`, and `%g` without `#` becomes the `%e` or `%f` it selects with the precision
of its digits without trailing zeros (`%20.17g` of 1e13 lost the width). The Windows length modifiers `I64`,
`I32` and `I`, `%S`/`%C` and the legacy wide `%s`/`%c` of `_snwprintf` are kept beside `hh`, `ll`, `j`, `z`, `t`
and `%ls` of C.

`strtod`, `strtof`, `wcstod` and `wcstof` (and `__mingw_strtod` and the like, wrapped) are `src/Float.cpp`, a port
of the floating point parser of Go 1.27 (`internal/strconv`, by Russ Cox: unrounded scaling by a 128-bit power of
10, the exact multiprecision decimal as fallback) to the syntax of C: correctly rounded, ties to even, with any
number of digits. `strtold` stays the gdtoa of libmingwex, which the wrapper corrects where it converts nothing
(0, not -0) or reads a `nan(...)` with other characters than letters, digits and `_`. The scanf of mingw-w64 is
wrapped to accept `I64` after a width (`%4I64d`), and `wcstoll`, `wcstoull`, `wcstoimax` and `wcstoumax` of
libmingwex to convert "0x" without a hex digit as 0 with the end after the "0".

`test/fuzz/format.c` compares these with ucrtbase.dll, `__stdio_common_vsprintf` in its C mode and its legacy
modes. Differences it does not report, where UCRT or mingw-w64 is the one off:

- UCRT rounds exact decimal ties of printf away from zero (`%.0f` of 2.5 is 3, NtCRT: 2, ties to even), and those
  of `strtof`; it reads no more than 768 significant digits in `strtod`, the fuzz checks those inputs, midpoints
  of two doubles or floats and a digit more or less, against the exact result instead
- UCRT `%#E` of infinity is `I.NF`; it counts the width of a narrow `%ls` in `wchar_t`
- the wide printf of mingw-w64 writes nothing for `%c` or `%lc` of the null character
- the scanf of mingw-w64 takes a prefix of a number that is not a number (`"1e"`, `"0x"`, `"nan("`) where C has a
  matching failure, and its narrow `%lf` rounds to long double first, then to double: a number whose long double
  is halfway between two doubles is 1 ulp off (the wide scanf calls `wcstod`)
- UCRT `strtol` of "0x" without a hex digit converts nothing (C: 0, the end after "0")

`btowc` and `wctob` of libmingwex are wrapped to NtCRT the same way, and `ftruncate64` (used by
`std::filesystem::resize_file`), which checks the free space of the volume by kernel32 functions NtCRT does not
have.

### libstdc++

`test/cxx` (`make test-cxx`) checks libstdc++ on NtCRT. Its generic locale model converts by the C library, so in
the "C.UTF-8" locale set by `setlocale`:

- `std::wcout`/`std::wcin` (by `putwc`/`getwc`), `std::wofstream`/`std::wifstream` and
  `std::codecvt<wchar_t, char, mbstate_t>` convert UTF-8, an invalid sequence is a conversion error
- a narrow file name (`std::ofstream`, `std::filesystem::path` from `std::string`, `path::string()`) is UTF-8
- `std::format`, `std::to_string`, exceptions, `std::thread` and `std::filesystem` work, the program imports
  ntdll.dll only

`std::locale("C.UTF-8")` and other named locales throw `std::runtime_error`: the generic model of libstdc++
knows "C" and "POSIX" only. Set the C library locale with `setlocale` and use the classic `std::locale`.

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
- The default `_matherr` (installed when a program sets `_MINGW_INSTALL_DEBUG_MATHERR`) prints the error and the
  function name without the arguments, the one of libmingw32 formats them by `fprintf`
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

`src/Float.cpp` and `src/FloatPow10.inl` are ported from the Go 1.27 source (`src/internal/strconv`), under the
BSD 3-clause license of The Go Authors, whose notice is in `src/Float.cpp`.
