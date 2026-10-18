/* msvcrt and C behaviors that the CRT tests of Wine found NtCRT to miss (make test-wine) */

#include <KNSoft/NDK/NDK.h>

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <mbctype.h>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>

extern "C" BOOL __cdecl __crtGetStringTypeW(DWORD Unused, DWORD InfoType, LPCWSTR Src, int SrcCount, LPWORD Types);

static BOOL g_Ok;

static void Check(bool Ok, const char* What)
{
    if (!Ok)
    {
        printf("FAIL msvcrt: %s\n", What);
        g_Ok = FALSE;
    }
}

#define CHECK(Expr) Check(Expr, #Expr)

static void TestNullArguments(void)
{
    char* End = (char*)1;
    char Buffer[16];

    errno = 0;
    CHECK(strtod(NULL, &End) == 0 && End == NULL && errno == EINVAL);
    errno = 0;
    CHECK(_gcvt(1.2, 5, NULL) == NULL && errno == EINVAL);
    errno = 0;
    CHECK(_gcvt(1.2, -1, Buffer) == NULL && errno == ERANGE);
    errno = 0;
    CHECK(strxfrm(NULL, "src", 1) == INT_MAX && errno == EINVAL);
    CHECK(strxfrm(NULL, "src", 0) == 3);
    errno = 0;
    CHECK(wcsxfrm(NULL, L"src", 1) == INT_MAX && errno == EINVAL);
    errno = 0;
    CHECK(_stricmp(NULL, "a") == _NLSCMPERROR && errno == EINVAL);
    errno = 0;
    CHECK(_strnicmp("a", NULL, 1) == _NLSCMPERROR && errno == EINVAL);
    errno = 0;
    CHECK(_memicmp("a", NULL, 1) == _NLSCMPERROR && errno == EINVAL);
    CHECK(_memicmp(NULL, NULL, 0) == 0);
    CHECK(_stricmp("a", "C") == -2 && _strnicmp("abX", "ABy", 2) == 0 && _memicmp("[", "a", 1) == '[' - 'a');
}

static void TestConversions(void)
{
    _CRT_DOUBLE Double;
    _CRT_FLOAT Float;
    WORD Type = 0;

    CHECK(__crtGetStringTypeW(0, CT_CTYPE1, L"A", 1, &Type) && (Type & C1_UPPER));
    CHECK(_atodbl(&Double, (char*)"1e-309") == _UNDERFLOW);
    CHECK(_atodbl(&Double, (char*)"1e-400") == _UNDERFLOW);
    CHECK(_atodbl(&Double, (char*)"1e400") == _OVERFLOW);
    CHECK(_atoflt_l(&Float, (char*)"1e-40", NULL) == _UNDERFLOW);
}

static void TestTime(void)
{
    struct tm Tm = { 0, 0, 0, 1, 0, 70, 4, 0, 0 };
    char Buffer[64];

    strftime(Buffer, sizeof(Buffer), "%#c|%#x", &Tm);
    CHECK(strcmp(Buffer, "Thursday, January 01, 1970 00:00:00|Thursday, January 01, 1970") == 0);
    memset(&Tm, 0, sizeof(Tm));
    CHECK(_gmtime32_s(&Tm, NULL) == EINVAL && Tm.tm_year == -1 && Tm.tm_mday == -1);

    /* The day of asctime by "%3d" of C as UCRT, msvcrt writes "Jan 01" */
    struct tm Day = { 5, 4, 3, 2, 0, 80, 3, 0, 0 };
    CHECK(strcmp(asctime(&Day), "Wed Jan  2 03:04:05 1980\n") == 0);
    Day.tm_mday = 12;
    CHECK(asctime_s(Buffer, sizeof(Buffer), &Day) == 0 && strcmp(Buffer, "Wed Jan 12 03:04:05 1980\n") == 0);
    /* %c of C as UCRT, msvcrt writes "01/12/80 03:04:05" */
    Day.tm_mday = 2;
    strftime(Buffer, sizeof(Buffer), "%c|%x|%X", &Day);
    CHECK(strcmp(Buffer, "Wed Jan  2 03:04:05 1980|01/02/80|03:04:05") == 0);
    wchar_t WideBuffer[64];
    wcsftime(WideBuffer, 64, L"%c", &Day);
    CHECK(wcscmp(WideBuffer, L"Wed Jan  2 03:04:05 1980") == 0);
}

