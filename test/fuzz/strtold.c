/*
 * Differential fuzz of strtold and wcstold against gdtoa of libmingwex (gdtoa.dll, see gdtoa.c), which NtCRT replaces:
 * decimal numbers of any length across the range of long double, the decimal and hex forms of random long doubles,
 * hex numbers halfway between two long doubles and a little off. gdtoa returns -0 when it converts nothing from a
 * string with a minus sign, NtCRT 0 (C); NtCRT sets ERANGE for an underflow to zero only, as UCRT.
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

typedef long double (*PSTRTOLD)(const char*, char**);
typedef long double (*PWCSTOLD)(const wchar_t*, wchar_t**);

static PSTRTOLD RefStrtold;
static PWCSTOLD RefWcstold;
static uint64_t g_Rng = 0x9E3779B97F4A7C15ull;
static unsigned long g_Iteration, g_Failures, g_Checks;

static uint32_t rnd(uint32_t n)
{
    g_Rng ^= g_Rng << 13;
    g_Rng ^= g_Rng >> 7;
    g_Rng ^= g_Rng << 17;
    return (uint32_t)(g_Rng >> 32) % n;
}

static uint64_t rnd64(void)
{
    return ((uint64_t)rnd(0x10000) << 48) ^ ((uint64_t)rnd(0x1000000) << 24) ^ rnd(0x1000000);
}

/* The 80 bits of a long double */
typedef struct
{
    uint64_t Mant;
    uint16_t SignExp;
} LDBL_BITS;

static LDBL_BITS bits_of(long double x)
{
    LDBL_BITS b;

    memcpy(&b, &x, 10);
    return b;
}

static long double from_bits(uint64_t Mant, int SignExp)
{
    long double x = 0;
    LDBL_BITS b = { Mant, (uint16_t)SignExp };

    memcpy(&x, &b, 10);
    return x;
}

/* A random finite long double, often a denormal or near the limits */
static long double gen_ldbl(void)
{
    int Exp;
    uint64_t Mant = rnd64() | 1ull << 63;

    switch (rnd(6))
    {
    case 0:
        return from_bits(rnd64() >> rnd(64) & ~(1ull << 63), rnd(2) << 15);
    case 1:
        Exp = 0x7FFE - (int)rnd(4);
        break;
    case 2:
        Exp = 1 + (int)rnd(4);
        break;
    case 3:
        Exp = 0x3FFF - 70 + (int)rnd(140);
        break;
    default:
        Exp = 1 + (int)rnd(0x7FFE);
        break;
    }
    return from_bits(Mant, Exp | (int)rnd(2) << 15);
}

static void gen_digits(char* s, int n)
{
    for (int i = 0; i < n; i++)
        s[i] = (char)('0' + rnd(10));
    s[n] = '\0';
}

static void gen_input(char* s)
{
    static const int Exps[] = { 4932, 4933, -4931, -4950, -4951, -4952, -4966, 0, 18, -18, 27, -27 };
    char* p = s;
    long double x;
    LDBL_BITS b;

    if (rnd(4) == 0)
        p += sprintf(p, "%s", rnd(2) ? "-" : "+");
    switch (rnd(7))
    {
    case 0:
    case 1:
    {
        /* Digits, a point and an exponent */
        int n = 1 + (int)rnd(rnd(8) == 0 ? 200 : 40), Point = (int)rnd((uint32_t)n + 1);
        int e = rnd(2) ? Exps[rnd(12)] - (int)rnd(40) : (int)rnd(10000) - 5000;

        if (rnd(4) == 0)
        {
            memset(p, '0', rnd(30));
            p += strlen(p);
        }
        gen_digits(p, n);
        if (rnd(3) != 0)
        {
            memmove(p + Point + 1, p + Point, (size_t)(n - Point) + 1);
            p[Point] = '.';
        }
        p += strlen(p);
        if (rnd(8) != 0)
            sprintf(p, "%c%d", rnd(2) ? 'e' : 'E', e);
        break;
    }
    case 2:
        /* A long double and a digit more or less, %Le is exact */
        x = gen_ldbl();
        sprintf(p, "%.*Le", 17 + (int)rnd(10), x);
        if (rnd(2))
        {
            char* e = strchr(p, 'e');
            e[-1] = (char)('0' + rnd(10));
        }
        break;
    case 3:
        x = gen_ldbl();
        if (rnd(2))
            sprintf(p, "%La", x);
        else
            sprintf(p, "%.*Le", 40 + (int)rnd(40), x);
        break;
    case 4:
    case 5:
    {
        /* Halfway between two long doubles, or off by a bit far below */
        int Exp;
        static const char* const Tails[] = { "8", "8", "8000000000000001", "7fffffffffffffff", "80000", "7" };

        x = gen_ldbl();
        b = bits_of(x);
        Exp = (b.SignExp & 0x7FFF) == 0 ? -16445 : (b.SignExp & 0x7FFF) - 16383 - 63;
        sprintf(p, "0x%llx.%sp%d", (unsigned long long)b.Mant, Tails[rnd(6)], Exp);
        break;
    }
    default:
    {
        static const char* const Words[] = { "inf", "INFINITY", "nan", "nan(1_a)", "  1.5x", "\t-0", "0x1p-16446",
                                             "0x1p-16445", "0x1.ffffffffffffffffp16383", "0x1.fffffffffffffffeffp16383",
                                             "1e5000", "1e-5000", "0x", ".e1", "0x.8p-16445", "-0.0e99999" };
        strcpy(p, Words[rnd(16)]);
        break;
    }
    }
}

