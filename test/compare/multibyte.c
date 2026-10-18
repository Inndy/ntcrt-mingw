#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <errno.h>
#include <ctype.h>
#include <mbctype.h>
#include <uchar.h>
#ifndef _MSC_VER
/* The <uchar.h> of mingw-w64 declares them for UCRT only */
size_t mbrtoc16(char16_t*, const char*, size_t, mbstate_t*);
size_t c16rtomb(char*, char16_t, mbstate_t*);
size_t mbrtoc32(char32_t*, const char*, size_t, mbstate_t*);
size_t c32rtomb(char*, char32_t, mbstate_t*);
#endif
unsigned int __cdecl ___lc_codepage_func(void);

static void ih(const wchar_t* a, const wchar_t* b, const wchar_t* c, unsigned d, uintptr_t e) {}

static void hexs(const char* s, int n)
{
    printf("[");
    for (int i = 0; i < n; i++)
        printf("%02x", (unsigned char)s[i]);
    printf("]");
}

static void hexw(const wchar_t* s, int n)
{
    printf("[");
    for (int i = 0; i < n; i++)
        printf("%04x ", s[i]);
    printf("]");
}

static const char* mbs[] = {
    "a", "\xC3\xA9", "\xE4\xB8\xAD", "\xF0\x9F\x98\x80", "\x80", "\xC3", "\xC0\x80", "\xED\xA0\x80",
    "\xE4\xB8", "\xF4\x90\x80\x80", "\xFF", "\xC3\x28", "",
};

