/*
 * scanf of NtCRT (src/Scanf.inl) where test/fuzz/format.c cannot compare with UCRT: the examples of C11 7.21.6.2,
 * streams (pushback across calls, the end of the input, text mode), the wide functions of msvcrt and of C99, the _s
 * and _l functions, the conventions of msvcrt, the conversions of the locale and the floating point numbers that
 * would round twice. The expected values of floating point numbers are computed outside (Python Decimal).
 */

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <process.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int g_Failures;

#define EXPECT(Cond, ...) \
    do { \
        if (!(Cond)) \
        { \
            printf("FAIL line %d: ", __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
            g_Failures++; \
        } \
    } while (0)

static uint64_t bits(double d)
{
    uint64_t u;

    memcpy(&u, &d, 8);
    return u;
}

static uint32_t bitsf(float f)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    return u;
}

/* malloc and realloc of the library fail while set, the Makefile links with --wrap of them */
static int g_FailMalloc, g_FailRealloc;

void* __real__NtCRT_malloc(size_t Size);
void* __real__NtCRT_realloc(void* Block, size_t Size);

void* __wrap__NtCRT_malloc(size_t Size)
{
    return g_FailMalloc ? NULL : __real__NtCRT_malloc(Size);
}

void* __wrap__NtCRT_realloc(void* Block, size_t Size)
{
    return g_FailRealloc ? NULL : __real__NtCRT_realloc(Block, Size);
}

static char g_Path[512];

/* A file of Text (binary) reopened in Mode */
static FILE* file_of(const char* Text, const char* Mode)
{
    FILE* f = fopen(g_Path, "wb");

    fwrite(Text, 1, strlen(Text), f);
    fclose(f);
    return fopen(g_Path, Mode);
}

static void examples(void)
{
    int n, i, d1 = -1, d2 = -1, n1 = -1, n2 = -1, count;
    float x, quant;
    char name[50], units[21], item[21];
    FILE* f;

    /* EXAMPLE 1, the cast rounds the constant on x87 too (FLT_EVAL_METHOD 2) */
    n = sscanf("25 54.32E-1 thompson", "%d%f%s", &i, &x, name);
    EXPECT(n == 3 && i == 25 && x == (float)5.432f && strcmp(name, "thompson") == 0, "example 1: %d %d %g %s", n, i, x,
           name);

    /* EXAMPLE 2: the next character is a */
    f = file_of("56789 0123 56a72", "rb");
    n = fscanf(f, "%2d%f%*d %[0123456789]", &i, &x, name);
    EXPECT(n == 3 && i == 56 && x == 789.0f && strcmp(name, "56") == 0, "example 2: %d %d %g %s", n, i, x, name);
    EXPECT(fgetc(f) == 'a', "example 2: next character");
    fclose(f);

    /* EXAMPLE 3 */
    {
        static const struct
        {
            int Count;
            float Quant;
            const char* Units;
            const char* Item;
        } Want[] = { { 3, 2, "quarts", "oil" }, { 2, -12.8f, "degrees", NULL }, { 0, 0, NULL, NULL },
                     { 3, 10, "LBS", "dirt" }, { 0, 0, NULL, NULL }, { EOF, 0, NULL, NULL } };
        int k = 0;

        f = file_of("2 quarts of oil\n-12.8degrees Celsius\nlots of luck\n10.0LBS     of\ndirt\n100ergs of energy\n",
                    "r");
        do
        {
            count = fscanf(f, "%f%20s of %20s", &quant, units, item);
            EXPECT(k < 6 && count == Want[k].Count, "example 3: line %d count %d", k, count);
            if (k < 6 && count >= 1)
                EXPECT(quant == Want[k].Quant, "example 3: line %d quant %g", k, quant);
            if (k < 6 && count >= 2)
                EXPECT(strcmp(units, Want[k].Units) == 0, "example 3: line %d units %s", k, units);
            if (k < 6 && count >= 3)
                EXPECT(strcmp(item, Want[k].Item) == 0, "example 3: line %d item %s", k, item);
            fscanf(f, "%*[^\n]");
            k++;
        } while (!feof(f) && !ferror(f));
        EXPECT(k == 6, "example 3: %d lines", k);
        fclose(f);
    }

    /* EXAMPLE 4 */
    i = sscanf("123", "%d%n%n%d", &d1, &n1, &n2, &d2);
    EXPECT(i == 1 && d1 == 123 && n1 == 3 && n2 == 3 && d2 == -1, "example 4: %d %d %d %d %d", i, d1, n1, n2, d2);

    /* EXAMPLE 5 and 6 with UTF-8, which has no shift state: %s stores the bytes, %ls the wide characters */
    setlocale(LC_ALL, "C.UTF-8");
    {
        wchar_t w[8] = { 0 };

        n = sscanf("\xe4\xb8\xad\xe6\x96\x87 x", "%s", name);
        EXPECT(n == 1 && strcmp(name, "\xe4\xb8\xad\xe6\x96\x87") == 0, "example 5: %d", n);
        n = sscanf("\xe4\xb8\xad\xe6\x96\x87 x", "%ls", w);
        EXPECT(n == 1 && wcscmp(w, L"\x4e2d\x6587") == 0, "example 5 %%ls: %d %04x", n, w[0]);
        n = sscanf("\xe4\xb8\xad\xe6\x96\x87", "%2lc", w);
        EXPECT(n == 1 && w[0] == 0x4e2d && w[1] == 0x6587, "example 6 %%2lc: %d", n);
    }
    setlocale(LC_ALL, "C");
}

