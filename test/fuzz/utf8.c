/*
 * Differential fuzz of the UTF-8 codec of the "C.UTF-8" locale. MultiByteToWideChar and WideCharToMultiByte of
 * kernel32.dll are the reference, the polyfills of them in NtCRT are compared too.
 *
 *   utf8.exe [iterations] [seed]
 */
#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <wchar.h>
#include <windows.h>

/* The <uchar.h> of mingw-w64 declares them for UCRT only */
size_t mbrtoc16(char16_t*, const char*, size_t, mbstate_t*);
size_t c16rtomb(char*, char16_t, mbstate_t*);
size_t mbrtoc32(char32_t*, const char*, size_t, mbstate_t*);
size_t c32rtomb(char*, char32_t, mbstate_t*);

#define MAX_BYTES 32
#define MAX_UNITS 24
#define ERR ((size_t)-1)
#define INCOMPLETE ((size_t)-2)
#define PENDING ((size_t)-3)

static int (WINAPI* RefMbToWc)(UINT, DWORD, LPCCH, int, LPWSTR, int);
static int (WINAPI* RefWcToMb)(UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL);

static uint64_t g_Rng;
static unsigned long g_Iteration, g_Failures;
static const char* g_Input;
static int g_InputBytes;
static const wchar_t* g_InputWide;

static uint32_t rnd(uint32_t n)
{
    g_Rng ^= g_Rng << 13;
    g_Rng ^= g_Rng >> 7;
    g_Rng ^= g_Rng << 17;
    return (uint32_t)(g_Rng >> 32) % n;
}

static void hexb(const char* s, int n)
{
    for (int i = 0; i < n; i++)
        printf("%02x", (unsigned char)s[i]);
}

static void hexw(const wchar_t* s, int n)
{
    for (int i = 0; i < n; i++)
        printf("%s%04x", i ? " " : "", s[i]);
}

static void fail(const char* What, const char* Format, ...)
{
    va_list Args;

    if (g_Failures++ >= 20)
        return;
    printf("#%lu %s: ", g_Iteration, What);
    va_start(Args, Format);
    vprintf(Format, Args);
    va_end(Args);
    printf(" (input ");
    if (g_InputWide != NULL)
        hexw(g_InputWide, (int)wcslen(g_InputWide));
    else
        hexb(g_Input, g_InputBytes);
    printf(")\n");
}

#define FAIL(...)                    \
    do                               \
    {                                \
        fail(What, __VA_ARGS__);     \
        return;                      \
    } while (0)

static int is_high(unsigned int u)
{
    return u >= 0xD800 && u <= 0xDBFF;
}

static int is_low(unsigned int u)
{
    return u >= 0xDC00 && u <= 0xDFFF;
}

#pragma region Multibyte to wide

/* Reference decode of n bytes, -1 if invalid */
static int ref_decode(const char* s, int n, wchar_t* w, int Size)
{
    int r;

    if (n == 0)
        return 0;
    r = RefMbToWc(CP_UTF8, MB_ERR_INVALID_CHARS, s, n, w, Size);
    return r > 0 ? r : -1;
}

/* Whether 1 - 4 bytes are a valid character or the start of one */
static int extendable(const char* Tail, int n)
{
    static const unsigned char Cont[] = { 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF };
    char b[4];
    wchar_t w[4];

    if (ref_decode(Tail, n, w, 4) > 0)
        return 1;
    if (n >= 4)
        return 0;
    memcpy(b, Tail, n);
    for (int i = 0; i < (int)sizeof(Cont); i++)
    {
        b[n] = (char)Cont[i];
        if (extendable(b, n + 1))
            return 1;
    }
    return 0;
}

/*
 * A byte string by the reference: its characters up to the first encoding error, where the error is detected
 * and where the invalid character starts. An incomplete character at the end is an error at the null byte.
 */