/*
 * A NaN by its bits: gdtoa of MinGW-w64 15 on x86 clears the integer bit of a NaN with a payload, a pseudo-NaN that
 * isnan of the x87 does not take
 */
static int is_nan(LDBL_BITS b)
{
    return (b.SignExp & 0x7FFF) == 0x7FFF && (b.Mant << 1) != 0;
}

static int same(long double a, long double b)
{
    LDBL_BITS x = bits_of(a), y = bits_of(b);

    if (is_nan(x) && is_nan(y))
        return 1;
    return x.Mant == y.Mant && x.SignExp == y.SignExp;
}

static void fail(const char* What, const char* s, long double Got, long double Want, long GotEnd, long WantEnd)
{
    LDBL_BITS g = bits_of(Got), w = bits_of(Want);

    if (g_Failures++ >= 30)
        return;
    printf("#%lu %s(\"%.120s\") = %04x:%016llx end %ld, gdtoa %04x:%016llx end %ld\n", g_Iteration, What, s, g.SignExp,
           (unsigned long long)g.Mant, GotEnd, w.SignExp, (unsigned long long)w.Mant, WantEnd);
}

static void check(const char* s)
{
    static wchar_t w[1024];
    char *End, *RefEnd;
    wchar_t *WEnd, *RefWEnd;
    long double Got, Want;
    int Err, i;

    errno = 0;
    Got = strtold(s, &End);
    Err = errno;
    Want = RefStrtold(s, &RefEnd);
    if (RefEnd == s)
        Want = 0;
    g_Checks++;
    if (!same(Got, Want) || End != RefEnd)
        fail("strtold", s, Got, Want, (long)(End - s), (long)(RefEnd - s));
    /* ERANGE for infinity from a finite number, never for a nonzero finite number */
    if (isinf(Got) ? (Err == ERANGE) == (strpbrk(s, "iI") != NULL) : Got != 0 && Err != 0)
        fail("strtold errno", s, Got, (long double)Err, 0, 0);

    for (i = 0; s[i] != '\0'; i++)
        w[i] = (unsigned char)s[i];
    w[i] = 0;
    Got = wcstold(w, &WEnd);
    Want = RefWcstold(w, &RefWEnd);
    if (RefWEnd == w)
        Want = 0;
    g_Checks++;
    if (!same(Got, Want) || WEnd != RefWEnd)
        fail("wcstold", s, Got, Want, (long)(WEnd - w), (long)(RefWEnd - w));
}

int main(int argc, char** argv)
{
    unsigned long Iterations = argc > 1 ? strtoul(argv[1], NULL, 0) : 20000;
    HMODULE Ref = LoadLibraryW(argc > 2 ? NULL : L"gdtoa.dll");
    char s[1024];

    if (argc > 2)
    {
        wchar_t Path[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, Path, MAX_PATH);
        Ref = LoadLibraryW(Path);
    }
    if (Ref == NULL)
    {
        printf("gdtoa.dll is not found\n");
        return 2;
    }
    *(FARPROC*)&RefStrtold = GetProcAddress(Ref, "gdtoa_strtold");
    *(FARPROC*)&RefWcstold = GetProcAddress(Ref, "gdtoa_wcstold");
    for (g_Iteration = 0; g_Iteration < Iterations && g_Failures < 30; g_Iteration++)
    {
        gen_input(s);
        check(s);
    }
    printf("strtold: %lu iterations, %lu checks, %lu failures\n", g_Iteration, g_Checks, g_Failures);
    return g_Failures != 0;
}
