/*
 * The wildcards of the arguments (_dowildcard): the program makes glob.tmp with files and runs itself there with each
 * command line, the child compares its argv with the arguments that msvcrt gives (msvcrt 7.0.26100 of Windows 11).
 * Built with -lntcrt_glob: alone, with -DDEFINE=-1 (int _dowildcard = -1), with CRT_glob.o of MinGW-w64 and -DWIDE
 * (wmain); -DNONE expects no expansion: without the library and _dowildcard, or with -DDEFINE=0.
 */

#include <direct.h>
#include <io.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#ifdef DEFINE
int _dowildcard = DEFINE;
#endif

#ifdef WIDE
#define T(s) L##s
typedef wchar_t Char;
#define Compare wcscmp
#define FORMAT "%ls"
#else
#define T(s) s
typedef char Char;
#define Compare strcmp
#define FORMAT "%s"
#endif

#define C_FILES T(".hid.c"), T("_x.c"), T("A.c"), T("a2.c"), T("b.c"), T("B2.c"), T("sp ace.c")
#define ALL T(".hid.c"), T("_x.c"), T("A.c"), T("a2.c"), T("b.c"), T("B2.c"), T("sdir"), T("sp ace.c"), T("sub"), \
            T("x.html")

/* A command line after the program name and the arguments it gives */
static const struct
{
    const wchar_t* Line;
    const Char* Expected[12];
} Cases[] = {
#ifdef NONE
    { L"*.c \"?\" a*", { T("*.c"), T("?"), T("a*") } },
#else
    { L"*.c", { C_FILES } },
    { L"\"*.c\"", { C_FILES } },
    { L"a\"*\".c", { T("A.c"), T("a2.c") } },
    { L"*.zzz", { T("*.zzz") } },
    { L"sub/*.c", { T("sub/S0.c"), T("sub/s1.c") } },
    { L"sub\\*.c", { T("sub\\S0.c"), T("sub\\s1.c") } },
    { L"s*/x.c", { T("s*/x.c") } },
    { L"*", { ALL } },
    { L"*.*", { ALL } },
    { L"?.c", { T("A.c"), T("b.c") } },
    { L"a?.c", { T("A.c"), T("a2.c") } },
    { L"[ab].c", { T("[ab].c") } },
    { L"*.", { T("sdir"), T("sub") } },
    { L"*..", { T("sdir"), T("sub") } },
    { L"*.c.", { C_FILES } },
    { L"????", { T("sdir"), T("sub") } },
    { L".\\a*", { T(".\\A.c"), T(".\\a2.c") } },
    { L"sp*", { T("sp ace.c") } },
    /* A quote of the pattern is the DOS_DOT of NtQueryDirectoryFile, as for FindFirstFile */
    { L"\\\"*.c", { T(".hid.c") } },
    { L"*.zzz x \"a b\" ?.c", { T("*.zzz"), T("x"), T("a b"), T("A.c"), T("b.c") } },
#endif
};

static const wchar_t* Files[] = { L"b.c", L"A.c", L"a2.c", L"_x.c", L"B2.c", L".hid.c", L"x.html", L"sp ace.c",
                                  L"sub\\s1.c", L"sub\\S0.c" };

static int Child(int Index, int argc, Char** argv)
{
    const Char* const* Expected = Cases[Index].Expected;
    int Count = 0;

    while (Count < 12 && Expected[Count] != NULL)
    {
        Count++;
    }
    for (int i = 0; i < Count; i++)
    {
        if (i + 1 >= argc || Compare(argv[i + 1], Expected[i]) != 0)
        {
            Count = -1;
            break;
        }
    }
    if (Count != argc - 1)
    {
        printf("glob: %ls gives", Cases[Index].Line);
        for (int i = 1; i < argc; i++)
        {
            printf(" [" FORMAT "]", argv[i]);
        }
        printf("\n");
        return 1;
    }
    return 0;
}

#ifdef WIDE
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    wchar_t Index[8];
    int Failures = 0;

    if (_wgetenv(L"GLOB_CASE") != NULL)
    {
        return Child(_wtoi(_wgetenv(L"GLOB_CASE")), argc, argv);
    }
    _wmkdir(L"glob.tmp");
    _wchdir(L"glob.tmp");
    _wmkdir(L"sub");
    _wmkdir(L"sdir");
    for (size_t i = 0; i < sizeof(Files) / sizeof(Files[0]); i++)
    {
        /* "w" fails for the hidden file of an earlier run */
        FILE* File = _waccess(Files[i], 0) == 0 ? NULL : _wfopen(Files[i], L"w");

        if (File == NULL && _waccess(Files[i], 0) != 0)
        {
            printf("glob: cannot make %ls\n", Files[i]);
            return 1;
        }
        if (File != NULL)
        {
            fclose(File);
        }
    }
    /* Hidden files match too */
    SetFileAttributesW(L"b.c", FILE_ATTRIBUTE_HIDDEN);
    /* _wspawnl joins the arguments with spaces, the quotes are those of the command line */
    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        swprintf(Index, 8, L"%u", (unsigned)i);
        _wputenv_s(L"GLOB_CASE", Index);
        Failures += _wspawnl(_P_WAIT, _wpgmptr, L"glob", Cases[i].Line, NULL) != 0;
    }
    if (Failures == 0)
    {
        printf("glob: ok\n");
    }
    return Failures != 0;
}