typedef struct
{
    const char* s;
    int n;
    int Chars;
    int ByteAt[MAX_BYTES + 1]; /* the first byte of each character, then the end */
    int UnitAt[MAX_BYTES + 1];
    wchar_t Units[MAX_BYTES * 2];
    int ErrorByte; /* -1 if valid */
    int ErrorStart;
} MBMODEL;

static void mb_model(MBMODEL* m, const char* s, int n)
{
    int b = 0;

    memset(m, 0, sizeof(*m));
    m->s = s;
    m->n = n;
    m->ErrorByte = -1;
    for (int i = 0; i <= n; i++)
    {
        wchar_t w[4];
        int u;

        if (i == n)
        {
            if (b != n)
            {
                m->ErrorByte = n;
                m->ErrorStart = b;
            }
            break;
        }
        if (!extendable(s + b, i - b + 1))
        {
            m->ErrorByte = i;
            m->ErrorStart = b;
            break;
        }
        u = ref_decode(s + b, i - b + 1, w, 4);
        if (u > 0)
        {
            memcpy(m->Units + m->UnitAt[m->Chars], w, u * sizeof(wchar_t));
            m->Chars++;
            m->ByteAt[m->Chars] = i + 1;
            m->UnitAt[m->Chars] = m->UnitAt[m->Chars - 1] + u;
            b = i + 1;
        }
    }
}

static int at_boundary(const MBMODEL* m, int p)
{
    for (int i = 0; i <= m->Chars; i++)
        if (m->ByteAt[i] == p)
            return 1;
    return 0;
}

/* Byte strings biased to UTF-8 boundaries, no null byte */
static int gen_bytes(char* s)
{
    static const unsigned char Edge[] = { 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF, 0xE0,
                                          0xED, 0xEE, 0xEF, 0xF0, 0xF3, 0xF4, 0xF5, 0xF7, 0xF8, 0xFE, 0xFF };
    static const uint32_t Points[] = { 0x7F, 0x80, 0x7FF, 0x800, 0xD7FF, 0xD800, 0xDBFF, 0xDC00, 0xDFFF,
                                       0xE000, 0xFFFD, 0xFFFF, 0x10000, 0x10FFFF, 0x110000, 0x1FFFFF };
    int n = 0, Target = (int)rnd(MAX_BYTES - 4) + 1;

    while (n < Target)
    {
        uint32_t c;
        int k = (int)rnd(10), Length, Drop = 0;
        char e[4];

        if (k < 2)
        {
            s[n++] = (char)(rnd(255) + 1);
            continue;
        } else if (k < 4)
        {
            s[n++] = (char)Edge[rnd(sizeof(Edge))];
            continue;
        }
        /* A code point of a random range, encoded by the plain algorithm, which allows invalid ones */
        switch (rnd(6))
        {
        case 0: c = rnd(0x7F) + 1; break;
        case 1: c = rnd(0x800); break;
        case 2: c = rnd(0x10000); break;
        case 3: c = 0x10000 + rnd(0x100000); break;
        case 4: c = Points[rnd(sizeof(Points) / sizeof(Points[0]))]; break;
        default: c = rnd(0x200000); break;
        }
        Length = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (rnd(8) == 0 && Length < 4)
            Length++; /* overlong */
        if (rnd(6) == 0)
            Drop = (int)rnd(Length); /* truncated */
        if (Length == 1)
        {
            if (c != 0)
                s[n++] = (char)c;
            continue;
        }
        for (int i = Length - 1; i > 0; i--, c >>= 6)
            e[i] = (char)(0x80 | (c & 0x3F));
        e[0] = (char)((0xF00 >> Length) | c);
        for (int i = 0; i < Length - Drop && n < MAX_BYTES - 1; i++)
            s[n++] = e[i];
    }
    s[n] = '\0';
    return n;
}

enum
{
    F_MBRTOWC,
    F_MBRLEN,
    F_MBRTOC16,
    F_MBRTOC32,
    F_MBTOWC,
};

/*
 * Convert the string by mbrtowc, mbrlen, mbrtoc16, mbrtoc32 or mbtowc in random chunks (one call with the rest
 * for mbtowc, which cannot return an incomplete character). The units before the error must be those of the
 * reference, and the error must be found in the chunk with the byte the reference fails at.
 */
