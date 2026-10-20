/*
 * printf of NtCRT (src/Printf.cpp) where test/fuzz/format.c cannot compare with UCRT: long double, positional
 * arguments, %Z, %n, %a without a precision, NaN signs, huge precisions and errors. The expected strings of
 * floating point numbers are exact decimal expansions, computed outside (Python Decimal).
 */

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

static int g_Failures;

static void check(const char* Want, int WantRet, const char* Format, ...)
{
    char Got[2048];
    va_list ap;
    int Ret;

    va_start(ap, Format);
    Ret = __mingw_vsnprintf(Got, sizeof(Got), Format, ap);
    va_end(ap);
    if (Ret != WantRet || (Want != NULL && strcmp(Got, Want) != 0))
    {
        printf("FAIL \"%s\": %d [%s], want %d [%s]\n", Format, Ret, Got, WantRet, Want != NULL ? Want : "");
        g_Failures++;
    }
}

#define CHECK(Want, ...) check(Want, (int)strlen(Want), __VA_ARGS__)

static void checkw(const wchar_t* Want, int Legacy, const wchar_t* Format, ...)
{
    wchar_t Got[256];
    va_list ap;
    int Ret;

    va_start(ap, Format);
    Ret = Legacy ? _vsnwprintf(Got, 256, Format, ap) : __mingw_vsnwprintf(Got, 256, Format, ap);
    va_end(ap);
    if (Ret != (int)wcslen(Want) || wcscmp(Got, Want) != 0)
    {
        printf("FAIL L\"%ls\" legacy %d: %d [%ls]\n", Format, Legacy, Ret, Got);
        g_Failures++;
    }
}

static long double make_ld(uint64_t Mant, uint16_t SignExp)
{
    long double x = 0;

    memcpy(&x, &Mant, 8);
    memcpy((char*)&x + 8, &SignExp, 2);
    return x;
}

typedef struct
{
    unsigned short Length;
    unsigned short MaximumLength;
    char* Buffer;
} ANSI_STRING_T;

typedef struct
{
    unsigned short Length;
    unsigned short MaximumLength;
    wchar_t* Buffer;
} UNICODE_STRING_T;

