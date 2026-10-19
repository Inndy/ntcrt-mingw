#include <KNSoft/NDK/NDK.h>

#include <conio.h>
#include <stdio.h>

extern "C"
BOOL
TestSmoke(VOID)
{
    char* p;

    p = new char[100];
    DbgPrint("%p\n", p);
    delete[] p;
    _putwch(L'a');
    _putwch(L'\n');
    _cputws(L"xxxxxxxx\nxxx\n");

    /* Formatted once: %n changes the string after it is printed */
    {
        int Narrow[2] = { 0x44434241, 0 }, Wide[2] = { 0x00420041, 0 };

        _cprintf("%s%n\n", (char*)Narrow, &Narrow[0]);
        _cwprintf(L"%s%n\n", (wchar_t*)Wide, &Wide[0]);
        if (Narrow[0] != 4 || Wide[0] != 2)
        {
            printf("FAIL _cprintf %%n: %d %d\n", Narrow[0], Wide[0]);
            return FALSE;
        }
    }

    return TRUE;
}