static void check_mb_stream(const MBMODEL* m, int Func, int MaxChunk)
{
    static const char* Names[] = { "mbrtowc", "mbrlen", "mbrtoc16", "mbrtoc32", "mbtowc" };
    const char* What = Names[Func];
    wchar_t Out[MAX_BYTES * 2];
    char32_t Points[MAX_BYTES];
    int Units = 0, Count = 0, p = 0, Failed = 0, Expect;
    mbstate_t st = 0;

    if (Func == F_MBTOWC)
        mbtowc(NULL, NULL, 0);
    for (;;)
    {
        int n = Func == F_MBTOWC ? m->n + 1 - p : (int)rnd(MaxChunk) + 1;
        size_t r;
        wchar_t wc = 0xCCCC;
        char16_t c16 = 0xCCCC;
        char32_t c32 = 0xCCCCCCCC;

        if (p + n > m->n + 1)
            n = m->n + 1 - p;
        errno = 0;
        switch (Func)
        {
        case F_MBRTOWC: r = mbrtowc(&wc, m->s + p, n, &st); break;
        case F_MBRLEN: r = mbrlen(m->s + p, n, &st); break;
        case F_MBRTOC16: r = mbrtoc16(&c16, m->s + p, n, &st); wc = c16; break;
        case F_MBRTOC32: r = mbrtoc32(&c32, m->s + p, n, &st); break;
        default:
        {
            int i = mbtowc(&wc, m->s + p, n);

            r = i == -1 ? ERR : (size_t)i;
            break;
        }
        }
        if (r == ERR)
        {
            if (errno != EILSEQ)
                FAIL("errno %d", errno);
            if (m->ErrorByte < p || m->ErrorByte >= p + n)
                FAIL("error in bytes %d+%d, the reference fails at byte %d", p, n, m->ErrorByte);
            Failed = 1;
            break;
        } else if (r == INCOMPLETE)
        {
            if (Func == F_MBTOWC)
                FAIL("returned -2");
            p += n;
            if (p > m->n)
                FAIL("incomplete after the null byte");
            continue;
        } else if (r == PENDING)
        {
            if (Func != F_MBRTOC16 || Units == 0 || !is_high(Out[Units - 1]))
                FAIL("returned -3");
            Out[Units++] = wc;
            continue;
        } else if (r > (size_t)n)
        {
            FAIL("returned %d for %d bytes", (int)r, n);
        } else if (r == 0)
        {
            if (p != m->n || (Func == F_MBRTOC32 ? c32 != 0 : Func != F_MBRLEN && wc != 0))
                FAIL("returned 0 in bytes %d+%d", p, n);
            if (Func != F_MBTOWC && !mbsinit(&st))
                FAIL("state not initial after the null character");
            break;
        }
        if (Func == F_MBRTOC32)
            Points[Count++] = c32;
        else if (Func == F_MBRLEN)
            Units++;
        else
            Out[Units++] = wc;
        p += (int)r;
        if (Func != F_MBTOWC && Func != F_MBRLEN && !(Func != F_MBRTOC32 && is_high(wc)) &&
            at_boundary(m, p) != mbsinit(&st))
            FAIL("mbsinit is %d at byte %d", mbsinit(&st), p);
    }
    if (!Failed && m->ErrorByte >= 0)
        FAIL("no error, the reference fails at byte %d", m->ErrorByte);
    if (Func == F_MBRTOC32)
    {
        if (Count != m->Chars)
            FAIL("%d characters, expected %d", Count, m->Chars);
        for (int i = 0; i < Count; i++)
        {
            const wchar_t* u = m->Units + m->UnitAt[i];
            char32_t c = m->UnitAt[i + 1] - m->UnitAt[i] == 2 ? 0x10000 + ((u[0] - 0xD800) << 10) + (u[1] - 0xDC00)
                                                              : u[0];

            if (Points[i] != c)
                FAIL("character %d is %x, expected %x", i, (unsigned)Points[i], (unsigned)c);
        }
        return;
    }
    Expect = m->UnitAt[m->Chars];
    /* mbrtowc, mbrlen and mbtowc return the high surrogate before the 4th byte of a character is checked */
    if (Failed && m->ErrorByte - m->ErrorStart == 3 && Func != F_MBRTOC16)
    {
        if (Units != Expect + 1 || (Func != F_MBRLEN && !is_high(Out[Expect])))
            FAIL("%d units, expected %d and a high surrogate", Units, Expect);
        Units--;
    }
    if (Units != Expect)
        FAIL("%d units, expected %d", Units, Expect);
    if (Func != F_MBRLEN && memcmp(Out, m->Units, Expect * sizeof(wchar_t)) != 0)
        FAIL("different units");
}