/* An item that is a prefix of a matching sequence only is a matching failure, its characters are consumed */
static void prefixes(void)
{
    static const struct
    {
        const char* Format;
        const char* Input;
        int Ret;
        const char* Rest;
    } Cases[] = {
        { "%f", "1e", 0, "" },          { "%f", "1e+x", 0, "x" },      { "%lf", "100ergs", 0, "rgs" },
        { "%x", "0x", 0, "" },          { "%x", "0xg", 0, "g" },       { "%i", "0x", 0, "" },
        { "%d", "-", 0, "" },           { "%u", "+x", 0, "x" },        { "%f", "nan(", 0, "" },
        { "%f", "nan(ab x", 0, " x" },  { "%f", "infin", 0, "" },      { "%f", "infix", 0, "x" },
        { "%f", "infx", 1, "x" },       { "%f", "0x1p", 0, "" },       { "%f", "0x.p1", 0, "p1" },
        { "%f", "-.x", 0, "x" },        { "%2f", "1e5", 0, "5" },      { "%2x", "0x1", 0, "1" },
        { "%1d", "-5", 0, "5" },        { "%5c", "ab", 0, "" },        { "%d", "", EOF, "" },
        { "%d", "   ", EOF, "" },       { "%d%d", "5 ", 1, "" },       { "%*d%d", "5", 0, "" },
        { "%n%d", "", 0, "" },          { "x%d", "x", EOF, "" },       { "abc", "ab", EOF, "" },
        { "abd%d", "abc", 0, "c" },     { "%[a]", "", EOF, "" },       { "%[a]", "b", 0, "b" },
        { "%c", "", EOF, "" },          { "%%%d", " %5", 1, "" },      { "%d %f", "5 1e", 1, "" },
        { "%i", "08", 1, "8" },         { "%3f", "inf", 1, "" },       { "%f", "0x1.8p1", 1, "" },
    };
    unsigned char v[3][32];

    for (size_t k = 0; k < sizeof(Cases) / sizeof(Cases[0]); k++)
    {
        FILE* f = file_of(Cases[k].Input, "rb");
        char Rest[32] = "";
        int Ret, c, n = 0;

        Ret = fscanf(f, Cases[k].Format, v[0], v[1], v[2]);
        while ((c = fgetc(f)) != EOF)
            Rest[n++] = (char)c;
        EXPECT(Ret == Cases[k].Ret && strcmp(Rest, Cases[k].Rest) == 0, "\"%s\" \"%s\": %d rest \"%s\"",
               Cases[k].Format, Cases[k].Input, Ret, Rest);
        fclose(f);
        Ret = sscanf(Cases[k].Input, Cases[k].Format, v[0], v[1], v[2]);
        EXPECT(Ret == Cases[k].Ret, "sscanf \"%s\" \"%s\": %d", Cases[k].Format, Cases[k].Input, Ret);
    }
}

