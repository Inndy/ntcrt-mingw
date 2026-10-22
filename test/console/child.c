/*
 * Console scenarios run by conpty.exe in a pseudo console, built with NtCRT. The console code pages are set to
 * 437, the "C.UTF-8" locale must not depend on them. What is read is printed in hex.
 *
 *   child.exe write | dbcs | open | u8write | read | ctrlz | u8read | u16read | getch | scanf
 */
#include <conio.h>
#include <fcntl.h>
#include <io.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

static void hex(const char* Tag, const void* p, size_t n)
{
    printf("%s:", Tag);
    for (size_t i = 0; i < n; i++)
        printf("%02x", ((const unsigned char*)p)[i]);
    printf("\n");
}

/* Wide results are printed as hex units, in the narrow mode of stdout */
static void hexw(const char* Tag, const wchar_t* w, size_t n)
{
    printf("%s:", Tag);
    for (size_t i = 0; i < n; i++)
        printf("%04x", w[i]);
    printf("\n");
}

int main(int argc, char** argv)
{
    const char* Scenario = argc > 1 ? argv[1] : "";

    SetConsoleOutputCP(437);
    SetConsoleCP(437);
    setlocale(LC_ALL, "C.UTF-8");
    if (strcmp(Scenario, "write") == 0)
    {
        printf("W1[a\xe4\xb8\xad\xc3\xa9]\n");
        fflush(stdout);
        /* A character split between writes */
        _write(1, "W2[\xf0\x9f", 5);
        _write(1, "\x98\x80]\n", 4);
        _cputs("W3[\xe4\xb8\xad\xf0\x9f\x98\x80]\r\n");
        fprintf(stderr, "W4[%ls]\n", L"\xe9\x4e2d\xd83d\xde00");
        _putwch(L'W');
        _putwch(L'5');
        _putwch(L'[');
        _putwch(0xD83D);
        _putwch(0xDE00);
        _cputs("]\r\nend\r\n");
    } else if (strcmp(Scenario, "dbcs") == 0)
    {
        /*
         * Code page 950, "C": conhost of Windows 10 and later reports fewer bytes than it took for a write of bytes
         * that are not characters of the code page (UTF-8 here). Binary mode must not write the rest again, text mode
         * must write the chunks after a short one (one _write of more than 1023 bytes).
         */
        static const char Text[] = "<\xe4\xb8\xad\xe6\x96\x87 \xc3\xa9 \xf0\x9f\x98\x80>";
        static char Long[1200];
        int n = 0;

        SetConsoleOutputCP(950);
        setlocale(LC_ALL, "C");
        _setmode(1, _O_BINARY);
        _write(1, "D1", 2);
        _write(1, Text, sizeof(Text) - 1);
        _write(1, "|\r\n", 3);
        _setmode(1, _O_TEXT);
        n += sprintf(Long + n, "D2%s|\n", Text);
        for (int i = 0; i < 20; i++)
            n += sprintf(Long + n, "%050d\n", i);
        n += sprintf(Long + n, "D3:end\n");
        _write(1, Long, n);
        printf("end\n");
    } else if (strcmp(Scenario, "open") == 0)
    {
        /* The console by name: kernel32 opens it before Windows 8, NtCreateFile after */
        char Line[16];
        int Fd = _open("CONOUT$", _O_WRONLY);
        FILE* f = fopen("con", "w");
        int n = 0;

        if (Fd < 0 || f == NULL)
        {
            printf("O:open failed %d %p\n", Fd, (void*)f);
            return 1;
        }
        _write(Fd, "O1\n", 3);
        _close(Fd);
        fputs("O2\n", f);
        fclose(f);
        Fd = _open("CONIN$", _O_RDONLY);
        if (Fd < 0)
        {
            printf("O:CONIN$ failed\n");
            return 1;
        }
        while (n < (int)sizeof(Line) && _read(Fd, Line + n, 1) == 1 && Line[n++] != '\n')
            ;
        _close(Fd);
        hex("O3", Line, n);
        printf("O4:%04x\n", _getwch());
        printf("end\n");
    } else if (strcmp(Scenario, "u8write") == 0)
    {
        _setmode(1, _O_U8TEXT);
        fputws(L"W6[\x4e2d\xe9]\n", stdout);
        /* A surrogate pair split between writes */
        fputws(L"W7[", stdout);
        fflush(stdout);
        fputwc(0xD83D, stdout);
        fflush(stdout);
        fputwc(0xDE00, stdout);
        fputws(L"]\nend\n", stdout);
    } else if (strcmp(Scenario, "read") == 0)
    {
        char Line[64];
        int c, n = 0;

        if (fgets(Line, sizeof(Line), stdin))
            hex("R1", Line, strlen(Line));
        while ((c = getchar()) != EOF && n < (int)sizeof(Line))
        {
            Line[n++] = (char)c;
            if (c == '\n')
                break;
        }
        hex("R2", Line, n);
        /* Ctrl+Z at the start of a line */
        printf("R3:%s\n", fgets(Line, sizeof(Line), stdin) == NULL && feof(stdin) ? "eof" : "data");
        printf("end\n");
    } else if (strcmp(Scenario, "ctrlz") == 0)
    {
        /*
         * Each way of reading gets "ab^Zcd", "ef", "^Z" and "gh" typed: Ctrl+Z in the middle of a line is kept and
         * the rest of the line dropped, at the start of a line it is the end of the input, once
         */
        char Line[32], Out[128];
        wchar_t Wide[32];

        for (int Mode = 0; Mode < 3; Mode++)
        {
            int Fd = Mode == 0 ? 0 : _dup(0);
            FILE* f = stdin;
            int n = sprintf(Out, "Z%d:", Mode);

            if (Mode != 0)
            {
                _setmode(Fd, Mode == 1 ? _O_U8TEXT : _O_U16TEXT);
                f = _wfdopen(Fd, L"r");
            }
            for (int i = 0; i < 3; i++)
            {
                if (i != 0)
                    Out[n++] = ',';
                if (Mode == 0 ? fgets(Line, sizeof(Line), f) == NULL : fgetws(Wide, 32, f) == NULL)
                {
                    n += sprintf(Out + n, "%s", feof(f) ? "eof" : "error");
                    clearerr(f);
                } else if (Mode == 0)
                {
                    for (size_t j = 0; Line[j] != '\0'; j++)
                        n += sprintf(Out + n, "%02x", (unsigned char)Line[j]);
                } else
                {
                    for (size_t j = 0; Wide[j] != L'\0'; j++)
                        n += sprintf(Out + n, "%04x", Wide[j]);
                }
            }
            /* After the echo of the input */
            printf("%s\n", Out);
            if (Mode != 0)
                fclose(f);
        }
        printf("end\n");
    } else if (strcmp(Scenario, "ctrlzraw") == 0)
    {
        /* Without ENABLE_PROCESSED_INPUT a line ends by CR alone: "ab^Zcd", "EF" and "gh" */
        char Buffer[32], Out[128];
        int n = 0, r;

        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
        n = sprintf(Out, "Z3:");
        for (int i = 0; i < 3; i++)
        {
            r = _read(0, Buffer, sizeof(Buffer));
            if (i != 0)
                Out[n++] = ',';
            for (int j = 0; j < r; j++)
                n += sprintf(Out + n, "%02x", (unsigned char)Buffer[j]);
        }
        printf("%s\n", Out);
        printf("end\n");
    } else if (strcmp(Scenario, "u8read") == 0 || strcmp(Scenario, "u16read") == 0)
    {
        wchar_t Line[64];
        int Fd = _dup(0);

        _setmode(Fd, Scenario[1] == '8' ? _O_U8TEXT : _O_U16TEXT);
        FILE* f = _wfdopen(Fd, L"r");
        if (fgetws(Line, 64, f))
            hexw("R4", Line, wcslen(Line));
        /* Read by _read in units smaller than the line */
        {
            wchar_t Buffer[64];
            int Units = 0, r;

            _setmode(0, Scenario[1] == '8' ? _O_U8TEXT : _O_U16TEXT);
            while (Units < 62 && (r = _read(0, Buffer + Units, 2 * sizeof(wchar_t))) > 0)
            {
                Units += r / (int)sizeof(wchar_t);
                if (Buffer[Units - 1] == L'\n')
                    break;
            }
            hexw("R5", Buffer, Units);
        }
        /* By one unit, the CR of CRLF ends a read */
        {
            wchar_t Buffer[8];
            int Units = 0;

            while (Units < 8 && _read(0, Buffer + Units, sizeof(wchar_t)) > 0 && Buffer[Units++] != L'\n')
                ;
            hexw("R8", Buffer, Units);
        }
        printf("end\n");
    } else if (strcmp(Scenario, "getch") == 0)
    {
        unsigned char Bytes[8];
        wchar_t Wide[2];

        for (int i = 0; i < 7; i++)
            Bytes[i] = (unsigned char)_getch();
        hex("R6", Bytes, 7);
        Wide[0] = _getwch();
        Wide[1] = _getwch();
        hexw("R7", Wide, 2);
        printf("end\n");
    } else if (strcmp(Scenario, "scanf") == 0)
    {
        wchar_t Wide[8] = { 0 };
        char Narrow[8] = { 0 };
        int i = 0, j = 0, r;

        /* stdin of the console, then the next line by _cscanf */
        r = scanf("%d %ls", &i, Wide);
        printf("S1:%d %d %04x\n", r, i, Wide[0]);
        r = _cscanf("%d %7s", &j, Narrow);
        printf("S2:%d %d %s\n", r, j, Narrow);
        printf("end\n");
    } else
    {
        printf("unknown scenario\n");
        return 2;
    }
    return 0;
}