/* mbstowcs and mbsrtowcs to a buffer of Size units (NULL if Size < 0) */
static void check_mbs(const MBMODEL* m, int Size, int Restartable)
{
    const char* What = Restartable ? "mbsrtowcs" : "mbstowcs";
    wchar_t Out[MAX_BYTES * 2 + 1];
    const char* Src = m->s;
    size_t r, Expect;
    int k = 0, Stop = -1, NulWritten = 0;
    mbstate_t st = 0;

    for (int i = 0; i <= MAX_BYTES * 2; i++)
        Out[i] = 0xCCCC;
    errno = 0;
    if (Restartable)
        r = mbsrtowcs(Size < 0 ? NULL : Out, &Src, Size < 0 ? 0 : Size, &st);
    else
        r = mbstowcs(Size < 0 ? NULL : Out, m->s, Size < 0 ? 0 : Size);
    /* The characters that fit */
    if (Size >= 0)
        for (; k < m->Chars && m->UnitAt[k + 1] <= Size; k++)
            ;
    else
        k = m->Chars;
    /* Conversion stops when the buffer is full, before an encoding error */
    if (k < m->Chars || (Size >= 0 && m->UnitAt[k] == Size))
    {
        Expect = m->UnitAt[k];
        Stop = m->ByteAt[k];
    } else if (m->ErrorByte >= 0)
    {
        Expect = ERR;
        Stop = m->ErrorStart;
    } else
    {
        Expect = m->UnitAt[k];
        NulWritten = 1;
    }
    if (r != Expect)
        FAIL("size %d returned %d, expected %d", Size, (int)r, (int)Expect);
    if (r == ERR && errno != EILSEQ)
        FAIL("errno %d", errno);
    if (Size < 0)
        return;
    if (r != ERR && memcmp(Out, m->Units, r * sizeof(wchar_t)) != 0)
        FAIL("size %d: different units", Size);
    if (r != ERR && NulWritten != (Out[r] == 0))
        FAIL("size %d: null character %s", Size, NulWritten ? "missing" : "written");
    if (r != ERR && Out[r + NulWritten] != 0xCCCC && (int)(r + NulWritten) < Size)
        FAIL("size %d: written beyond the result", Size);
    if (Restartable && (Stop < 0 ? Src != NULL : Src != m->s + Stop))
        FAIL("size %d: src at %d, expected %d", Size, Src ? (int)(Src - m->s) : -1, Stop);
}

/* mbrtowc up to a byte, then mbsrtowcs with the state for the rest */
static void check_mb_handover(const MBMODEL* m)
{
    const char* What = "mbrtowc+mbsrtowcs";
    wchar_t Out[MAX_BYTES * 2 + 1];
    int q = (int)rnd(m->n + 1), Units = 0;
    const char* Src = m->s + q;
    mbstate_t st = 0;
    size_t r;

    if (m->ErrorByte >= 0 && m->ErrorByte < q)
        return;
    for (int p = 0; p < q;)
    {
        r = mbrtowc(Out + Units, m->s + p, 1, &st);
        if (r == ERR)
            FAIL("error at byte %d", p);
        if (r != INCOMPLETE)
            Units++;
        p++;
    }
    r = mbsrtowcs(Out + Units, &Src, MAX_BYTES * 2 + 1 - Units, &st);
    if (m->ErrorByte >= 0)
    {
        if (r != ERR)
            FAIL("split at %d returned %d, expected an error", q, (int)r);
        return;
    }
    if (r == ERR)
        FAIL("split at %d failed", q);
    Units += (int)r;
    if (Units != m->UnitAt[m->Chars] || memcmp(Out, m->Units, Units * sizeof(wchar_t)) != 0 || Src != NULL)
        FAIL("split at %d: different units", q);
}

