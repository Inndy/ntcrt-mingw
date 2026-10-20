/* msvcrt and C behaviors that the CRT tests of Wine found NtCRT to miss (make test-wine) */

#include <KNSoft/NDK/NDK.h>

#include <ctype.h>
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <io.h>
#include <limits.h>
#include <locale.h>
#include <malloc.h>
#include <math.h>
#include <mbctype.h>
#include <mbstring.h>
#include <process.h>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

extern "C" BOOL __cdecl __crtGetStringTypeW(DWORD Unused, DWORD InfoType, LPCWSTR Src, int SrcCount, LPWORD Types);

/* <new.h> declares them with C++ linkage */
typedef int(__cdecl* NEW_HANDLER)(size_t);
extern "C" NEW_HANDLER __cdecl _set_new_handler(NEW_HANDLER Handler);
extern "C" int __cdecl _set_new_mode(int Mode);

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

/* A binary fd keeps no Unicode restriction of a previous _setmode */
static void TestBinaryNarrow(void)
{
    static const char Name[] = "ntcrt_msvcrt.tmp";
    FILE* File;

    WriteBytes(Name, "a", 1);
    File = fopen(Name, "rb");
    CHECK(File != NULL);
    if (File != NULL)
    {
        _setmode(_fileno(File), _O_U16TEXT);
        _setmode(_fileno(File), _O_BINARY);
        CHECK(fgetc(File) == 'a');
        fclose(File);
    }
    remove(Name);
}

