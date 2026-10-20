/*
 * Differential fuzz of formatting and number parsing: the printf and scanf families, strtod and the strtol family
 * of NtCRT against ucrtbase.dll of the system. The arguments are built as the 8-byte slots of an x64 va_list and
 * passed to both, the C locale unless noted.
 *
 *   format.exe [iterations] [seed]
 */
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <windows.h>

/* Options of __stdio_common_* */
#define PRINTF_LEGACY_NULL_TERMINATION 0x1ull
#define PRINTF_STANDARD_SNPRINTF 0x2ull
#define PRINTF_LEGACY_WIDE_SPECIFIERS 0x4ull
#define SCANF_LEGACY_WIDE_SPECIFIERS 0x2ull

typedef int(__cdecl* PCOMMON_VSPRINTF)(unsigned long long, char*, size_t, const char*, void*, va_list);
typedef int(__cdecl* PCOMMON_VSWPRINTF)(unsigned long long, wchar_t*, size_t, const wchar_t*, void*, va_list);
typedef int(__cdecl* PCOMMON_VSSCANF)(unsigned long long, const char*, size_t, const char*, void*, va_list);
typedef int(__cdecl* PCOMMON_VSWSCANF)(unsigned long long, const wchar_t*, size_t, const wchar_t*, void*, va_list);
typedef void(__cdecl* PINVALID_PARAMETER)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t);

static PCOMMON_VSPRINTF RefVsprintf;
static int RefSnprintf(char* Buffer, size_t Size, const char* Format, ...);
static PCOMMON_VSWPRINTF RefVswprintf;
static PCOMMON_VSSCANF RefVsscanf;
static PCOMMON_VSWSCANF RefVswscanf;
static int*(__cdecl* RefErrno)(void);
static char*(__cdecl* RefSetlocale)(int, const char*);
static double(__cdecl* RefStrtod)(const char*, char**);
static double(__cdecl* RefWcstod)(const wchar_t*, wchar_t**);
static float(__cdecl* RefStrtof)(const char*, char**);
static long(__cdecl* RefStrtol)(const char*, char**, int);
static unsigned long(__cdecl* RefStrtoul)(const char*, char**, int);
static long long(__cdecl* RefStrtoll)(const char*, char**, int);
static unsigned long long(__cdecl* RefStrtoull)(const char*, char**, int);
static long(__cdecl* RefWcstol)(const wchar_t*, wchar_t**, int);
static unsigned long long(__cdecl* RefWcstoull)(const wchar_t*, wchar_t**, int);

static int RefSnprintf(char* Buffer, size_t Size, const char* Format, ...)
{
    va_list Args;
    int n;

    va_start(Args, Format);
    n = RefVsprintf(PRINTF_STANDARD_SNPRINTF, Buffer, Size, Format, NULL, Args);
    va_end(Args);
    return n;
}

static uint64_t g_Rng;
static unsigned long g_Iteration, g_Failures, g_Checks;
static int g_Utf8;

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

static void fail(const char* What, const char* Format, ...)
{
    va_list Args;

    if (g_Failures++ >= 30)
        return;
    printf("#%lu %s%s: ", g_Iteration, What, g_Utf8 ? " (UTF-8)" : "");
    va_start(Args, Format);
    vprintf(Format, Args);
    va_end(Args);
    printf("\n");
}

/* Printable form of a byte string */
static const char* show(const char* s, int n)
{
    static char Buf[4][1600];
    static int Next;
    char* b = Buf[Next++ % 4];
    int k = 0;

    for (int i = 0; i < n && k < (int)sizeof(Buf[0]) - 8 && (unsigned char)s[i] != 0xCC; i++)
    {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c < 0x7F && c != '\\')
            b[k++] = (char)c;
        else
            k += sprintf(b + k, "\\x%02x", c);
    }
    b[k] = '\0';
    return b;
}

static const char* showw(const wchar_t* s, int n)
{
    static char Buf[4][1600];
    static int Next;
    char* b = Buf[Next++ % 4];
    int k = 0;

    for (int i = 0; i < n && k < (int)sizeof(Buf[0]) - 8 && s[i] != 0xCCCC; i++)
    {
        if (s[i] >= 0x20 && s[i] < 0x7F && s[i] != '\\')
            b[k++] = (char)s[i];
        else
            k += sprintf(b + k, "\\u%04x", s[i]);
    }
    b[k] = '\0';
    return b;
}

/* printf */

typedef struct
{
    char Conv;
    int Precision; /* -1 if none */
    double Value;
} FLOAT_SPEC;

typedef struct
{
    char Format[256];
    uint64_t Slots[16];
    uint64_t Args[16]; /* the arguments as a call passes them, ArgsSize bytes */
    int ArgsSize;
    int Count;
    FLOAT_SPEC Floats[4];
    int FloatCount;
} PRINTF_CASE;

static const char* const g_Strings[] = { "", "a", "hello", "x y\tz", "0123456789abcdefghijklmnopqrstuvwxyz", "%d" };
static const char* const g_StringsUtf8[] = { "caf\xc3\xa9", "\xe4\xb8\xad\xe6\x96\x87", "\xf0\x9f\x98\x80!" };
static const wchar_t* const g_Wide[] = { L"", L"w", L"wide str", L"0123456789ABCDEFGHIJ" };
static const wchar_t* const g_WideUtf8[] = { L"caf\xe9", L"\x4e2d\x6587", L"\xd83d\xde00!", L"\x00a0\x00ff" };

/* An argument of Size bytes, 8 on x64 whatever its size */
static void add_slot(PRINTF_CASE* c, uint64_t v, size_t Size)
{
#ifdef _WIN64
    Size = 8;
#endif
    c->Slots[c->Count++] = v;
    memcpy((char*)c->Args + c->ArgsSize, &v, Size);
    c->ArgsSize += (int)Size;
}

/* The size of an integer argument of Length */
static size_t int_size(const char* Length)
{
    if (strcmp(Length, "ll") == 0 || strcmp(Length, "j") == 0 || strcmp(Length, "I64") == 0)
        return 8;
    if (strcmp(Length, "z") == 0 || strcmp(Length, "t") == 0)
        return sizeof(size_t);
    return sizeof(int);
}