/* The polyfills against kernel32 */
static void check_mb_polyfill(const MBMODEL* m)
{
    const char* What = "MultiByteToWideChar";
    wchar_t a[MAX_BYTES * 2], b[MAX_BYTES * 2];

    for (int Flags = 0; Flags <= MB_ERR_INVALID_CHARS; Flags += MB_ERR_INVALID_CHARS)
    {
        int n = rnd(4) == 0 ? -1 : m->n;
        int ra, rb;

        SetLastError(0);
        ra = RefMbToWc(CP_UTF8, Flags, m->s, n, a, MAX_BYTES * 2);
        DWORD ea = GetLastError();
        SetLastError(0);
        rb = MultiByteToWideChar(CP_UTF8, Flags, m->s, n, b, MAX_BYTES * 2);
        DWORD eb = GetLastError();
        if (ra != rb || (ra == 0 && ea != eb) || memcmp(a, b, ra * sizeof(wchar_t)) != 0)
            FAIL("flags %x length %d: returned %d (%lu), kernel32 %d (%lu)", Flags, n, rb, eb, ra, ea);
        if (RefMbToWc(CP_UTF8, Flags, m->s, n, NULL, 0) != MultiByteToWideChar(CP_UTF8, Flags, m->s, n, NULL, 0))
            FAIL("flags %x length %d: different length", Flags, n);
    }
}

#pragma endregion

#pragma region Wide to multibyte

/* A wide string by the reference: its characters (a unit, a pair or an invalid surrogate) and their UTF-8 */
typedef struct
{
    const wchar_t* w;
    int n;
    int Chars;
    int UnitAt[MAX_UNITS + 1];
    int ByteAt[MAX_UNITS + 1];
    int Invalid; /* index of the first invalid character, -1 if none */
    char Bytes[MAX_UNITS * 3];
} WCMODEL;

static void wc_model(WCMODEL* m, const wchar_t* w, int n)
{
    memset(m, 0, sizeof(*m));
    m->w = w;
    m->n = n;
    m->Invalid = -1;
    for (int i = 0; i < n;)
    {
        int u = is_high(w[i]) && i + 1 < n && is_low(w[i + 1]) ? 2 : 1;
        int b = RefWcToMb(CP_UTF8, WC_ERR_INVALID_CHARS, w + i, u, m->Bytes + m->ByteAt[m->Chars], 4, NULL, NULL);

        if (b <= 0)
        {
            m->Invalid = m->Chars;
            break;
        }
        m->Chars++;
        i += u;
        m->UnitAt[m->Chars] = i;
        m->ByteAt[m->Chars] = m->ByteAt[m->Chars - 1] + b;
    }
}

static int gen_wide(wchar_t* w)
{
    int n = 0, Target = (int)rnd(MAX_UNITS - 2) + 1;

    while (n < Target)
    {
        uint32_t k = rnd(12), c;

        if (k < 3)
            c = rnd(0x7F) + 1;
        else if (k < 6)
            c = rnd(0xFFFF) + 1;
        else if (k < 7)
            c = 0xD800 + rnd(0x400);
        else if (k < 8)
            c = 0xDC00 + rnd(0x400);
        else
        {
            uint32_t p = 0x10000 + (rnd(4) == 0 ? (rnd(2) ? 0 : 0xFFFFF) : rnd(0x100000));

            if (n + 2 > Target)
                continue;
            w[n++] = (wchar_t)(0xD800 + ((p - 0x10000) >> 10));
            c = 0xDC00 + (p & 0x3FF);
        }
        if (c >= 0xD800 && c <= 0xDFFF && rnd(2) == 0)
            c = rnd(2) ? 0xD7FF : 0xE000;
        w[n++] = (wchar_t)c;
    }
    w[n] = 0;
    return n;
}