/* The stat functions converting from _stat64 reject a NULL buffer as it does */
static void TestStatNull(void)
{
    errno = 0;
    CHECK(_stat32(".", NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(_wstati64(L".", NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(_fstat32(0, NULL) == -1 && errno == EINVAL);
}

static int g_Dup2Fd;
static volatile LONG g_Dup2Done;

static unsigned __stdcall Dup2Loop(void* Arg)
{
    for (int i = 0; i < 20000; i++)
    {
        _dup2((int)(intptr_t)Arg, g_Dup2Fd);
        _close(g_Dup2Fd);
    }
    g_Dup2Done = TRUE;
    return 0;
}

/* _dup2 replaces fd2 at once, an fd opened meanwhile never takes it to have its handle leaked */
static void TestDup2(void)
{
    ULONG Before = 0, After = 0;
    HANDLE Thread;
    int fd = _open("NUL", _O_RDONLY);

    /* fds of a block not allocated yet */
    CHECK(fd >= 0 && _dup2(fd, 200) == 0 && _dup2(fd, 200) == 0 && _get_osfhandle(200) != -1 && _close(200) == 0);
    g_Dup2Fd = _dup(fd);
    _close(g_Dup2Fd);
    NtQueryInformationProcess(NtCurrentProcess(), (PROCESSINFOCLASS)20 /* ProcessHandleCount */, &Before,
                              sizeof(Before), NULL);
    Thread = (HANDLE)_beginthreadex(NULL, 0, Dup2Loop, (void*)(intptr_t)fd, 0, NULL);
    while (!g_Dup2Done)
    {
        int Other = _open("NUL", _O_RDONLY);
        if (Other != g_Dup2Fd)
        {
            _close(Other);
        }
    }
    WaitForSingleObject(Thread, INFINITE);
    CloseHandle(Thread);
    _close(g_Dup2Fd);
    NtQueryInformationProcess(NtCurrentProcess(), (PROCESSINFOCLASS)20, &After, sizeof(After), NULL);
    CHECK(After == Before);
    _close(fd);
}

/* The mask of _umask takes the write permission of a new file */
static void TestUmask(void)
{
    static const char Name[] = "ntcrt_msvcrt.tmp";
    int fd;

    remove(Name);
    CHECK(_umask(_S_IWRITE) == 0);
    fd = _open(Name, _O_CREAT | _O_EXCL | _O_WRONLY, _S_IREAD | _S_IWRITE);
    CHECK(fd >= 0 && (GetFileAttributesA(Name) & FILE_ATTRIBUTE_READONLY));
    _close(fd);
    _umask(0);
    _chmod(Name, _S_IREAD | _S_IWRITE);
    remove(Name);
}

/* The buffer _getcwd allocates has maxlen bytes at least */
static void TestGetcwd(void)
{
    char* Cwd = _getcwd(NULL, 4096);

    CHECK(Cwd != NULL && _msize(Cwd) >= 4096);
    free(Cwd);
}

static void PutFile(const char* Name, const char* Data, size_t Size)
{
    FILE* File = fopen(Name, "wb");

    if (File != NULL)
    {
        fwrite(Data, 1, Size, File);
        fclose(File);
    }
}

/*
 * After Units units of Data read a unit at a time by an _O_U8TEXT fd (Mode NULL) or by a buffered stream of Mode,
 * the position is Position and a seek by Ahead from the current position reads Next
 */
static BOOL CheckPosition(const char* Data, size_t Size, const char* Mode, int Units, long Position, long Ahead,
                          wchar_t Next)
{
    static const char Name[] = "ntcrt_position.tmp";
    wchar_t Unit = 0;
    FILE* File;
    BOOL Ok;
    int fd;

    PutFile(Name, Data, Size);
    if (Mode == NULL)
    {
        fd = _open(Name, _O_RDONLY | _O_U8TEXT);
        Ok = fd >= 0;
        for (int i = 0; Ok && i < Units; i++)
        {
            Ok = _read(fd, &Unit, 2) == 2;
        }
        Ok = Ok && _telli64(fd) == Position && _lseeki64(fd, Ahead, SEEK_CUR) == Position + Ahead &&
             _read(fd, &Unit, 2) == 2 && Unit == Next;
        _close(fd);
    } else
    {
        File = fopen(Name, Mode);
        Ok = File != NULL;
        for (int i = 0; Ok && i < Units; i++)
        {
            Ok = fgetwc(File) != WEOF;
        }
        Ok = Ok && ftell(File) == Position && fseek(File, Ahead, SEEK_CUR) == 0 && fgetwc(File) == Next;
        fclose(File);
    }
    remove(Name);
    return Ok;
}

/* Positioning and flushing of streams by C */
static void TestStreams(void)
{
    static const char Name[] = "ntcrt_streams.tmp";
    wchar_t Line[8], Unit = 0;
    FILE* File;
    fpos_t Pos;
    int fd;

    /* A position query keeps the characters an fd or a stream read ahead */
    PutFile(Name, "\xE2\x82\xAC" "AB", 5);
    fd = _open(Name, _O_RDONLY | _O_U8TEXT);
    CHECK(fd >= 0 && _read(fd, &Unit, 2) == 2 && Unit == 0x20AC && _telli64(fd) == 3);
    CHECK(_read(fd, &Unit, 2) == 2 && Unit == L'A' && _telli64(fd) == 4);
    _close(fd);
    File = fopen(Name, "r, ccs=UTF-8");
    CHECK(File != NULL && setvbuf(File, NULL, _IONBF, 0) == 0 && fgetwc(File) == 0x20AC);
    CHECK(ftell(File) == 3 && fgetwc(File) == L'A' && ftell(File) == 4);
    fclose(File);

    /*
     * Positions count the bytes in the file of a newline (LF or CRLF), of U+FFFD for an invalid byte and of a
     * character beyond U+FFFF, also of the units read ahead by an fd or held by the buffer of a stream
     */
    CHECK(CheckPosition("\xC2\xA9\n" "A", 4, NULL, 1, 2, 1, L'A'));
    CHECK(CheckPosition("\xC2\xA9\r\n" "A", 5, NULL, 1, 2, 2, L'A'));
    CHECK(CheckPosition("\xC2\xA9\xFF" "A", 4, NULL, 1, 2, 1, L'A'));
    CHECK(CheckPosition("\xE2\x41\n" "B", 4, NULL, 1, 1, 2, L'B'));
    CHECK(CheckPosition("\xF0\x9F\x98\x80\n" "A", 6, NULL, 2, 4, 1, L'A'));
    CHECK(CheckPosition("A\nB", 3, "r, ccs=UTF-8", 1, 1, 1, L'B'));
    CHECK(CheckPosition("A\r\nB", 4, "r, ccs=UTF-8", 1, 1, 2, L'B'));
    CHECK(CheckPosition("A\xFF" "B", 3, "r, ccs=UTF-8", 1, 1, 1, L'B'));
    CHECK(CheckPosition("\xF0\x9F\x98\x80\n" "B", 6, "r, ccs=UTF-8", 2, 4, 1, L'B'));
    CHECK(CheckPosition("A\0\n\0B\0", 6, "r, ccs=UTF-16LE", 1, 2, 2, L'B'));

    /* ftell after each character of a buffer, which goes on from the position it found before, and after ungetwc */
    PutFile(Name, "a\nb\r\nc\xC3\xA9" "d", 9);
    File = fopen(Name, "r, ccs=UTF-8");
    if (File != NULL)
    {
        static const long Positions[] = { 1, 2, 3, 5, 6, 8, 9 };

        for (long Want : Positions)
        {
            CHECK(fgetwc(File) != WEOF && ftell(File) == Want);
        }
        CHECK(ungetwc(L'd', File) == L'd' && ftell(File) == 8 && fgetwc(File) == L'd' && ftell(File) == 9);
        fclose(File);
    }

    /* The position of Unicode output counts the encoded bytes after the BOM */
    File = fopen(Name, "w, ccs=UTF-8");
    CHECK(File != NULL && fputwc(0x20AC, File) == 0x20AC && ftell(File) == 6);
    CHECK(fputwc(L'\n', File) == L'\n' && ftell(File) == 8);
    fclose(File);
    File = fopen(Name, "w, ccs=UTF-16LE");
    CHECK(File != NULL && fputwc(0x20AC, File) == 0x20AC && fputwc(L'\n', File) == L'\n' && ftell(File) == 8);
    fclose(File);

    /* fflush(NULL) flushes output streams only, a stream after fflush may read */
    PutFile(Name, "abc", 3);
    File = fopen(Name, "rb");
    CHECK(File != NULL && fgetc(File) == 'a' && fflush(NULL) == 0 && fgetc(File) == 'b');
    fclose(File);
    File = fopen(Name, "r+b");
    CHECK(File != NULL && setvbuf(File, NULL, _IONBF, 0) == 0 && fputc('X', File) == 'X');
    CHECK(fflush(File) == 0 && fgetc(File) == 'b' && !ferror(File));
    fclose(File);

    /* A write error of the flush fails fsetpos */
    fd = _open(Name, _O_RDONLY | _O_BINARY);
    File = _fdopen(fd, "r+b");
    CHECK(File != NULL && fgetpos(File, &Pos) == 0 && fputc('Y', File) == 'Y');
    CHECK(fsetpos(File, &Pos) != 0 && ferror(File));
    fclose(File);

    /* fgetws fails on an encoding error after some characters */
    if (setlocale(LC_ALL, "C.UTF-8") != NULL)
    {
        PutFile(Name, "A\xFF", 2);
        File = fopen(Name, "r");
        errno = 0;
        CHECK(File != NULL && fgetws(Line, ARRAYSIZE(Line), File) == NULL && errno == EILSEQ && ferror(File));
        fclose(File);
        PutFile(Name, "A\xE2\x82", 3);
        File = fopen(Name, "r");
        CHECK(File != NULL && fgetws(Line, ARRAYSIZE(Line), File) == NULL && ferror(File));
        clearerr(File);
        CHECK(fgetws(Line, ARRAYSIZE(Line), File) == NULL && !ferror(File));
        fclose(File);
        setlocale(LC_ALL, "C");
    }
    remove(Name);
}

/* The conversions stop at the count, an encoding error leaves an empty destination */
static void TestMultibyte(void)
{
    char Bytes[4] = { 'X', 'X', 'X', 'X' };
    wchar_t Wide[4] = { L'X', L'X', L'X', L'X' };
    const wchar_t* WideSrc = L"A\u0100";
    const char* Src;
    mbstate_t State = 0;
    size_t Count;

    CHECK(wcstombs_s(&Count, Bytes, sizeof(Bytes), L"A\u0100", 1) == 0 && Count == 2 && strcmp(Bytes, "A") == 0);
    /* A character beyond the count does not make a short buffer an encoding error */
    CHECK(wcstombs_s(&Count, Bytes, 1, L"A\u0100", 1) == ERANGE);
    CHECK(wcstombs_s(&Count, Bytes, 1, L"\u0100", 1) == EILSEQ);
    CHECK(wcstombs(Bytes, L"A\u0100", 1) == 1 && Bytes[0] == 'A');
    CHECK(wcsrtombs(Bytes, &WideSrc, 1, &State) == 1 && WideSrc != NULL && *WideSrc == 0x100);
    WideSrc = L"a\u0100";
    memset(Bytes, 'X', sizeof(Bytes));
    CHECK(wcsrtombs_s(&Count, Bytes, sizeof(Bytes), &WideSrc, 3, &State) == EILSEQ && Count == (size_t)-1 &&
          Bytes[0] == '\0');
    if (setlocale(LC_CTYPE, "C.UTF-8") != NULL)
    {
        CHECK(mbstowcs_s(&Count, Wide, 4, "A\xff", 1) == 0 && Count == 2 && wcscmp(Wide, L"A") == 0);
        CHECK(mbstowcs_s(&Count, Wide, 1, "A\xff", 1) == ERANGE && mbstowcs_s(&Count, Wide, 1, "\xff", 1) == EILSEQ);
        /* The fourth byte of a character of a surrogate pair, a lone high surrogate */
        CHECK(mbstowcs_s(&Count, Wide, 1, "\xF0\x9F\x98" "A", 1) == EILSEQ);
        CHECK(wcstombs_s(&Count, Bytes, 1, L"\xD800", 1) == EILSEQ);
        Src = "a\xff";
        State = 0;
        wmemset(Wide, L'X', 4);
        CHECK(mbsrtowcs_s(&Count, Wide, 4, &Src, 3, &State) == EILSEQ && Count == (size_t)-1 && Wide[0] == L'\0');
        /* No null byte in the bytes counted, as UCRT */
        CHECK(_mbstrnlen("\xC3\xA9", 2) == 2 && _mbstrnlen("\xC3\xA9", 3) == 1);
        setlocale(LC_CTYPE, "C");
    }
}

/* volatile: GCC assumes that the allocation functions write no global variable */
static volatile int g_NewHandlerCalls;

static int __cdecl CountNewHandler(size_t Size)
{
    (void)Size;
    g_NewHandlerCalls = g_NewHandlerCalls + 1;
    return 0;
}

static void TestMisc(void)
{
    int Decpt, Sign;
    char* Digits;

    /* The table is set before any call of __sys_errlist */
    CHECK(_sys_errlist[EINVAL] != NULL && strcmp(_sys_errlist[EINVAL], "Invalid argument") == 0);

    /* A tab is blank and a control character, not printing as UCRT (msvcrt says printing) */
    CHECK(!iswprint(L'\t') && iswprint(L' ') && iswblank(L'\t'));

    Digits = _fcvt(1.0, INT_MAX, &Decpt, &Sign);
    CHECK(Digits != NULL && strlen(Digits) == 347 && Decpt == 1);

    /* _DN_FLUSH_OPERANDS_SAVE_RESULTS (0x02000000) sets DAZ, _DN_SAVE_OPERANDS_FLUSH_RESULTS (0x03000000) FTZ */
    _controlfp(0x02000000, _MCW_DN);
    CHECK((__builtin_ia32_stmxcsr() & 0x8040) == 0x0040 && (_controlfp(0, 0) & _MCW_DN) == 0x02000000);
    _controlfp(0x03000000, _MCW_DN);
    CHECK((__builtin_ia32_stmxcsr() & 0x8040) == 0x8000 && (_controlfp(0, 0) & _MCW_DN) == 0x03000000);
    _controlfp(_DN_SAVE, _MCW_DN);

    /* The new handler is called for calloc and realloc too */
    NEW_HANDLER Old = _set_new_handler(CountNewHandler);
    int OldMode = _set_new_mode(1);
    void* Block = malloc(16);
    /* volatile: GCC takes an allocation that is only compared with NULL as successful */
    void* volatile Result;
    g_NewHandlerCalls = 0;
    Result = calloc(1, (size_t)1 << 48);
    CHECK(Result == NULL && g_NewHandlerCalls == 1);
    Result = realloc(Block, (size_t)1 << 48);
    CHECK(Result == NULL && g_NewHandlerCalls == 2);
    free(Block);
    _set_new_mode(OldMode);
    _set_new_handler(Old);
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
    /* The source terminator ends the concatenation, nothing is written after it */
    unsigned char Cat[3] = { 0, 0, 'X' };
    CHECK(_mbsnbcat(Cat, (const unsigned char*)"A", 2) == Cat && strcmp((char*)Cat, "A") == 0 && Cat[2] == 'X');
    Cat[0] = 0;
    CHECK(_mbsncat(Cat, (const unsigned char*)"A", 2) == Cat && strcmp((char*)Cat, "A") == 0 && Cat[2] == 'X');
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

    /* The lead bytes of the locale object, not of the current locale */
    _locale_t Utf8 = _create_locale(LC_ALL, "C.UTF-8");
    CHECK(Utf8 != NULL && _isleadbyte_l(0xE4, Utf8) && _isctype_l(0xE4, _LEADBYTE, Utf8) && !isleadbyte(0xE4));
    _free_locale(Utf8);
}

typedef void(__cdecl* PVFV)(void);

extern "C" _onexit_t __cdecl __dllonexit(_onexit_t func, PVFV** pbegin, PVFV** pend);

static int __cdecl OnExitNothing(void)
{
    return 0;
}

static void TestDllOnExit(void)
{
    PVFV* Begin = (PVFV*)malloc(2 * sizeof(PVFV));
    PVFV* End = Begin;

    /* __dllonexit appends to the table of the caller (a DLL), which it grows */
    for (int i = 0; i < 40; i++)
    {
        CHECK(__dllonexit(OnExitNothing, &Begin, &End) == OnExitNothing);
    }
    CHECK(End - Begin == 40 && Begin[0] == (PVFV)OnExitNothing && Begin[39] == (PVFV)OnExitNothing);
    free(Begin);
}

/* The slot of RtlSetEnvironmentVar in the import table */
extern "C" NTSTATUS(NTAPI* __imp_RtlSetEnvironmentVar)(PVOID*, PCWSTR, SIZE_T, PCWSTR, SIZE_T);

static NTSTATUS NTAPI SetEnvironmentNoMemory(PVOID*, PCWSTR, SIZE_T, PCWSTR, SIZE_T)
{
    return STATUS_NO_MEMORY;
}

static void TestEnvironment(void)
{
    DWORD Protect;

    /* Names that _wgetenv finds equal are one variable */
    CHECK(_wputenv(L"NTCRT_\u00c4=one") == 0 && _wputenv(L"ntcrt_\u00e4=two") == 0);
    CHECK(_wgetenv(L"NTCRT_\u00c4") != NULL && wcscmp(_wgetenv(L"NTCRT_\u00c4"), L"two") == 0);
    CHECK(_wputenv(L"ntcrt_\u00e4=") == 0 && _wgetenv(L"NTCRT_\u00c4") == NULL);

    /* The process environment that children inherit fails: the variable is not set */
    if (VirtualProtect(&__imp_RtlSetEnvironmentVar, sizeof(PVOID), PAGE_READWRITE, &Protect))
    {
        NTSTATUS(NTAPI * Saved)(PVOID*, PCWSTR, SIZE_T, PCWSTR, SIZE_T) = __imp_RtlSetEnvironmentVar;

        __imp_RtlSetEnvironmentVar = SetEnvironmentNoMemory;
        errno = 0;
        CHECK(_putenv("NTCRT_NOMEM=1") == -1 && errno == ENOMEM && getenv("NTCRT_NOMEM") == NULL);
        __imp_RtlSetEnvironmentVar = Saved;
    }
}

static void TestProcessNames(void)
{
    const char* Args[] = { "x", "\xff", NULL };

    /* An invalid name or argument is EILSEQ, not ENOMEM */
    if (setlocale(LC_CTYPE, "C.UTF-8") != NULL)
    {
        errno = 0;
        CHECK(system("\xff") == -1 && errno == EILSEQ);
        errno = 0;
        CHECK(_spawnv(_P_WAIT, "\xff", Args) == -1 && errno == EILSEQ);
        errno = 0;
        CHECK(_spawnv(_P_WAIT, "x.exe", Args) == -1 && errno == EILSEQ);
        errno = 0;
        CHECK(_popen("\xff", "r") == NULL && errno == EILSEQ);
        setlocale(LC_CTYPE, "C");
    }
}

/* The functions of msvcrt as MSVC calls them, past the redirection of <stdio.h> to __mingw_* */
extern "C" int(__cdecl* __imp__snprintf)(char*, size_t, const char*, ...);
extern "C" int(__cdecl* __imp__snwprintf)(wchar_t*, size_t, const wchar_t*, ...);
extern "C" int(__cdecl* __imp_sscanf)(const char*, const char*, ...);
extern "C" int(__cdecl* __imp_swscanf)(const wchar_t*, const wchar_t*, ...);
extern "C" int(__cdecl* __imp_fscanf)(FILE*, const char*, ...);
extern "C" int __cdecl __mingw_snprintf(char*, size_t, const char*, ...);
extern "C" int __cdecl __mingw_sscanf(const char*, const char*, ...);
extern "C" int __cdecl __mingw_swscanf(const wchar_t*, const wchar_t*, ...);

/* %Lf is a double for the functions of msvcrt (the long double of MSVC), the 80-bit long double for __mingw_* */
static void TestLongDouble(void)
{
    static const char Name[] = "ntcrt_longdouble.tmp";
    struct
    {
        double Value[2];
        unsigned char Guard[8];
    } d;
    long double ld = 0;
    char Text[64];
    wchar_t WideText[64];
    FILE* File;

    CHECK(__imp__snprintf(Text, sizeof(Text), "%Lf %.1Le %Lg %La", 42.5, 1.5, 0.25, 1.0) > 0 &&
          strcmp(Text, "42.500000 1.5e+00 0.25 0x1p+0") == 0);
    CHECK(__imp__snwprintf(WideText, 64, L"%Lf", 42.5) > 0 && wcscmp(WideText, L"42.500000") == 0);
    memset(&d, 0xAA, sizeof(d));
    CHECK(__imp_sscanf("1.5 2.5", "%Lf %llf", &d.Value[0], &d.Value[1]) == 2 && d.Value[0] == 1.5 &&
          d.Value[1] == 2.5 && d.Guard[0] == 0xAA && d.Guard[1] == 0xAA);
    memset(&d, 0xAA, sizeof(d));
    CHECK(__imp_swscanf(L"0.25", L"%Lf", &d.Value[1]) == 1 && d.Value[1] == 0.25 && d.Guard[0] == 0xAA);
    File = fopen(Name, "w+");
    CHECK(File != NULL);
    if (File != NULL)
    {
        memset(&d, 0xAA, sizeof(d));
        CHECK(fputs("3.5", File) >= 0 && fseek(File, 0, SEEK_SET) == 0);
        CHECK(__imp_fscanf(File, "%Lf", &d.Value[1]) == 1 && d.Value[1] == 3.5 && d.Guard[0] == 0xAA);
        fclose(File);
        remove(Name);
    }

    CHECK(__mingw_snprintf(Text, sizeof(Text), "%Lf", 42.5L) > 0 && strcmp(Text, "42.500000") == 0);
    CHECK(__mingw_sscanf("1.5", "%Lf", &ld) == 1 && ld == 1.5L);
    ld = 0;
    CHECK(__mingw_swscanf(L"0.75", L"%Lf", &ld) == 1 && ld == 0.75L);
}

extern "C"
BOOL
TestMsvcrt(VOID)
{
    g_Ok = TRUE;
    TestLongDouble();
    TestNullArguments();
    TestConversions();
    TestMultibyte();
    TestMisc();
    TestTime();
    TestTmpnam();
    TestFiles();
    TestUtf8Pending();
    TestCtrlZ();
    TestBinaryNarrow();
    TestStatNull();
    TestDup2();
    TestUmask();
    TestGetcwd();
    TestStreams();
    TestMbcs();
    TestDllOnExit();
    TestEnvironment();
    TestProcessNames();
    return g_Ok;
}
