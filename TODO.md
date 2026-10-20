# TODO

Work taken up, in order. Proposals not started yet are in `IDEAS.md`.

## x86 (i686), phase 5

Phases 1 to 4 are done: the build (`build-i686` by the gcc of MSYS2 mingw32, MinGW-w64 15), the startup, setjmp,
the arguments of printf and scanf, pthreads, the 32-bit `time_t` names and the x87 control word.
`make test-thread` and `make test-start` pass.

- [ ] **`make test` on x86.** `test-upstream` passes; `test-fuzz`, `test-printf`, `test-scanf`, `test-console`,
  `test-cxx` and `test-install` are left.
- [ ] **clang for x86.** MSYS2 has no clang32: clang64 with `--target=i686-w64-mingw32` on the sysroot of mingw32.
- [ ] **Size.** Under `--gc-sections` of GNU ld the `.eh_frame` keeps every function alive: hello is 179200 bytes,
  46080 with `-fno-asynchronous-unwind-tables`. Without the tables a C++ exception cannot go through a callback
  called by NtCRT (`qsort`, `bsearch`, `pthread_once`, `call_once`). Measure and choose.
- [ ] **Split `src/Time32.cpp`.** It holds the `*32` names of the clocks and of pthreads together, so a program
  that calls `clock_gettime32` links the pthread objects.

## Windows 7

At 3f871d1 Windows 7 SP1 x64 and WOW64 run `ret7`, `puts`, `nlsdata`, `pebteb`, `tlsattach` and the `thread_local`
destructors (`tl`). The VM is reached over WinRM by the ntcrt-mingw-f2 session. `LoadLibrary` passes a search path
to `LdrLoadDll` before Windows 8 (`test/start/loadlib.c`), to be run there.

- [ ] **The whole test suite on Windows 7.** Only the programs above ran there. The event parker of
  `PthreadWait.cpp` (no `NtWaitForAlertByThreadId` before Windows 8) runs on Windows 7 only.
- [ ] **ntdll imports that Windows 7 does not export.** An import missing from ntdll stops the loader before
  `main`. Compare the imports of `libntdll_crt.a` with the exports of ntdll of Windows 7 x64 and x86, and look up
  at run time what is newer (as `PthreadWait.cpp` and `PthreadName.cpp` do).

## tcc

- [ ] **R8: an ELF `libntcrt.a` for tcc**, so that the programs tcc-ntdll builds import ntdll only. clang
  `--target=x86_64-w64-windows-gnu-elf` compiles 76 of 85 sources; the COFF assembly of the `__imp_` macro of
  `src/NtCRT.h` blocks the rest. Without pthreads and C11 threads (tcc defines `__STDC_NO_THREADS__`), with a
  curated `ntdll.def`. Waiting for a decision.

## kernel32

- [ ] **A kernel32 polyfill library**, opt-in, for the kernel32 functions ported programs call
  (`GetSystemDirectoryA`, `SearchPathA` for tcc, `SetConsoleMode`, `GetConsoleScreenBufferInfo` for quickjs).
  kernel32 is linked after NtCRT by default, `-ntcrt-strict` leaves it out.

## Housekeeping

- [ ] Squash a6ed296 into 740db45 (both count the Unicode text mode positions), before the push.
- [ ] Push main (63 commits ahead of `fojo/main` at 3f871d1).