int main(void)
{
    char Buffer[64];
    char* Allocated;
    int n1 = -1, n2 = -1;
    signed char hh = -1;
    long long ll = -1;
    int Ret;

    /* Exact decimal expansions and ties to even */
    CHECK("1000000000000000052504760255204420248704468581108159154915854115511802457988908195786371375080447864043704"
          "443832883878176942523235360430575644792184786706982848387200926575803737830233794788090059368953234970799"
          "945081119038967640880074652742780142494579258788820056842838115669472196386865459400540160",
          "%.0f", 1e300);
    CHECK("0.000100000000000000004792173602385929598312941379845142364501953125", "%.*g", INT_MAX, 0.0001);
    CHECK("0.10000000000000001", "%.17g", 0.1);
    CHECK("4.94065645841246544177e-324", "%.20e", 5e-324);
    CHECK("2 0.2 2e+01 0.125 1.2e-01", "%.0f %.1f %.0e %.3f %.1e", 2.5, 0.25, 25.0, 0.125, 0.125);
    CHECK("1e+06 100000 0.0001 1e-05 1.00000", "%g %g %g %g %#g", 1e6, 1e5, 1e-4, 1e-5, 1.0);
    Ret = __mingw_snprintf(NULL, 0, "%.1074f", 5e-324);
    if (Ret != 1076)
    {
        printf("FAIL %%.1074f of 5e-324: %d\n", Ret);
        g_Failures++;
    }

    /* long double: 80 bits */
    CHECK("3.333333333333333333423683514374e-01", "%.30Le", make_ld(0xAAAAAAAAAAAAAAABull, 0x3FFD));
    CHECK("3.645e-4951", "%.3Le", make_ld(1, 0));
    CHECK("1.189731e+4932", "%Le", make_ld(~0ull, 0x7FFE));
    CHECK("0.667 1.5 -inf nan", "%.3Lg %Lg %Lf %Lf", 2.0L / 3, 1.5L, make_ld(1ull << 63, 0xFFFF),
          make_ld(3ull << 62, 0x7FFF));

    /* %a */
    CHECK("0x1p+0 0x1.8p+1 -0x1.cp+1 0x0p+0 0x1.000p+0 0x1.p+0", "%a %a %a %a %.3a %#.0a", 1.0, 3.0, -3.5, 0.0, 1.0,
          1.0);
    CHECK("0x0.0000000000001p-1022 0X1.999999999999AP-4 0x2p+0", "%a %A %.0a", 5e-324, 0.1, 1.5);
    CHECK("0x8p-3 0x1p+4 0x0.000000000000001p-16385", "%La %.0La %La", 1.0L, 15.5L, make_ld(1, 0));

    /* Signs of infinity and NaN, the 0 flag pads them with spaces */
    CHECK("-nan nan   INF +inf -inf", "%f %f %5F %+f %e", -NAN, NAN, INFINITY, INFINITY, -INFINITY);

    /* Positional arguments */
    CHECK("x-7-x", "%2$s-%1$d-%2$s", 7, "x");
    CHECK("    5|1.50", "%1$*2$d|%3$.*4$f", 5, 5, 1.5, 2);

    /* Windows: %Z, %wZ, I64, %S, %p */
    {
        ANSI_STRING_T a = { 3, 4, "abcd" };
        UNICODE_STRING_T u = { 4, 6, L"xyz" };

        CHECK("[abc] [xy] [  a] [(null)]", "[%Z] [%wZ] [%3.1Z] [%Z]", &a, &u, &a, (void*)NULL);
    }
    CHECK(sizeof(void*) == 8 ? "-5 4294967295 wide 0000000000001234" : "-5 4294967295 wide 00001234",
          "%I64d %I32u %S %p", -5ll, 0xFFFFFFFFu, L"wide", (void*)0x1234);
    checkw(L"wide|narrow", 1, L"%s|%S", L"wide", "narrow");
    checkw(L"narrow|wide", 0, L"%s|%S", "narrow", L"wide");

    /* %n by its length modifier, before and after the output */
    __mingw_snprintf(Buffer, sizeof(Buffer), "%nab%hhn%lln%n", &n1, &hh, &ll, &n2);
    if (n1 != 0 || hh != 2 || ll != 2 || n2 != 2)
    {
        printf("FAIL %%n: %d %d %lld %d\n", n1, hh, ll, n2);
        g_Failures++;
    }

    /* Not a conversion: copied */
    CHECK("%k %y 5 %", "%k %y %d %", 5);

    /* The length does not fit an int */
    errno = 0;
    Ret = __mingw_snprintf(NULL, 0, "%.*f", INT_MAX, 1.0);
    if (Ret != -1 || errno != EOVERFLOW)
    {
        printf("FAIL EOVERFLOW: %d errno %d\n", Ret, errno);
        g_Failures++;
    }

    /* Widths up to INT_MAX, the magnitude of INT_MIN and more do not fit */
    {
        static const char* const Formats[] = { "%2147483640d", "%*d", "%2147483648d", "%99999999999d" };
        static const int Want[] = { 2147483640, -1, -1, -1 };

        for (int i = 0; i < 4; i++)
        {
            errno = 0;
            Ret = i == 1 ? __mingw_snprintf(NULL, 0, Formats[i], INT_MIN, 1) : __mingw_snprintf(NULL, 0, Formats[i], 1);
            if (Ret != Want[i] || (Ret == -1 && errno != EOVERFLOW))
            {
                printf("FAIL width \"%s\": %d errno %d\n", Formats[i], Ret, errno);
                g_Failures++;
            }
        }
    }

    /* A precision ends an array without a null character: nothing after it is read (the page is not accessible) */
    setlocale(LC_ALL, "C.UTF-8");
    {
        char* Page = (char*)VirtualAlloc(NULL, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        DWORD Old;
        wchar_t* Wide = (wchar_t*)(Page + 0x1000) - 2;
        char* Narrow = Page + 0x1000 - 1;

        VirtualProtect(Page + 0x1000, 0x1000, PAGE_NOACCESS, &Old);
        Wide[0] = 0xD83D;
        Wide[1] = 0xDE00;
        CHECK("\xf0\x9f\x98\x80|", "%.4ls|%.0ls", Wide, Wide + 2);
        /* In the last byte of Wide */
        *Narrow = 'n';
        checkw(L"n|", 0, L"%.1s|%.0s", Narrow, Narrow + 1);
        checkw(L"n|", 1, L"%.1S|%.0S", Narrow, Narrow + 1);
        VirtualFree(Page, 0, MEM_RELEASE);
    }

    /* A character that does not convert fails the call */
    CHECK("\xe4\xb8\xad|\xf0\x9f\x98\x80", "%lc|%ls", (wint_t)0x4E2D, L"\xD83D\xDE00");
    errno = 0;
    check(NULL, -1, "%lc", (wint_t)0xD800);
    if (errno != EILSEQ)
    {
        printf("FAIL %%lc of a lone surrogate: errno %d\n", errno);
        g_Failures++;
    }
    setlocale(LC_ALL, "C");
    errno = 0;
    check(NULL, -1, "%ls", L"\x4E2D");
    if (errno != EILSEQ)
    {
        printf("FAIL %%ls of U+4E2D in \"C\": errno %d\n", errno);
        g_Failures++;
    }
    /* The secure functions keep the error, ERANGE is for an output that does not fit */
    errno = 0;
    Ret = _snprintf_s(Buffer, sizeof(Buffer), _TRUNCATE, "%ls", L"\x4E2D");
    if (Ret != -1 || errno != EILSEQ)
    {
        printf("FAIL _snprintf_s %%ls of U+4E2D in \"C\": %d errno %d\n", Ret, errno);
        g_Failures++;
    }
    errno = 0;
    Ret = sprintf_s(Buffer, sizeof(Buffer), "%ls", L"\x4E2D");
    if (Ret != -1 || errno != EILSEQ)
    {
        printf("FAIL sprintf_s %%ls of U+4E2D in \"C\": %d errno %d\n", Ret, errno);
        g_Failures++;
    }

    /* asprintf */
    Ret = __mingw_asprintf(&Allocated, "%s=%d", "a", 1);
    if (Ret != 3 || strcmp(Allocated, "a=1") != 0)
    {
        printf("FAIL asprintf: %d\n", Ret);
        g_Failures++;
    }
    free(Allocated);

    /* _snprintf of msvcrt: no terminator when the output fills the buffer, -1 when it does not fit */
    memset(Buffer, 'x', sizeof(Buffer));
    Ret = _snprintf(Buffer, 3, "%d", 123);
    if (Ret != 3 || memcmp(Buffer, "123x", 4) != 0 || _snprintf(Buffer, 2, "%d", 123) != -1)
    {
        printf("FAIL _snprintf: %d\n", Ret);
        g_Failures++;
    }

    /* _sprintf_p: a position used again takes a slot every time */
    {
        static char Format[4 * 400 + 1], Out[400 + 1];

        for (int i = 0; i < 400; i++)
        {
            memcpy(Format + 4 * i, "%1$d", 4);
        }
        Ret = _sprintf_p(Out, sizeof(Out), Format, 7);
        if (Ret != 400 || strspn(Out, "7") != 400)
        {
            printf("FAIL _sprintf_p of %%1$d 400 times: %d\n", Ret);
            g_Failures++;
        }
    }

    printf("printf: %d failures\n", g_Failures);
    return g_Failures != 0;
}