static void streams(void)
{
    char s[16];
    int i = 0, j = 0, Ret;
    char c = 0;
    FILE* f;

    /* Pushback across calls */
    f = file_of("12x 34", "rb");
    Ret = fscanf(f, "%d", &i);
    EXPECT(Ret == 1 && i == 12 && fgetc(f) == 'x', "pushback: %d %d", Ret, i);
    Ret = fscanf(f, "%d", &i);
    EXPECT(Ret == 1 && i == 34, "after pushback: %d %d", Ret, i);
    Ret = fscanf(f, "%d", &i);
    EXPECT(Ret == EOF && feof(f), "at the end: %d", Ret);
    fclose(f);

    /* A matching failure leaves the input to the next call */
    f = file_of("abc 7", "rb");
    Ret = fscanf(f, "%d", &i);
    EXPECT(Ret == 0, "matching failure: %d", Ret);
    Ret = fscanf(f, "%s%d", s, &i);
    EXPECT(Ret == 2 && strcmp(s, "abc") == 0 && i == 7, "after a matching failure: %d %s %d", Ret, s, i);
    fclose(f);

    /* The characters of an item that fails are consumed, one is pushed back */
    f = file_of("1e+x", "rb");
    Ret = fscanf(f, "%f", (float*)&i);
    EXPECT(Ret == 0 && fgetc(f) == 'x' && fgetc(f) == EOF, "1e+x: %d", Ret);
    fclose(f);

    /* ungetc after the pushback of scanf at the start of the buffer: the rest moves up, a full buffer has no room */
    f = file_of("hello", "rb");
    Ret = fscanf(f, "%[x]", s);
    EXPECT(Ret == 0 && ungetc('x', f) == 'x' && fgetc(f) == 'x' && fgetc(f) == 'h' && ftell(f) == 1,
           "ungetc after a pushback: %d", Ret);
    fclose(f);
    f = file_of("hello", "rb");
    setvbuf(f, NULL, _IOFBF, 5);
    Ret = fscanf(f, "%[x]", s);
    EXPECT(Ret == 0 && ungetc('x', f) == EOF && fgetc(f) == 'h', "ungetc in a full buffer: %d", Ret);
    fclose(f);

    /* Text mode: CRLF is LF */
    f = file_of("1\r\n2\r\nend", "r");
    Ret = fscanf(f, "%d%c%d", &i, &c, &j);
    EXPECT(Ret == 3 && i == 1 && c == '\n' && j == 2, "text mode: %d %d %d %d", Ret, i, c, j);
    Ret = fscanf(f, " %3c", s);
    EXPECT(Ret == 1 && memcmp(s, "end", 3) == 0, "text mode %%3c: %d", Ret);
    fclose(f);

    /* An end of input is not read again: %d%d stops at the end of the first number */
    f = file_of("5", "rb");
    Ret = fscanf(f, "%d%d", &i, &j);
    EXPECT(Ret == 1 && i == 5, "end: %d", Ret);
    fclose(f);

    /* Wide streams: the UTF-8 of a narrow stream in "C.UTF-8", the high surrogate pushed back */
    setlocale(LC_ALL, "C.UTF-8");
    {
        wchar_t w[8] = { 0 };

        f = file_of("5\xf0\x9f\x98\x80 \xe4\xb8\xad!", "r");
        Ret = fwscanf(f, L"%d%2lc %l[^!]", &i, w, w + 2);
        EXPECT(Ret == 3 && i == 5 && w[0] == 0xd83d && w[1] == 0xde00 && wcscmp(w + 2, L"\x4e2d") == 0,
               "fwscanf UTF-8: %d %04x %04x %04x", Ret, w[0], w[1], w[2]);
        EXPECT(fgetwc(f) == L'!', "fwscanf UTF-8: next character");
        fclose(f);
    }
    setlocale(LC_ALL, "C");
}