static uint64_t gen_int(void)
{
    static const int64_t Edges[] = { 0, 1, -1, 7, 8, 9, 10, 99, 100, 127, 128, -128, -129, 255, 256, 32767, 32768,
                                     -32768, 65535, 65536, INT_MAX, INT_MIN, UINT_MAX, (int64_t)UINT_MAX + 1,
                                     LLONG_MAX, LLONG_MIN, 1000000, -999999, 0x7FFFFFFF00000000ll };
    switch (rnd(4))
    {
    case 0:
        return (uint64_t)Edges[rnd(sizeof(Edges) / sizeof(Edges[0]))];
    case 1:
        return (uint64_t)((int64_t)rnd(2001) - 1000);
    case 2:
        return rnd64() >> rnd(64);
    default:
        return rnd64();
    }
}

static double gen_double(void)
{
    static const double Edges[] = { 0.0, -0.0, 1.0, -1.0, 0.5, 0.25, 0.1, 1.5, 2.5, 9.5, 99.5, 0.05, 0.15, 1e15,
                                     1e16, 1e17, 1e21, 1e22, 1e23, 123456789.0, DBL_MAX, -DBL_MAX, DBL_MIN,
                                     DBL_TRUE_MIN, 1.0 / 3, 2.0 / 3, 9.999999, 0.999999999, 1e-5, 1e-4,
                                     0.000123456, 5e-324, 2.2250738585072009e-308 };
    double d;
    uint64_t Bits;

    switch (rnd(8))
    {
    case 0:
        return Edges[rnd(sizeof(Edges) / sizeof(Edges[0]))];
    case 1:
        return rnd(3) == 0 ? INFINITY : rnd(2) ? -INFINITY : NAN;
    case 2:
        /* A tie of a short decimal: n.5 * 10^-k */
        d = ((double)rnd(2000) + 0.5) / pow(10, rnd(6));
        return rnd(2) ? d : -d;
    case 3:
        d = (double)(int32_t)rnd64() * pow(10, (int)rnd(40) - 20);
        return d;
    case 4:
        /* Subnormal */
        Bits = rnd64() & 0x800FFFFFFFFFFFFFull;
        memcpy(&d, &Bits, 8);
        return d;
    default:
        /* Any finite value */
        do
        {
            Bits = rnd64();
            memcpy(&d, &Bits, 8);
        } while (!isfinite(d));
        return d;
    }
}

/*
 * One conversion specification and its arguments. Wide is 0 for narrow functions, 1 for the wide functions of
 * msvcrt (%s and %c wide) and 2 for the C99 wide functions (%s and %c narrow); %hs/%hc are narrow and %ls/%lc
 * wide. Combinations whose behavior C leaves undefined are not made, nor those UCRT gets wrong: the # flag with
 * infinity or NaN ("I.NF"), a width or precision of %ls in "C.UTF-8" (counted in wchar_t, a character split).
 */
static void gen_spec(PRINTF_CASE* c, int Wide)
{
    static const char* const IntLengths[] = { "", "", "hh", "h", "l", "ll", "j", "z", "t", "I64", "I32" };
    static const char IntConvs[] = "diuoxX";
    static const char FloatConvs[] = "eEfFgGaA";
    char Flags[8] = "", Width[16] = "", Precision[16] = "", Length[8] = "";
    char Conv;
    int Kind = rnd(10), nf = 0, Text = 0;
    double d = gen_double();
    char* f = c->Format + strlen(c->Format);

    /* 0-3 integer, 4-6 floating, 7 string, 8 character, 9 %% */
    if (Kind == 9)
    {
        strcpy(f, "%%");
        return;
    }
    if (Kind <= 3)
    {
        strcpy(Length, IntLengths[rnd(sizeof(IntLengths) / sizeof(IntLengths[0]))]);
        Conv = IntConvs[rnd(6)];
    } else if (Kind <= 6)
    {
        strcpy(Length, rnd(4) == 0 ? "l" : "");
        Conv = FloatConvs[rnd(8)];
    } else
    {
        int w = rnd(3);
        strcpy(Length, w == 0 ? "" : w == 1 ? "h" : "l");
        Conv = Kind == 7 ? 's' : 'c';
        Text = g_Utf8;
    }
    for (int i = rnd(4); i > 0; i--)
    {
        char Flag = "-+ #0"[rnd(5)];
        if (Flag == '#' && (Kind <= 3 ? (Conv == 'd' || Conv == 'i' || Conv == 'u') : Kind >= 7 || !isfinite(d)))
            continue;
        if (Flag == '0' && Kind >= 7)
            continue;
        Flags[nf++] = Flag;
    }
    Flags[nf] = '\0';
    switch (Text ? 0 : rnd(4))
    {
    case 0:
        break;
    case 1:
        strcpy(Width, "*");
        break;
    default:
        sprintf(Width, "%u", 1 + rnd(30));
        break;
    }
    if (Conv == 'a' || Conv == 'A')
    {
        sprintf(Precision, ".%u", rnd(16));
    } else if (Kind != 8 && !Text)
    {
        switch (rnd(5))
        {
        case 0:
            strcpy(Precision, ".");
            break;
        case 1:
            strcpy(Precision, ".*");
            break;
        case 2:
            sprintf(Precision, ".%u", rnd(Kind <= 3 ? 25 : Kind <= 6 ? 30 : 12));
            break;
        default:
            break;
        }
    }
    sprintf(f, "%%%s%s%s%s%c", Flags, Width, Precision, Length, Conv);

    if (strcmp(Width, "*") == 0)
        add_slot(c, (uint64_t)(int64_t)((int)rnd(61) - 30), sizeof(int));
    if (strcmp(Precision, ".*") == 0)
        add_slot(c, (uint64_t)(int64_t)((int)rnd(40) - 5), sizeof(int));
    if (Kind <= 3)
    {
        add_slot(c, gen_int(), int_size(Length));
    } else if (Kind <= 6)
    {
        FLOAT_SPEC* s = &c->Floats[c->FloatCount++];
        uint64_t Bits;
        memcpy(&Bits, &d, 8);
        add_slot(c, Bits, sizeof(double));
        s->Conv = Conv;
        s->Value = d;
        s->Precision = Precision[0] == '\0' ? -1 : Precision[1] == '*' ? (int)c->Slots[c->Count - 2] : atoi(Precision + 1);
    } else
    {
        int WideArg = Length[0] == 'l' || (Length[0] == '\0' && Wide == 1);
        if (Kind == 7)
        {
            if (WideArg)
                add_slot(c, (uintptr_t)(g_Utf8 && rnd(2) ? g_WideUtf8[rnd(4)] : g_Wide[rnd(4)]), sizeof(void*));
            else
                add_slot(c, (uintptr_t)(g_Utf8 && rnd(2) ? g_StringsUtf8[rnd(3)] : g_Strings[rnd(6)]), sizeof(void*));
        } else
        {
            /* A narrow character is an int converted to unsigned char */
            /* A wide printf of mingw-w64 writes nothing for the null character (see README) */
            if (WideArg)
                add_slot(c, g_Utf8 && rnd(2) ? L"\xe9\x4e2d\x3000"[rnd(3)] : (wchar_t)(0x20 + rnd(0x5F)), sizeof(int));
            else
                add_slot(c, rnd(8) == 0 ? 0x100 * (1 + rnd(0x40)) + 0x41 * (Wide != 0 || rnd(2))
                                        : 0x20 + rnd(0x5F),
                         sizeof(int));
        }
    }
}

