/* msvcrt and C behaviors that the CRT tests of Wine found NtCRT to miss (make test-wine) */

#include <KNSoft/NDK/NDK.h>

#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <locale.h>
#include <malloc.h>
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

    /* ERANGE for an underflow to zero only as UCRT and msvcrt, not for a denormal */
    errno = 0;
    CHECK(strtod("1e-310", NULL) != 0 && errno == 0);
    CHECK(strtof("1e-40", NULL) != 0 && errno == 0);
    CHECK(wcstod(L"0x1p-1074", NULL) != 0 && errno == 0);
    CHECK(strtold("0x1p-16445", NULL) != 0 && errno == 0);
    CHECK(strtod("2e-324", NULL) == 0 && errno == ERANGE);
    errno = 0;
    CHECK(strtof("1e-46", NULL) == 0 && errno == ERANGE);
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

/* A UTF-8 read that completes the character kept by the previous read has 3 units beyond the buffer at most */
static void TestUtf8Pending(void)
{
    typedef HANDLE(WINAPI* PCREATE_NAMED_PIPE_W)(LPCWSTR, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, PVOID);
    auto const CreateNamedPipeW = (PCREATE_NAMED_PIPE_W)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                       "CreateNamedPipeW");
    wchar_t Name[64], Wide[8];
    char Back[4];
    DWORD Count;
    HANDLE Server, Client;
    int fd, n = 0;

    swprintf(Name, ARRAYSIZE(Name), L"\\\\.\\pipe\\ntcrt_msvcrt_%lu", GetCurrentProcessId());
    Server = CreateNamedPipeW(Name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 1, 64, 64, 0, NULL);
    Client = CreateFileW(Name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(Server != INVALID_HANDLE_VALUE && Client != INVALID_HANDLE_VALUE);
    if (Server == INVALID_HANDLE_VALUE || Client == INVALID_HANDLE_VALUE)
    {
        return;
    }
    /* F0 80 is kept by the first read, the second reads 80, keeps F0 80 80 and reads 1 byte more */
    CHECK(WriteFile(Client, "A\xf0\x80\x80" "BCDE", 8, &Count, NULL) && Count == 8);
    fd = _open_osfhandle((intptr_t)Server, 0);
    CHECK(_setmode(fd, _O_U8TEXT) != -1 && _read(fd, Wide, 3 * sizeof(wchar_t)) == 2 && Wide[0] == L'A');
    while (n < 8 && _read(fd, Wide + n, sizeof(wchar_t)) == 2 && Wide[n++] != L'E')
    {
    }
    CHECK(n >= 5 && Wide[0] == 0xFFFD && wmemcmp(Wide + n - 4, L"BCDE", 4) == 0);
    /* A unit beyond Pending would be a high surrogate waiting for the next write */
    CHECK(_write(fd, L"x", sizeof(wchar_t)) == 2 && ReadFile(Client, Back, sizeof(Back), &Count, NULL) &&
          Count == 1 && Back[0] == 'x');
    _close(fd);
    CloseHandle(Client);
}

static void WriteBytes(const char* Name, const void* Bytes, size_t Size)
{
    FILE* File = fopen(Name, "wb");

    CHECK(File != NULL && fwrite(Bytes, 1, Size, File) == Size);
    if (File != NULL)
    {
        fclose(File);
    }
}

/* A Ctrl+Z at the end of a text file opened for reading and writing is removed, the last byte of UTF-16 is kept */
static void TestCtrlZ(void)
{
    static const char Name[] = "ntcrt_msvcrt.tmp";
    wchar_t Unit;
    int fd;

    WriteBytes(Name, "a\x1a", 2);
    fd = _open(Name, _O_RDWR | _O_TEXT);
    CHECK(fd >= 0 && _filelength(fd) == 1 && _tell(fd) == 0);
    _close(fd);
    WriteBytes(Name, "\xff\xfe\x00\x1a", 4);
    fd = _open(Name, _O_RDWR | _O_U16TEXT);
    CHECK(fd >= 0 && _filelength(fd) == 4 && _read(fd, &Unit, sizeof(Unit)) == 2 && Unit == 0x1A00);
    _close(fd);
    /* Nothing is left, the file gets the BOM of an empty one */
    WriteBytes(Name, "\x1a", 1);
    fd = _open(Name, _O_RDWR | _O_U8TEXT);
    CHECK(fd >= 0 && _filelength(fd) == 3 && _tell(fd) == 3);
    _close(fd);
    remove(Name);
}

/* The buffer _getcwd allocates has maxlen bytes at least */
static void TestGetcwd(void)
{
    char* Cwd = _getcwd(NULL, 4096);

    CHECK(Cwd != NULL && _msize(Cwd) >= 4096);
    free(Cwd);
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
    TestUtf8Pending();
    TestCtrlZ();
    TestGetcwd();
    TestMbcs();
    return g_Ok;
}
