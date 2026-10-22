/*
 * The math functions return a value of their type (C11 F.6), on x86 too, where the x87 computes at the precision of
 * long double (src/MathX87.cpp). C (FLT_EVAL_METHOD 2, -fexcess-precision=standard) takes the result of a call as
 * rounded: each one is compared with the value of its type by the pointer of the function (no builtin), and the raw
 * st(0) by a pointer that returns a long double, which x86 returns in st(0) as float and double.
 */

#include <math.h>
#include <stdio.h>

static int g_Failures;

#define CHECK_ROUNDED(Expr, Want)                                                                     \
    do                                                                                                \
    {                                                                                                 \
        if ((Expr) != (Want))                                                                         \
        {                                                                                             \
            printf("FAIL %s: %a want %a\n", #Expr, (double)(Expr), (double)(Want));                  \
            g_Failures++;                                                                             \
        }                                                                                             \
    } while (0)

/* The raw return of a function of 1 to 3 arguments of type T, which must be a value of T */
#define CHECK_RAW(F, T, Params, ...)                                                                  \
    do                                                                                                \
    {                                                                                                 \
        long double(*volatile Raw) Params = (long double(*) Params)(void (*)(void))F;                 \
        long double v = Raw(__VA_ARGS__);                                                             \
        if (v == v && v != (long double)(T)v)                                                         \
        {                                                                                             \
            printf("FAIL %s returns %La, not a %s\n", #F, v, #T);                                     \
            g_Failures++;                                                                             \
        }                                                                                             \
    } while (0)
#define CHECK_RAW1(F, T, x) CHECK_RAW(F, T, (T), x)
#define CHECK_RAW2(F, T, x, y) CHECK_RAW(F, T, (T, T), x, y)
#define CHECK_RAW3(F, T, x, y, z) CHECK_RAW(F, T, (T, T, T), x, y, z)

int main(void)
{
    double (*volatile Sqrt)(double) = sqrt;
    float (*volatile Sqrtf)(float) = sqrtf;
    double (*volatile Pow)(double, double) = pow;
    double (*volatile Log10)(double) = log10;

    CHECK_ROUNDED(Sqrt(2.0), 0x1.6a09e667f3bcdp+0);
    CHECK_ROUNDED(Sqrtf(2.0f), 0x1.6a09e6p+0f);
    CHECK_ROUNDED(Pow(2.0, 0.5), 0x1.6a09e667f3bcdp+0);
    CHECK_ROUNDED(Log10(2.0), 0x1.34413509f79ffp-2);

#ifdef __i386__
    CHECK_RAW1(acos, double, 0.3);
    CHECK_RAW1(acosh, double, 3.3);
    CHECK_RAW1(asin, double, 0.3);
    CHECK_RAW2(atan2, double, 0.3, 1.7);
    CHECK_RAW1(cbrt, double, 3.3);
    CHECK_RAW1(erf, double, 0.3);
    CHECK_RAW1(erfc, double, 0.3);
    CHECK_RAW1(exp2, double, 0.3);
    CHECK_RAW1(expm1, double, 0.3);
    CHECK_RAW2(fdim, double, 3.3, 0.7);
    CHECK_RAW3(fma, double, 3.3, 0.7, 0.1);
    CHECK_RAW1(lgamma, double, 3.3);
    CHECK_RAW1(log10, double, 3.3);
    CHECK_RAW1(log1p, double, 0.3);
    CHECK_RAW1(log2, double, 3.3);
    CHECK_RAW2(pow, double, 3.0, 0.5);
    CHECK_RAW1(sqrt, double, 3.0);
    CHECK_RAW1(tgamma, double, 3.3);
    CHECK_RAW1(acosf, float, 0.3f);
    CHECK_RAW1(acoshf, float, 3.3f);
    CHECK_RAW1(asinf, float, 0.3f);
    CHECK_RAW2(atan2f, float, 0.3f, 1.7f);
    CHECK_RAW1(atanf, float, 0.3f);
    CHECK_RAW1(erfcf, float, 0.3f);
    CHECK_RAW1(erff, float, 0.3f);
    CHECK_RAW1(exp2f, float, 0.3f);
    CHECK_RAW1(expm1f, float, 0.3f);
    CHECK_RAW2(fdimf, float, 3.3f, 0.7f);
    CHECK_RAW3(fmaf, float, 3.3f, 0.7f, 0.1f);
    CHECK_RAW1(lgammaf, float, 3.3f);
    CHECK_RAW1(log1pf, float, 0.3f);
    CHECK_RAW1(log2f, float, 3.3f);
    CHECK_RAW1(sqrtf, float, 3.0f);
#endif

    printf("math: %d failures\n", g_Failures);
    return g_Failures != 0;
}