static void gen_printf(PRINTF_CASE* c, int Wide)
{
    static const char* const Texts[] = { "", "", "x", " ", "abc ", "[", "]\n" };
    int n = 1 + rnd(3);

    c->Format[0] = '\0';
    c->Count = 0;
    c->ArgsSize = 0;
    c->FloatCount = 0;
    for (int i = 0; i < n; i++)
    {
        strcat(c->Format, Texts[rnd(7)]);
        gen_spec(c, Wide);
    }
    strcat(c->Format, Texts[rnd(7)]);
}

/*
 * Whether a floating conversion rounds an exact tie: C rounds it to even in the default rounding mode, UCRT away
 * from zero. The digits come from %.1100e of UCRT, the exact decimal expansion of a double.
 */
static int is_tie(const FLOAT_SPEC* s)
{
    char Exact[1200];
    char Digits[1200];
    int n = 0, Kept, Exponent, Precision = s->Precision;
    char Conv = (char)(s->Conv | 0x20);
    uint64_t Bits;

    if (!isfinite(s->Value))
        return 0;
    if (Conv == 'a')
    {
        /* The bits after the hex digits kept are 10...0 */
        int Drop = 52 - 4 * Precision;
        memcpy(&Bits, &s->Value, 8);
        Bits &= 0xFFFFFFFFFFFFFull;
        return Precision >= 0 && Drop > 0 && (Bits & ((1ull << Drop) - 1)) == 1ull << (Drop - 1);
    }
    if (Precision < 0)
        Precision = 6;
    RefSnprintf(Exact, sizeof(Exact), "%.1100e", fabs(s->Value));
    for (const char* p = Exact; *p != 'e'; p++)
        if (*p >= '0' && *p <= '9')
            Digits[n++] = *p;
    Exponent = atoi(strchr(Exact, 'e') + 1);
    Kept = Conv == 'e' ? Precision + 1 : Conv == 'f' ? Exponent + 1 + Precision : Precision == 0 ? 1 : Precision;
    if (Kept < 0 || Kept >= n || Digits[Kept] != '5')
        return 0;
    for (int i = Kept + 1; i < n; i++)
        if (Digits[i] != '0')
            return 0;
    return 1;
}

static int has_tie(const PRINTF_CASE* c)
{
    for (int i = 0; i < c->FloatCount; i++)
        if (is_tie(&c->Floats[i]))
            return 1;
    return 0;
}

static void check_vsnprintf(const char* What, int (*Fn)(char*, size_t, const char*, va_list), unsigned long long
    Options, const PRINTF_CASE* c, int Legacy)
{
    char Got[1100], Want[1100];
    size_t Size = rnd(3) == 0 ? 1 + rnd(40) : 1000;
    int g, w, ge, we;

    memset(Got, 0xCC, sizeof(Got));
    memset(Want, 0xCC, sizeof(Want));
    errno = 0;
    *RefErrno() = 0;
    g = Fn(Got, Size, c->Format, (va_list)c->Args);
    ge = errno;
    w = RefVsprintf(Options, Want, Size, c->Format, NULL, (va_list)c->Args);
    we = *RefErrno();
    g_Checks++;
    if (g < 0 && w < 0)
        memcpy(Got, Want, sizeof(Got));
    if ((g != w || memcmp(Got, Want, Size + 8) != 0 || (g < 0 && ge != we)) && !has_tie(c))
    {
        int n = Legacy ? (int)Size : (g >= 0 && g < (int)Size ? g : (int)Size - 1);
        int m = Legacy ? (int)Size : (w >= 0 && w < (int)Size ? w : (int)Size - 1);
        fail(What, "\"%s\" size %u: %d/%d [%s], UCRT %d/%d [%s]", show(c->Format, (int)strlen(c->Format)),
             (unsigned)Size, g, ge, show(Got, n), w, we, show(Want, m));
    }
}

static int __cdecl wrap_mingw_vsnprintf(char* b, size_t n, const char* f, va_list ap)
{
    return __mingw_vsnprintf(b, n, f, ap);
}

static int __cdecl wrap_vsnprintf(char* b, size_t n, const char* f, va_list ap)
{
    return vsnprintf(b, n, f, ap);
}

static int __cdecl wrap_legacy_vsnprintf(char* b, size_t n, const char* f, va_list ap)
{
    return _vsnprintf(b, n, f, ap);
}

