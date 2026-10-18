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
- `test/upstream/` the KNSoft.NtCRT test suite, and `Msvcrt.cpp` with what the tests of Wine found
- `test/fuzz/` differential fuzz (`make test-fuzz`, `FUZZ_ITERATIONS=n`): the UTF-8 codec against kernel32, the
  printf and scanf families, strtod and strtol against ucrtbase.dll, strtold against the gdtoa of libmingwex
- `test/float/` `strtod`, `strtof` and `wcstod` against the 5.3 million lines of
  [parse-number-fxx-test-data](https://github.com/nigeltao/parse-number-fxx-test-data), and printf `%.17g`,
  `%.9g` and `%a` read back (`make test-float-corpus FLOAT_CORPUS=<its data directory>`)
- `test/printf/` printf against exact expected strings (`make test-printf`)
- `test/gofloat/` `strtod`, `strtof` and `wcstod` in a DLL tested by Go (`make test-go`, `GO_FUZZTIME=d`): the
  tables and test data of Go strconv, and `go test -fuzz` against `strconv.ParseFloat` (`math/big` past its
  limits) with the syntax of C
- `test/console/` console reads and writes in a pseudo console (`make test-console`), `conpty.exe` types the
  input and checks the screen
- `test/cxx/` libstdc++ on NtCRT (`make test-cxx`)
- `test/start/` pseudo relocations of data auto-imported from a DLL and `_matherr` (`make test-start`)
- `test/size/` small programs measured by `make size`
- `test/compare/` tests whose output is compared with UCRT, `run.sh` finds `vcvars64.bat` by vswhere or `VCVARS`
- `test/wine/` the msvcrt and ucrtbase tests of Wine 11.0 on NtCRT (`make test-wine`, downloads them into the build
  directory, LGPL): `GetProcAddress` of msvcrt.dll finds the functions of the program, `skip.txt` leaves out the
  tests of UCRT-only APIs
- `IDEAS.md` proposals not started yet

## Size

ld cannot drop the unused functions of an x64 PE image: the linker script keeps `.pdata`, which refers to every
function, so `--gc-sections` has no effect and a program links whole objects of `libntcrt.a`. The runtime is
split so that the startup code and small programs link few and small objects:

- what every program reaches is apart from the rest of its file: the startup of `Startup.cpp` (`getenv` and
  `_putenv` are `Environ.cpp`, `raise`, `_XcptFilter` and `abort` `Signal.cpp`, `_assert` `Assert.cpp`, threads
  `Thread.cpp`, `__wgetmainargs` `WideMain.cpp`), the descriptor table and the raw writes of fd 2 (`LowIOInit.cpp`),
  the last error, critical sections and TLS that libmingw32 uses (`Kernel32Lock.cpp`), the code pages and the
  strings converted by them (`CodePage.cpp`), malloc and free (`Heap.cpp`)
- low-level I/O is split into `_write` (`LowIO.cpp`), `_read`, opening, `stat` and the other descriptor functions,
  stdio into what `puts` and printf write by (`Stdio.cpp`), input, wide output, wide input, positioning, control,
  opening and temporary files, kernel32 into `LoadLibrary`, `WriteConsoleW` and the paths that `_open` and `remove`
  use (`FilePath.cpp`) beside the rest
- the printf engine is a template (`Printf.inl`) instantiated for narrow (`Printf.cpp`) and wide (`PrintfWide.cpp`)
  output, the multibyte functions keep the codec, `mbrtowc` and `wcrtomb` that printf uses in `MultibyteChar.cpp`
- the `_l` functions are beside the functions they call or in `Locale*.cpp`, locale objects, character class
  tables and scanf are objects of their own, and stdio is flushed at exit by a pointer that stdio sets
- GCC builds without `-fdata-sections`, which puts zero data in `.data$` sections that take space in the file
  (`.bss` does not)

`libntcrt_start.a` replaces the pseudo relocator and `_matherr` of libmingw32, which report errors by `fprintf` and
would link the printf family into every program.

lld (`make LLVM=1`) drops the unused functions: clang makes the `.pdata` and `.xdata` of a function associative to
it, so a program links the functions it reaches. The libraries of GCC (libmingwex, libstdc++) are still linked by
object. What lld keeps is what the startup code reaches and the sections that are not COMDAT in the objects it
loads, so:

- the export stubs (`__imp_Name` and the jump `Name`) are COMDAT
- the functions of libmingwex that libntcrt replaces (printf, `wcstoll`, `btowc`, ...) are not wrapped for lld,
  which takes them from libntcrt, the first library that defines them: with `--wrap` it loads the member of
  libmingwex for a reference and every object of a `__wrap_` function. The wrappers left (scanf, the strtod
  family) refer to libmingwex by `__real_` only (which loads nothing, see "LLVM"), and the objects with functions
  that call libmingwex are apart: `ScanfWrap.cpp` from `StdioScanf.cpp`, `Integer.cpp` and `Perror.cpp` from
  `String.cpp`, `_atoldbl` in `LocaleConv.cpp`
- no `.CRT$XI*` initializer for data that can be constant, `_ctype` is initialized data
- the functions the startup code refers to in every program (`_amsg_exit`, `abort`, `_matherr`) write their ASCII
  messages to the handle of fd 2 as msvcrt does, `_write` would link the console and text mode conversions

Stripped sizes in bytes (`make size`), before the UTF-8 locale work, before the split, now, and with lld:

| Program | Before UTF-8 | Before split | Now | lld | msvcrt.dll |
|---|---:|---:|---:|---:|---:|
| `return 0` | 248832 | 445440 | 37376 | 25088 | 14336 |
| `puts` | 248832 | 445440 | 48128 | 35328 | 14336 |
| `printf("%d")` | 248832 | 445440 | 81920 | 66048 | 41984 |
| `wprintf` | 248832 | 445440 | 82432 | 69120 | 41472 |
| `std::cout` | 1257472 | 1420800 | 1179648 | 748544 | 1035776 |

The msvcrt.dll column is the default MinGW-w64 runtime of GCC, which imports it. A program with printf links the
narrow engine (`src/Printf.cpp`), the digits of `src/FloatFormat.cpp` and the decimal of `src/Float.cpp`, with ld
all of them. What ld links beyond lld is the unused functions of these objects, most of all the code page
conversions of the PolyFill (`MultiByteToWideChar` and `GetCPInfo` beside the `WideCharToMultiByte` of `argv`,
one object of the shared sources).

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

The printf family is NtCRT's own (`src/Printf.inl`): the functions of msvcrt, and the `__mingw_*printf` that
MinGW-w64 formats through in C++, with `__USE_MINGW_ANSI_STDIO` and for its inline `snprintf` (wrapped by the
specs, defined by libntcrt for lld). The printf of libmingwex (`pformat`, `gdtoa`) is not linked. It follows C and
takes the conventions of msvcrt beside:

- C: the flags, width and precision (also `*` and `*m$`), positional arguments (`%n$`, POSIX), `hh`, `h`, `l`,
  `ll`, `j`, `z`, `t` and `L` (an 80-bit long double), `%a`, `%n`. Floating point numbers are exact and round ties
  to even, by the digits of `src/FloatFormat.cpp` (the ftoa of Go 1.27: unrounded scaling for up to 18 digits, the
  multiprecision decimal for more and for long double). A length beyond `INT_MAX` fails with `EOVERFLOW`.
- msvcrt: the length modifiers `I`, `I32`, `I64` and `w`, `%S`/`%C` (the other width than the function), `%Z`
  (`ANSI_STRING`, `%wZ` `UNICODE_STRING`), `%p` as 16 uppercase digits, and in the wide functions of msvcrt
  (`swprintf`, `_snwprintf`, `fwprintf`, ...) `%s`/`%c` wide and `%S`/`%C` narrow, where the C99 ones of MinGW-w64
  take `%s`/`%c` narrow. `_snprintf` and `_snwprintf` write no terminator when the output fills the buffer and
  return -1 when it does not fit, `snprintf` truncates as C does.
- `%ls`/`%lc` of the narrow functions and the narrow strings of the wide functions convert by the current locale,
  an unconvertible character fails the call with -1 and `EILSEQ` and a stream gets no output. The precision of a
  narrow `%ls` counts bytes and never cuts a character, the precision of a narrow string in a wide function counts
  `wchar_t` and never splits a surrogate pair.
- A conversion it does not know is copied to the output as it is. NaN has its sign (`-nan`).

`strtod`, `strtof`, `wcstod` and `wcstof` (and `__mingw_strtod` and the like, wrapped) are `src/FloatParse.cpp`, a
port of the floating point parser of Go 1.27 (`internal/strconv`, by Russ Cox: unrounded scaling by a 128-bit power
of 10, the exact multiprecision decimal as fallback) to the syntax of C: correctly rounded, ties to even, with any
number of digits. `strtold` and `wcstold` (80-bit) are NtCRT's too, a decimal number as a big integer times or
divided by a power of 5, the gdtoa of libmingwex is not linked: the scanf of mingw-w64, `_atoldbl` and libstdc++
call them. They convert nothing to 0 (gdtoa: -0 after a minus sign) and read `nan(...)` with letters, digits and
`_` only. The scanf of mingw-w64 is
wrapped to accept `I64` after a width (`%4I64d`), and `wcstoll`, `wcstoull`, `wcstoimax` and `wcstoumax` of
libmingwex to convert "0x" without a hex digit as 0 with the end after the "0". `make test-go` checks the parser
with the tests of the Go source it is ported from.

`test/fuzz/format.c` compares these with ucrtbase.dll, `__stdio_common_vsprintf` in its C mode and its legacy
modes, `test/printf/printf.c` (`make test-printf`) checks what UCRT does not do (long double, positional
arguments, `%Z`) against exact expansions. Differences the fuzz does not report, where UCRT or mingw-w64 is the
one off:

- UCRT rounds exact decimal ties of printf away from zero (`%.0f` of 2.5 is 3, NtCRT: 2, ties to even), and those
  of `strtof`; it reads no more than 768 significant digits in `strtod`, the fuzz checks those inputs, midpoints
  of two doubles or floats and a digit more or less, against the exact result instead
- UCRT `%#E` of infinity is `I.NF`, of a negative NaN `-nan(ind)`; it counts the width of a narrow `%ls` in
  `wchar_t`; its `%a` without a precision keeps the trailing zeros (`0x1.0000000000000p+0`, NtCRT: `0x1p+0`)
- the scanf of mingw-w64 takes a prefix of a number that is not a number (`"1e"`, `"0x"`, `"nan("`) where C has a
  matching failure, and its narrow `%lf` rounds to long double first, then to double: a number whose long double
  is halfway between two doubles is 1 ulp off (the wide scanf calls `wcstod`)
- UCRT `strtol` of "0x" without a hex digit converts nothing (C: 0, the end after "0")

`btowc` and `wctob` of libmingwex are replaced by NtCRT the same way, and `ftruncate64` (used by
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
- The `printf` family follows C where msvcrt has its own output: the exponent has two digits or more (`1e+00`,
  msvcrt `1e+000`, `_set_output_format` only records its argument), infinity and NaN are `inf` and `nan` (msvcrt
  `1.#INF00`, `1.#QNAN0`) and the `0` flag pads `%c` and `%s` with spaces (msvcrt with zeros)
- The scanf functions of msvcrt are the C99 scanf of MinGW-w64 (see "printf and scanf"): `%p` takes a `0x`
  prefix, `%Lf` stores an 80-bit long double, and a reversed range in a set is no range (`%[c-a]` reads "ca" of
  "cab", msvcrt reads a to c)
- `strftime` has the C99 conversions (`%C`, `%D`, `%e`, `%F`, `%g`, `%G`, `%h`, `%n`, `%R`, `%t`, `%T`, `%u`, `%V`),
  msvcrt returns 0
- `strtod` and `wcstod` set `ERANGE` for a denormal result as well as for an underflow to zero, msvcrt for zero
  only; `_atodbl` reports either as `_UNDERFLOW`
- `wcstol` and the other wide integer conversions read ASCII digits only, msvcrt also takes other Unicode decimal
  digits (U+0660, U+FF10, ...)
- `_winver`, `_winmajor`, `_winminor` and `_osver` are the version of Windows, msvcrt reports 6.2 to a program
  without a compatibility manifest
- `_wenviron` is set at startup in a program with `main`, msvcrt leaves it `NULL`
- `_get_heap_handle` returns the process heap, which `malloc` allocates from (msvcrt has a heap of its own)
- 512 streams at most: `_setmaxstdio` above 512 fails with `EINVAL` (msvcrt takes up to 2048)
- `getenv_s` with a `NULL` name returns `EINVAL` as UCRT, msvcrt returns 0
- `_vswprintf_l` takes a count after the buffer as the MinGW-w64 headers declare it, the export of msvcrt.dll has
  none (`buffer, format, locale, args`)
- `_strnicmp` takes any count, msvcrt returns `_NLSCMPERROR` for one above `INT_MAX`
- `fopen` takes the mode `x` of C11 (`"wx"` fails with `EEXIST` for an existing file), msvcrt fails with
  `EINVAL`

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

`src/Float.cpp`, `src/Float.inl`, `src/FloatParse.cpp`, `src/FloatFormat.cpp` and `src/FloatPow10.inl` are ported
from the Go 1.27 source (`src/internal/strconv`), under the BSD 3-clause license of The Go Authors, whose notice is in
`src/Float.cpp`.
