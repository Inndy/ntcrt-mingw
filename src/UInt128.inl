/*
 * NTCRT_UINT128: unsigned __int128, or on x86, which has none, a class of two halves with the operators the float
 * conversions use.
 */

#pragma once

#ifdef __SIZEOF_INT128__

typedef unsigned __int128 NTCRT_UINT128;

#else

struct NTCRT_UINT128
{
    ULONG64 Lo, Hi;

    constexpr NTCRT_UINT128() : Lo(0), Hi(0) {}
    constexpr NTCRT_UINT128(ULONG64 Low) : Lo(Low), Hi(0) {}
    constexpr NTCRT_UINT128(ULONG64 High, ULONG64 Low) : Lo(Low), Hi(High) {}
    constexpr NTCRT_UINT128(unsigned Low) : Lo(Low), Hi(0) {}
    constexpr NTCRT_UINT128(int Low) : Lo((ULONG64)(LONG64)Low), Hi(Low < 0 ? ~0ull : 0) {}

    explicit constexpr operator ULONG64() const { return Lo; }
    explicit constexpr operator unsigned() const { return (unsigned)Lo; }
    explicit constexpr operator bool() const { return (Lo | Hi) != 0; }

    /* Shifts by 0 to 127 */
    friend constexpr NTCRT_UINT128 operator<<(NTCRT_UINT128 a, int n)
    {
        return n == 0 ? a : n >= 64 ? NTCRT_UINT128(a.Lo << (n - 64), 0) : NTCRT_UINT128(a.Hi << n | a.Lo >> (64 - n), a.Lo << n);
    }
    friend constexpr NTCRT_UINT128 operator>>(NTCRT_UINT128 a, int n)
    {
        return n == 0 ? a : n >= 64 ? NTCRT_UINT128(a.Hi >> (n - 64)) : NTCRT_UINT128(a.Hi >> n, a.Lo >> n | a.Hi << (64 - n));
    }
    friend constexpr NTCRT_UINT128 operator|(NTCRT_UINT128 a, NTCRT_UINT128 b) { return {a.Hi | b.Hi, a.Lo | b.Lo}; }
    friend constexpr NTCRT_UINT128 operator&(NTCRT_UINT128 a, NTCRT_UINT128 b) { return {a.Hi & b.Hi, a.Lo & b.Lo}; }
    friend constexpr NTCRT_UINT128 operator+(NTCRT_UINT128 a, NTCRT_UINT128 b)
    {
        return {a.Hi + b.Hi + (a.Lo + b.Lo < a.Lo), a.Lo + b.Lo};
    }
    friend constexpr NTCRT_UINT128 operator-(NTCRT_UINT128 a, NTCRT_UINT128 b)
    {
        return {a.Hi - b.Hi - (a.Lo < b.Lo), a.Lo - b.Lo};
    }

    /* The low 128 bits of a * b */
    friend constexpr NTCRT_UINT128 operator*(NTCRT_UINT128 a, ULONG64 b)
    {
        ULONG64 a0 = (ULONG)a.Lo, a1 = a.Lo >> 32, b0 = (ULONG)b, b1 = b >> 32;
        ULONG64 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
        ULONG64 Mid = (p00 >> 32) + (ULONG)p01 + (ULONG)p10;

        return {p11 + (p01 >> 32) + (p10 >> 32) + (Mid >> 32) + a.Hi * b, Mid << 32 | (ULONG)p00};
    }

    friend constexpr bool operator==(NTCRT_UINT128 a, NTCRT_UINT128 b) { return a.Lo == b.Lo && a.Hi == b.Hi; }
    friend constexpr bool operator<(NTCRT_UINT128 a, NTCRT_UINT128 b)
    {
        return a.Hi != b.Hi ? a.Hi < b.Hi : a.Lo < b.Lo;
    }
    friend constexpr bool operator>(NTCRT_UINT128 a, NTCRT_UINT128 b) { return b < a; }

    NTCRT_UINT128& operator|=(NTCRT_UINT128 b) { return *this = *this | b; }
    NTCRT_UINT128& operator+=(NTCRT_UINT128 b) { return *this = *this + b; }
    NTCRT_UINT128& operator<<=(int n) { return *this = *this << n; }
    NTCRT_UINT128& operator>>=(int n) { return *this = *this >> n; }
    NTCRT_UINT128& operator++() { return *this = *this + 1u; }
    NTCRT_UINT128 operator++(int)
    {
        NTCRT_UINT128 Old = *this;

        ++*this;
        return Old;
    }
};

#endif
