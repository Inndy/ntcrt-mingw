# Ideas

Proposals not started yet, kept here until they are taken up or dropped.

## Correctness

- **A scanf engine of NtCRT.** The scanf family is still the one of libmingwex behind a format rewrite. Its
  narrow `%lf` converts through a long double and rounds twice, 1 ulp off at times: `test/fuzz/format.c` allows
  that ulp for the narrow functions. One engine for narrow and wide characters, like printf, the numbers by
  `src/FloatParse.cpp`, would remove the tolerance and the remaining gdtoa and pformat code of libmingwex.
- **Paths with spaces.** The `--sysroot` line of `ntcrt.cfg` and the `-L` of `ntcrt.specs` break when the path of
  the checkout or the toolchain contains a space.
- **Wildcards of `__getmainargs`.** msvcrt expands `*` and `?` of the command line when the program sets
  `_dowildcard` (`CRT_glob` of MinGW-w64, `-D_CRT_glob` or `int _dowildcard = -1`), NtCRT passes them as they are.
  Behind the flag, so that a program without it does not link the directory functions. The tests of Wine show it.
- **A correctly rounded libm**, a port of CORE-MATH for example, in place of the math of libmingwex. A large work.

## Builds and features

- **Opt-in feature libraries**, `libntcrt_nls.a` first: the core keeps a lookup hook (a locale by name, a code
  page by number) that knows "C" and "C.UTF-8" only, the library placed before `-lntcrt` replaces it. One source
  tree and one test matrix instead of a lean and a full build. What the core does not support fails (`setlocale`
  returns `NULL`), it never gives a different result. Candidates:
  - locales with a language: `localeconv`, the names of `strftime`, `strcoll`. The data is the undocumented
    `locale.nls` of Windows (no size) or a subset of CLDR built in (stable across Windows versions).
  - code pages `.1252`, `.950` and so on by `RtlInitCodePageTable` on the `C_*.NLS` files of the system, GB18030
    for `_setmbcp`, and `iconv` on the same tables.
  - time zones from the registry by `NtQueryValueKey`.
- **A msvcrt-compatible build**, msvcrt quirks reproduced, for testing and compatibility. Not a replacement of
  msvcrt.dll. Later.

## Libraries

- libssp and `_FORTIFY_SOURCE` (`__stack_chk_fail`, `__stack_chk_guard`, `__memcpy_chk` and the like), now taken
  from mingw-w64.
- `dlopen`, `dlsym`, `dlclose` and `dlerror` by `LdrLoadDll` and `LdrGetProcedureAddress`.
- `mmap` and `munmap` by `NtCreateSection` and `NtMapViewOfSection`; `clock_gettime` and `nanosleep` if the
  pthread work leaves them out.
- Sockets would mean IOCTLs on `\Device\Afd` (undocumented, large): not recommended.

## Size and speed

- **iostream** (lld 754688 bytes, GNU ld 1229312): mostly the locale facets of libstdc++. Measure what pulls them
  before deciding whether it is worth the work.
- **Benchmarks** of the printf engine against UCRT and the pformat of libmingwex for common formats (`%d`, `%s`,
  `%.17g`), and of strtod against UCRT.

## Tests

- musl libc-test: the functional and regression tests of string, stdio, stdlib and wchar, and the vectors of
  `src/math`. Many tests are POSIX only.
- The vectors of the printf, strtod and math tests of LLVM libc (`libc/test`), without its framework.
- A subset of the libstdc++ testsuite (`27_io`, `22_locale`) for libstdc++ on NtCRT.
