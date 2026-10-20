# TODO

Work taken up, in order. Proposals not started yet are in `IDEAS.md`.

## x86 (i686), phase 5

Phases 1 to 4 are done: the build (`build-i686` by the gcc of MSYS2 mingw32, MinGW-w64 15), the startup, setjmp,
the arguments of printf and scanf, pthreads, the 32-bit `time_t` names and the x87 control word.
`make test` passes on x86 (MinGW-w64 15 of MSYS2 mingw32).

- [ ] **clang for x86.** MSYS2 has no clang32: clang64 with `--target=i686-w64-mingw32` on the sysroot of mingw32.
- [ ] **Size.** Under `--gc-sections` of GNU ld the `.eh_frame` keeps every function alive: hello is 179200 bytes,
  46080 with `-fno-asynchronous-unwind-tables`. Without the tables a C++ exception cannot go through a callback
  called by NtCRT (`qsort`, `bsearch`, `pthread_once`, `call_once`). Measure and choose.

## Windows 7

Windows 7 SP1 x64 and WOW64 run `ret7`, `puts`, `nlsdata`, `pebteb`, `tlsattach`, the `thread_local` destructors
(`tl`) and `loadlib`, and tcc-ntdll runs `-run` there. The VM is reached over WinRM by the ntcrt-mingw-f2 session.
The ntdll functions NtCRT imports (145 on x64, 144 on x86) are all exported by ntdll of Windows 7; of
`libntdll_crt.a` only `atan2` is not, which libmingwex defines before it.

- [ ] **`_open("CONOUT$")` and `"CONIN$"` on Windows 7.** CreateFile of kernel32 opens them by the console
  server, `NtCreateFile` does not.
- [ ] **The whole test suite on Windows 7.** Only the programs above ran there. The event parker of
  `PthreadWait.cpp` (no `NtWaitForAlertByThreadId` before Windows 8) runs on Windows 7 only.

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
