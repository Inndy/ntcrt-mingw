/*
 * The math functions return a value of their type (C11 F.6), on x86 too, where the x87 computes at the precision of
 * long double (src/MathX87.cpp). C (FLT_EVAL_METHOD 2, -fexcess-precision=standard) takes the result of a call as
 * rounded: each one is compared with the value of its type by the pointer of the function (no builtin), and the raw
 * st(0) by a pointer that returns a long double, which x86 returns in st(0) as float and double. sqrt and fdim are
 * correctly rounded too.
 */

#include <errno.h>
#include <fenv.h>
#include <float.h>
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

    /* sqrt and fdim correctly rounded (F.3), where the x87 at 64 bits and then 53 rounds twice: the results of x64 */
    {
        double (*volatile Fdim)(double, double) = fdim;
        unsigned int Control = _control87(0, 0);

        CHECK_ROUNDED(Sqrt(0x1.f4c29bf4f040dp+875), 0x1.fa5952265cf4fp+437);
        CHECK_ROUNDED(Sqrt(0x1.a9d2481df801p+811), 0x1.d2ed3da1b882bp+405);
        CHECK_ROUNDED(Sqrt(0x1.bb4143d328f9bp+971), 0x1.dc6398fb55035p+485);
        CHECK_ROUNDED(Sqrt(0x1.c13a2555ee334p-907), 0x1.df966eb417e95p-454);
        CHECK_ROUNDED(Fdim(0x1.bb31f4ec8337bp+0, 0x1.2e21fff8c1dddp-38), 0x1.bb31f4ec7e7f3p+0);
        CHECK_ROUNDED(Fdim(0x1.30b7edc196d7p+0, 0x1.f7df40d0003aap-25), 0x1.30b7ecc5a7369p+0);

        /* The precision control of the x87 back as it was; the domain error of sqrt (F.10.4.5: invalid), the exact
           and inexact results, the range error of fdim (math_errhandling: errno and the overflow) */
        errno = 0;
        feclearexcept(FE_ALL_EXCEPT);
        if (!isnan(Sqrt(-1.0)) || errno != EDOM || !fetestexcept(FE_INVALID) || _control87(0, 0) != Control)
        {
            printf("FAIL sqrt(-1): errno %d, exceptions %#x, control %#x want %#x\n", errno,
                   fetestexcept(FE_ALL_EXCEPT), _control87(0, 0), Control);
            g_Failures++;
        }
        errno = 0;
        feclearexcept(FE_ALL_EXCEPT);
        if (Sqrt(4.0) != 2.0 || Sqrt(-0.0) != 0.0 || !signbit(Sqrt(-0.0)) || !isnan(Sqrt(NAN)) || errno != 0 ||
            fetestexcept(FE_ALL_EXCEPT) || (Sqrt(2.0), fetestexcept(FE_ALL_EXCEPT) != FE_INEXACT))
        {
            printf("FAIL sqrt: errno %d, exceptions %#x\n", errno, fetestexcept(FE_ALL_EXCEPT));
            g_Failures++;
        }
        errno = 0;
        feclearexcept(FE_ALL_EXCEPT);
        if (!isinf(Fdim(DBL_MAX, -DBL_MAX)) || errno != ERANGE || !fetestexcept(FE_OVERFLOW) ||
            (errno = 0, Fdim(INFINITY, 1.0) != INFINITY) || errno != 0)
        {
            printf("FAIL fdim of an overflow: errno %d, exceptions %#x\n", errno, fetestexcept(FE_ALL_EXCEPT));
            g_Failures++;
        }
    }

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