static void check_vsnwprintf(const char* What, const PRINTF_CASE* c, int Legacy)
{
    wchar_t Format[256], Got[1100], Want[1100];
    size_t Size = rnd(3) == 0 ? 1 + rnd(40) : 1000;
    int g, w, ge, we;

    for (int i = 0;; i++)
        if ((Format[i] = (unsigned char)c->Format[i]) == 0)
            break;
    memset(Got, 0xCC, sizeof(Got));
    memset(Want, 0xCC, sizeof(Want));
    errno = 0;
    *RefErrno() = 0;
    g = Legacy ? _vsnwprintf(Got, Size, Format, (va_list)c->Args)
               : __mingw_vsnwprintf(Got, Size, Format, (va_list)c->Args);
    ge = errno;
    w = RefVswprintf(Legacy ? PRINTF_LEGACY_NULL_TERMINATION | PRINTF_LEGACY_WIDE_SPECIFIERS
                            : PRINTF_STANDARD_SNPRINTF,
                     Want, Size, Format, NULL, (va_list)c->Args);
    we = *RefErrno();
    g_Checks++;
    if (g < 0 && w < 0)
        memcpy(Got, Want, sizeof(Got));
    if ((g != w || memcmp(Got, Want, (Size + 4) * sizeof(wchar_t)) != 0 || (g < 0 && ge != we)) && !has_tie(c))
    {
        int n = Legacy ? (int)Size : (g >= 0 && g < (int)Size ? g : (int)Size - 1);
        int m = Legacy ? (int)Size : (w >= 0 && w < (int)Size ? w : (int)Size - 1);
        fail(What, "L\"%s\" size %u: %d/%d [%s], UCRT %d/%d [%s]", show(c->Format, (int)strlen(c->Format)),
             (unsigned)Size, g, ge, showw(Got, n), w, we, showw(Want, m));
    }
}

static void fuzz_printf(void)
{
    PRINTF_CASE c;

    gen_printf(&c, 0);
    check_vsnprintf("__mingw_vsnprintf", wrap_mingw_vsnprintf, PRINTF_STANDARD_SNPRINTF, &c, 0);
    check_vsnprintf("vsnprintf", wrap_vsnprintf, PRINTF_STANDARD_SNPRINTF, &c, 0);
    check_vsnprintf("_vsnprintf", wrap_legacy_vsnprintf, PRINTF_LEGACY_NULL_TERMINATION, &c, 1);
    gen_printf(&c, 1);
    check_vsnwprintf("_vsnwprintf", &c, 1);
    gen_printf(&c, 2);
    check_vsnwprintf("__mingw_vsnwprintf", &c, 0);
}

/* Number parsing */

