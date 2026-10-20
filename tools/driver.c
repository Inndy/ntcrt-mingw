/*
 * The compiler drivers of an installation of NtCRT: <cpu>-w64-ntcrt-mingw32-<tool>.exe in <prefix>/bin runs
 *
 *   gcc, g++, cc, c++   NTCRT_GCC_DIR gcc or g++ -static -specs=<prefix>/lib/gcc/ntcrt.specs
 *                       -isystem <prefix>/include -L<prefix>/lib/gcc
 *   clang, clang++      NTCRT_LLVM_DIR clang or clang++ --config=<prefix>/lib/clang/ntcrt.cfg
 *
 * with the arguments of the driver after these, and returns its exit code. -ntcrt-strict leaves kernel32 out (GCC:
 * the specs read it, clang: ntcrt-strict.cfg). An empty NTCRT_GCC_DIR or NTCRT_LLVM_DIR finds the compiler by the
 * search of CreateProcessW (PATH). The arguments are quoted again as the C runtime of the compiler parses them.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#ifndef NTCRT_GCC_DIR
#define NTCRT_GCC_DIR L""
#endif
#ifndef NTCRT_LLVM_DIR
#define NTCRT_LLVM_DIR L""
#endif

#define DRIVER_TRIPLE L"-w64-ntcrt-mingw32-"

static wchar_t* Line;
static size_t Length, Capacity;

static void Fail(const wchar_t* Text, const wchar_t* More)
{
    fputws(L"ntcrt driver: ", stderr);
    fputws(Text, stderr);
    fputws(More, stderr);
    fputws(L"\n", stderr);
}

static void* Allocate(void* Block, size_t Size)
{
    if ((Block = realloc(Block, Size)) == NULL)
    {
        Fail(L"out of memory", L"");
        exit(1);
    }
    return Block;
}

static void Append(const wchar_t* Text, size_t Count)
{
    if (Length + Count + 1 > Capacity)
    {
        Capacity = (Length + Count + 1) * 2;
        Line = (wchar_t*)Allocate(Line, Capacity * sizeof(wchar_t));
    }
    memcpy(Line + Length, Text, Count * sizeof(wchar_t));
    Length += Count;
    Line[Length] = L'\0';
}

/* An argument quoted so that the C runtime of the compiler parses it back: backslashes before a quote doubled */
static void AppendArgument(const wchar_t* Argument)
{
    size_t Backslashes = 0;

    if (Length != 0)
    {
        Append(L" ", 1);
    }
    Append(L"\"", 1);
    for (const wchar_t* p = Argument; *p != L'\0'; p++)
    {
        if (*p == L'\\')
        {
            Backslashes++;
            continue;
        }
        for (size_t i = 0; i < (*p == L'"' ? Backslashes * 2 + 1 : Backslashes); i++)
        {
            Append(L"\\", 1);
        }
        Backslashes = 0;
        Append(p, 1);
    }
    for (size_t i = 0; i < Backslashes * 2; i++)
    {
        Append(L"\\", 1);
    }
    Append(L"\"", 1);
}

/* An argument of two parts, the second a path */
static void AppendOption(const wchar_t* Option, const wchar_t* Root, const wchar_t* Path)
{
    size_t Size = wcslen(Option) + wcslen(Root) + wcslen(Path) + 1;
    wchar_t* Argument = (wchar_t*)Allocate(NULL, Size * sizeof(wchar_t));

    wcscat(wcscat(wcscpy(Argument, Option), Root), Path);
    AppendArgument(Argument);
    free(Argument);
}

int wmain(int argc, wchar_t** argv)
{
    static wchar_t Self[32768];
    wchar_t *Name, *Tool, *End;
    STARTUPINFOW Startup = {sizeof(Startup)};
    PROCESS_INFORMATION Process;
    DWORD ExitCode;
    int Strict = 0, Clang;

    /* <prefix>\bin\<cpu>-w64-ntcrt-mingw32-<tool>.exe: the root is <prefix> */
    if (GetModuleFileNameW(NULL, Self, 32768) == 0 || (Name = wcsrchr(Self, L'\\')) == NULL)
    {
        Fail(L"no path of the driver", L"");
        return 1;
    }
    *Name++ = L'\0';
    End = wcsrchr(Self, L'\\');
    if (End != NULL)
    {
        *End = L'\0';
    }
    if ((End = wcsrchr(Name, L'.')) != NULL && _wcsicmp(End, L".exe") == 0)
    {
        *End = L'\0';
    }
    Tool = wcsstr(Name, DRIVER_TRIPLE);
    if (Tool == NULL || Tool - Name != 6 || wcsncmp(Name, L"x86_64", 6) != 0)
    {
        Fail(Name, L" is not x86_64" DRIVER_TRIPLE L"<tool>, NtCRT is x64 only");
        return 1;
    }
    Tool += wcslen(DRIVER_TRIPLE);

    for (int i = 1; i < argc; i++)
    {
        Strict |= wcscmp(argv[i], L"-ntcrt-strict") == 0;
    }
    Clang = wcscmp(Tool, L"clang") == 0 || wcscmp(Tool, L"clang++") == 0;
    if (Clang)
    {
        AppendOption(NTCRT_LLVM_DIR, L"", Tool);
        AppendOption(L"--config=", Self, Strict ? L"\\lib\\clang\\ntcrt-strict.cfg" : L"\\lib\\clang\\ntcrt.cfg");
    } else if (wcscmp(Tool, L"gcc") == 0 || wcscmp(Tool, L"cc") == 0 || wcscmp(Tool, L"g++") == 0 ||
               wcscmp(Tool, L"c++") == 0)
    {
        AppendOption(NTCRT_GCC_DIR, L"", wcscmp(Tool, L"gcc") == 0 || wcscmp(Tool, L"cc") == 0 ? L"gcc" : L"g++");
        AppendArgument(L"-static");
        AppendOption(L"-specs=", Self, L"\\lib\\gcc\\ntcrt.specs");
        AppendArgument(L"-isystem");
        AppendOption(L"", Self, L"\\include");
        AppendOption(L"-L", Self, L"\\lib\\gcc");
    } else
    {
        Fail(L"no tool ", Tool);
        return 1;
    }
    for (int i = 1; i < argc; i++)
    {
        if (!Clang || wcscmp(argv[i], L"-ntcrt-strict") != 0)
        {
            AppendArgument(argv[i]);
        }
    }

    if (!CreateProcessW(NULL, Line, NULL, NULL, TRUE, 0, NULL, NULL, &Startup, &Process))
    {
        wchar_t Error[16];

        _ultow(GetLastError(), Error, 10);
        Fail(L"cannot run ", Line);
        Fail(L"error ", Error);
        return 1;
    }
    WaitForSingleObject(Process.hProcess, INFINITE);
    if (!GetExitCodeProcess(Process.hProcess, &ExitCode))
    {
        ExitCode = 1;
    }
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return (int)ExitCode;
}
