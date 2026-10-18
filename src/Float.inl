/*
 * The parts of the floating point conversions that parsing (FloatParse.cpp) and formatting (FloatFormat.cpp) share:
 * the 128-bit powers of 10 and the multiprecision decimal of Float.cpp, unrounded scaling. Ported from internal/strconv
 * of Go 1.27, see Float.cpp for its license.
 */

#pragma once

typedef struct _NTCRT_POW10
{
    ULONG64 Hi;
    ULONG64 Lo;
} NTCRT_POW10;

/* 1e-348 to 1e347, see FloatPow10.inl */
#define NTCRT_POW10_MIN (-348)
#define NTCRT_POW10_MAX 347
extern "C" const NTCRT_POW10 _NtCRT_Pow10[NTCRT_POW10_MAX - NTCRT_POW10_MIN + 1];

#pragma region Unrounded scaling (uscale.go)

/*
 * An unrounded value: the integer part shifted left by 2, bit 1 is the half and bit 0 is set if any lower bit
 * is set ("sticky").
 */
typedef ULONG64 NTCRT_UNROUNDED;

static inline ULONG64 _NtCRT_Round(NTCRT_UNROUNDED u)
{
    return (u + 1 + ((u >> 2) & 1)) >> 2;
}

/* The minimum unrounded that rounds to x */
static inline NTCRT_UNROUNDED _NtCRT_Unmin(ULONG64 x)
{
    return (x << 2) - 2;
}

static inline int _NtCRT_Len64(ULONG64 x)
{
    return x == 0 ? 0 : 64 - __builtin_clzll(x);
}

/* floor(log2(10**x)), log2 10 ~ 108853 / 2**15 */
static inline int _NtCRT_Log2Pow10(int x)
{
    return (x * 108853) >> 15;
}

typedef struct _NTCRT_SCALER
{
    ULONG64 PmHi;
    ULONG64 PmLo;
    int s;
} NTCRT_SCALER;

/* The scaling constants of e, p, lp must be _NtCRT_Log2Pow10(p) */
static inline void _NtCRT_Prescale(NTCRT_SCALER* Pre, int e, int p, int lp)
{
    Pre->PmHi = _NtCRT_Pow10[p - NTCRT_POW10_MIN].Hi;
    Pre->PmLo = _NtCRT_Pow10[p - NTCRT_POW10_MIN].Lo;
    Pre->s = -(e + lp + 3);
}

/* The unrounded x * 2**e * 10**p, x left-justified (high bit set), s < 64 */
static inline NTCRT_UNROUNDED _NtCRT_Uscale(ULONG64 x, const NTCRT_SCALER* c)
{
    unsigned __int128 Product = (unsigned __int128)x * c->PmHi;
    ULONG64 Hi = (ULONG64)(Product >> 64), Mid = (ULONG64)Product, Mid2;
    int s = c->s & 63;

    if (Hi >> s << s != Hi)
    {
        return Hi >> s | 1;
    }
    Mid2 = (ULONG64)(((unsigned __int128)x * c->PmLo) >> 64);
    Hi -= Mid < Mid2;
    return Hi >> s | (Mid - Mid2 > 1);
}

#pragma endregion

#pragma region Multiprecision decimal (decimal.go)

/*
 * The digits are in a buffer of the caller: 800 hold any double exactly (Go), 12000 any 80-bit long double, whose
 * smallest denormal 2**-16445 has 11500 significant digits.
 */
typedef struct _NTCRT_DECIMAL
{
    char* d;     /* Digits */
    int Cap;     /* Size of d */
    int nd;      /* Number of digits */
    int dp;      /* Decimal point */
    BOOL Neg;
    BOOL Trunc; /* Nonzero digits discarded beyond d[nd - 1] */
} NTCRT_DECIMAL;

#define NTCRT_DECIMAL_DIGITS 800

extern "C" void _NtCRT_DecimalTrim(NTCRT_DECIMAL* a);
/* Shift left by k bits, right if k is negative */
extern "C" void _NtCRT_DecimalShift(NTCRT_DECIMAL* a, int k);
extern "C" void _NtCRT_DecimalAssign(NTCRT_DECIMAL* a, ULONG64 v);
/* Whether a chopped at nd digits rounds up, a tie to even */
extern "C" BOOL _NtCRT_DecimalShouldRoundUp(const NTCRT_DECIMAL* a, int nd);
/* Round a to nd digits */
extern "C" void _NtCRT_DecimalRound(NTCRT_DECIMAL* a, int nd);
/* The integer part, rounded */
extern "C" ULONG64 _NtCRT_DecimalRoundedInteger(const NTCRT_DECIMAL* a);

#pragma endregion