/* A number-like string: spaces, sign, prefix, digits of a base with a point and an exponent, junk */
static void gen_number(char* s, int Float)
{
    static const char* const Specials[] = { "inf", "INF", "infinity", "InFiNiTy", "infin", "nan", "NAN", "nan()",
                                            "nan(123)", "nan(abc_9)", "nan(", "nan(-)", "in", "na" };
    static const char Digits[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFXYZ";
    int k = 0, Hex = 0;

    for (int i = rnd(4) == 0 ? rnd(3) : 0; i > 0; i--)
        s[k++] = " \t\n\v\f\r"[rnd(6)];
    if (rnd(3) == 0)
        s[k++] = "+-"[rnd(2)];
    if (Float && rnd(10) == 0)
    {
        k += sprintf(s + k, "%s", Specials[rnd(sizeof(Specials) / sizeof(Specials[0]))]);
    } else
    {
        int Base = rnd(4) == 0 ? 16 : rnd(4) == 0 ? 36 : 10;
        int n = rnd(5) == 0 ? rnd(60) : rnd(12);

        if (rnd(4) == 0)
        {
            k += sprintf(s + k, "%s", rnd(2) ? "0x" : "0X");
            Hex = 1;
            Base = 16;
        } else if (rnd(6) == 0)
            s[k++] = '0';
        for (int i = 0; i < n; i++)
        {
            if (rnd(30) == 0)
                s[k++] = "0123456789"[rnd(10)];
            s[k++] = rnd(3) == 0 ? '0' : Digits[rnd(Base)];
            if (Float && rnd(n + 1) == 0)
                s[k++] = '.';
        }
        if (Float && rnd(2))
        {
            s[k++] = Hex ? "pP"[rnd(2)] : "eE"[rnd(2)];
            if (rnd(2))
                s[k++] = "+-"[rnd(2)];
            k += sprintf(s + k, "%u", rnd(4) == 0 ? rnd(5000) : rnd(400));
        }
    }
    if (rnd(3) == 0)
        s[k++] = "x.e+- 9_)"[rnd(9)];
    s[k] = '\0';
}

/*
 * The exact decimal of the midpoint between a double or a float and the next one, the hardest input to round,
 * or a little more or less. The midpoint is a long double, printed exactly by mingw-w64. The UCRT rounds a tie
 * of strtof away from zero and ignores the digits after the 768th: *Want is the correct result instead, the even
 * one of the two for a tie.
 */
typedef struct
{
    int Float;
    double Want;
} MIDPOINT;

static void gen_midpoint(char* s, MIDPOINT* m)
{
    double d = fabs(gen_double()), Lo, Hi;
    long double Mid;
    char Exp[16];
    char* e;
    int n, Pad, Keep = 18;

    m->Float = rnd(2);
    if (!m->Float)
    {
        if (!(d < DBL_MAX))
            d = 1e300;
        Lo = d;
        Hi = nextafter(d, INFINITY);
    } else
    {
        float f;

        if (!(d < FLT_MAX))
            d = 1e30;
        f = (float)d;
        Lo = f;
        Hi = nextafterf(f, INFINITY);
        Keep = 10;
    }
    Mid = ((long double)Lo + Hi) / 2;
    __mingw_snprintf(s, 1000, "%.780Le", Mid);
    e = strchr(s, 'e');
    strcpy(Exp, e);
    /*
     * The digits up to the last one that is not 0, 1 if the midpoint is a power of 10 ("1."). A digit more or
     * less after the Keep-th digit changes it by less than half the distance from Lo and from Hi.
     */
    for (n = (int)(e - s); s[n - 1] == '0'; n--)
        ;
    Pad = n < Keep + 2 ? Keep + 2 - n : 0;
    switch (rnd(3))
    {
    case 0:
        /* A tie */
        Pad = rnd(10);
        memset(s + n, '0', Pad);
        n += Pad;
        m->Want = m->Float ? (*(uint32_t*)&(float){ (float)Lo } & 1 ? Hi : Lo) : (*(uint64_t*)&Lo & 1 ? Hi : Lo);
        break;
    case 1:
        /* More: a 1 after some zeros */
        Pad += rnd(20);
        memset(s + n, '0', Pad);
        n += Pad;
        s[n++] = '1';
        m->Want = Hi;
        break;
    default:
        /* Less: fewer digits, or the last digit down and some 9s after */
        if (rnd(2) && n - 1 > Keep + 1)
        {
            n = Keep + 1 + rnd(n - 1 - (Keep + 1));
        } else
        {
            s[n - 1 - (s[n - 1] == '.')]--;
            Pad += rnd(20);
            memset(s + n, '9', Pad);
            n += Pad;
        }
        m->Want = Lo;
        break;
    }
    strcpy(s + n, Exp);
}

static void check_strtod(void)
{
    char s[1024];
    wchar_t w[1024];
    char *ge, *we;
    wchar_t *gwe, *wwe;
    double g, r;
    float gf, rf;
    int gerr, werr;
    MIDPOINT m;
    int Mid = rnd(4) == 0;

    if (Mid)
        gen_midpoint(s, &m);
    else
        gen_number(s, 1);
    errno = 0;
    *RefErrno() = 0;
    g = strtod(s, &ge);
    gerr = errno;
    r = RefStrtod(s, &we);
    werr = *RefErrno();
    if (Mid && !m.Float)
    {
        r = m.Want;
        we = s + strlen(s);
        werr = r == 0 ? ERANGE : 0;
    }
    g_Checks++;
    if (memcmp(&g, &r, 8) != 0 && !(isnan(g) && isnan(r)) || ge - s != we - s || gerr != werr)
        fail("strtod", "\"%s\": %a end %d errno %d, UCRT %a end %d errno %d", show(s, (int)strlen(s)), g,
             (int)(ge - s), gerr, r, (int)(we - s), werr);

    errno = 0;
    *RefErrno() = 0;
    gf = strtof(s, &ge);
    gerr = errno;
    rf = RefStrtof(s, &we);
    werr = *RefErrno();
    if (Mid && m.Float)
    {
        rf = (float)m.Want;
        we = s + strlen(s);
        werr = rf == 0 ? ERANGE : 0;
    }
    if (memcmp(&gf, &rf, 4) != 0 && !(isnan(gf) && isnan(rf)) || ge - s != we - s || gerr != werr)
        fail("strtof", "\"%s\": %a end %d errno %d, UCRT %a end %d errno %d", show(s, (int)strlen(s)), (double)gf,
             (int)(ge - s), gerr, (double)rf, (int)(we - s), werr);

    for (int i = 0;; i++)
        if ((w[i] = (unsigned char)s[i]) == 0)
            break;
    errno = 0;
    *RefErrno() = 0;
    g = wcstod(w, &gwe);
    gerr = errno;
    r = RefWcstod(w, &wwe);
    werr = *RefErrno();
    if (Mid && !m.Float)
    {
        r = m.Want;
        wwe = w + wcslen(w);
        werr = r == 0 ? ERANGE : 0;
    }
    if (memcmp(&g, &r, 8) != 0 && !(isnan(g) && isnan(r)) || gwe - w != wwe - w || gerr != werr)
        fail("wcstod", "\"%s\": %a end %d errno %d, UCRT %a end %d errno %d", show(s, (int)strlen(s)), g,
             (int)(gwe - w), gerr, r, (int)(wwe - w), werr);
}

static void check_strtol(void)
{
    char s[256];
    wchar_t w[256];
    char *ge, *we;
    wchar_t *gwe, *wwe;
    int Base = rnd(8) == 0 ? (int)rnd(40) - 1 : rnd(3) == 0 ? 0 : rnd(3) == 0 ? 2 + (int)rnd(35) : rnd(2) ? 10 : 16;
    unsigned long long g, r;
    int gerr, werr;

    int Prefix = -1;

    gen_number(s, 0);
    for (int i = 0;; i++)
        if ((w[i] = (unsigned char)s[i]) == 0)
            break;
    if (Base == 0 || Base == 16)
    {
        const char* p = s + strspn(s, " \t\n\v\f\r");
        p += *p == '+' || *p == '-';
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && !isxdigit((unsigned char)p[2]))
            Prefix = (int)(p + 1 - s);
    }
#define CHECK_STRTOL(Name, Type, Got, Ref, Str, End, RefEnd)                                                    \
    do                                                                                                         \
    {                                                                                                          \
        errno = 0;                                                                                             \
        *RefErrno() = 0;                                                                                       \
        End = NULL;                                                                                            \
        RefEnd = NULL;                                                                                         \
        g = (unsigned long long)(Type)Got(Str, &End, Base);                                                     \
        gerr = errno;                                                                                          \
        r = (unsigned long long)(Type)Ref(Str, &RefEnd, Base);                                                  \
        werr = *RefErrno();                                                                                    \
        if (Prefix >= 0)                                                                                       \
        {                                                                                                      \
            r = 0;                                                                                             \
            RefEnd = Str + Prefix;                                                                             \
            werr = 0;                                                                                          \
        }                                                                                                      \
        g_Checks++;                                                                                            \
        if (g != r || End != RefEnd || gerr != werr)                                                           \
            fail(Name, "\"%s\" base %d: %llx end %d errno %d, UCRT %llx end %d errno %d",                     \
                 show(s, (int)strlen(s)), Base, g, End ? (int)(End - Str) : -1, gerr, r,                        \
                 RefEnd ? (int)(RefEnd - Str) : -1, werr);                                                     \
    } while (0)
    CHECK_STRTOL("strtol", long, strtol, RefStrtol, s, ge, we);
    CHECK_STRTOL("strtoul", unsigned long, strtoul, RefStrtoul, s, ge, we);
    CHECK_STRTOL("strtoll", long long, strtoll, RefStrtoll, s, ge, we);
    CHECK_STRTOL("strtoull", unsigned long long, strtoull, RefStrtoull, s, ge, we);
    CHECK_STRTOL("wcstol", long, wcstol, RefWcstol, w, gwe, wwe);
    CHECK_STRTOL("wcstoull", unsigned long long, wcstoull, RefWcstoull, w, gwe, wwe);
}

