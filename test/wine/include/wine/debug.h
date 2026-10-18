/* The part of wine/debug.h that the Wine CRT tests use, for wine/test.h on MinGW-w64 headers */

#ifndef __WINE_WINE_DEBUG_H
#define __WINE_WINE_DEBUG_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <windows.h> /* wine/test.h sets the console mode */

#ifndef __WINE_PRINTF_ATTR
#define __WINE_PRINTF_ATTR(fmt, args) __attribute__((format(printf, fmt, args)))
#endif

/* A ring of buffers, a message may hold several strings */
static inline char *wine_dbg_buffer(void)
{
    static char buffers[16][1024];
    static unsigned int next;
    return buffers[next++ % 16];
}

static inline const char *wine_dbg_vsprintf(const char *format, va_list args)
{
    char *buffer = wine_dbg_buffer();
    vsnprintf(buffer, 1024, format, args);
    return buffer;
}

static inline const char *__WINE_PRINTF_ATTR(1, 2) wine_dbg_sprintf(const char *format, ...)
{
    va_list args;
    const char *s;
    va_start(args, format);
    s = wine_dbg_vsprintf(format, args);
    va_end(args);
    return s;
}

/* A quoted string with C escapes, cut to fit the buffer */
static inline const char *wine_dbgstr_ch(const void *str, int n, int wide)
{
    char *buffer, *p;
    int i;

    if (!str) return "(null)";
    if ((ULONG_PTR)str < 0x10000) return wine_dbg_sprintf("#%04x", (unsigned int)(ULONG_PTR)str);
    buffer = p = wine_dbg_buffer();
    if (wide) *p++ = 'L';
    *p++ = '"';
    for (i = 0; n < 0 ? (wide ? ((const WCHAR *)str)[i] : ((const char *)str)[i]) != 0 : i < n; i++)
    {
        unsigned int c = wide ? ((const WCHAR *)str)[i] : (unsigned char)((const char *)str)[i];
        if (p - buffer > 1024 - 16)
        {
            strcpy(p, "\"...");
            return buffer;
        }
        switch (c)
        {
        case '\n': *p++ = '\\'; *p++ = 'n'; break;
        case '\r': *p++ = '\\'; *p++ = 'r'; break;
        case '\t': *p++ = '\\'; *p++ = 't'; break;
        case '"': *p++ = '\\'; *p++ = '"'; break;
        case '\\': *p++ = '\\'; *p++ = '\\'; break;
        default:
            if (c >= ' ' && c < 0x7f) *p++ = (char)c;
            else p += sprintf(p, c > 0xff ? "\\u%04x" : "\\x%02x", c);
        }
    }
    *p++ = '"';
    *p = 0;
    return buffer;
}

static inline const char *wine_dbgstr_an(const char *s, int n) { return wine_dbgstr_ch(s, n, 0); }
static inline const char *wine_dbgstr_wn(const WCHAR *s, int n) { return wine_dbgstr_ch(s, n, 1); }
static inline const char *wine_dbgstr_a(const char *s) { return wine_dbgstr_ch(s, -1, 0); }
static inline const char *wine_dbgstr_w(const WCHAR *s) { return wine_dbgstr_ch(s, -1, 1); }

static inline const char *wine_dbgstr_longlong(ULONGLONG ll)
{
    if (ll >> 32) return wine_dbg_sprintf("%lx%08lx", (unsigned long)(ll >> 32), (unsigned long)ll);
    return wine_dbg_sprintf("%lx", (unsigned long)ll);
}

#define debugstr_a wine_dbgstr_a
#define debugstr_an wine_dbgstr_an
#define debugstr_w wine_dbgstr_w
#define debugstr_wn wine_dbgstr_wn

#endif