enum
{
    F_WCRTOMB,
    F_WCTOMB,
    F_C16RTOMB,
};

/* Convert unit by unit, the output must be the reference and the error must be at the right unit */
static void check_wc_stream(const WCMODEL* m, int Func)
{
    static const char* Names[] = { "wcrtomb", "wctomb", "c16rtomb" };
    const char* What = Names[Func];
    char Out[MAX_UNITS * 4 + 8];
    int Bytes = 0, ErrorUnit = -1, Expect;
    mbstate_t st = 0;

    if (Func == F_WCTOMB)
        wctomb(NULL, 0);
    if (m->Invalid >= 0)
    {
        int u = m->UnitAt[m->Invalid];

        /* A lone high surrogate is found invalid at the next unit */
        ErrorUnit = is_high(m->w[u]) ? u + 1 : u;
    }
    for (int i = 0; i <= m->n; i++)
    {
        size_t r;

        errno = 0;
        switch (Func)
        {
        case F_WCRTOMB: r = wcrtomb(Out + Bytes, m->w[i], &st); break;
        case F_WCTOMB:
        {
            int k = wctomb(Out + Bytes, m->w[i]);

            r = k == -1 ? ERR : (size_t)k;
            break;
        }
        default: r = c16rtomb(Out + Bytes, m->w[i], &st); break;
        }
        if (r == ERR)
        {
            if (errno != EILSEQ)
                FAIL("errno %d", errno);
            if (i != ErrorUnit)
                FAIL("error at unit %d, expected %d", i, ErrorUnit);
            break;
        }
        if (i == ErrorUnit)
            FAIL("no error at unit %d", i);
        if (r > 4 || (r == 0) != (is_high(m->w[i]) && i < m->n))
            FAIL("returned %d at unit %d", (int)r, i);
        Bytes += (int)r;
        if (Func != F_WCTOMB && mbsinit(&st) == is_high(m->w[i]))
            FAIL("mbsinit is %d after unit %d", mbsinit(&st), i);
    }
    Expect = m->ByteAt[m->Chars];
    if (ErrorUnit < 0)
    {
        if (Bytes != Expect + 1 || Out[Expect] != '\0')
            FAIL("no null byte at the end");
        Bytes--;
    }
    if (Bytes != Expect || memcmp(Out, m->Bytes, Expect) != 0)
        FAIL("%d bytes, expected %d", Bytes, Expect);
}

/* wcstombs and wcsrtombs to a buffer of Size bytes (NULL if Size < 0) */
static void check_wcs(const WCMODEL* m, int Size, int Restartable)
{
    const char* What = Restartable ? "wcsrtombs" : "wcstombs";
    char Out[MAX_UNITS * 4 + 8];
    const wchar_t* Src = m->w;
    size_t r, Expect;
    int k = 0, Stop, NulWritten = 0;
    mbstate_t st = 0;

    memset(Out, 0xCC, sizeof(Out));
    errno = 0;
    if (Restartable)
        r = wcsrtombs(Size < 0 ? NULL : Out, &Src, Size < 0 ? 0 : Size, &st);
    else
        r = wcstombs(Size < 0 ? NULL : Out, m->w, Size < 0 ? 0 : Size);
    if (Size >= 0)
        for (; k < m->Chars && m->ByteAt[k + 1] <= Size; k++)
            ;
    else
        k = m->Chars;
    if (k < m->Chars)
    {
        Expect = m->ByteAt[k];
        Stop = m->UnitAt[k];
    } else if (m->Invalid >= 0)
    {
        Expect = ERR;
        Stop = m->UnitAt[m->Invalid];
    } else
    {
        Expect = m->ByteAt[k];
        NulWritten = Size < 0 || (int)Expect < Size;
        Stop = NulWritten ? -1 : m->n;
    }
    if (r != Expect)
        FAIL("size %d returned %d, expected %d", Size, (int)r, (int)Expect);
    if (r == ERR && errno != EILSEQ)
        FAIL("errno %d", errno);
    if (Size < 0)
        return;
    if (r != ERR && memcmp(Out, m->Bytes, r) != 0)
        FAIL("size %d: different bytes", Size);
    if (r != ERR && NulWritten != (Out[r] == 0))
        FAIL("size %d: null byte %s", Size, NulWritten ? "missing" : "written");
    if (r != ERR && (unsigned char)Out[r + NulWritten] != 0xCC && (int)(r + NulWritten) < Size)
        FAIL("size %d: written beyond the result", Size);
    if (Restartable && (Stop < 0 ? Src != NULL : Src != m->w + Stop))
        FAIL("size %d: src at %d, expected %d", Size, Src ? (int)(Src - m->w) : -1, Stop);
}