/* The wide strtol family skips what iswspace takes, the narrow one isspace of the C locale only */
static void check_strtol_space(void)
{
    static const wchar_t Chars[] = { 0x09, 0x20, 0x85, 0xA0, 0x1680, 0x2000, 0x2028, 0x3000, 0xFEFF, 'x' };
    static const char* const Names[] = { "wcstol", "wcstoul", "wcstoll", "wcstoull", "wcstoimax", "wcstoumax" };
    wchar_t w[5] = { 0, '-', '1', '2', 0 };
    char s[] = { '\xA0', '1', '2', 0 };
    char* se;

    g_Checks++;
    if (!iswspace(0xA0) || iswspace('x'))
        fail("iswspace", "U+00A0 %d, 'x' %d", iswspace(0xA0) != 0, iswspace('x') != 0);
    for (int i = 0; i < (int)(sizeof(Chars) / sizeof(Chars[0])); i++)
    {
        long long Want = iswspace(Chars[i]) ? -12 : 0;
        int WantEnd = Want != 0 ? 4 : 0;

        w[0] = Chars[i];
        for (int k = 0; k < 6; k++)
        {
            wchar_t* e = NULL;
            long long g;

            errno = 0;
            switch (k)
            {
            case 0:
                g = wcstol(w, &e, 10);
                break;
            case 1:
                g = (long)wcstoul(w, &e, 10);
                break;
            case 2:
                g = wcstoll(w, &e, 10);
                break;
            case 3:
                g = (long long)wcstoull(w, &e, 10);
                break;
            case 4:
                g = wcstoimax(w, &e, 10);
                break;
            default:
                g = (long long)wcstoumax(w, &e, 10);
                break;
            }
            g_Checks++;
            if (g != Want || e != w + WantEnd || errno != 0)
                fail(Names[k], "U+%04X \"-12\": %lld end %d errno %d, want %lld end %d", (unsigned)Chars[i], g,
                     e ? (int)(e - w) : -1, errno, Want, WantEnd);
        }
    }
    g_Checks++;
    if (strtol(s, &se, 10) != 0 || se != s)
        fail("strtol", "\"\\xA0\" \"12\": end %d, want 0", (int)(se - s));
}

/* scanf */

typedef struct
{
    char Format[1024];
    char Input[1024];
    uintptr_t Slots[16];
    int Size[16]; /* bytes stored through Slots[i], 0 for none */
    int Float[16];
    int WideOut[16]; /* wchar_t of a narrow string conversion */
    int Count;
    int BadAtEnd; /* the last directive fails on a prefix of a number at the end of the input */
} SCANF_CASE;

/*
 * A scanf format and its input: integers in and out of the range of their type, floating point numbers, strings,
 * scan sets, characters, %n and literals, a width that may cut a number. The last directive may be a prefix of a
 * number that is not a number ("1e", "0x", "-"), a matching failure. Some formats begin with long text.
 */
