#include <KNSoft/NDK/NDK.h>

#include <conio.h>

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

    return TRUE;
}
