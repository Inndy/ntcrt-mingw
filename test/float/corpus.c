/*
 * strtod, strtof and wcstod against parse-number-fxx-test-data (https://github.com/nigeltao/parse-number-fxx-test-data),
 * lines of "f16 f32 f64 string" in hex, and printf "%.17g", "%.9g" and "%a" of each value read back to the same bits.
 * Usage: corpus.exe file...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int Failures;

static void Fail(const char* File, const char* Line, const char* What, unsigned long long Got, unsigned long long Want)
{
    if (Failures++ < 20)
        printf("%s: %s: %s = %llx, want %llx\n", File, Line, What, Got, Want);
}

static unsigned long long Bits64(double d)
{
    unsigned long long u;
    memcpy(&u, &d, sizeof(u));
    return u;
}

static unsigned Bits32(float f)
{
    unsigned u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

static wchar_t Wide[1100];

static long long CheckLine(const char* File, char* Line)
{
    char* p;
    unsigned f32 = (unsigned)strtoul(Line + 5, &p, 16);
    unsigned long long f64 = strtoull(p + 1, &p, 16);
    const char* s = p + 1;
    size_t n = strlen(s);
    char* End;
    char Buffer[64];

    double d = strtod(s, &End);
    if (Bits64(d) != f64 || End != s + n)
        Fail(File, s, "strtod", Bits64(d), f64);
    float f = strtof(s, &End);
    if (Bits32(f) != f32 || End != s + n)
        Fail(File, s, "strtof", Bits32(f), f32);
    if (n < sizeof(Wide) / sizeof(Wide[0])) {
        wchar_t* WideEnd;
        for (size_t i = 0; i <= n; i++)
            Wide[i] = (unsigned char)s[i];
        double w = wcstod(Wide, &WideEnd);
        if (Bits64(w) != f64 || WideEnd != Wide + n)
            Fail(File, s, "wcstod", Bits64(w), f64);
    }

    double v;
    memcpy(&v, &f64, sizeof(v));
    snprintf(Buffer, sizeof(Buffer), "%.17g", v);
    if (Bits64(strtod(Buffer, NULL)) != f64)
        Fail(File, Buffer, "strtod(%.17g)", Bits64(strtod(Buffer, NULL)), f64);
    snprintf(Buffer, sizeof(Buffer), "%a", v);
    if (Bits64(strtod(Buffer, NULL)) != f64)
        Fail(File, Buffer, "strtod(%a)", Bits64(strtod(Buffer, NULL)), f64);
    float g;
    memcpy(&g, &f32, sizeof(g));
    snprintf(Buffer, sizeof(Buffer), "%.9g", g);
    if (Bits32(strtof(Buffer, NULL)) != f32)
        Fail(File, Buffer, "strtof(%.9g)", Bits32(strtof(Buffer, NULL)), f32);
    return 1;
}

int main(int argc, char** argv)
{
    long long Total = 0;
    for (int i = 1; i < argc; i++) {
        FILE* fp = fopen(argv[i], "rb");
        if (fp == NULL) {
            printf("%s: cannot open\n", argv[i]);
            return 2;
        }
        const char* Name = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
        char Line[1200];
        long long Count = 0;
        while (fgets(Line, sizeof(Line), fp) != NULL) {
            size_t n = strcspn(Line, "\r\n");
            if (Line[n] == '\0' && !feof(fp)) {
                printf("%s: line too long\n", Name);
                return 2;
            }
            Line[n] = '\0';
            if (n > 31)
                Count += CheckLine(Name, Line);
        }
        fclose(fp);
        printf("%-32s %10lld lines\n", Name, Count);
        Total += Count;
    }
    printf("%lld lines, %d failures\n", Total, Failures);
    return Failures != 0;
}