static void gen_scanf(SCANF_CASE* c, unsigned char (*Store)[64])
{
    static const char* const Lengths[] = { "", "hh", "h", "l", "ll", "j", "z", "t", "I64", "I32", "I" };
    static const int LengthSize[] = { 4, 1, 2, 4, 8, 8, 8, 8, 8, 4, 8 };
    static const char* const Sets[] = { "[abc]", "[^,]", "[a-z0-9]", "[^ \n]", "[]x]", "[^]a]", "[-+.0-9]" };
    static const char* const Specials[] = { "inf", "-INF", "infinity", "+Infinity", "nan", "-NaN", "nan(abc_1)",
                                            "NAN()", "0x1.8P+3", "0X.Cp-2", "-0x1p-1074", "0x1.fffffffffffff8p1023",
                                            "1e400", "-1e-400", "4.9e-324", "2.4703282292062328e-324",
                                            "1.00000005960464477539062", "0x1.000001p0", "1.e5", ".5E-3" };
    static const char* const BadIntegers[] = { "-", "+", "0x", "-0X", "+0x" };
    static const char* const BadFloats[] = { "1e", "1E+", "-2.5e-", "-", ".", "+.", "0x", "0x.", "-0X", "0x1p",
                                             "0x1.8P-", "nan(", "nan(ab", "in", "infi", "INFINIT", "n", "Na" };
    int n = 1 + rnd(4);

    c->Format[0] = c->Input[0] = '\0';
    c->Count = 0;
    c->BadAtEnd = 0;
    if (rnd(8) == 0)
    {
        int Text = 200 + (int)rnd(400);

        for (int i = 0; i < Text; i++)
            c->Format[i] = c->Input[i] = "abIq-"[rnd(5)];
        c->Format[Text] = c->Input[Text] = '\0';
    }
    for (int i = 0; i < n; i++)
    {
        char* f;
        char* in;
        int Width = rnd(3) == 0 ? 1 + (int)rnd(12) : 0;
        int Kind = rnd(10), Suppress = rnd(8) == 0;

        /* A string or a scan set may read the separators and the next items, it is the last directive */
        if ((Kind == 5 || Kind == 6 || Kind == 9) && i != n - 1)
            Kind = rnd(5);
        if (i > 0 || rnd(4) == 0)
        {
            const char* Sep = rnd(2) ? " " : ",";
            strcat(c->Format, Sep);
            strcat(c->Input, Sep);
        }
        f = c->Format + strlen(c->Format);
        in = c->Input + strlen(c->Input);
        if (Kind <= 2)
        {
            /* An integer, at times out of the range of its type or of 64 bits, a sign also for the unsigned ones */
            int l = rnd(11);
            char Conv = "diuoxXip"[rnd(8)];
            int Bits = LengthSize[l] * 8;
            uint64_t v = rnd(8) == 0 ? rnd64() : rnd64() >> (64 - Bits + rnd(Bits));
            const char* Sign = rnd(4) == 0 ? (rnd(2) ? "-" : "+") : "";
            const char* More = rnd(16) == 0 ? "123456789" : "";

            if (Conv == 'p')
                l = 0;
            if ((Conv == 'd' || Conv == 'i') && rnd(2))
                Sign = "-";
            switch (Conv)
            {
            case 'o':
                sprintf(in, "%s%s%llo%s", Sign, rnd(4) == 0 ? "0" : "", (unsigned long long)v, More);
                break;
            case 'x':
            case 'X':
            case 'p':
                sprintf(in, rnd(4) == 0 ? "%s%s%016llX%s" : "%s%s%llx%s", Sign, rnd(3) == 0 ? "0x" : "",
                        (unsigned long long)v, More);
                break;
            case 'i':
                sprintf(in, rnd(3) == 0 ? "%s0x%llx%s" : rnd(3) == 0 ? "%s0%llo%s" : "%s%llu%s", Sign,
                        (unsigned long long)v, More);
                break;
            default:
                sprintf(in, "%s%llu%s", Sign, (unsigned long long)v, More);
                break;
            }
            if (Width != 0)
                sprintf(f, "%%%s%d%s%c", Suppress ? "*" : "", Width, Lengths[l], Conv);
            else
                sprintf(f, "%%%s%s%c", Suppress ? "*" : "", Lengths[l], Conv);
            if (!Suppress)
                c->Size[c->Count] = Conv == 'p' ? 8 : LengthSize[l];
        } else if (Kind <= 4)
        {
            char Conv = "efgaEFGA"[rnd(8)];
            int Double = rnd(2);
            double d = gen_double();

            switch (rnd(5))
            {
            case 0:
                sprintf(in, "%a", d);
                break;
            case 1:
                sprintf(in, "%.*e", (int)rnd(20), d);
                break;
            case 2:
                sprintf(in, "%.*f", (int)rnd(8), fabs(d) < 1e30 ? d : 1.0 / d);
                break;
            case 3:
                strcpy(in, Specials[rnd(sizeof(Specials) / sizeof(Specials[0]))]);
                break;
            default:
                sprintf(in, "%.17g", d);
                break;
            }
            if (Width != 0)
                sprintf(f, "%%%s%d%s%c", Suppress ? "*" : "", Width, Double ? "l" : "", Conv);
            else
                sprintf(f, "%%%s%s%c", Suppress ? "*" : "", Double ? "l" : "", Conv);
            if (!Suppress)
            {
                c->Size[c->Count] = Double ? 8 : 4;
                c->Float[c->Count] = 1;
            }
        } else if (Kind == 5)
        {
            /* A narrow or wide string, the bytes of the "C" locale are U+0000..U+00FF */
            static const char* const Words[] = { "word", "a,b", "x", "abc123", "]]x", "-+1.5", "caf\xc3\xa9" };
            const char* Conv = rnd(2) ? "s" : rnd(4) == 0 ? "ls" : rnd(3) == 0 ? "S" : Sets[rnd(7)];

            if (Width != 0)
                sprintf(f, "%%%s%d%s", Suppress ? "*" : "", Width, Conv);
            else
                sprintf(f, "%%%s%s", Suppress ? "*" : "", Conv);
            strcpy(in, Words[rnd(7)]);
            if (!Suppress)
            {
                c->Size[c->Count] = 64;
                c->WideOut[c->Count] = Conv[0] == 'l' || Conv[0] == 'S';
            }
        } else if (Kind == 6)
        {
            const char* Conv = rnd(4) ? "c" : rnd(2) ? "lc" : "C";

            if (Width != 0)
                sprintf(f, "%%%s%d%s", Suppress ? "*" : "", Width, Conv);
            else
                sprintf(f, "%%%s%s", Suppress ? "*" : "", Conv);
            strcpy(in, "chars, more chars");
            if (!Suppress)
            {
                c->Size[c->Count] = 64;
                c->WideOut[c->Count] = Conv[0] != 'c';
            }
        } else if (Kind == 7)
        {
            strcpy(f, rnd(2) ? "%n" : rnd(2) ? "%hhn" : "%lln");
            Suppress = 0;
            c->Size[c->Count] = f[1] == 'n' ? 4 : f[2] == 'h' ? 1 : 8;
        } else if (Kind == 8)
        {
            strcpy(f, rnd(2) ? "%%" : ":");
            strcpy(in, rnd(4) ? f[0] == '%' ? "%" : ":" : "?");
            Suppress = 1;
        } else
        {
            /* A prefix of a number that is not one, then the end of the input or a character that ends it */
            int Float = rnd(2);
            const char* Bad = Float ? BadFloats[rnd(sizeof(BadFloats) / sizeof(BadFloats[0]))]
                                    : BadIntegers[rnd(sizeof(BadIntegers) / sizeof(BadIntegers[0]))];
            char Conv = Float ? "efgaEFGA"[rnd(8)] : Bad[strlen(Bad) - 1] == '-' || Bad[strlen(Bad) - 1] == '+'
                                                         ? "diuoxXp"[rnd(7)]
                                                         : "xXip"[rnd(4)];
            int Double = Float && rnd(2);

            sprintf(f, "%%%s%s%c", Suppress ? "*" : "", Double ? "l" : "", Conv);
            strcpy(in, Bad);
            if (rnd(2))
                strcat(in, rnd(2) ? " 5" : "!");
            else
                c->BadAtEnd = 1;
            if (!Suppress)
            {
                c->Size[c->Count] = Float ? (Double ? 8 : 4) : Conv == 'p' ? 8 : 4;
                c->Float[c->Count] = Float;
            }
        }
        if (!Suppress)
        {
            c->Slots[c->Count] = (uintptr_t)Store[c->Count];
            c->Count++;
        }
    }
}

/* The vswscanf of msvcrt, %s, %c and %[ are wide */
#ifdef _WIN64
extern int(__cdecl* __imp_vswscanf)(const wchar_t*, const wchar_t*, va_list);
#else
extern int(__cdecl* __imp_vswscanf)(const wchar_t*, const wchar_t*, va_list) __asm__("__imp__vswscanf");
#endif