static void wide(void)
{
    wchar_t w[16];
    char s[16];
    int Ret, i;

    /* msvcrt: %s, %c and %[ wide, %S, %C, %hs narrow */
    memset(w, 0, sizeof(w));
    memset(s, 0, sizeof(s));
    Ret = __ms_swscanf(L"abc def g", L"%s %S %hc", w, s, s + 8);
    EXPECT(Ret == 3 && wcscmp(w, L"abc") == 0 && strcmp(s, "def") == 0 && s[8] == 'g', "msvcrt swscanf: %d", Ret);
    memset(w, 0, sizeof(w));
    Ret = __ms_swscanf(L"xyz", L"%[x-y]%c", w, w + 8);
    EXPECT(Ret == 2 && wcscmp(w, L"xy") == 0 && w[8] == L'z', "msvcrt swscanf %%[: %d", Ret);

    /* C99: %s, %c and %[ narrow, %ls, %lc, %S wide */
    memset(w, 0, sizeof(w));
    memset(s, 0, sizeof(s));
    Ret = swscanf(L"abc def g", L"%s %ls %c", s, w, s + 8);
    EXPECT(Ret == 3 && strcmp(s, "abc") == 0 && wcscmp(w, L"def") == 0 && s[8] == 'g', "C99 swscanf: %d", Ret);
    memset(w, 0, sizeof(w));
    Ret = swscanf(L"uvw", L"%S", w);
    EXPECT(Ret == 1 && wcscmp(w, L"uvw") == 0, "C99 swscanf %%S: %d", Ret);

    /* The width counts wchar_t: a surrogate pair is two */
    memset(w, 0, sizeof(w));
    Ret = __ms_swscanf(L"\xd83d\xde00z", L"%1s", w);
    EXPECT(Ret == 1 && w[0] == 0xd83d && w[1] == 0, "wide width: %d", Ret);

    /* EOF of an empty string, msvcrt returns 0 */
    Ret = __ms_swscanf(L" \t\n", L"%s", w);
    EXPECT(Ret == EOF, "wide EOF: %d", Ret);
    Ret = __ms_swscanf(L"", L"%d", &i);
    EXPECT(Ret == EOF, "wide EOF %%d: %d", Ret);

    /* Numbers of the wide functions take ASCII digits only, as wcstol */
    Ret = __ms_swscanf(L"\x661", L"%d", &i);
    EXPECT(Ret == 0, "wide Arabic-Indic digit: %d", Ret);
    {
        double d = 0;

        Ret = swscanf(L"0x1.8p1", L"%lf", &d);
        EXPECT(Ret == 1 && d == 3.0, "wide hex float: %d %g", Ret, d);
    }
}

static void secure(void)
{
    char s[8];
    wchar_t w[4];
    int Ret, i = -1;

    /* A string that does not fit fails, the array is emptied, ENOMEM */
    memset(s, 'x', sizeof(s));
    errno = 0;
    Ret = sscanf_s("abcdef 5", "%s %d", s, 3, &i);
    EXPECT(Ret == 0 && s[0] == '\0' && errno == ENOMEM && i == -1, "sscanf_s small: %d %d", Ret, errno);
    Ret = sscanf_s("ab 5", "%s %d", s, 3, &i);
    EXPECT(Ret == 2 && strcmp(s, "ab") == 0 && i == 5, "sscanf_s fits: %d", Ret);
    memset(s, 'x', sizeof(s));
    Ret = sscanf_s("123", "%3c", s, 2);
    EXPECT(Ret == 0 && s[0] == '\0' && s[1] == '2' && s[2] == 'x', "sscanf_s %%3c: %d", Ret);
    memset(s, 'x', sizeof(s));
    Ret = sscanf_s("123", "%3c", s, 3);
    EXPECT(Ret == 1 && memcmp(s, "123x", 4) == 0, "sscanf_s %%3c fits: %d", Ret);
    memset(s, 'x', sizeof(s));
    Ret = sscanf_s("abc", "%s", s, 0);
    EXPECT(Ret == 0 && s[0] == 'x', "sscanf_s size 0: %d", Ret);
    Ret = sscanf_s("abc", "%[a-b]%n", s, 3, &i);
    EXPECT(Ret == 1 && strcmp(s, "ab") == 0 && i == 2, "sscanf_s %%[ %%n: %d %d", Ret, i);
    Ret = sscanf_s("12 abc", "%*d %*s%n", &i);
    EXPECT(Ret == 0 && i == 6, "sscanf_s suppressed: %d %d", Ret, i);

    /* A null array stops with EINVAL as UCRT (msvcrt crashes), what is before is assigned */
    memset(s, 'x', sizeof(s));
    errno = 0;
    Ret = sscanf_s("5 a b", "%d %c %s", &i, (char*)NULL, 1, s, 8);
    EXPECT(Ret == 1 && i == 5 && errno == EINVAL && s[0] == 'x', "sscanf_s null %%c: %d %d", Ret, errno);
    errno = 0;
    Ret = sscanf_s("ab", "%s", (char*)NULL, 8);
    EXPECT(Ret == 0 && errno == EINVAL, "sscanf_s null %%s: %d %d", Ret, errno);
    errno = 0;
    Ret = swscanf_s(L"ab", L"%[ab]", (wchar_t*)NULL, 8);
    EXPECT(Ret == 0 && errno == EINVAL, "swscanf_s null %%[: %d %d", Ret, errno);

    /* Wide: the size counts wchar_t, of a narrow array bytes */
    Ret = swscanf_s(L"abcd", L"%s", w, 4);
    EXPECT(Ret == 0 && w[0] == 0, "swscanf_s small: %d", Ret);
    Ret = swscanf_s(L"abc", L"%s", w, 4);
    EXPECT(Ret == 1 && wcscmp(w, L"abc") == 0, "swscanf_s fits: %d", Ret);
    Ret = swscanf_s(L"a1", L"%c", w, 1);
    EXPECT(Ret == 1 && w[0] == L'a', "swscanf_s %%c: %d", Ret);
    Ret = swscanf_s(L"hello", L"%S", s, 5);
    EXPECT(Ret == 0 && s[0] == '\0', "swscanf_s %%S small: %d", Ret);

    /* _snscanf: the length limits the input */
    Ret = _snscanf("12345", 3, "%d", &i);
    EXPECT(Ret == 1 && i == 123, "_snscanf: %d %d", Ret, i);
    Ret = _snscanf("12345", 0, "%d", &i);
    EXPECT(Ret == EOF, "_snscanf 0: %d", Ret);
    Ret = _snscanf_s("abcdef", 4, "%s", s, 8);
    EXPECT(Ret == 1 && strcmp(s, "abcd") == 0, "_snscanf_s: %d", Ret);
    Ret = _snwscanf(L"12345", 2, L"%d", &i);
    EXPECT(Ret == 1 && i == 12, "_snwscanf: %d %d", Ret, i);
}

