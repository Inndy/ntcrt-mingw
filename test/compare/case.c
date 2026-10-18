#include <stdio.h>
#include <ctype.h>
#include <wctype.h>
#include <locale.h>
#include <wchar.h>
#include <stdlib.h>
#include <mbctype.h>
#include <string.h>

static void ranges(void)
{
    int prev = -1, start = 0;

    for (int i = 0; i <= 256; i++)
    {
        int v = i < 256 ? __pctype_func()[i] : -2;

        if (v != prev)
        {
            if (prev != -1)
                printf(" _pctype %02x-%02x: %04x\n", start, i - 1, prev);
            prev = v;
            start = i;
        }
    }
}

static void run(const char* n)
{
    wchar_t a[] = L"a\xe9\x3b1Z\x130", b[8];
    char s[] = "\xc3\xa9Z", l[] = "A\xc3\x89";

    printf("== %s\n", n);
    ranges();
    printf(" tolower(c9)=%x towlower(c9)=%x towupper(3b1)=%x iswupper(c9)=%d iswalpha(e9)=%d iswspace(3000)=%d"
           " iswdigit(663)=%d iswpunct(3001)=%d\n",
           tolower(0xc9), towlower(0xc9), towupper(0x3b1), iswupper(0xc9), iswalpha(0xe9), iswspace(0x3000),
           iswdigit(0x663), iswpunct(0x3001));
    wcscpy(b, a);
    _wcsupr(b);
    printf(" _wcsupr=");
    for (int i = 0; b[i]; i++)
        printf("%04x ", b[i]);
    wcscpy(b, a);
    _wcslwr(b);
    printf(" _wcslwr=");
    for (int i = 0; b[i]; i++)
        printf("%04x ", b[i]);
    printf("\n _wcsicmp=%d _wcsnicmp=%d wcscoll=%d _wcsicoll=%d towupper(1c5)=%x towlower(130)=%x towupper(df)=%x"
           " towupper(ff)=%x\n",
           _wcsicmp(L"\xe9", L"\xc9"), _wcsnicmp(L"\x3b1", L"\x391", 1), wcscoll(L"\xe9", L"f"),
           _wcsicoll(L"\xe9", L"\xc9"), towupper(0x1c5), towlower(0x130), towupper(0xdf), towupper(0xff));
    _strupr(s);
    _strlwr(l);
    printf(" _strupr=%02x%02x%02x _strlwr=%02x%02x%02x _stricmp=%d strcoll=%d,%d _mbstrlen=%d\n", (unsigned char)s[0],
           (unsigned char)s[1], (unsigned char)s[2], (unsigned char)l[0], (unsigned char)l[1], (unsigned char)l[2],
           _stricmp("\xc3\xa9", "\xc3\x89"), strcoll("\xc3\xa9", "f"), strcoll("a", "B"),
           (int)_mbstrlen("a\xe4\xb8\xad\xf0\x9f\x98\x80"));
    printf(" _getmbcp=%d _ismbblead(e4)=%d\n", _getmbcp(), _ismbblead(0xe4));
}

int main()
{
    run("C");
    setlocale(LC_ALL, ".UTF-8");
    run("UTF-8");
    setlocale(LC_ALL, "C");
    setlocale(LC_CTYPE, ".UTF-8");
    run("LC_CTYPE=UTF-8");
    setlocale(LC_ALL, "C");
    printf("_setmbcp(65001)=%d _getmbcp=%d\n", _setmbcp(65001), _getmbcp());
    return 0;
}
