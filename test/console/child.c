/*
 * Console scenarios run by conpty.exe in a pseudo console, built with NtCRT. The console code pages are set to
 * 437, the "C.UTF-8" locale must not depend on them. What is read is printed in hex.
 *
 *   child.exe write | u8write | read | u8read | u16read | getch | scanf
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