static void conventions(void)
{
    void* p = NULL;
    long long ll = 0;
    int i = 0, j = 0, Ret;
    short h = 0;
    char s[16], *m = NULL, *mc = NULL;
    wchar_t* wm = NULL;

    /* %4I64d, I32, I */
    Ret = sscanf("123456", "%4I64d", &ll);
    EXPECT(Ret == 1 && ll == 1234, "%%4I64d: %d %lld", Ret, ll);
    Ret = sscanf("-5 7 8", "%I64d %I32d %Id", &ll, &i, (long long*)&p);
    EXPECT(Ret == 3 && ll == -5 && i == 7 && p == (void*)8, "I64 I32 I: %d", Ret);

    /* %p as printf writes it, a 0x prefix, %P */
    Ret = sscanf("000000000046F170", "%p", &p);
    EXPECT(Ret == 1 && p == (void*)0x46F170, "%%p: %d %p", Ret, p);
    Ret = sscanf("0x46F173", "%p", &p);
    EXPECT(Ret == 1 && p == (void*)0x46F173, "%%p 0x: %d %p", Ret, p);
    Ret = __ms_sscanf("1234", "%P", &p);
    EXPECT(Ret == 1 && p == (void*)0x1234, "%%P: %d %p", Ret, p);

    /* The width and the length modifiers in any order as msvcrt, digits accumulate */
    i = (int)0xdeadbeef;
    Ret = __ms_sscanf("12345678", "%h02d", &i);
    EXPECT(Ret == 1 && i == (int)0xdead000c, "%%h02d: %d %x", Ret, i);
    i = (int)0xdeadbeef;
    Ret = __ms_sscanf("12345678", "%2h0d", &i);
    EXPECT(Ret == 1 && i == (int)0xdead614e, "%%2h0d: %d %x", Ret, i);

    /* An unknown conversion character matches itself */
    Ret = __ms_sscanf("\"%12@", "%\"%%%d%@", &i);
    EXPECT(Ret == 1 && i == 12, "%%\": %d %d", Ret, i);

    /* Positional arguments */
    Ret = sscanf("1 2", "%2$d %1$d", &i, &j);
    EXPECT(Ret == 2 && i == 2 && j == 1, "%%n$: %d %d %d", Ret, i, j);

    /* Integers: strtoll and strtoull, stored in the size of the type */
    Ret = sscanf("99999999999 -1 70000 300", "%d %u %hd %hhd", &i, &j, &h, s);
    EXPECT(Ret == 4 && i == (int)99999999999LL && j == -1 && h == (short)70000 && s[0] == (char)300,
           "wraparound: %d %d %d %d", Ret, i, h, s[0]);
    errno = 0;
    Ret = sscanf("99999999999999999999", "%lld", &ll);
    EXPECT(Ret == 1 && ll == LLONG_MAX && errno == ERANGE, "%%lld overflow: %d %lld", Ret, ll);
    errno = 0;
    Ret = sscanf("-99999999999999999999", "%d", &i);
    EXPECT(Ret == 1 && i == 0 && errno == ERANGE, "%%d of LLONG_MIN: %d %d", Ret, i);
    Ret = sscanf("-0x10 017 +0x1f", "%i %i %hx", &i, &j, &h);
    EXPECT(Ret == 3 && i == -16 && j == 15 && h == 0x1f, "%%i %%x: %d %d %d %d", Ret, i, j, h);

    /* Scan sets: ] first, ^, a forward range, a reversed or edge - is a member, a range after a range */
    Ret = sscanf("]a-b", "%[]a-]", s);
    EXPECT(Ret == 1 && strcmp(s, "]a-") == 0, "%%[]a-]: %d %s", Ret, s);
    Ret = sscanf("b-ca", "%[c-a]", s);
    EXPECT(Ret == 0, "%%[c-a]: %d", Ret);
    Ret = sscanf("c-a b", "%[c-a]", s);
    EXPECT(Ret == 1 && strcmp(s, "c-a") == 0, "%%[c-a] members: %d %s", Ret, s);
    Ret = sscanf("-412", "%[1-2-4]", s);
    EXPECT(Ret == 0, "%%[1-2-4]: %d", Ret);
    Ret = sscanf("4123-", "%[1-2-4]", s);
    EXPECT(Ret == 1 && strcmp(s, "4123") == 0, "%%[1-2-4] 1 to 4: %d %s", Ret, s);
    Ret = sscanf("ab]", "%[^]]", s);
    EXPECT(Ret == 1 && strcmp(s, "ab") == 0, "%%[^]]: %d %s", Ret, s);
    Ret = sscanf("-./0a", "%[--0]", s);
    EXPECT(Ret == 1 && strcmp(s, "-./0") == 0, "%%[--0]: %d %s", Ret, s);

    /* %c, %[ and %n do not skip white space, %% and the others do */
    Ret = sscanf(" a", "%c", s);
    EXPECT(Ret == 1 && s[0] == ' ', "%%c white space: %d", Ret);
    Ret = sscanf("1 %", "%d%%", &i);
    EXPECT(Ret == 1, "%%%% white space: %d", Ret);

    /* m: an allocated string */
    Ret = sscanf("hello world", "%ms %4mc", &m, &mc);
    EXPECT(Ret == 2 && m != NULL && strcmp(m, "hello") == 0 && memcmp(mc, "worl", 4) == 0, "%%m: %d", Ret);
    free(m);
    free(mc);
    m = NULL;
    Ret = sscanf("12", "%ms", &m);
    EXPECT(Ret == 1 && strcmp(m, "12") == 0, "%%ms: %d", Ret);
    free(m);
    {
        char Long[200];

        memset(Long, 'q', 199);
        Long[199] = '\0';
        Ret = sscanf(Long, "%mls", &wm);
        EXPECT(Ret == 1 && wcslen(wm) == 199 && wm[198] == L'q', "%%mls grown: %d", Ret);
        free(wm);

        /* No memory is an error: EOF before the first conversion (POSIX), ENOMEM, the pointer is not stored */
        m = NULL;
        g_FailMalloc = 1;
        errno = 0;
        Ret = sscanf("abc", "%ms", &m);
        EXPECT(Ret == EOF && errno == ENOMEM && m == NULL, "%%ms no memory: %d %d", Ret, errno);
        Ret = sscanf("5 abc", "%d %ms", &i, &m);
        EXPECT(Ret == 1 && i == 5 && m == NULL, "%%ms no memory after %%d: %d", Ret);
        g_FailMalloc = 0;
        g_FailRealloc = 1;
        errno = 0;
        Ret = sscanf(Long, "%ms", &m);
        EXPECT(Ret == EOF && errno == ENOMEM && m == NULL, "%%ms grown, no memory: %d %d", Ret, errno);
        Ret = sscanf("abc", "%ms", &m);
        EXPECT(Ret == 1 && m != NULL && strcmp(m, "abc") == 0, "%%ms short, no realloc: %d", Ret);
        g_FailRealloc = 0;
        free(m);
    }
}