static void compare_scanf(const char* What, const SCANF_CASE* c, int g, int w, unsigned char (*Got)[64],
                          unsigned char (*Want)[64], int Narrow)
{
    g_Checks++;
    /* A NaN may have another payload */
    for (int i = 0; i < c->Count; i++)
        if (c->Float[i] && (c->Size[i] == 8 ? isnan(*(double*)Got[i]) && isnan(*(double*)Want[i])
                                            : isnan(*(float*)Got[i]) && isnan(*(float*)Want[i])))
            memcpy(Got[i], Want[i], 64);
    /* UCRT sign-extends the bytes 0x80..0xFF of the "C" locale to wchar_t in a narrow function (see README) */
    for (int i = 0; Narrow && i < c->Count; i++)
        for (int k = 0; c->WideOut[i] && k < 64; k += 2)
            if (Want[i][k + 1] == 0xFF && Want[i][k] >= 0x80 && (Got[i][k + 1] == 0 && Got[i][k] == Want[i][k]))
                Want[i][k + 1] = 0;
    /*
     * UCRT returns EOF when the input ends in an item that is not a matching sequence and nothing was assigned, C
     * has a matching failure, the count of assignments 0 (see README)
     */
    if (c->BadAtEnd && w == EOF)
        w = 0;
    if (g != w || memcmp(Got, Want, 16 * 64) != 0)
    {
        char Detail[1200] = "";
        for (int i = 0; i < c->Count; i++)
            if (memcmp(Got[i], Want[i], 64) != 0)
            {
                int n = c->Size[i] < 64 ? c->Size[i] : Narrow ? 24 : 48;
                sprintf(Detail + strlen(Detail), " arg%d %s / UCRT %s", i, show((char*)Got[i], n),
                        show((char*)Want[i], n));
                break;
            }
        fail(What, "\"%s\" \"%s\": %d, UCRT %d%s", show(c->Input, (int)strlen(c->Input)),
             show(c->Format, (int)strlen(c->Format)), g, w, Detail);
    }
}

static void check_scanf(void)
{
    SCANF_CASE c;
    unsigned char Got[16][64], Want[16][64];
    wchar_t Format[1024], Input[1024];
    int g, w;

    memset(c.Size, 0, sizeof(c.Size));
    memset(c.Float, 0, sizeof(c.Float));
    memset(c.WideOut, 0, sizeof(c.WideOut));
    gen_scanf(&c, Got);
    memset(Got, 0xCC, sizeof(Got));
    memset(Want, 0xCC, sizeof(Want));
    g = vsscanf(c.Input, c.Format, (va_list)c.Slots);
    for (int i = 0; i < c.Count; i++)
        c.Slots[i] = (uintptr_t)Want[i];
    w = RefVsscanf(0, c.Input, (size_t)-1, c.Format, NULL, (va_list)c.Slots);
    compare_scanf("vsscanf", &c, g, w, Got, Want, 1);

    /* The same in the wide scanf of msvcrt, the bytes as U+0000..U+00FF */
    for (int i = 0; i == 0 || c.Format[i - 1] != '\0'; i++)
        Format[i] = (unsigned char)c.Format[i];
    for (int i = 0; i == 0 || c.Input[i - 1] != '\0'; i++)
        Input[i] = (unsigned char)c.Input[i];
    memset(Got, 0xCC, sizeof(Got));
    memset(Want, 0xCC, sizeof(Want));
    for (int i = 0; i < c.Count; i++)
        c.Slots[i] = (uintptr_t)Got[i];
    g = __imp_vswscanf(Input, Format, (va_list)c.Slots);
    for (int i = 0; i < c.Count; i++)
        c.Slots[i] = (uintptr_t)Want[i];
    w = RefVswscanf(SCANF_LEGACY_WIDE_SPECIFIERS, Input, (size_t)-1, Format, NULL, (va_list)c.Slots);
    compare_scanf("vswscanf (msvcrt)", &c, g, w, Got, Want, 0);
}

static void __cdecl ignore_invalid_parameter(const wchar_t* e, const wchar_t* f, const wchar_t* file,
                                             unsigned int line, uintptr_t r)
{
    (void)e, (void)f, (void)file, (void)line, (void)r;
}

int main(int argc, char** argv)
{
    unsigned long Iterations = argc > 1 ? strtoul(argv[1], NULL, 0) : 20000;
    HMODULE Ucrt = LoadLibraryW(L"ucrtbase.dll");
    PINVALID_PARAMETER(__cdecl * SetHandler)(PINVALID_PARAMETER);

    g_Rng = argc > 2 ? strtoull(argv[2], NULL, 0) : 0x9E3779B97F4A7C15ull;
    if (Ucrt == NULL)
    {
        printf("ucrtbase.dll is not found\n");
        return 2;
    }
#define REF(Var, Name) *(FARPROC*)&Var = GetProcAddress(Ucrt, Name)
    REF(RefVsprintf, "__stdio_common_vsprintf");
    REF(RefVswprintf, "__stdio_common_vswprintf");
    REF(RefVsscanf, "__stdio_common_vsscanf");
    REF(RefVswscanf, "__stdio_common_vswscanf");
    REF(RefErrno, "_errno");
    REF(RefSetlocale, "setlocale");
    REF(RefStrtod, "strtod");
    REF(RefWcstod, "wcstod");
    REF(RefStrtof, "strtof");
    REF(RefStrtol, "strtol");
    REF(RefStrtoul, "strtoul");
    REF(RefStrtoll, "strtoll");
    REF(RefStrtoull, "strtoull");
    REF(RefWcstol, "wcstol");
    REF(RefWcstoull, "wcstoull");
    REF(SetHandler, "_set_invalid_parameter_handler");
    SetHandler(ignore_invalid_parameter);

    check_strtol_space();
    for (g_Iteration = 0; g_Iteration < Iterations && g_Failures < 30; g_Iteration++)
    {
        g_Utf8 = g_Iteration % 4 == 3;
        setlocale(LC_ALL, g_Utf8 ? "C.UTF-8" : "C");
        RefSetlocale(LC_ALL, g_Utf8 ? ".UTF-8" : "C");
        fuzz_printf();
        if (!g_Utf8)
        {
            check_strtod();
            check_strtol();
            check_scanf();
        }
    }
    printf("format: %lu iterations, %lu checks, %lu failures\n", g_Iteration, g_Checks, g_Failures);
    return g_Failures != 0;
}