static void conv(void)
{
    int n = sizeof(mbs) / sizeof(mbs[0]);
    for (int i = 0; i < n; i++)
    {
        const char* s = mbs[i];
        int len = (int)strlen(s);
        wchar_t w = 0xCCCC;
        printf(" mb%d", i);
        hexs(s, len);
        errno = 0;
        int r = mblen(s, len + 1);
        printf(" mblen=%d/%d", r, errno);
        errno = 0;
        r = mbtowc(&w, s, len + 1);
        printf(" mbtowc=%d/%d w=%04x", r, errno, w);
        errno = 0;
        r = mbtowc(&w, s, 1);
        printf(" mbtowc1=%d/%d", r, errno);
        mbstate_t st = { 0 };
        wchar_t ws[8];
        errno = 0;
        size_t q = mbrtowc(ws, s, len + 1, &st);
        printf(" mbrtowc=%d/%d w=%04x", (int)q, errno, ws[0]);
        if (q != (size_t)-1 && q != (size_t)-2)
        {
            errno = 0;
            q = mbrtowc(ws, s + q, len + 1 - q, &st);
            printf(" next=%d/%d w=%04x", (int)q, errno, ws[0]);
        }
        memset(&st, 0, sizeof(st));
        errno = 0;
        q = mbrtowc(ws, s, 1, &st);
        printf(" byte1=%d/%d", (int)q, errno);
        if (q == (size_t)-2 && len > 1)
        {
            errno = 0;
            q = mbrtowc(ws, s + 1, len, &st);
            printf(" rest=%d/%d w=%04x", (int)q, errno, ws[0]);
        }
        errno = 0;
        size_t m = mbstowcs(ws, s, 8);
        printf(" mbstowcs=%d/%d", (int)m, errno);
        if (m != (size_t)-1)
            hexw(ws, (int)m);
        errno = 0;
        m = mbstowcs(NULL, s, 0);
        printf(" mbstowcs0=%d/%d", (int)m, errno);
        char16_t c16 = 0xCCCC;
        memset(&st, 0, sizeof(st));
        errno = 0;
        q = mbrtoc16(&c16, s, len + 1, &st);
        printf(" c16=%d/%d %04x", (int)q, errno, c16);
        if (q != (size_t)-1 && q != (size_t)-2)
        {
            errno = 0;
            q = mbrtoc16(&c16, s + q, len + 1 - q, &st);
            printf(" next=%d/%d %04x", (int)q, errno, c16);
        }
        char32_t c32 = 0xCCCC;
        memset(&st, 0, sizeof(st));
        errno = 0;
        q = mbrtoc32(&c32, s, len + 1, &st);
        printf(" c32=%d/%d %x", (int)q, errno, c32);
        printf(" btowc=%x\n", btowc((unsigned char)s[0]));
    }
    wchar_t wcs[] = { 0x61, 0xE9, 0x4E2D, 0xD83D, 0xDE00, 0xFFFF, 0x7F };
    for (int i = 0; i < 7; i++)
    {
        char b[16] = { 0 };
        errno = 0;
        int r = wctomb(b, wcs[i]);
        printf(" wc%04x wctomb=%d/%d", wcs[i], r, errno);
        hexs(b, r > 0 ? r : 0);
        mbstate_t st = { 0 };
        errno = 0;
        size_t q = wcrtomb(b, wcs[i], &st);
        printf(" wcrtomb=%d/%d", (int)q, errno);
        hexs(b, q != (size_t)-1 ? (int)q : 0);
        errno = 0;
        q = c16rtomb(b, wcs[i], &st);
        printf(" c16rtomb=%d/%d", (int)q, errno);
        hexs(b, q != (size_t)-1 ? (int)q : 0);
        printf(" wctob=%d\n", wctob(wcs[i]));
    }
    {
        mbstate_t st = { 0 };
        char b[16];
        errno = 0;
        size_t q1 = wcrtomb(b, 0xD83D, &st);
        size_t q2 = wcrtomb(b + (q1 == (size_t)-1 ? 0 : q1), 0xDE00, &st);
        printf(" wcrtomb pair=%d,%d/%d", (int)q1, (int)q2, errno);
        hexs(b, (int)((q1 == (size_t)-1 ? 0 : q1) + (q2 == (size_t)-1 ? 0 : q2)));
        memset(&st, 0, sizeof(st));
        errno = 0;
        q1 = c16rtomb(b, 0xD83D, &st);
        q2 = c16rtomb(b + (q1 == (size_t)-1 ? 0 : q1), 0xDE00, &st);
        printf(" c16rtomb pair=%d,%d/%d", (int)q1, (int)q2, errno);
        hexs(b, (int)((q1 == (size_t)-1 ? 0 : q1) + (q2 == (size_t)-1 ? 0 : q2)));
        memset(&st, 0, sizeof(st));
        errno = 0;
        q1 = c32rtomb(b, 0x1F600, &st);
        printf(" c32rtomb=%d/%d", (int)q1, errno);
        hexs(b, q1 == (size_t)-1 ? 0 : (int)q1);
        errno = 0;
        q1 = c32rtomb(b, 0xD800, &st);
        printf(" c32rtomb(d800)=%d/%d\n", (int)q1, errno);
    }
    {
        const wchar_t* ws[] = { L"a\x4e2d\xd83d\xde00", L"\xd83d", L"\xde00z", L"\xe9" };
        for (int i = 0; i < 4; i++)
        {
            char b[32] = { 0 };
            errno = 0;
            size_t q = wcstombs(b, ws[i], sizeof(b));
            printf(" wcstombs%d=%d/%d", i, (int)q, errno);
            hexs(b, q == (size_t)-1 ? 0 : (int)q);
            errno = 0;
            q = wcstombs(NULL, ws[i], 0);
            printf(" len=%d/%d", (int)q, errno);
            errno = 0;
            q = wcstombs(b, ws[i], 3);
            printf(" n3=%d/%d", (int)q, errno);
            hexs(b, q == (size_t)-1 ? 0 : (int)q);
            const wchar_t* p = ws[i];
            mbstate_t st = { 0 };
            errno = 0;
            q = wcsrtombs(b, &p, sizeof(b), &st);
            printf(" wcsrtombs=%d/%d p=%d\n", (int)q, errno, p ? (int)(p - ws[i]) : -1);
        }
        const char* ms[] = { "a\xe4\xb8\xad\xf0\x9f\x98\x80", "a\x80z", "ab\xe4\xb8" };
        for (int i = 0; i < 3; i++)
        {
            wchar_t w[16] = { 0 };
            const char* p = ms[i];
            mbstate_t st = { 0 };
            errno = 0;
            size_t q = mbsrtowcs(w, &p, 16, &st);
            printf(" mbsrtowcs%d=%d/%d p=%d", i, (int)q, errno, p ? (int)(p - ms[i]) : -1);
            hexw(w, q == (size_t)-1 ? 0 : (int)q);
            p = ms[i];
            memset(&st, 0, sizeof(st));
            errno = 0;
            q = mbsrtowcs(w, &p, 2, &st);
            printf(" n2=%d/%d p=%d", (int)q, errno, p ? (int)(p - ms[i]) : -1);
            errno = 0;
            q = mbrlen(ms[i] + 1, 3, &st);
            printf(" mbrlen=%d/%d\n", (int)q, errno);
        }
    }
}