static void floats(void)
{
    double d = 0, d2 = 0, d3 = 0;
    float f = 0;
    long double ld = 0;
    uint64_t Mant = 0;
    uint16_t SignExp = 0;
    int Ret;

    /* 1 + 2**-53 + 2**-80: 1 + 2**-52 as a double, 1 when it is rounded to a long double first */
    Ret = sscanf("1.00000000000000011102230328969626659539084168049072331996285356581211090087890625", "%lf", &d);
    EXPECT(Ret == 1 && bits(d) == 0x3FF0000000000001ull, "double rounding %%lf: %016llx",
           (unsigned long long)bits(d));
    Ret = swscanf(L"1.00000000000000011102230328969626659539084168049072331996285356581211090087890625", L"%lf", &d);
    EXPECT(Ret == 1 && bits(d) == 0x3FF0000000000001ull, "double rounding wide %%lf: %016llx",
           (unsigned long long)bits(d));
    /* 1 + 2**-24 + 2**-60: 1 + 2**-23 as a float, 1 when it is rounded to a double first */
    Ret = sscanf("1.000000059604644776257986737988403547205962240695953369140625", "%f", &f);
    EXPECT(Ret == 1 && bitsf(f) == 0x3F800001u, "double rounding %%f: %08x", bitsf(f));
    /* 1 + 2**-64 + 2**-100: 1 + 2**-63 as a long double */
    Ret = sscanf("1.000000000000000000054210108625064082605593651848908814136571848729673206435109023004770278930664"
                 "0625",
                 "%Lf", &ld);
    memcpy(&Mant, &ld, 8);
    memcpy(&SignExp, (char*)&ld + 8, 2);
    EXPECT(Ret == 1 && Mant == 0x8000000000000001ull && SignExp == 0x3FFF, "%%Lf: %016llx %04x",
           (unsigned long long)Mant, SignExp);

    /* Hex floats, inf, nan */
    Ret = sscanf("0x1.8p1 -0X.8P-1 0x1p-1074", "%lf %f %lf", &d, &f, &d2);
    EXPECT(Ret == 3 && d == 3.0 && f == -0.25f && d2 == (double)5e-324, "hex: %d %g %g %g", Ret, d, f, d2);
    Ret = sscanf("inf -INFINITY nan(abc_1) -NaN", "%lf %f %lf %lf", &d, &f, &d2, &d3);
    EXPECT(Ret == 4 && isinf(d) && d > 0 && isinf(f) && f < 0 && isnan(d2) && isnan(d3) && signbit(d3),
           "inf nan: %d", Ret);
    errno = 0;
    Ret = sscanf("1e400 1e-400", "%lf %lf", &d, &d2);
    EXPECT(Ret == 2 && isinf(d) && d2 == 0 && errno == ERANGE, "range: %d %d", Ret, errno);

    /* Many digits */
    {
        char Long[2100] = "0.";

        memset(Long + 2, '0', 2000);
        strcpy(Long + 2002, "1e2001");
        Ret = sscanf(Long, "%lf", &d);
        EXPECT(Ret == 1 && d == 1.0, "2000 zeros: %d %g", Ret, d);
    }

    /* The digits move the point by more than the exponent saturates at: 0x1p0 */
    {
        size_t Zeros = 67108865;
        char* Long = malloc(Zeros + 16);
        char* End;

        strcpy(Long, "0x1");
        memset(Long + 3, '0', Zeros);
        strcpy(Long + 3 + Zeros, "p-268435460");
        errno = 0;
        d = strtod(Long, &End);
        f = strtof(Long, NULL);
        ld = strtold(Long, NULL);
        EXPECT(d == 1.0 && f == 1.0f && ld == 1.0L && *End == '\0' && errno == 0,
               "0x1, %zu zeros, p-268435460: %g %g %g", Zeros, d, (double)f, (double)ld);
        free(Long);
    }
}