static void check_c32(void)
{
    const char* What = "c32rtomb";
    static const uint32_t Edge[] = { 0, 0x7F, 0x80, 0x7FF, 0x800, 0xD7FF, 0xD800, 0xDFFF, 0xE000, 0xFFFF,
                                     0x10000, 0x10FFFF, 0x110000, 0xFFFFFFFF };
    uint32_t c = rnd(3) == 0 ? Edge[rnd(sizeof(Edge) / sizeof(Edge[0]))] : rnd(2) ? rnd(0x10000) : rnd(0x120000);
    char Out[8], Ref[8];
    wchar_t w[2];
    int u = 0, b;
    mbstate_t st = 0;
    size_t r;

    if (c < 0x10000)
        w[u++] = (wchar_t)c;
    else if (c < 0x110000)
    {
        w[u++] = (wchar_t)(0xD800 + ((c - 0x10000) >> 10));
        w[u++] = (wchar_t)(0xDC00 + (c & 0x3FF));
    }
    b = c == 0 ? 1 : u == 0 ? -1 : RefWcToMb(CP_UTF8, WC_ERR_INVALID_CHARS, w, u, Ref, 8, NULL, NULL);
    if (c == 0)
        Ref[0] = 0;
    if (b <= 0)
        b = -1;
    errno = 0;
    r = c32rtomb(Out, c, &st);
    if ((int)r != b || (b > 0 && memcmp(Out, Ref, b) != 0) || (b < 0 && errno != EILSEQ))
        FAIL("%x returned %d, expected %d", c, (int)r, b);
}

static void check_wc_polyfill(const WCMODEL* m)
{
    const char* What = "WideCharToMultiByte";
    char a[MAX_UNITS * 4], b[MAX_UNITS * 4];

    for (int Flags = 0; Flags <= WC_ERR_INVALID_CHARS; Flags += WC_ERR_INVALID_CHARS)
    {
        int n = rnd(4) == 0 ? -1 : m->n;
        int ra, rb;

        SetLastError(0);
        ra = RefWcToMb(CP_UTF8, Flags, m->w, n, a, sizeof(a), NULL, NULL);
        DWORD ea = GetLastError();
        SetLastError(0);
        rb = WideCharToMultiByte(CP_UTF8, Flags, m->w, n, b, sizeof(b), NULL, NULL);
        DWORD eb = GetLastError();
        if (ra != rb || (ra == 0 && ea != eb) || memcmp(a, b, ra) != 0)
            FAIL("flags %x length %d: returned %d (%lu), kernel32 %d (%lu)", Flags, n, rb, eb, ra, ea);
    }
}

#pragma endregion