static void fmt(void)
{
    char b[64];
    wchar_t w[64];
    int r;
    errno = 0;
    r = snprintf(b, sizeof(b), "<%ls>", L"a\x4e2d\xd83d\xde00");
    printf(" %%ls=%d/%d", r, errno);
    hexs(b, r > 0 ? r : 0);
    errno = 0;
    r = snprintf(b, sizeof(b), "<%lc>", (wint_t)0x4e2d);
    printf(" %%lc=%d/%d", r, errno);
    hexs(b, r > 0 ? r : 0);
    errno = 0;
    r = snprintf(b, sizeof(b), "<%ls>", L"\xd83dz");
    printf(" %%ls(lone)=%d/%d", r, errno);
    hexs(b, r > 0 ? r : 0);
    errno = 0;
    r = snprintf(b, sizeof(b), "<%.4ls>", L"a\x4e2d\x4e2d");
    printf(" %%.4ls=%d/%d", r, errno);
    hexs(b, r > 0 ? r : 0);
    errno = 0;
    r = swprintf(w, 64, L"<%hs>", "a\xe4\xb8\xad\xf0\x9f\x98\x80");
    printf(" %%hs=%d/%d", r, errno);
    hexw(w, r > 0 ? r : 0);
    errno = 0;
    r = swprintf(w, 64, L"<%hs>", "a\x80z");
    printf(" %%hs(bad)=%d/%d", r, errno);
    hexw(w, r > 0 ? r : 0);
    errno = 0;
    r = swprintf(w, 64, L"<%hc>", 0xe4);
    printf(" %%hc=%d/%d", r, errno);
    hexw(w, r > 0 ? r : 0);
    printf("\n");
    wchar_t sw[16] = { 0 };
    char sb[16] = { 0 };
    errno = 0;
    r = sscanf("a\xe4\xb8\xad\xf0\x9f\x98\x80 z", "%15ls", sw);
    printf(" sscanf%%ls=%d/%d", r, errno);
    hexw(sw, (int)wcslen(sw));
    errno = 0;
    memset(sw, 0, sizeof(sw));
    r = sscanf("\xe4\xb8\xad", "%lc", sw);
    printf(" sscanf%%lc=%d/%d", r, errno);
    hexw(sw, 2);
    errno = 0;
    r = swscanf(L"a\x4e2d\xd83d\xde00 z", L"%15hs", sb);
    printf(" swscanf%%hs=%d/%d", r, errno);
    hexs(sb, (int)strlen(sb));
    errno = 0;
    memset(sb, 0, sizeof(sb));
    r = swscanf(L"\x4e2d", L"%hc", sb);
    printf(" swscanf%%hc=%d/%d", r, errno);
    hexs(sb, 4);
    printf("\n");
}

static void ct(void)
{
    printf(" isalpha(e9)=%d isleadbyte(e4)=%d _ismbblead(e4)=%d toupper(e9)=%x towupper(e9)=%x iswalpha(4e2d)=%d"
           " _getmbcp=%d cp=%u mbcur=%d\n",
           isalpha(0xe9), isleadbyte(0xe4), _ismbblead(0xe4), toupper(0xe9), towupper(0xe9), iswalpha(0x4e2d),
           _getmbcp(), ___lc_codepage_func(), (int)MB_CUR_MAX);
}

int main(int argc, char** argv)
{
    _set_invalid_parameter_handler(ih);
    const char* names[] = { "C", ".UTF-8", ".utf8", ".utf-8", "C.UTF-8", "C.utf8", ".65001", "en-US.UTF-8",
                            "en_US.UTF-8", "POSIX", "", ".UTF8", "English_United States.65001", ".1252" };
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
    {
        setlocale(LC_ALL, "C");
        char* r = setlocale(LC_ALL, names[i]);
        printf("setlocale(LC_ALL, \"%s\") = %s cp=%u mbcur=%d\n", names[i], r ? r : "(null)", ___lc_codepage_func(),
               (int)MB_CUR_MAX);
    }
    setlocale(LC_ALL, "C");
    printf("ctype only: %s", setlocale(LC_CTYPE, ".UTF-8") ? "ok" : "fail");
    printf(" all=%s\n", setlocale(LC_ALL, NULL));
    printf("numeric only: %s", setlocale(LC_NUMERIC, ".UTF-8") ? "ok" : "fail");
    printf(" all=%s\n", setlocale(LC_ALL, NULL));
    printf("back: %s\n", setlocale(LC_ALL, setlocale(LC_ALL, NULL)) ? setlocale(LC_ALL, NULL) : "(null)");
    setlocale(LC_ALL, "C");
    printf("== C\n");
    ct();
    conv();
    fmt();
    setlocale(LC_ALL, ".UTF-8");
    printf("== UTF-8\n");
    ct();
    conv();
    fmt();
    return 0;
}