static void locales(void)
{
    wchar_t w[16];
    char s[16];
    int Ret, n;

    /* "C": a byte is the character of the same code point */
    memset(w, 0, sizeof(w));
    Ret = sscanf("\xe9\xff", "%2lc", w);
    EXPECT(Ret == 1 && w[0] == 0xe9 && w[1] == 0xff, "C %%lc: %d %04x %04x", Ret, w[0], w[1]);
    errno = 0;
    Ret = swscanf(L"\x4e2d", L"%s", s);
    EXPECT(Ret == 0 && errno == EILSEQ, "C narrow of U+4E2D: %d %d", Ret, errno);

    setlocale(LC_ALL, "C.UTF-8");
    /* %ls and %lc: a multibyte character is one, above U+FFFF two wchar_t */
    memset(w, 0, sizeof(w));
    Ret = sscanf("a\xe4\xb8\xad\xf0\x9f\x98\x80 z", "%ls", w);
    EXPECT(Ret == 1 && wcscmp(w, L"a\x4e2d\xd83d\xde00") == 0, "UTF-8 %%ls: %d", Ret);
    memset(w, 0, sizeof(w));
    Ret = sscanf("\xf0\x9f\x98\x80x", "%lc%n", w, &n);
    EXPECT(Ret == 1 && w[0] == 0xd83d && w[1] == 0xde00 && n == 4, "UTF-8 %%lc pair: %d %d", Ret, n);
    memset(w, 0, sizeof(w));
    Ret = sscanf("a\xe4\xb8\xad\xf0\x9f\x98\x80", "%2ls%n", w, &n);
    EXPECT(Ret == 1 && wcscmp(w, L"a\x4e2d") == 0 && n == 4, "UTF-8 %%2ls: %d %d", Ret, n);
    errno = 0;
    Ret = sscanf("\xc3x", "%ls", w);
    EXPECT(Ret == 0 && errno == EILSEQ, "UTF-8 invalid: %d %d", Ret, errno);
    errno = 0;
    Ret = sscanf("\xe4\xb8", "%ls", w);
    EXPECT(Ret == 0 && errno == EILSEQ, "UTF-8 truncated: %d %d", Ret, errno);

    /* The narrow strings of the wide functions */
    memset(s, 0, sizeof(s));
    Ret = swscanf(L"a\x4e2d\xd83d\xde00 z", L"%s", s);
    EXPECT(Ret == 1 && strcmp(s, "a\xe4\xb8\xad\xf0\x9f\x98\x80") == 0, "UTF-8 wide to narrow: %d", Ret);
    memset(s, 0, sizeof(s));
    Ret = __ms_swscanf(L"\xd83d\xde00", L"%2hc", s);
    EXPECT(Ret == 1 && strcmp(s, "\xf0\x9f\x98\x80") == 0, "UTF-8 %%2hc pair: %d", Ret);
    errno = 0;
    Ret = __ms_swscanf(L"\xd83d\xde00", L"%hc", s);
    EXPECT(Ret == 0 && errno == EILSEQ, "UTF-8 %%hc half a pair: %d %d", Ret, errno);
    errno = 0;
    Ret = __ms_swscanf(L"\xde00", L"%hs", s);
    EXPECT(Ret == 0 && errno == EILSEQ, "UTF-8 lone low surrogate: %d %d", Ret, errno);
    setlocale(LC_ALL, "C");

    /* _l: the code page of the locale object */
    {
        _locale_t Utf8 = _create_locale(LC_ALL, "C.UTF-8");

        memset(w, 0, sizeof(w));
        Ret = _sscanf_l("\xe4\xb8\xad", "%ls", Utf8, w);
        EXPECT(Ret == 1 && w[0] == 0x4e2d && w[1] == 0, "_sscanf_l: %d %04x", Ret, w[0]);
        memset(w, 0, sizeof(w));
        Ret = _snscanf_l("\xe4\xb8\xad\xe6\x96\x87", 3, "%ls", Utf8, w);
        EXPECT(Ret == 1 && w[0] == 0x4e2d && w[1] == 0, "_snscanf_l: %d %04x", Ret, w[0]);
        _free_locale(Utf8);
    }
}

int main(void)
{
    const char* Temp = getenv("TEMP");

    snprintf(g_Path, sizeof(g_Path), "%s\\ntcrt_scanf_%lu.tmp", Temp != NULL ? Temp : ".",
             (unsigned long)_getpid());
    examples();
    prefixes();
    streams();
    wide();
    secure();
    conventions();
    floats();
    locales();
    remove(g_Path);
    printf("scanf: %d failures\n", g_Failures);
    return g_Failures != 0;
}