/* In the "C" locale a byte is the character of its value */
static void check_c_locale(const char* s, int n, const wchar_t* w, int wn)
{
    const char* What = "C locale";
    wchar_t Out[MAX_BYTES + 1];
    char Bytes[MAX_UNITS + 1];
    int Over = -1;

    if (mbstowcs(Out, s, MAX_BYTES + 1) != (size_t)n)
        FAIL("mbstowcs");
    for (int i = 0; i < n; i++)
        if (Out[i] != (unsigned char)s[i] || mblen(s + i, 1) != 1)
            FAIL("byte %d", i);
    for (int i = 0; i < wn && Over < 0; i++)
        if (w[i] > 0xFF)
            Over = i;
    if (wcstombs(Bytes, w, MAX_UNITS + 1) != (Over < 0 ? (size_t)wn : ERR))
        FAIL("wcstombs");
}

int main(int argc, char** argv)
{
    unsigned long Iterations = argc > 1 ? strtoul(argv[1], NULL, 0) : 20000;
    HMODULE Kernel32 = LoadLibraryW(L"kernel32.dll");
    char s[MAX_BYTES];
    wchar_t w[MAX_UNITS];

    g_Rng = argc > 2 ? strtoull(argv[2], NULL, 0) : 0x9E3779B97F4A7C15ull;
    RefMbToWc = (int (WINAPI*)(UINT, DWORD, LPCCH, int, LPWSTR, int))(void*)GetProcAddress(Kernel32,
                                                                                         "MultiByteToWideChar");
    RefWcToMb = (int (WINAPI*)(UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL))(void*)GetProcAddress(
        Kernel32, "WideCharToMultiByte");
    if (RefMbToWc == NULL || RefWcToMb == NULL)
    {
        printf("kernel32.dll is not found\n");
        return 2;
    }
    for (g_Iteration = 0; g_Iteration < Iterations && g_Failures < 20; g_Iteration++)
    {
        MBMODEL mm;
        WCMODEL wm;
        int n = gen_bytes(s), wn = gen_wide(w);

        g_Input = s;
        g_InputBytes = n;
        g_InputWide = NULL;
        setlocale(LC_ALL, "C");
        check_c_locale(s, n, w, wn);
        setlocale(LC_ALL, "C.UTF-8");
        mb_model(&mm, s, n);
        check_mb_stream(&mm, F_MBRTOWC, 1);
        check_mb_stream(&mm, F_MBRTOWC, 6);
        check_mb_stream(&mm, F_MBRLEN, 6);
        check_mb_stream(&mm, F_MBRTOC16, 6);
        check_mb_stream(&mm, F_MBRTOC32, 6);
        check_mb_stream(&mm, F_MBTOWC, 0);
        check_mbs(&mm, -1, 0);
        check_mbs(&mm, (int)rnd(mm.UnitAt[mm.Chars] + 3), 0);
        check_mbs(&mm, -1, 1);
        check_mbs(&mm, (int)rnd(mm.UnitAt[mm.Chars] + 3), 1);
        check_mb_handover(&mm);
        check_mb_polyfill(&mm);
        if (mblen(s, n + 1) != (n == 0 ? 0 : mm.Chars > 0 ? mm.ByteAt[1] : -1))
            fail("mblen", "returned %d", mblen(s, n + 1));
        /* An incomplete character */
        if (mm.Chars > 0 && mm.ByteAt[1] > 1 && mblen(s, mm.ByteAt[1] - 1) != -1)
            fail("mblen", "returned %d for %d bytes", mblen(s, mm.ByteAt[1] - 1), mm.ByteAt[1] - 1);

        g_InputWide = w;
        wc_model(&wm, w, wn);
        check_wc_stream(&wm, F_WCRTOMB);
        check_wc_stream(&wm, F_WCTOMB);
        check_wc_stream(&wm, F_C16RTOMB);
        check_wcs(&wm, -1, 0);
        check_wcs(&wm, (int)rnd(wm.ByteAt[wm.Chars] + 4), 0);
        check_wcs(&wm, -1, 1);
        check_wcs(&wm, (int)rnd(wm.ByteAt[wm.Chars] + 4), 1);
        check_wc_polyfill(&wm);
        check_c32();
    }
    printf("utf8: %lu iterations, %lu failures\n", g_Iteration, g_Failures);
    return g_Failures != 0;
}
