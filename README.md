# ntcrt-mingw

A C runtime for MinGW-w64 (x64, GCC or clang; x86 in progress) that imports `ntdll.dll` only. C and C++ programs link the msvcrt
functions from this library instead of `msvcrt.dll` and `kernel32.dll`. A program links `kernel32.dll` only for the
Win32 functions it calls that NtCRT does not have (`objdump -p hello.exe` lists them under `DLL Name: KERNEL32.dll`);
with `-ntcrt-strict` (GCC) or `--config=build-llvm/ntcrt-strict.cfg` (clang) kernel32 is not linked and such a call
fails to link.

```sh
make            # build/libntcrt.a, the support libraries and build/ntcrt.specs
make test       # build and run test/upstream, the fuzz, console, libstdc++, thread and startup tests
make size       # the size of the programs of test/size
make compare    # build test/compare with NtCRT and with UCRT (cl of Visual Studio) and show the differences
g++ -static -specs=build/ntcrt.specs -o hello.exe hello.cpp

make LLVM=1     # the same with clang into build-llvm (LLVM=<dir>/ for clang in <dir>), make LLVM=1 test, ...
clang++ --config=build-llvm/ntcrt.cfg -static -o hello.exe hello.cpp

make install PREFIX=C:/ntcrt          # include/, lib/gcc and the drivers in bin/
make LLVM=1 install PREFIX=C:/ntcrt   # lib/clang and the clang drivers
x86_64-w64-ntcrt-mingw32-gcc -o hello.exe hello.c    # with C:/ntcrt/bin in PATH

make TCC=1 LLVM=<dir>/   # build-tcc/lib: libntcrt.a and ntdll.def for tcc in place of msvcrt.def, see "tcc"
make TCC=1 LLVM=<dir>/ install PREFIX=C:/ntcrt   # those two into lib/tcc
```