/* The names of tmpnam fit in L_tmpnam, "\s<pid>.<counter>" in base 32 as msvcrt */
static void TestTmpnam(void)
{
    char Name[L_tmpnam], Last[L_tmpnam] = "";
    wchar_t WideName[L_tmpnam];

    for (int i = 0; i < 100; i++)
    {
        CHECK(tmpnam(Name) == Name && strlen(Name) < L_tmpnam && strncmp(Name, "\\s", 2) == 0 &&
              strchr(Name, '.') != NULL && strcmp(Name, Last) != 0);
        strcpy(Last, Name);
    }
    CHECK(tmpnam(NULL) != NULL && strlen(tmpnam(NULL)) < L_tmpnam);
    CHECK(_wtmpnam(WideName) == WideName && wcslen(WideName) < L_tmpnam && WideName[0] == L'\\');
    CHECK(tmpnam_s(Name, sizeof(Name)) == 0 && strncmp(Name, "\\s", 2) == 0);
    CHECK(tmpnam_s(Name, 3) == ERANGE && Name[0] == '\0');
}

static void TestFiles(void)
{
    static const char Name[] = "ntcrt_msvcrt.tmp";
    struct _stat64 St;
    FILE* File = NULL;
    HANDLE Other;
    int Pipe[2], Saved, fd;

    CHECK(fopen_s(&File, Name, "w") == 0 && File != NULL);
    if (File != NULL)
    {
        /* _SH_SECURE: no sharing for a write */
        Other = CreateFileA(Name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        CHECK(Other == INVALID_HANDLE_VALUE);
        if (Other != INVALID_HANDLE_VALUE)
        {
            CloseHandle(Other);
        }
        CHECK(_fstat64(_fileno(File), &St) == 0 && St.st_dev == 0 && (St.st_mode & _S_IFMT) == _S_IFREG);
        errno = 0;
        CHECK(_pclose(File) == -1 && errno == EBADF);
        fclose(File);
    }
    CHECK(_pipe(Pipe, 64, _O_BINARY) == 0);
    CHECK(_fstat64(Pipe[0], &St) == 0 && St.st_mode == _S_IFIFO && St.st_dev == (_dev_t)Pipe[0]);
    _close(Pipe[0]);
    _close(Pipe[1]);

    /* An fd 0 to 2 opened is the standard handle */
    Saved = _dup(0);
    _close(0);
    fd = _open(Name, _O_RDONLY);
    CHECK(fd == 0 && GetStdHandle(STD_INPUT_HANDLE) == (HANDLE)_get_osfhandle(0));
    _dup2(Saved, 0);
    _close(Saved);
    remove(Name);
}

static void TestMbcs(void)
{
    char Drive[8], Dir[32], Fname[32], Ext[8];
    int CodePage = _getmbcp();

    /* 0x83 0x5C is one character of code page 932, the 0x5C is not a separator */
    CHECK(_setmbcp(932) == 0);
    _splitpath("c:\\\x83\x5c\\\x83\x5c.bin", Drive, Dir, Fname, Ext);
    CHECK(strcmp(Dir, "\\\x83\x5c\\") == 0 && strcmp(Fname, "\x83\x5c") == 0 && strcmp(Ext, ".bin") == 0);
    CHECK(_splitpath_s("\x83\x5c.b", NULL, 0, Dir, sizeof(Dir), Fname, sizeof(Fname), Ext, sizeof(Ext)) == 0 &&
          Dir[0] == 0 && strcmp(Fname, "\x83\x5c") == 0);
    CHECK(_setmbcp(1252) == 0);
    _splitpath("c:\\\x83\x5cx.bin", Drive, Dir, Fname, Ext);
    CHECK(strcmp(Dir, "\\\x83\x5c") == 0 && strcmp(Fname, "x") == 0);
    _setmbcp(CodePage);

    /* The POSIX names are those of NtCRT, Unicode case in "C.UTF-8" */
    if (setlocale(LC_CTYPE, "C.UTF-8") != NULL)
    {
        CHECK(wcsicmp(L"\u00e9", L"\u00c9") == 0 && wcsnicmp(L"\u00e9x", L"\u00c9y", 1) == 0);
        setlocale(LC_CTYPE, "C");
    }
}

extern "C"
BOOL
TestMsvcrt(VOID)
{
    g_Ok = TRUE;
    TestNullArguments();
    TestConversions();
    TestTime();
    TestTmpnam();
    TestFiles();
    TestMbcs();
    return g_Ok;
}
