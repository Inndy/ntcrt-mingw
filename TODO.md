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

- [ ] **`CONIN$` and `CONOUT$` on Windows 7.** CreateFile of kernel32 opens them by the console server,
  `NtCreateFile` does not: `_open` (`_NtCRT_CreateFile`) and `CreateFileW` (`_K32_CreateFileW`) fail. `_getwch`,
  `_getch` and the other functions of `Conio.cpp` open `CONIN$` by `_K32_CreateFileW` and always return `WEOF` there
  (win32lab, `research/console_encoding/README.md` §10). Fixed, to verify on Windows 7: before Windows 8 the names
  of `BaseIsThisAConsoleName` of kernel32 (`_NtCRT_IsLegacyConsoleName`: `CONIN$`, `CONOUT$`, `CON` for reading or
  writing alone) go to CreateFileW of kernel32 and give a legacy console handle; `_open` makes it inheritable by
  `SetHandleInformation`. `make test-console` opens them by name (`open`).
- [ ] **The whole test suite on Windows 7.** Only the programs above ran there. The event parker of
  `PthreadWait.cpp` (no `NtWaitForAlertByThreadId` before Windows 8) runs on Windows 7 only.

## Console

Found by win32lab on NtCRT 0f359eb (`research/console_encoding/README.md` §9 and §10 of win32lab, commits 5e424df
and 59b9e70). Waiting for the user to order them with the rest.

- [x] **The bytes written that conhost reports in a DBCS code page.** conhost of Windows 10 and later reports the
  length of the text converted back to the code page (`GetALengthFromW`, `src/host/_stream.cpp:629` of
  microsoft/terminal), less than the bytes it took when a character is not in the code page; Windows 7 reports the
  bytes taken. `_NtCRT_write` (`src/LowIO.cpp`) trusts the count:
  - binary mode writes the rest again, the console shows the end twice;
  - ANSI text mode takes a short chunk (1023 bytes) as the end and writes no more: 70 of 1093 bytes are lost while
    `fwrite` and `printf` return the whole count.
  Fixed: a write to a console that succeeds took all the bytes (`_NtCRT_WriteBytes`, `_NtCRT_IsConsoleFd` on a
  short count only); `make test-console` writes in code page 950 (`dbcs`). The Unicode modes and "C.UTF-8" write
  by `WriteConsoleW` and are right.
- [ ] **Ctrl+Z in the middle of a console line.** In "C" and `""` a read keeps `ab^Z` and drops the rest of the
  line, then waits for more (as msvcrt); in "C.UTF-8" and the Unicode modes it returns `ab`, then end of file once.
  Choose one: the second (the end of the input, as at the start of a line) unless the user wants the msvcrt one.
- [x] Verified by win32lab: "C.UTF-8" reads a console by `ReadConsoleW`, right on Windows 7 and 11 in code pages
  950 and 65001, the only read of a C runtime that is (`ReadFile` gets 0 bytes for a line with non-ASCII in 65001
  on Windows 7).

## tcc

- [x] **R8: an ELF `libntcrt.a` for tcc** (`make TCC=1`, README "tcc"). With tcc's own `crt1.o` and libtcc1, a
  program imports ntdll only. tcctest.c gives the output of msvcrt but for the digits and nan/inf of NtCRT, tcc
  builds itself on it, and that tcc builds tcctest.c and itself byte-identical.
- [ ] **Integration into tcc-ntdll** (the ntcrt-mingw-e1 session): the SDK and the libraries tcc links by default,
  `tcc -run` (runmain.o), Windows 7.

## kernel32

- Out of scope: a kernel32 polyfill library (`GetSystemDirectoryA`, `SearchPathA`, `SetConsoleMode`, ...) is not
  the work of a C runtime. kernel32 is linked after NtCRT by default, `-ntcrt-strict` leaves it out.

## Housekeeping

- [ ] Squash a6ed296 into 740db45 (both count the Unicode text mode positions), before the push.
- [ ] Push main (63 commits ahead of `fojo/main` at 3f871d1).