An installation can be moved: its drivers `x86_64-w64-ntcrt-mingw32-gcc`, `g++`, `cc`, `c++`, `clang` and `clang++`
(`tools/driver.c`) run the compiler of the build with `-static` and the specs or configuration file, include
directory and libraries found relative to themselves, so that `CC=x86_64-w64-ntcrt-mingw32-gcc` is one word for
configure, make or CMake. They take `-ntcrt-strict` too. The specs and configuration files link statically
(`-Bstatic`, `-static`) when `-static` is not given, a program does not import `libstdc++-6.dll` or
`libgcc_s_seh-1.dll`. The drivers are x64 only, see [x86](#x86).

The path of the checkout, of the toolchain or of `PREFIX` may have spaces: the specs escape them and the
configuration files quote them. `make test-install` installs into `build/install test` and builds with the drivers
there, with specs or a configuration file of that directory, and for clang with a sysroot through a junction whose
path has a space. Only `LLVM=<dir>/` cannot have one, as make splits it: put that clang first in PATH and give
`LLVM=1`.

## Layout

- `src/` the runtime, `NtCRT.h` is the internal header
- `src/start/` replacements of libmingw32 and libgcc members in `libntcrt_start.a`, linked before them
- `polyfill/` code page and console helpers built on ntdll
- `tools/` the drivers of an installation (`driver.c`) and the relocations of the members for tcc (`coffrel.c`)
- `include/KNSoft/NDK/` the part of KNSoft.NDK used by the sources, on top of MinGW-w64 headers
- `include/threads.h` C11 threads, the drivers add `include/` to the system include path of programs
- `include/errhandlingapi.h`, `include/processthreadsapi.h` include those of MinGW-w64 and inline `GetLastError`,
  `SetLastError` (the TEB), `GetCurrentProcess`, `GetCurrentThread` (the pseudo handles -1 and -2),
  `GetCurrentProcessId` and `GetCurrentThreadId` (the ClientId of the TEB) as gnu_inline functions, not macros:
  `::GetLastError()` and a member named `GetLastError` stay valid, `&GetLastError` is the function of NtCRT.
  `NTCRT_NO_INLINE_WIN32` turns them off
- `include/swprintf.inl` the `swprintf` and `vswprintf` of C for the MinGW-w64 headers before 13 (below)
- `test/upstream/` the KNSoft.NtCRT test suite, and `Msvcrt.cpp` with what the tests of Wine found
- `test/fuzz/` differential fuzz (`make test-fuzz`, `FUZZ_ITERATIONS=n`): the UTF-8 codec against kernel32, the
  printf and scanf families, strtod and strtol against ucrtbase.dll, strtold against the gdtoa of libmingwex
- `test/float/` `strtod`, `strtof` and `wcstod` against the 5.3 million lines of
  [parse-number-fxx-test-data](https://github.com/nigeltao/parse-number-fxx-test-data), and printf `%.17g`,
  `%.9g` and `%a` read back (`make test-float-corpus FLOAT_CORPUS=<its data directory>`)
- `test/printf/` printf against exact expected strings (`make test-printf`)
- `test/math/` the math functions return a value of their type, the x87 ones of x86 too (`make test-math`)
- `test/scanf/` scanf where the fuzz cannot compare with UCRT (`make test-scanf`), its link map shows no scanf of
  libmingwex
- `test/gofloat/` `strtod`, `strtof` and `wcstod` in a DLL tested by Go (`make test-go`, `GO_FUZZTIME=d`): the
  tables and test data of Go strconv, and `go test -fuzz` against `strconv.ParseFloat` (`math/big` past its
  limits) with the syntax of C
- `test/console/` console reads and writes in a pseudo console (`make test-console`), `conpty.exe` types the
  input and checks the screen
- `test/cxx/` libstdc++ on NtCRT (`make test-cxx`)
- `test/thread/` the pthread functions, `<threads.h>` and the threads of libstdc++ (`make test-thread`)
- `test/dl/` `<dlfcn.h>` with two DLLs (`make test-dl`)
- `test/start/` pseudo relocations of data auto-imported from a DLL, `_matherr`, the narrow `argv` of
  characters converted to bytes that a parser would take for a backslash or a quote and the wildcards of `argv`
  (`make test-start`)
- `test/size/` small programs measured by `make size`
- `test/compare/` tests whose output is compared with UCRT, `run.sh` finds `vcvars64.bat` by vswhere or `VCVARS`
- `test/wine/` the msvcrt and ucrtbase tests of Wine 11.0 on NtCRT (`make test-wine`, downloads them into the build
  directory, LGPL): `GetProcAddress` of msvcrt.dll finds the functions of the program, `skip.txt` leaves out the
  tests of UCRT-only APIs
- `test/pthread/` the tests of pthreads4w 3.0.0 (Apache-2.0) and the thread tests of musl libc-test (MIT) on NtCRT
  (`make test-pthread`, downloads them into the build directory, see "Threads"), `skip.txt` leaves out what NtCRT
  does not support and what is of pthreads4w or Linux
- `test/libc-test/` the other tests of musl libc-test, `functional/` and `regression/`, on NtCRT (`make test-libc`,
  downloads them into the build directory from the clone of `test/pthread`, MIT): `libc-test.sed` cuts the parts of
  POSIX out of tests that check more of C, `skip.txt` leaves out the tests of POSIX, GNU or BSD interfaces and of
  processes, signals and resource limits
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
would link the printf family into every program, and the emulated thread-local variables of libgcc (see "Threads").

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

Stripped sizes in bytes (`make size`), before the UTF-8 locale work, before the split, now, and with lld (clang and lld
22.1 of msys2 CLANG64):

| Program | Before UTF-8 | Before split | Now | lld | msvcrt.dll |
|---|---:|---:|---:|---:|---:|
| `return 0` | 248832 | 445440 | 37888 | 26112 | 14336 |
| `puts` | 248832 | 445440 | 48128 | 36352 | 14336 |
| `printf("%d")` | 248832 | 445440 | 82432 | 66560 | 41984 |
| `wprintf` | 248832 | 445440 | 83456 | 69632 | 41472 |
| `sscanf("%d %lf %s")` | | | 96768 | 64000 | 56320 |
| `std::cout` | 1257472 | 1420800 | 1164288 | 726016 | 1035776 |
| `std::thread` | | | 192512 | 103936 | 135168 |

The msvcrt.dll column is the default MinGW-w64 runtime of GCC, which imports it (and links winpthreads). A program
with printf links the narrow engine (`src/Printf.cpp`), the digits of `src/FloatFormat.cpp` and the decimal of
`src/Float.cpp`, with ld all of them; the 128-bit powers of 10 of `src/FloatPow10.inl` are 11136 bytes of them,
a sixth of `printf` with lld. A file grows by 512 bytes (the file alignment) at a time: a change of a few bytes
moves a program a step up or down, with ld and lld apart. What ld links beyond lld is the unused functions of these objects, most of all
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

## x86

With a MinGW-w64 toolchain for i686 first in `PATH` (`g++ -dumpmachine` is `i686-w64-mingw32`, such as the
`mingw32/bin` of msys2, MinGW-w64 15), `make` builds for x86 into `build-i686`. The port is in progress:

- Done: the PEB and TEB of x86 (`fs`), the stubs with the names of x86 (`_printf` and `__imp__printf`, a stdcall
  function of kernel32 with `@` and the size of its arguments, which `make` reads from the objects), the startup, stdio,
  the float conversions (a 128-bit integer of two halves for `unsigned __int128`), `setjmp` (`_setjmp3`) and `longjmp`,
  which unwinds the SEH frames registered since as msvcrt does (the ntdll of x86 has neither), the DWARF unwinder of
  libgcc (`libgcc_eh`) for C++ exceptions, and the arguments of printf and scanf: by the size of their types, the
  positional ones by the types the whole format gives them (a long double of GCC takes 12 bytes).
- Threads (see [Threads](#threads)): a mutex and a read-write lock are in their word of 32 bits, with the address of
  the TEB of the owner (a page), a condition variable, a barrier and a semaphore point to their state in the heap as in
  winpthreads. `pthread_once` resets the once for an exception by the personality routine of DWARF and for a `longjmp`
  by an SEH handler.
- `time_t`: NtCRT has the 64 bits of x64. The names of msvcrt without a size (`time`, `localtime`, `mktime`, `_stati64`,
  `_findfirst`, `_utime`, ...) take the 32-bit `time_t` of the x86 msvcrt, which the headers of MinGW-w64 default to
  (`_USE_32BIT_TIME_T`, unless `_TIME_BITS=64`); the headers call the others by names with 32 or 64 (`_time64`,
  `_stat32i64`, `_findfirst32i64`, `clock_gettime32`, `cnd_timedwait32`, ...), which libntcrt has.
- `_control87`, `_controlfp`: the x87 control word with its precision and infinity control, or'ed with the control of
  SSE and `_EM_AMBIGUOUS` when the two differ, as the x86 msvcrt.
- Math: the x87 computes at the 64-bit precision of long double, and some math functions of MinGW-w64 (`sqrt`, `pow`,
  `atan2`, `cbrt`, `sqrtf`, ... of lib32 libmsvcrt, `lgamma` and `lgammaf` of libmingwex) return st(0) as it is, where
  C11 F.6 wants a `double` or a `float` (GCC takes the result of a call as rounded). `src/MathX87.cpp` rounds them:
  the Makefile renames them in `libmsvcrt_static.a` and in copies of the members of libmingwex (`--wrap` for GNU ld,
  which reads libmingwex first), `make test-math` checks the raw st(0) of each. `sqrt` and `fdim`, one `fsqrt` or
  `fsub` that C wants correctly rounded, run at the precision control of 53 bits: rounded to 64 bits and then to 53,
  the result may round twice (for `float`, 64 bits are enough). The `sqrt` of libmingwex on x64 is an `fsqrt` too,
  wrapped the same way, in the library for tcc as well. The wrappers fix the errors of mingw-w64 too (x86 and x64):
  `sqrt` of a negative number raises the invalid operation (F.10.4.5), of a NaN sets no `EDOM`; `fdim` sets `ERANGE`
  on an overflow (on x86 the x87 had none), not on the exact infinity of an infinite argument.
- clang: `make LLVM=<clang64>/` with the i686 GCC first in `PATH`, clang of msys2 clang64 for `i686-w64-mingw32` on
  the sysroot of mingw32 (there is no clang32). The library has the DWARF unwind tables of GCC (`-funwind-tables`), and
  the personality routine of `pthread_once` is in a section that is no COMDAT: lld keeps no section for the
  relocations of `.eh_frame`.
- `make test` passes with GCC and clang.

On x86 the specs of GCC link with `--gc-sections`, which drops unused functions there (not on x64, see "Size").
The unwind tables of NtCRT let a C++ exception go through a callback that NtCRT calls (`qsort`, `bsearch`,
`pthread_once` of `std::call_once`), but the `.eh_frame` of an object keeps all its functions with GNU ld, and lld
keeps the tables of the functions it drops. `make UNWIND=0` builds without them into `build-i686-nounwind`
(`build-llvm-i686-nounwind`), where such an exception ends the program. The default keeps them, as GCC does on x86
and x64. `make size`, bytes:

| x86          | GNU ld  | GNU ld, `UNWIND=0` | lld    | lld, `UNWIND=0` |
|--------------|---------|--------------------|--------|-----------------|
| empty `main` | 179712  | 44032              | 77312  | 41472           |
| `printf`     | 216576  | 79360              | 112128 | 73728           |
| `sscanf`     | 229888  | 83968              | 118272 | 79872           |
| iostream     | 1398784 | 1219584            | 847872 | 794112          |
| threads      | 455168  | 310272             | 289280 | 244736          |

## tcc

`make TCC=1` (with `LLVM=<dir>/` for clang in `<dir>`) builds `build-tcc/lib/libntcrt.a` and `build-tcc/lib/ntdll.def`
for the x64 tcc: linked in place of `msvcrt.def` (and `kernel32.def` for what NtCRT has), a program tcc builds imports
`ntdll.dll` only. The startup is that of tcc (`crt1.c` of `libtcc1.a`), which calls `__getmainargs` of NtCRT.
`make TCC=1 install PREFIX=<dir>` copies the two into `<dir>/lib/tcc`.

- tcc links ELF objects only: clang compiles NtCRT for the x64 of Windows into ELF (`x86_64-w64-windows-gnu-elf`,
  `NTCRT_TCC` in `src/NtCRT.h`). A function of msvcrt is a weak alias in the object that implements it, which a
  program may define itself; a variable is given by `_imp__Name`, as the headers of tcc read it, or by its name as
  an import slot where they declare it `__declspec(dllimport)` (`_timezone`, `_daylight`, `_dstbias`, `_tzname` and
  those without `_`).
- What a GCC build takes from libmingwex, libmsvcrt, libmingw32 and libgcc of the toolchain (`exp`, `fmod`,
  `___chkstk_ms`, ...) and the C99 math of `<math.h>` (double and float: the `long double` of tcc is a `double`),
  `strnlen`, `wcsnlen`, `snprintf` and `vsnprintf`: their members, made ELF objects by `objcopy -O elf64-x86-64` without
  unwind tables and debug information. `src/Members.awk` takes the members needed, except those that would replace a
  function of NtCRT. They precede ntdll as in a GCC build (the `sin` of libmingwex). `tools/coffrel.c` sets the addend
  (-4) of the relative relocations, which objcopy leaves at 0.
- tcc has no sections grouped by `$`: the C initializers of NtCRT are in `.init_array`, which `crt1.c` runs after
  `__getmainargs` (and after the constructors of the program). A program of tcc has no TLS callback: the threads of
  `_beginthread`, `_beginthreadex` and `CreateThread` free the per-thread data of NtCRT when they end, `ExitProcess`
  runs the `atexit` functions and flushes the streams first (msvcrt does both at its `DLL_PROCESS_DETACH`). `tcc -run`
  ends by the `exit` and `atexit` of its `runmain.o`: NtCRT flushes the streams by an `atexit` function registered
  before `main`, which runs after those of the program.
- `-Os`, tcc links whole objects: `int main(void) { return 7; }` is 26624 bytes (37376 with GCC and NtCRT), the
  floating point environment that the startup of tcc sets is apart from the math (`src/FloatEnv.cpp`).
  `libntcrt.a` is 870 KB, 1.7 MB with a member for each name.
- No thread functions (tcc defines `__STDC_NO_THREADS__`). tcc takes `_strlwr` for `strlwr` itself, `ntdll.def`
  has no aliases.

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
- `_isctype`, `__iscsym` and `__iscsymf` classify by the current locale, `_isctype_l` and `_isleadbyte_l` by the
  locale object (`_LEADBYTE` for C2..F4 in "C.UTF-8").
- `_setmbcp` (the `_mbs*` functions) is independent of `setlocale` and cannot select UTF-8 (65001) or 54936
  (GB18030, which has no NLS table).

### printf and scanf

The printf family is NtCRT's own (`src/Printf.inl`): the functions of msvcrt, and the `__mingw_*printf` that
MinGW-w64 formats through in C++, with `__USE_MINGW_ANSI_STDIO` and for its inline `snprintf` (wrapped by the
specs, defined by libntcrt for lld). The printf of libmingwex (`pformat`, `gdtoa`) is not linked. It follows C and
takes the conventions of msvcrt beside:

- C: the flags, width and precision (also `*` and `*m$`), positional arguments (`%n$`, POSIX, 100 at most as the
  `_ARGMAX` of msvcrt, beyond is `EINVAL`), `hh`, `h`, `l`, `ll`, `j`, `z`, `t` and `L`, `%a`, `%n`. Floating point
  numbers are exact and round ties to even, by the digits of `src/FloatFormat.cpp` (the ftoa of Go 1.27: unrounded
  scaling for up to 18 digits, the multiprecision decimal for more and for long double). A length beyond `INT_MAX` fails
  with `EOVERFLOW`.
- The long double of `%Lf` is that of the caller, as in mingw-w64: a `double` for the functions of msvcrt (the `long
  double` of MSVC and of the Windows ABI, also what tcc passes), the 80-bit long double of GCC and clang for
  `__mingw_*printf` and `__mingw_*scanf`, which `<stdio.h>` calls with `__USE_MINGW_ANSI_STDIO` (C99 and C++ with
  msvcrt). scanf stores a `double` for `%Lf` and `%llf` of the functions of msvcrt. A program built with
  `__USE_MINGW_ANSI_STDIO=0` that passes a GCC long double to `printf` prints 0, as with msvcrt and UCRT. `L` of an
  integer conversion (`%Lx`) and of `%Ln` is ignored by the functions of msvcrt, as by msvcrt, and is `ll` for
  `__mingw_*printf` and `__mingw_*scanf`, as by mingw-w64.
- msvcrt: the length modifiers `I`, `I32`, `I64` and `w` (`z`, `t` and `I` the size of a pointer), `%S`/`%C` (the other
  width than the function), `%Z` (`ANSI_STRING`, `%wZ` `UNICODE_STRING`), `%p` as uppercase digits (16 on x64, 8 on
  x86), and in the wide functions of msvcrt (`swprintf`, `_snwprintf`, `fwprintf`, ...) `%s`/`%c` wide and `%S`/`%C`
  narrow, where the C99 ones of MinGW-w64 take `%s`/`%c` narrow. `_snprintf` and `_snwprintf` write no terminator when
  the output fills the buffer and return -1 when it does not fit, `snprintf` truncates as C does.
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
  (hex digits as printf writes them, with or without `0x`), positional arguments (`%n$`) and `m` (`%ms`, an
  allocated string, POSIX: no memory is an error, `ENOMEM` and EOF before the first conversion).
- Integers are the result of `strtoll` (`%d`, `%i`) or `strtoull` (`%u`, `%o`, `%x`, `%p`, which take a sign too),
  base prefixes for `%i`, stored in the size of the type: the low bits of a number out of its range, `ERANGE` and
  the limit beyond 64 bits, as UCRT. Floating point numbers are converted by `src/FloatParse.cpp` (below) at the
  precision of their type, `float`, `double` or the long double of `%Lf` (above), with `inf`, `infinity`, `nan`,
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

The reference is the ucrtbase.dll of the system. That of Windows 7 (10.0.14393, Windows 10 1607) has no UTF-8
locale, pads infinity of `%0f` with zeros, rounds the last digit of some `%a`, and returns EOF for a format that
ends in `%`: `fuzz format` reports such differences there (30 of 2459 checks on Windows 7 SP1, win32lab-e4).

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
code. The headers of winpthreads of MinGW-w64 13 and later call the functions that take a `timespec` by names that
end in 64 (`pthread_mutex_timedlock64`, `clock_gettime64`, ...), or in 32 for `_USE_32BIT_TIME_T`: libntcrt has all
three names.

- A mutex, condition variable, read-write lock, spin lock, barrier or semaphore is the word of its type, nothing is
  allocated and no kernel object created: a static initializer or 0 is a valid object and `pthread_*_destroy` only
  checks. On x86, whose word has 32 bits, a condition variable, a barrier and a semaphore point to 8 bytes in the heap
  as in winpthreads (`ENOMEM`, `ENOSPC` for `sem_init`), allocated by `pthread_cond_init` of `CLOCK_MONOTONIC` or the
  first wait, `pthread_barrier_init` and `sem_init`, freed by their destroy. A thread waits by address
  (`src/PthreadWait.cpp`) with its node in a table of buckets, as `RtlWaitOnAddress` of Windows 8, and parks: the waker
  takes the node out under the lock of the bucket, unparks the thread and marks the node done, it never blocks; the
  waiter leaves once its node is done. The parker is chosen at the first wait by the exports of ntdll, not by the
  version: `NtWaitForAlertByThreadId` and `NtAlertThreadByThreadId` (Windows 8 and later, found by
  `LdrGetProcedureAddress`, not imported), else an auto-reset event per thread (`NtWaitForSingleObject` and
  `NtSetEvent`), created by `pthread_create` and `thrd_create` (`EAGAIN` if it cannot be), for other threads at their
  first wait, closed when the thread ends. A thread that cannot get one (out of memory or handles) looks at its node
  after each sleep of 1 ms, which lasts a clock tick (15.6 ms by default). The waits of ntdll (critical sections, SRW
  locks) take the same alerts, and a critical section deadlocks on one it did not send (threads alerted while they start
  and allocate hang the lock of the heap): a thread is alerted only for its node of a wait of NtCRT, consumes the alert
  before it leaves, and waits in no SRW lock while the alert may be on its way. `make PARKER=event test-thread` and
  `test-pthread` run the tests on the event (an initializer of `test/thread/parker.c` chooses it), the default on the
  alerts of Windows 11.
- Before it parks, a thread spins 100 rounds of `pause` (1.4 us on Zen 5, about 3.5 us on Intel since Skylake; a park
  and unpark cost about 4 us) for a mutex, a read-write lock or a post of a semaphore while no other thread waits
  for it, and for a signal of a condition while no thread is parked in its bucket (a signal ends the spin of every
  spinner, but unparks one). It does not spin when the affinity of the process (read at the first spin) has one
  processor. A ping-pong by a condition takes 0.4 us in place of 4 (winpthreads 16), by semaphores 0.07 us in place
  of 4; 8 threads that lock a mutex with work between take 0.9 us an iteration in place of 1.4. A mutex that threads
  lock in a loop with nothing between goes from thread to thread rather than staying with one: 8 threads take 73 ns
  a lock in place of 55 (winpthreads 30, an SRW lock 43).
- Mutexes are normal (`PTHREAD_MUTEX_DEFAULT`, a normal mutex locked again by its owner deadlocks, any thread may
  unlock it), errorcheck (`EDEADLK`, `EPERM`) or recursive (`EPERM` for another thread, `EAGAIN` past 2^28 levels;
  on x86 512 in the word, beyond that 2^32 for 4 mutexes a thread),
  timed by `pthread_mutex_timedlock`. A condition wait unlocks a recursive mutex whatever its count and restores it.
- Condition variables wait by `CLOCK_REALTIME` or `CLOCK_MONOTONIC` (`pthread_condattr_setclock`), or a relative
  time (`pthread_cond_timedwait_relative_np`). An absolute `CLOCK_REALTIME` time follows changes of the clock. A
  condition counts its waiters from before they unlock the mutex until their wait ends: POSIX lets a thread destroy
  and free a condition on which no thread is blocked, right after a broadcast, while the woken threads may still be
  in `pthread_cond_wait`, and `pthread_cond_destroy` waits for them to leave (`EBUSY` while a thread is blocked on it).
- Read-write locks prefer readers: a thread may take a read lock again while a writer waits, so writers can starve. A
  writer locking again gets `EDEADLK` (`EBUSY` from the try functions). A barrier counts up to 2^21 - 1 threads
  (`EINVAL` beyond). The last thread of a round waits until the threads released from the previous one left the
  barrier, `pthread_barrier_destroy` until those released left; a destroyed barrier is invalid (`EINVAL`).
- `pthread_once` (and `call_once`, `std::call_once`) is reset when the routine exits by an exception (an unwind
  handler of x64 SEH on the frame that calls it; on x86 a personality routine of DWARF there, and an SEH handler for
  `longjmp`) or is cancelled, another call runs the routine again (with
  winpthreads `std::call_once` deadlocks after an exception).
- Keys are TLS slots (`TlsAlloc`, 1088 for all users). `pthread_exit`, `thrd_exit` and a return from the start
  routine of `pthread_create` or `thrd_create` run the destructors after the cleanup handlers, with cancellation
  disabled, outside the loader lock as winpthreads and Cygwin do: a destructor may create threads and join them.
  `PTHREAD_DESTRUCTOR_ITERATIONS` (256) rounds while values are set again. At the exit of any thread (also of
  `CreateThread`, `_beginthreadex`, `_endthreadex` and `ExitThread`) the TLS callback of NtCRT runs the destructors
  of the values set until then, after the callback of libmingw32, which runs the `thread_local` destructors of C++
  (these run after the key destructors of `pthread_exit`, glibc runs them before). A TLS callback runs under the
  loader lock, as `DllMain` does, which no runtime can avoid for a thread that it did not create and that ends
  without `pthread_exit`: there a destructor must not join a thread, nor create one and wait for it, since a thread
  cannot start or end while the lock is held.
- The emulated thread-local variables (`thread_local` of GCC, and of clang with `-femulated-tls`) are those of
  `libntcrt_start` in place of emutls of libgcc, which frees them in a key destructor that may run before others. They
  are freed by a TLS callback after that of NtCRT, so that the `thread_local` and the key destructors find the
  variables of their thread alive (with libgcc and winpthreads, which runs the key destructors before the
  `thread_local` ones, a `thread_local` destructor runs on freed memory). A DLL that `FreeLibrary` unloads keeps its
  TLS slot and the variables of its threads, as with libgcc: the TLS callbacks run before `DllMain` and the
  destructors of the DLL, which may still use them. Loading and unloading such a DLL takes a slot of the 1088 of the
  process each time.
- `pthread_create` creates a thread by `RtlCreateUserThread` with the stack size of the attribute as its reservation,
  a stack address of `pthread_attr_setstack` is not used. A `pthread_t` points to the data of the thread. A thread not
  created by `pthread_create` (the main thread, `CreateThread`) gets a detached one from `pthread_self`, it cannot be
  joined (`EINVAL`). A joiner parks until the TLS callback of NtCRT marks the thread ended (a thread ended by
  `TerminateThread` cannot be joined), then waits for its handle while the TLS callbacks of the program after that
  of NtCRT run (`_pthread_tryjoin` is `EBUSY` meanwhile), with an event that `pthread_cancel` sets: that of the
  parker, or with the alerts one the joiner makes at its first such wait (without one, it looks every 10 ms).
- A thread that ends by `pthread_exit`, `thrd_exit` or a return from the start routine just ends. The process ends as
  Windows ends it, when its last thread ends, whatever created it (a thread of `CreateThread` keeps the process
  running too), with the status of that thread: the low 32 bits of the result of the thread, not the `exit(0)` of
  POSIX and C11. That last thread gets no thread detach: its key destructors run if it ended by `pthread_exit`, its
  `thread_local` destructors with the `atexit` functions (libmingw32 registers them by `atexit`), as with msvcrt.
- When the process ends without `exit` (the end of its last thread, `ExitProcess`, `RtlExitUserProcess`), a TLS
  callback of NtCRT runs the `atexit` functions and flushes the streams at `DLL_PROCESS_DETACH`, before the other TLS
  callbacks of the program, as msvcrt does in its `DLL_PROCESS_DETACH`, which precedes them (UCRT flushes only), and
  flushes the streams again after the key destructors of libmingw32 (`__mingwthr_key_dtor`).
  `_exit`, `abort` and a fatal error skip that work, `TerminateProcess` too. The other threads were ended at any point
  by then: a stream whose lock, or the lock of whose fd, one of them held is not flushed (other I/O on that fd fails
  with `EBUSY`), also by `fflush(NULL)` and `_flushall` (the crt2.o of MinGW-w64 15 calls `fflush(NULL)` in an
  `atexit` function), and an `atexit` function that takes the lock of such a stream, or waits for another thread, ends
  the process there or hangs. NtCRT in a DLL does that work at the `DLL_PROCESS_DETACH` of the DLL, before its entry
  point calls its `DllMain` and runs its `atexit` functions (a table of the DLL startup code of MinGW-w64): what they
  write is not flushed.
- Cancellation is deferred, at `pthread_testcancel`, `pthread_join`, `pthread_cond_wait`, `pthread_cond_timedwait`,
  `sem_wait`, `sem_timedwait`, `nanosleep`, `clock_nanosleep`, `sleep`, `usleep` and `pthread_delay_np` (`sleep` and
  `usleep` of libmingwex are replaced, they wait by `Sleep`). These publish the address they wait on (the sleeps one of
  their own) and look at the cancellation before they park; `pthread_cancel` marks the cancellation, then takes the node
  of the thread out of that wait as a waker does, so no cancellation is lost, and one of a thread that disabled it is a
  spurious wake. A relative time (a sleep, `pthread_cond_timedwait_relative_np`, a condition of `CLOCK_MONOTONIC`) is
  counted on the performance counter: with a finer timer resolution than the clock tick the system may end the park
  early, the thread parks for the rest. No wait of NtCRT is alertable: a user APC runs in the alertable waits of the
  program only (`SleepEx`, `WaitForSingleObjectEx`, ...), and the identity of the waker (a token it impersonates) does
  not matter. A special user APC (`QueueUserAPC2`, Windows 11), which runs whatever the thread waits in, must not call
  the thread functions, as a signal handler must not. A cancelled thread passes on a signal or a post it was woken by, runs its cleanup handlers
  (`pthread_cleanup_push`) with cancellation disabled and its key destructors; the stack is not unwound (no C++
  destructors run). `PTHREAD_CANCEL_ASYNCHRONOUS` is kept and acts as deferred.
- Priorities are those of Windows as in winpthreads: `sched_priority` from `THREAD_PRIORITY_IDLE` (-15) to
  `THREAD_PRIORITY_TIME_CRITICAL` (15), between the levels toward normal; `SCHED_OTHER` only (`ENOTSUP` for
  `SCHED_FIFO` and `SCHED_RR`). `PTHREAD_INHERIT_SCHED` gives a thread the priority of its creator.
  `pthread_getschedparam` reads the priority of the thread (5 set is 2, `THREAD_PRIORITY_HIGHEST`), winpthreads
  returns the value set.
- `pthread_setname_np` and `pthread_getname_np` set and read the description of the thread (Windows 10 1607 and
  later; before, `pthread_setname_np` fails with `ENOTSUP` and `pthread_getname_np` reads an empty name) in the ANSI
  code page. `pthread_num_processors_np` counts the affinity of the process.
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
are macros of the header), asynchronous cancellation, robust mutexes (the headers of winpthreads have none), and the
internals of winpthreads (`thread_print`, `cond_print`, `rwl_print`, `pthread_create_wrapper`,
`_pthread_invoke_cancel`, `_pthread_cleanup_dest`, `__pthread_shallcancel`, `_pthread_key_dest`, ...).
`pthread_mutexattr_setprotocol`, `pthread_mutexattr_setprioceiling`, `pthread_attr_setscope` and
`pthread_setconcurrency` keep their value, which has no effect.

`std::counting_semaphore` of libstdc++ 15 with a maximum above 1 may block a thread for ever on MinGW-w64, with
winpthreads as with NtCRT: without futex its `acquire` waits for a version of a pool of mutexes and condition
variables that `release` raises only when the count leaves 0. `_S_do_try_acquire` of `bits/semaphore_base.h` tries one
compare-exchange: when the 16 tries of `__atomic_spin` (`bits/atomic_wait.h`) all lose to other threads, the thread
waits although the count is not 0. Four threads that acquired from one releasing (`counting_semaphore<4000>`) hung
within a few hundred rounds on Windows 11, with a compare-exchange retried while the count is not 0 none did.
`std::binary_semaphore` does not wait so: a compare-exchange lost leaves 0, which the next `release` leaves. The
deadline of `try_acquire_for` and `try_acquire_until` ends their wait without another notification.

`make test-pthread` (not in `make test`, it needs the network) runs the tests of pthreads4w 3.0.0 (Apache-2.0, the
release archive of SourceForge) and the thread, semaphore and TLS tests of musl libc-test (MIT, a pinned commit) on
NtCRT, `make LLVM=1 test-pthread` with clang and lld. `test/pthread/fetch.sh` downloads them into the build directory
and checks them by SHA-256, `run.sh` builds each test, checks that it imports ntdll.dll only and runs it with a
timeout (`TIMEOUT=s`, 120 by default; `RUNS=n` runs each n times, `PTHREAD_TESTS="suite/test ..."` some of them), a
summary of each suite follows. `pthreads4w.sed` adapts its tests to the ABI of winpthreads: a `pthread_t` of
pthreads4w is a struct and its objects are pointers, which the tests check (`NULL` after destroy); `libc-test.sed`
leaves out the `shm_open` and asynchronous parts of two tests. `skip.txt` gives the reason for each test left out:
the extensions and internals of pthreads4w (affinity, `pthread_getunique_np`, its handles, cancellation by
exceptions), what is not supported (asynchronous cancellation, robust mutexes, named semaphores; `fork`, signals and
`dlopen` of libc-test), the priorities and the preference for readers described above, and what POSIX leaves undefined
or optional (unlocking a mutex or spin lock that is not locked, the ID of an ended thread, `sem_getvalue` counting the
waiters).

### dlfcn

`<dlfcn.h>` (`include/dlfcn.h`, `src/Dlfcn.cpp`) has `dlopen`, `dlsym`, `dlclose`, `dlerror` and `dladdr` on the loader
of ntdll, as [dlfcn-win32](https://github.com/dlfcn-win32/dlfcn-win32) has them on kernel32, with its values of
`RTLD_*`. `-ldl` finds an empty `libdl.a`.

- `dlopen(NULL)` is the program. A file name is converted as for `fopen`, its slashes become backslashes, and the
  module is loaded with `LOAD_WITH_ALTERED_SEARCH_PATH`, without a hard error box (`SEM_FAILCRITICALERRORS` of the
  thread). `RTLD_LAZY` is `RTLD_NOW`: Windows binds the imports of a module when it loads it.
- A module is global unless `dlopen` loads it with `RTLD_LOCAL`: `dlsym` of the program and of `RTLD_DEFAULT`
  search the program, then the global modules in the order they were loaded. `RTLD_NEXT` searches the global
  modules loaded after the one that calls `dlsym`. A local module becomes global when `dlopen` opens it again
  without `RTLD_LOCAL`. The program and its modules export a symbol by `__declspec(dllexport)` (or
  `-Wl,--export-all-symbols`).
- `dlclose` of the program does nothing, each `dlopen` of a module needs a `dlclose`.
- `dlopen` and `dlclose` run one at a time, and not during the searches of `dlsym` and `dladdr`, so that what other
  threads open, close and search does not make a local module global. Not those of a `DllMain`, which holds the
  loader lock that the load of another thread may wait for, nor `LoadLibrary`: a module that `dlopen` with
  `RTLD_LOCAL` loads while another thread loads it by these is global. A `DllMain` (attach and detach) must not wait
  for another thread that calls these functions, which may wait for the one that loads or unloads that module (the
  rules of `DllMain` forbid waiting for another thread). On Windows 7 `dlsym` finds nothing in a module that is in
  its `DllMain` for `DLL_PROCESS_DETACH`, of its handle or by a search, as `GetProcAddress` there: the loader of
  Windows 7 takes the module out of its lookups before it calls the detach.
- `dladdr` gives the path of the module in a buffer of the thread, and the nearest named export at or below the
  address.
- The messages are those of FormatMessage in English for the common errors (`"name": The specified module could
  not be found.`), `Error <n>.` for the others. The threads that could not get their own data (no TLS slot or no
  memory for it) share one error, which `dlerror` gives as "Not enough memory for the error.", and `dladdr` gives
  them no path (`dli_fname` NULL).

These differ from dlfcn-win32, by POSIX:

- `dlerror` is of the thread, dlfcn-win32 has one for the process.
- Only `dlerror` clears the error: POSIX has it return the last error since the last `dlerror`, a `dlopen`,
  `dlsym` or `dlclose` that succeeds does not clear it as in dlfcn-win32.
- A local module stays local until each of its `dlopen` has a `dlclose`, dlfcn-win32 makes it global at the first
  `dlclose`. A module kept loaded only by another one that imports it is then global.

NtCRT is linked statically, so the program and each DLL linked with it have their own list of local modules:
a module that one of them loads with `RTLD_LOCAL` is hidden only from the `dlsym` of `RTLD_DEFAULT`, of the
program and of `RTLD_NEXT` called by that one, the others find it. dlfcn-win32 linked statically has the same
limitation.

### File names, console and streams

- The narrow file name functions (`fopen`, `_open`, `_stat`, `_mkdir`, `_findfirst`, `_fullpath`, `rename`,
  ...) convert names by UTF-8 when `LC_CTYPE` is "C.UTF-8" and by the ANSI code page otherwise. An invalid name
  fails with `EILSEQ`, there is no best-fit conversion. `argv`, `environ` and `_acmdln` are in the ANSI code
  page, which is UTF-8 with the `activeCodePage` manifest setting. The narrow `argv` is the wide one converted
  argument by argument, msvcrt parses the converted command line: there the trail byte 0x5C of a double-byte
  character (A5 5C in code page 950) is a backslash and a character converted to a quote by best fit (U+FF02 in code
  page 1252) ends an argument.
- As in UCRT, `argv` is not converted again by `setlocale`: after `setlocale(LC_ALL, ".UTF-8")` a narrow name of
  `argv` with non-ASCII characters is read as UTF-8 by `fopen` and not found (a Big5 name with the ANSI code page
  950). A program in "C.UTF-8" takes its arguments in UTF-8 with the `activeCodePage` manifest setting (UTF-8 is
  then the ANSI code page and the names of every code page work), or by `wmain` and `_wfopen`.
- The wildcards of `argv` are expanded as msvcrt expands them by `libntcrt_glob.a`: `-lntcrt_glob`, or
  `-ntcrt-glob` of the specs and of the drivers. An argument after the program name with `*` or `?`, quoted or
  not, is replaced by the names that match it, files and directories, each after the directory part of the
  argument as written. `.` and `..` are left out, hidden files are not. The names are sorted with the ASCII letters
  folded, the narrow ones after their conversion. They match as for `FindFirstFile`: `a?.c` matches `a.c`, `*.` the
  names without a dot, a short name matches too, and the trailing spaces and dots of the pattern are dropped as from
  a Win32 path (`"*.c "` matches as `*.c`). An argument that nothing matches, or with a wildcard in its directory
  part, stays as it is. The narrow `argv` is expanded from the wide one, so a name matches by its characters, not by
  their best fit in the ANSI code page.
- `argv` is built by the first call of `__getmainargs` (`__wgetmainargs` for `wargv`), the later calls return it
  whatever wildcard argument they pass: unlike msvcrt, which parses the command line again, a second call with
  another one does not expand it or undo its expansion. The narrow and the wide `argv` keep their own count.
- As in MinGW-w64, the program may define `_dowildcard`, which the library follows: 0 leaves the arguments as they
  are. Unlike MinGW-w64, `_dowildcard` alone does not link the expansion: a program that defines it as not 0
  (`int _dowildcard = -1;`, `CRT_glob.o` of MinGW-w64) without the library stops at its start with
  `_dowildcard requires -lntcrt_glob` on stderr and the exit code 255. A program with neither is not expanded, not
  even by the libmingw32 of a MinGW-w64 configured with `--enable-wildcard`. Only the programs that link the library
  have its code (4.5 KB of an x64 program by GCC); the others have the check, 128 bytes of `.text` (192 with
  `wmain`).
- tcc (`make TCC=1`, `lib/libntcrt_glob.a`): its `crt1.c` defines `_dowildcard` as 0, a program sets
  `int _dowildcard = -1;` (no conflict with that of `crt1.c`). tcc reads an archive when it comes to it and links a
  member for a weak reference, so the library must follow `libntcrt.a` in the libraries that tcc adds itself
  (`pe_add_runtime`): a `-lntcrt_glob` of the command line comes before `libntcrt.a`, which refers to the
  expansion, and links nothing (the program stops with the error). Added there for every program, the library would
  be linked into every one: tcc-ntdll adds it after `libntcrt.a` when the program asks for it by `-lntcrt_glob` or
  `#pragma comment(lib, "ntcrt_glob")`.
- When `LC_CTYPE` is "C.UTF-8", a text mode console fd writes and reads UTF-8 through `WriteConsoleW` and
  `ReadConsoleW` whatever the console code page is: a character split between writes is kept until it is
  complete, reads translate CRLF to LF. Ctrl+Z is read as in the "C" locale (`ReadFile` of a console, msvcrt and
  UCRT): at the start of a line it is the end of the input for one read, in the middle of a line it is kept and the
  rest of the line is dropped. The `_O_U8TEXT` and `_O_U16TEXT` reads of a console do the same. The console
  functions of `<conio.h>` (`_cputs`, `_getch`, ...) use UTF-8 likewise.
- A DOS device name (`CON`, `NUL`, `AUX`, `PRN`, `COMn`, `LPTn`, `CONIN$` and `CONOUT$` from Windows 8 on, also
  with a directory, an extension or a colon as ntdll of the version takes them) exists as kernel32 says, whether
  the device answers or not: `GetFileAttributesW` returns `FILE_ATTRIBUTE_ARCHIVE`, `_access` succeeds for reading
  and writing, and `_stat` reports a character device (`_S_IFCHR`, read and write permissions, one link, no size
  and no times). `_mkdir` of one fails with `ENOTDIR` (`NUL` succeeds as in kernel32), and with a missing directory
  (`nosuchdir\NUL`) these fail with `ENOENT`. A device by its path (`\\.\NUL`) is a character device for `_stat`
  as for `fstat`, a named pipe a FIFO (kernel32 has no attributes for them). msvcrt reports a regular file, UCRT a character device for `NUL`,
  `CONIN$` and `CONOUT$` only, and fails `_access` and the `_stat` of the others.
- `_getch` and `_getwch` take Alt and keypad digits as the composition of a character, which the console
  reports with the release of Alt. A pseudo console (Windows Terminal) types a character beyond U+FFFF in this
  way, one surrogate per composition.
- `getwc`, `putwc` and `ungetwc` of a narrow stream convert by UTF-8 with a state per stream in "C.UTF-8". An
  invalid or truncated sequence read sets the error indicator and `EILSEQ`, `fgetws` returns `NULL` for it.
- `_O_U8TEXT` reads return the characters of complete UTF-8 sequences, a sequence split by the read size is
  completed by the next read and an invalid or truncated sequence is U+FFFD. `ftell` of a Unicode mode stream and
  `_tell` of its fd are exact, except between the two units of a surrogate pair, where no byte offset exists. A
  newline in the file is LF or CRLF and a U+FFFD replaced 1 to 3 bytes, so `ftell` with units left in the buffer
  reads the file again from the start of the buffer up to them.

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
| the end of the last thread (`thrd_exit`, `pthread_exit` of winpthreads, `ExitThread`) | `exit(EXIT_SUCCESS)` (C11, POSIX) | the status of the thread, `atexit` functions run, streams flushed | the status of the thread, streams flushed | as msvcrt, as Windows ends a process (see "Threads") |
| `ExitProcess`, `RtlExitUserProcess` | | `atexit` functions run, streams flushed | streams flushed | as msvcrt |

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
- A read/write stream may read after `fflush` also when nothing was buffered (unbuffered or after a large `fwrite`),
  and `fseek`/`fsetpos` fail when flushing the output fails; msvcrt fails the read and reports the seek as done
- `iswprint` is 0 for a tab, which is blank and a control character, as UCRT (msvcrt says printing)
- `_mbsncat` and `_mbsnbcat` write nothing after the terminator of a source shorter than the count, msvcrt writes
  a second null byte past it
- `_stat` of a DOS device name reports a character device, msvcrt a regular file; `_mkdir` of one is `ENOTDIR`,
  msvcrt `EINVAL` (see "File names, console and streams")
- Unicode mode I/O:
  - `_O_U8TEXT` reads accept any buffer size, msvcrt reads at least 4 bytes and fails with `EINVAL` when they
    convert to more units than the buffer holds, and it drops a truncated sequence at the end of the file
  - `ftell` of a `_O_U8TEXT`/`_O_U16TEXT` read stream counts the bytes of the unread characters, msvcrt
    estimates them and `fseek` to its result can land elsewhere
  - `_O_U8TEXT`/`_O_U16TEXT` console fds read by `ReadConsoleW` and write to the handle of the fd, a surrogate
    pair split between writes is kept until it is complete
  - Ctrl+Z in the middle of a console line read in `_O_U16TEXT` is kept and the rest of the line dropped, as cmd,
    `ReadFile` and the other text modes read it; msvcrt and UCRT return the text before it and the fd reads no
    more
  - a UTF-16 text file opened for reading and writing keeps its last byte, msvcrt (UCRT too) removes it as a
    Ctrl+Z when it is 0x1A, the high byte of a unit such as U+1A00
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
  `inf`, `nan` and hex digits, the wide functions return EOF at the end of the
  input as the narrow ones (msvcrt `swscanf` returns 0), `%p` takes `0x`, and a reversed range of a scan
  set is its characters
- The wide integer conversions (`wcstol`, `wcstoul`, `_wtoi`, ...) read ASCII digits only, msvcrt also takes the
  other Unicode decimal digits (U+0660, U+FF10, ...)
- `_winver`, `_winmajor`, `_winminor` and `_osver` are the version of Windows, msvcrt reports 6.2 to a program
  without a compatibility manifest
- `_wenviron` is set at startup in a program with `main`, msvcrt leaves it `NULL`
- `_wgetenv` and `_wputenv` compare names in the case of Unicode as the environment of the process does (`Ä` and `ä`
  are one variable), msvcrt in that of ASCII
- `_get_heap_handle` returns the process heap, which `malloc` allocates from (msvcrt has a heap of its own)
- 512 streams at most: `_setmaxstdio` above 512 fails with `EINVAL` (msvcrt takes up to 2048)
- `getenv_s` with a `NULL` name returns `EINVAL` as UCRT, msvcrt returns 0
- `_vswprintf_l` takes a count after the buffer as the MinGW-w64 headers declare it, the export of msvcrt.dll has
  none (`buffer, format, locale, args`)
- `swprintf` and `vswprintf` (and `__ms_swprintf`, `__mingw_swprintf`) are those of the MinGW-w64 headers NtCRT is
  built with: from MinGW-w64 13 those of C, with the size of the buffer (-1 if the output does not fit), as
  libmsvcrt.a of MinGW-w64 13 defines them; before 13 those of msvcrt.dll without a size. `_swprintf` and
  `_vswprintf` are always those of msvcrt.dll. The headers before 13 define the `swprintf` and `vswprintf` of C as
  inline functions that call `vsnwprintf`, which returns the length of the whole output: `include/swprintf.inl`
  makes them call those of C of NtCRT (`%s` narrow with `__USE_MINGW_ANSI_STDIO`, wide without).
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
- A name that is invalid for Win32 (`a<b`, `a|b`, wildcards `a*b`) is `EINVAL` for `_stat` too, as for `_open`
  and `fopen` in both (`ENOENT`, as msvcrt): an invalid name is not a missing file
- Every DOS device name exists for `_access` and is a character device for `_stat` (`_access` fails, `_stat`
  succeeds for `NUL`, `CONIN$` and `CONOUT$` only), see "File names, console and streams"

## Origin and license

Derived from [KNSoft.NtCRT](https://github.com/KNSoft/KNSoft.NtCRT) at commit
`dc8159b829f3274127225f527069a2664847052d`, under the MIT license, see `LICENSE`. `polyfill/` and
`test/upstream/` are copied from it, `include/KNSoft/NDK/` follows
[KNSoft.NDK](https://github.com/KNSoft/KNSoft.NDK) (MIT).

`src/Float.cpp`, `src/Float.inl`, `src/FloatParse.cpp`, `src/FloatFormat.cpp` and `src/FloatPow10.inl` are ported
from the Go 1.27 source (`src/internal/strconv`), under the BSD 3-clause license of The Go Authors, whose notice is in
`src/Float.cpp`.
