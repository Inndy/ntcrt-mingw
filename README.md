# ntcrt-mingw

A C runtime for MinGW-w64 (x64, GCC or clang) that imports `ntdll.dll` only. C and C++ programs link the msvcrt
functions from this library instead of `msvcrt.dll` and `kernel32.dll`.

```sh
make            # build/libntcrt.a, the support libraries and build/ntcrt.specs
make test       # build and run test/upstream, the fuzz, console, libstdc++, thread and startup tests
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
- `include/threads.h` C11 threads, the drivers add `include/` to the system include path of programs
- `test/upstream/` the KNSoft.NtCRT test suite, and `Msvcrt.cpp` with what the tests of Wine found
- `test/fuzz/` differential fuzz (`make test-fuzz`, `FUZZ_ITERATIONS=n`): the UTF-8 codec against kernel32, the
  printf and scanf families, strtod and strtol against ucrtbase.dll, strtold against the gdtoa of libmingwex
- `test/float/` `strtod`, `strtof` and `wcstod` against the 5.3 million lines of
  [parse-number-fxx-test-data](https://github.com/nigeltao/parse-number-fxx-test-data), and printf `%.17g`,
  `%.9g` and `%a` read back (`make test-float-corpus FLOAT_CORPUS=<its data directory>`)
- `test/printf/` printf against exact expected strings (`make test-printf`)
- `test/scanf/` scanf where the fuzz cannot compare with UCRT (`make test-scanf`), its link map shows no scanf of
  libmingwex
- `test/gofloat/` `strtod`, `strtof` and `wcstod` in a DLL tested by Go (`make test-go`, `GO_FUZZTIME=d`): the
  tables and test data of Go strconv, and `go test -fuzz` against `strconv.ParseFloat` (`math/big` past its
  limits) with the syntax of C
- `test/console/` console reads and writes in a pseudo console (`make test-console`), `conpty.exe` types the
  input and checks the screen
- `test/cxx/` libstdc++ on NtCRT (`make test-cxx`)
- `test/thread/` the pthread functions, `<threads.h>` and the threads of libstdc++ (`make test-thread`)
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
- the scanf engine likewise (`Scanf.inl`): narrow (`Scanf.cpp`) and wide (`ScanfWide.cpp`) with the functions that
  scan a string, the streams apart (`StdioScanf.cpp`, `StdioWscanf.cpp`), the console and `_l` functions in
  `LocaleScanf.cpp`: `sscanf` links neither the wide engine nor the streams, printf, `printf_s` and `_getch` no scanf
- the `_l` functions are beside the functions they call or in `Locale*.cpp`, locale objects and character class
  tables are objects of their own, and stdio is flushed at exit by a pointer that stdio sets
- the pthread functions are objects by use (`Pthread*.cpp`): mutexes, condition variables, read-write locks,
  barriers and spin locks, semaphores, once, keys, the waits by address, threads, names; libstdc++ links the
  first ones in every C++ program (`std::cout` too), a program without threads none
- GCC builds without `-fdata-sections`, which puts zero data in `.data$` sections that take space in the file
  (`.bss` does not)

`libntcrt_start.a` replaces the pseudo relocator and `_matherr` of libmingw32, which report errors by `fprintf` and
would link the printf family into every program.

lld (`make LLVM=1`) drops the unused functions: clang makes the `.pdata` and `.xdata` of a function associative to
it, so a program links the functions it reaches. The libraries of GCC (libmingwex, libstdc++) are still linked by
object. What lld keeps is what the startup code reaches and the sections that are not COMDAT in the objects it
loads, so:

- the export stubs (`__imp_Name` and the jump `Name`) are COMDAT
- the functions of libmingwex that libntcrt replaces (printf, scanf, the strtod family, `wcstoll`, `btowc`, ...)
  are not wrapped for lld, which takes them from libntcrt, the first library that defines them: with `--wrap` it
  loads the member of libmingwex for a reference and every object of a `__wrap_` function. The objects with
  functions that call libmingwex are apart: `Integer.cpp` and `Perror.cpp` from `String.cpp`, `_atoldbl` in
  `LocaleConv.cpp`
- no `.CRT$XI*` initializer for data that can be constant, `_ctype` is initialized data
- the functions the startup code refers to in every program (`_amsg_exit`, `abort`, `_matherr`) write their ASCII
  messages to the handle of fd 2 as msvcrt does, `_write` would link the console and text mode conversions

Stripped sizes in bytes (`make size`), before the UTF-8 locale work, before the split, now, and with lld:

| Program | Before UTF-8 | Before split | Now | lld | msvcrt.dll |
|---|---:|---:|---:|---:|---:|
| `return 0` | 248832 | 445440 | 37376 | 24576 | 14336 |
| `puts` | 248832 | 445440 | 47616 | 35328 | 14336 |
| `printf("%d")` | 248832 | 445440 | 81920 | 66048 | 41984 |
| `wprintf` | 248832 | 445440 | 82432 | 69120 | 41472 |
| `sscanf("%d %lf %s")` | | | 96768 | 61440 | 56320 |
| `std::cout` | 1257472 | 1420800 | 1155584 | 720896 | 1035776 |
| `std::thread` | | | 187392 | 100352 | 135168 |

The msvcrt.dll column is the default MinGW-w64 runtime of GCC, which imports it (and links winpthreads). A program
with printf links the narrow engine (`src/Printf.cpp`), the digits of `src/FloatFormat.cpp` and the decimal of
`src/Float.cpp`, with ld all of them. What ld links beyond lld is the unused functions of these objects, most of all
the code page conversions of the PolyFill (`MultiByteToWideChar` and `GetCPInfo` beside the `WideCharToMultiByte` of
`argv`, one object of the shared sources). A program with sscanf links the narrow scanf engine (`src/Scanf.cpp`)
and the parser of `src/FloatParse.cpp`. With the scanf of libmingwex behind a format rewrite, the narrow and wide
scanf of mingw-w64 and the streams, it was 213504 bytes (lld 95232).

With winpthreads, before the pthread functions of NtCRT, `std::cout` was 1183232 bytes (lld 752640) and
`test/size/thread.cpp` (`std::thread` and `std::mutex`) 248832 (lld 165888): winpthreads linked the kernel32 shims
of events and waits and its own objects whole.

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
- `ntcrt.cfg` wraps nothing: libntcrt defines the functions that the specs wrap under their names too. With
  `--wrap=X`, lld would load the member of libmingwex for a reference to `X`, and resolve `__real_X` to it only if
  something else loads that member (otherwise to address 0, without an error); NtCRT calls no `__real_` function.
- The `__cpuidex` of MinGW-w64 headers before 12 clashes with `<cpuid.h>` of clang and is left out
  (`-D__INTRINSIC_DEFINED___cpuidex`).
- Thread-local variables are emulated (`-femulated-tls`) as GCC does: libstdc++ keeps the state of
  `std::call_once` in its emulated `thread_local` variables.
- The directory of the libraries is a `-L` of the driver, which precedes the directories of the toolchain:
  `-lpthread` finds the empty `libpthread.a` of NtCRT, not winpthreads.

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

The scanf family is NtCRT's own too (`src/Scanf.inl`): the functions of msvcrt, their `_s` and `_l` forms, and the
`__mingw_*scanf` (and the `__ms_v*scanf` of libmingwex) that MinGW-w64 calls in C++ and with
`__USE_MINGW_ANSI_STDIO`, wrapped by the specs and defined by libntcrt for lld. The scanf of libmingwex is not
linked. A string is read in place, a stream under its lock by `_getc_nolock` and `_getwc_nolock` with one character
of pushback at most, the console functions (`_cscanf`, ...) read a line and scan it. It follows C (7.21.6.2,
7.29.2.2) and takes the conventions of msvcrt beside:

- C: the input item is the longest prefix of a matching sequence within the width. A prefix that is not a matching
  sequence (`"1e"` for `%f`, `"0x"` for `%x`, `"-"` for `%d`, `"infi"`, `"nan(x"`) is a matching failure and its
  characters are consumed, as the `"100ergs"` of the examples of C. EOF is returned for an input failure (the end of
  the input, an encoding error of a stream) before the first conversion, a suppressed one (`%*d`) and `%n` count.
  `%c`, `%[` and `%n` do not skip white space, `%%` does. `hh`, `h`, `l`, `ll`, `j`, `z`, `t` and `L`, `%a`, `%p`
  (16 hex digits as printf writes them, with or without `0x`), positional arguments (`%n$`) and `m` (`%ms`, an
  allocated string, POSIX: no memory is an error, `ENOMEM` and EOF before the first conversion).
- Integers are the result of `strtoll` (`%d`, `%i`) or `strtoull` (`%u`, `%o`, `%x`, `%p`, which take a sign too),
  base prefixes for `%i`, stored in the size of the type: the low bits of a number out of its range, `ERANGE` and
  the limit beyond 64 bits, as UCRT. Floating point numbers are converted by `src/FloatParse.cpp` (below) at the
  precision of their type, `float`, `double` or the 80-bit long double of `%Lf`, with `inf`, `infinity`, `nan`,
  `nan(n-char-sequence)` and hex numbers, `ERANGE` as `strtod`.
- msvcrt: the length modifiers `I`, `I32`, `I64` (also after a width, `%4I64d`) and `w`, `%S`/`%C` (the other width
  than the function), `%P` as `%p`, the width, `*` and the length modifiers in any order (`%h02d`), an unknown
  conversion character matches itself (`%"`), and in the wide functions of msvcrt `%s`/`%c`/`%[` wide and
  `%S`/`%C`/`%hs` narrow, where the C99 ones of MinGW-w64 (`__mingw_swscanf`, ...) take `%s`/`%c`/`%[` narrow and
  `%ls`/`%S` wide. The `_s` functions take the size of the array after the argument of `%c`, `%s` and `%[` (an
  `unsigned`, in elements): an input that does not fit is a matching failure with `ENOMEM` and the array emptied
  (the character that does not fit is read), as msvcrt. A null array stops the scan with `EINVAL` as UCRT (msvcrt
  crashes).
- `%ls`, `%lc` and `%l[` of the narrow functions and the narrow strings of the wide functions convert by the current
  locale (the locale object of the `_l` functions) as `mbrtowc` and `wcrtomb`, a character above U+FFFF is a
  surrogate pair; an invalid or cut sequence is a matching failure with `EILSEQ`. The width counts the characters of
  the input: bytes, multibyte characters for a wide string of a narrow function (`%lc` reads one), `wchar_t` in the
  wide functions (a surrogate pair is two).
- A scan set has `]` first as a member and `^` for its complement. A `-` between two characters is the range from
  the one before to the one after if they are in order (`%[a-c-e]` is `a` to `c` and `c` to `e`, as glibc), a
  reversed one (`%[c-a]`) or one at an end is a member. The scan set of a narrow function is of bytes, of a wide one
  of `wchar_t`.

`strtod`, `strtof`, `wcstod` and `wcstof` (and `__mingw_strtod` and the like, wrapped) are `src/FloatParse.cpp`, a
port of the floating point parser of Go 1.27 (`internal/strconv`, by Russ Cox: unrounded scaling by a 128-bit power
of 10, the exact multiprecision decimal as fallback) to the syntax of C: correctly rounded, ties to even, with any
number of digits. `strtold` and `wcstold` (80-bit) are NtCRT's too, a decimal number as a big integer times or
divided by a power of 5, the gdtoa of libmingwex is not linked: `_atoldbl` and libstdc++ call them. They convert
nothing to 0 (gdtoa: -0 after a minus sign) and read `nan(...)` with letters, digits and `_` only. `wcstoll`,
`wcstoull`, `wcstoimax` and `wcstoumax` of libmingwex are wrapped to convert "0x" without a hex digit as 0 with the
end after the "0". `make test-go` checks the parser with the tests of the Go source it is ported from.

`test/fuzz/format.c` compares these with ucrtbase.dll, `__stdio_common_vsprintf` and `__stdio_common_vsscanf` in
their C mode and their legacy modes, `test/printf/printf.c` (`make test-printf`) checks what UCRT does not do (long
double, positional arguments, `%Z`) against exact expansions, `test/scanf/scanf.c` (`make test-scanf`) the examples
of C, streams, the `_s`, `_l` and C99 wide functions, the conventions of msvcrt and the locale. Differences the
fuzz does not report, where UCRT is the one off:

- UCRT rounds exact decimal ties of printf away from zero (`%.0f` of 2.5 is 3, NtCRT: 2, ties to even), and those
  of `strtof`; it reads no more than 768 significant digits in `strtod`, the fuzz checks those inputs, midpoints
  of two doubles or floats and a digit more or less, against the exact result instead
- UCRT `%#E` of infinity is `I.NF`, of a negative NaN `-nan(ind)`; it counts the width of a narrow `%ls` in
  `wchar_t`; its `%a` without a precision keeps the trailing zeros (`0x1.0000000000000p+0`, NtCRT: `0x1p+0`)
- UCRT scanf returns EOF when the input ends in an item that is not a matching sequence (`"1e"` for `%f`, `"-"` for
  `%d`) and nothing was assigned, C has a matching failure (0); its `%ls` and `%lc` of a narrow function sign-extend
  the bytes 0x80..0xFF of the "C" locale (`"\xe9"` is U+FFE9)
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
- `std::format`, `std::to_string`, exceptions, `std::thread` (see "Threads") and `std::filesystem` work, the
  program imports ntdll.dll only

`std::locale("C.UTF-8")` and other named locales throw `std::runtime_error`: the generic model of libstdc++
knows "C" and "POSIX" only. Set the C library locale with `setlocale` and use the classic `std::locale`.

### Threads

libntcrt has the POSIX threads of `<pthread.h>`, `<semaphore.h>`, `<sched.h>` and `<pthread_time.h>` with the ABI
of the winpthreads headers of MinGW-w64 (the types and their static initializers), and C11 `<threads.h>` on them.
libstdc++ (`std::thread`, `std::mutex`, `std::condition_variable`, `std::shared_mutex`, `std::call_once`, futures,
`std::jthread`, `std::counting_semaphore`, ...) and libgcc (emutls, the `thread_local` of GCC) call them. winpthreads
is not linked: the specs and `ntcrt.cfg` have no `-lpthread`, and the empty `libpthread.a` and `libwinpthread.a` of
the build directory take its place for `-lpthread` and `-lwinpthread` (`-pthread` adds nothing). The kernel32
functions winpthreads used (events, semaphores, `WaitForMultipleObjects`, ...) stay in `src/Kernel32.cpp` for other
code.

- A mutex, condition variable, read-write lock, spin lock, barrier or semaphore is the word of its type, nothing is
  allocated and no kernel object created: a static initializer or 0 is a valid object and `pthread_*_destroy` only
  checks. A thread waits by address (`src/PthreadWait.cpp`) on the keyed event of the system
  (`NtWaitForKeyedEvent`, Vista and later) with its node in a table of buckets, as `RtlWaitOnAddress` of Windows 8.
- Mutexes are normal (`PTHREAD_MUTEX_DEFAULT`, a normal mutex locked again by its owner deadlocks, any thread may
  unlock it), errorcheck (`EDEADLK`, `EPERM`) or recursive (`EPERM` for another thread, `EAGAIN` past 2^28 levels),
  timed by `pthread_mutex_timedlock`. A condition wait unlocks a recursive mutex whatever its count and restores it.
- Condition variables wait by `CLOCK_REALTIME` or `CLOCK_MONOTONIC` (`pthread_condattr_setclock`), or a relative
  time (`pthread_cond_timedwait_relative_np`). An absolute `CLOCK_REALTIME` time follows changes of the clock.
- Read-write locks prefer readers: a thread may take a read lock again while a writer waits, so writers can starve. A
  writer locking again gets `EDEADLK` (`EBUSY` from the try functions). A barrier counts up to 2^22 - 1 threads
  (`EINVAL` beyond).
- `pthread_once` (and `call_once`, `std::call_once`) is reset when the routine exits by an exception (an unwind
  handler of x64 SEH on the frame that calls it) or is cancelled, another call runs the routine again (with
  winpthreads `std::call_once` deadlocks after an exception).
- Keys are TLS slots (`TlsAlloc`, 1088 for all users). The destructors run at the exit of any thread, also of
  `CreateThread`, `_beginthreadex` and `_endthreadex`, in the TLS callback of NtCRT after that of libmingw32, which
  runs the `thread_local` destructors of C++: emutls frees their memory in a key destructor.
  `PTHREAD_DESTRUCTOR_ITERATIONS` (256) rounds while values are set again. They run under the loader lock, a
  destructor must not wait for a thread that starts or ends.
- `pthread_create` creates a thread by `RtlCreateUserThread` with the stack size of the attribute as its reservation,
  a stack address of `pthread_attr_setstack` is not used. A `pthread_t` points to the data of the thread. A thread not
  created by `pthread_create` (the main thread, `CreateThread`) gets a detached one from `pthread_self`, it cannot be
  joined (`EINVAL`).
- Cancellation is deferred, at `pthread_testcancel`, `pthread_join`, `pthread_cond_wait`, `pthread_cond_timedwait`,
  `sem_wait`, `sem_timedwait`, `nanosleep`, `clock_nanosleep` and `pthread_delay_np`. These wait alertably and
  `pthread_cancel` alerts the thread (`NtAlertThread`), so a user APC queued to a thread also runs in them, as a
  spurious wakeup (an APC may post or signal what its thread waits for; a wake that the waiter does not take within
  1 ms, as in an APC, alerts it). A cancelled thread passes on a signal or a post it was woken by, runs its cleanup
  handlers (`pthread_cleanup_push`) with cancellation disabled and its key destructors; the stack is not unwound (no C++
  destructors run). `PTHREAD_CANCEL_ASYNCHRONOUS` is kept and acts as deferred.
- Priorities are those of Windows as in winpthreads: `sched_priority` from `THREAD_PRIORITY_IDLE` (-15) to
  `THREAD_PRIORITY_TIME_CRITICAL` (15), between the levels toward normal; `SCHED_OTHER` only (`ENOTSUP` for
  `SCHED_FIFO` and `SCHED_RR`). `PTHREAD_INHERIT_SCHED` gives a thread the priority of its creator.
  `pthread_getschedparam` reads the priority of the thread (5 set is 2, `THREAD_PRIORITY_HIGHEST`), winpthreads
  returns the value set.
- `pthread_setname_np` and `pthread_getname_np` set and read the description of the thread (Windows 10 1607 and
  later, `ENOTSUP` before) in the ANSI code page. `pthread_num_processors_np` counts the affinity of the process.
- Clocks: `CLOCK_REALTIME` and `CLOCK_REALTIME_COARSE` are the system time (the resolution of the clock tick, as
  `time` and `GetSystemTimeAsFileTime`), `CLOCK_MONOTONIC` is `QueryPerformanceCounter`, the CPU time clocks are of the
  process and the thread. `nanosleep` and `clock_nanosleep` are not interrupted (no `EINTR`, `remain` is not written)
  and sleep the resolution of the clock tick at least. `timespec_get` (`TIME_UTC`), which `<time.h>` declares for
  UCRT only, is declared by `<threads.h>`.
- C11: `thrd_t` is a `pthread_t`, `mtx_t` a `pthread_mutex_t` (every mutex is timed, `mtx_recursive` recursive),
  `cnd_t` a `pthread_cond_t`, `tss_t` a key and `once_flag` a `pthread_once_t`; `TSS_DTOR_ITERATIONS` is 256.

Not supported: process-shared objects (`PTHREAD_PROCESS_SHARED` is `ENOTSUP`, `sem_init` with `pshared` `ENOSYS`; the
waits are of the process, spin locks work anyway), named semaphores (`sem_open`, `sem_close` and `sem_unlink` fail
with `ENOSYS`), signals (`pthread_kill` of a signal other than 0 is `EINVAL`, `pthread_sigmask` and `pthread_atfork`
are macros of the header), asynchronous cancellation, and the internals of winpthreads (`thread_print`, `cond_print`,
`rwl_print`, `pthread_create_wrapper`, `_pthread_invoke_cancel`, `_pthread_cleanup_dest`, `__pthread_shallcancel`,
`_pthread_key_dest`, ...). `pthread_mutexattr_setprotocol`, `pthread_mutexattr_setprioceiling`,
`pthread_attr_setscope` and `pthread_setconcurrency` keep their value, which has no effect.

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

### C, msvcrt and UCRT

Where C, msvcrt.dll and ucrtbase.dll of Windows 11 differ, measured by calling the DLLs (UCRT printf in its C mode).
NtCRT follows C where msvcrt and UCRT disagree with it, unless the MinGW-w64 headers fix the msvcrt behavior
(`L_tmpnam`); C leaves some of these to the implementation, marked "impl.".

| | C | msvcrt | UCRT | NtCRT |
|---|---|---|---|---|
| `asctime`, `ctime` | `Wed Jan  2 03:04:05 1980` (`%3d`) | `Jan 02` | as C | as C |
| `strftime` `%c` in "C" | `%a %b %e %T %Y` | `01/02/80 03:04:05` | as C | as C |
| `strftime` `%e`, `%F`, `%T`, `%G`, ... | C99 | returns 0 | as C | as C |
| printf exponent | 2 digits or more | 3 (`1e+000`) | as C | as C |
| printf infinity, NaN | `inf`, `nan` | `1.#INF00`, `1.#QNAN0` | as C | as C |
| printf `%F`, `hh` | C99 | `%F` writes nothing, `hh` is ignored | as C | as C |
| printf `%.0f` of 2.5 | 2 (correctly rounded, ties to even) | 3 | 3 | as C |
| printf `%.30f` of 0.1 | correctly rounded up to `DECIMAL_DIG` digits, exact recommended | 17 digits, zeros after | exact | exact |
| printf `%a` of 1.0 | exact, trailing zeros may be left out | `0x1.000000p+0` | `0x1.0000000000000p+0` | `0x1p+0` |
| printf `%05s`, `%05c` | undefined | zeros | zeros | spaces |
| `strtod` `inf`, `nan`, `0x1p4` | C99 | not read | as C | as C |
| `strtod` `1d5` | 1, ends at `d` | 100000 (Fortran exponent) | as C | as C |
| `strtod` of a denormal | `ERANGE` impl. | no `ERANGE` | no `ERANGE` | as UCRT, `ERANGE` for an underflow to zero only |
| `strtol("0x", 16)` | 0, ends after `0` | converts nothing | converts nothing | as C |
| `wcstol` of `L"\x661\x662"` | impl. (other locales) | 12 | 12 | converts nothing |
| scanf `%f` of `"1ex"`, `"1e"` | a matching failure, `1e` read (0, 0) | `1.0` (1, 1) | as C, EOF at the end (0, EOF) | as C |
| scanf `%d` of `"-"` | a matching failure (0) | EOF | EOF | as C |
| scanf `%5c` of `"ab"` | a matching failure (0) | stores `ab` (1) | EOF | as C |
| scanf `%*d%d` of `"5"` | 0, the input fails after a conversion | 0 | EOF | as C |
| scanf `%%%d` of `" %5"` | 1, white space is skipped | 0 | 0 | as C |
| scanf `%f` of `inf`, `0x1p4` | C99 | 0, `0` | as C | as C |
| scanf `%lf` of 1 + 2⁻⁵³ + 2⁻⁸⁰ (exact) | correctly rounded, as `strtod` | 1, rounds twice | 1 + 2⁻⁵² | as C |
| scanf `%d` of `99999999999`, `%lld` of `99999999999999999999` | undefined | the low bits | the low bits of `strtoll` (`LLONG_MAX`, `ERANGE`) | as UCRT |
| scanf `%lc` of a multibyte character | the width counts bytes, 1 if none | a character (2 bytes of code page 936) | in UTF-8 2 bytes, U+FFE4 | a character (4 bytes of UTF-8 are 2 `wchar_t`), as msvcrt |
| scanf `%[c-a]` | impl. | the range `a-c` | the range `a-c` | `c`, `-`, `a` |
| scanf `%[a-c-e]` | impl. | `a-c`, `-`, `e` | as msvcrt | `a-c`, `c-e` (glibc) |
| scanf `%p` of `0x10` | what printf `%p` writes | 0 | 0x10 | 0x10 |
| scanf `%P`, `%h02d` (a width after `h`), `%"` | undefined | `%p`, width 2, matches `"` | a matching failure | as msvcrt |
| scanf `%2$d`, `%ms` | POSIX | a matching failure | a matching failure | POSIX |
| scanf `%hhd` | a `char` | stores an `int` | as C | as C |
| `fopen` mode `x` | C11, `EEXIST` | `EINVAL` | as C | as C |
| `tmpnam` | a name of up to `L_tmpnam` - 1 | `\s<pid>.<n>`, the root of the current drive | the temporary directory, `L_tmpnam` 260 in the headers of the SDK | as msvcrt, `L_tmpnam` 14 in the MinGW-w64 headers |
| `tmpfile` | impl. | the root of the current drive, fails without write access to it | the temporary directory | the temporary directory |

### Differences from msvcrt

Beside the table above:

- The locales above only, see "Locales"
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
- `_set_output_format` only records its argument, the exponent of printf has two digits or more
- scanf follows C beside the conventions of msvcrt (see "printf and scanf" and the table above): an item that is a
  prefix of a number only is a matching failure, numbers are correctly rounded (msvcrt rounds `%lf` twice) and take
  `inf`, `nan` and hex digits, `%Lf` stores an 80-bit long double, the wide functions return EOF at the end of the
  input as the narrow ones (msvcrt `swscanf` returns 0), `%p` takes `0x`, and a reversed range of a scan
  set is its characters
- The wide integer conversions (`wcstol`, `wcstoul`, `_wtoi`, ...) read ASCII digits only, msvcrt also takes the
  other Unicode decimal digits (U+0660, U+FF10, ...)
- `_winver`, `_winmajor`, `_winminor` and `_osver` are the version of Windows, msvcrt reports 6.2 to a program
  without a compatibility manifest
- `_wenviron` is set at startup in a program with `main`, msvcrt leaves it `NULL`
- `_get_heap_handle` returns the process heap, which `malloc` allocates from (msvcrt has a heap of its own)
- 512 streams at most: `_setmaxstdio` above 512 fails with `EINVAL` (msvcrt takes up to 2048)
- `getenv_s` with a `NULL` name returns `EINVAL` as UCRT, msvcrt returns 0
- `_vswprintf_l` takes a count after the buffer as the MinGW-w64 headers declare it, the export of msvcrt.dll has
  none (`buffer, format, locale, args`)
- `_strnicmp` takes any count, msvcrt returns `_NLSCMPERROR` for one above `INT_MAX`

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
- scanf: an item that is not a matching sequence at the end of the input is a matching failure (EOF), and so is the
  end of the input after a suppressed conversion or `%n` (EOF when nothing was assigned); `%%` skips white space (a
  matching failure); `%P`, a width after a length modifier (`%h02d`), an unknown conversion character, `%n$` and
  `%ms` are taken (a matching failure)
- `_wcsicmp`/`_wcsnicmp` compare by Unicode case in "C.UTF-8" (ASCII only for `_wcsicmp`)
- `_mbstrlen` counts UTF-8 characters, 3 for `a中😀` (6)
- `ftell` of a `ccs=UTF-8` read stream is exact except inside a surrogate pair (wrong for every position after a
  character above U+FFFF)
- `_pctype` in "C.UTF-8" has the "C" classes and `_LEADBYTE` (UCRT adds `C1_DEFINED` to every class)
- `_atodbl` returns `_UNDERFLOW` for a denormal as msvcrt (0)

## Origin and license

Derived from [KNSoft.NtCRT](https://github.com/KNSoft/KNSoft.NtCRT) at commit
`dc8159b829f3274127225f527069a2664847052d`, under the MIT license, see `LICENSE`. `polyfill/` and
`test/upstream/` are copied from it, `include/KNSoft/NDK/` follows
[KNSoft.NDK](https://github.com/KNSoft/KNSoft.NDK) (MIT).

`src/Float.cpp`, `src/Float.inl`, `src/FloatParse.cpp`, `src/FloatFormat.cpp` and `src/FloatPow10.inl` are ported
from the Go 1.27 source (`src/internal/strconv`), under the BSD 3-clause license of The Go Authors, whose notice is in
`src/Float.cpp`.
