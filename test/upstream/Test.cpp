#include <KNSoft/NDK/NDK.h>

extern "C" BOOL TestGetStringTypeW(VOID);
extern "C" BOOL TestGetFileType(VOID);
extern "C" BOOL TestConsole(VOID);
extern "C" BOOL TestCodePage(VOID);
extern "C" BOOL TestConversion(VOID);
extern "C" BOOL TestSmoke(VOID);
extern "C" BOOL TestMsvcrt(VOID);

int
__cdecl
main()
{
    if (!TestGetStringTypeW())
    {
        return 1;
    }

    if (!TestGetFileType())
    {
        return 2;
    }

    if (!TestConsole())
    {
        return 3;
    }

    if (!TestCodePage())
    {
        return 4;
    }

    if (!TestConversion())
    {
        return 5;
    }

    if (!TestSmoke())
    {
        return 6;
    }

    if (!TestMsvcrt())
    {
        return 7;
    }

    return 0;
}
