/*
 * The narrow argv: each argument of the wide command line converted to the ANSI code page. No byte of a converted
 * character is a quote or a backslash for the parser: the trail byte 0x5C of a double-byte character (950, 932, 936)
 * and a character that the code page maps to a quote by best fit (U+FF02 in 1252). The program runs itself with
 * "a<character>" x for each.
 */

#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

/* A CJK character of the ANSI code page whose second byte is 0x5C, or 0 */
static wchar_t TrailBackslash(void)
{
    char Bytes[4];

    for (wchar_t c = 0x4E00; c <= 0x9FA5; c++)
    {
        if (WideCharToMultiByte(CP_ACP, 0, &c, 1, Bytes, sizeof(Bytes), NULL, NULL) == 2 && Bytes[1] == 0x5C)
        {
            return c;
        }
    }
    return 0;
}

static int Run(wchar_t c)
{
    wchar_t Code[8], Arg[5] = { L'"', L'a', c, L'"', 0 };
    intptr_t Status;

    /* _wspawnl joins the arguments with spaces, the quotes are those of the command line */
    swprintf(Code, 8, L"%04X", c);
    Status = _wspawnl(_P_WAIT, _wpgmptr, L"argv", L"child", Code, Arg, L"x", NULL);
    if (Status != 0)
    {
        printf("argv: the child returned %d for U+%04X in code page %u\n", (int)Status, c, GetACP());
        return 1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    int Failures = 0;
    wchar_t c;

    if (argc > 1 && strcmp(argv[1], "child") == 0)
    {
        wchar_t Wide[3] = { L'a', 0, 0 };
        char Expected[8];

        Wide[1] = argc > 2 ? (wchar_t)strtoul(argv[2], NULL, 16) : 0;
        WideCharToMultiByte(CP_ACP, 0, Wide, -1, Expected, sizeof(Expected), NULL, NULL);
        if (argc != 5 || strcmp(argv[3], Expected) != 0 || strcmp(argv[4], "x") != 0)
        {
            printf("argv: %d arguments, %s\n", argc, argc > 3 ? argv[3] : "");
            return 1;
        }
        return 0;
    }
    Failures += Run(0xFF02);
    c = TrailBackslash();
    if (c != 0)
    {
        Failures += Run(c);
    } else
    {
        printf("argv: no character with a trail byte 0x5C in code page %u, skipped\n", GetACP());
    }
    if (Failures == 0)
    {
        printf("argv: ok\n");
    }
    return Failures != 0;
}
