/* msvcrt stdio: FILE streams in the layout of msvcrt _iobuf */

#include "NtCRT.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <stdio.h>
#include <wchar.h>

#define NTCRT_IOB_ENTRIES 20
#define NTCRT_NSTREAM 512
#define NTCRT_BUFSIZ 4096
#define NTCRT_SMALL_BUFSIZ 512
#define NTCRT_STREAM_LOCKS 16

/* Flags not in mingw headers */
#define NTCRT_IOYOURBUF 0x0100
#define NTCRT_IOSETVBUF 0x0400
/* Internal: stream is flushed when the outermost lock is released, for consoles and stderr */
#define NTCRT_IOFLUSHUNLOCK 0x10000
/* Internal: stream is opened by tmpfile, for _rmtmp */
#define NTCRT_IOTMPFILE 0x20000

typedef struct _NTCRT_FILEX
{
    FILE File;
    RTL_CRITICAL_SECTION Lock;
} NTCRT_FILEX, *PNTCRT_FILEX;

extern "C"
{
    FILE _NtCRT_iob[NTCRT_IOB_ENTRIES] = {
        { NULL, 0, NULL, _IOREAD, 0, 0, 0, NULL },
        { NULL, 0, NULL, _IOWRT, 1, 0, 0, NULL },
        { NULL, 0, NULL, _IOWRT, 2, 0, 0, NULL },
    };
}

static RTL_CRITICAL_SECTION g_IobLocks[NTCRT_IOB_ENTRIES];
static PNTCRT_FILEX g_Streams[NTCRT_NSTREAM - NTCRT_IOB_ENTRIES];
static volatile LONG g_StdioState;

EXTERN_C char* __cdecl _NtCRT_strerror(int errnum);
EXTERN_C int __cdecl _NtCRT__snwprintf(wchar_t* buffer, size_t count, const wchar_t* format, ...);

static
void
_NtCRT_EnsureStdio(void)
{
    if (g_StdioState == 2)
    {
        return;
    }
    if (_InterlockedCompareExchange(&g_StdioState, 1, 0) == 0)
    {
        for (int i = 0; i < NTCRT_IOB_ENTRIES; i++)
        {
            RtlInitializeCriticalSection(&g_IobLocks[i]);
        }
        _InterlockedExchange(&g_StdioState, 2);
    } else
    {
        while (g_StdioState != 2)
        {
            YieldProcessor();
        }
    }
}

static
PRTL_CRITICAL_SECTION
_NtCRT_StreamLock(
    _In_ FILE* Stream)
{
    _NtCRT_EnsureStdio();
    if (Stream >= _NtCRT_iob && Stream < _NtCRT_iob + NTCRT_IOB_ENTRIES)
    {
        return &g_IobLocks[Stream - _NtCRT_iob];
    }
    return &CONTAINING_RECORD(Stream, NTCRT_FILEX, File)->Lock;
}

static
BOOL
_NtCRT_InUse(
    _In_ FILE* Stream)
{
    return (Stream->_flag & (_IOREAD | _IOWRT | _IORW)) != 0;
}

static int _NtCRT_flush_nolock(FILE* Stream);

EXTERN_C void __cdecl _NtCRT_lock_file(FILE* stream)
{
    RtlEnterCriticalSection(_NtCRT_StreamLock(stream));
}

EXTERN_C void __cdecl _NtCRT_unlock_file(FILE* stream)
{
    PRTL_CRITICAL_SECTION Lock = _NtCRT_StreamLock(stream);

    if (Lock->RecursionCount == 1 && (stream->_flag & NTCRT_IOFLUSHUNLOCK) && (stream->_flag & _IOWRT) &&
        stream->_base != NULL && stream->_ptr > stream->_base)
    {
        _NtCRT_flush_nolock(stream);
    }
    RtlLeaveCriticalSection(Lock);
}

/* Allocate a stream, returned locked */
static
FILE*
_NtCRT_AllocStream(void)
{
    FILE* Stream = NULL;

    _NtCRT_EnsureStdio();
    _NtCRT_lock(_IOB_SCAN_LOCK);
    for (int i = 3; i < NTCRT_IOB_ENTRIES; i++)
    {
        if (!_NtCRT_InUse(&_NtCRT_iob[i]))
        {
            RtlEnterCriticalSection(&g_IobLocks[i]);
            if (_NtCRT_InUse(&_NtCRT_iob[i]))
            {
                RtlLeaveCriticalSection(&g_IobLocks[i]);
                continue;
            }
            Stream = &_NtCRT_iob[i];
            break;
        }
    }
    for (unsigned int i = 0; Stream == NULL && i < ARRAYSIZE(g_Streams); i++)
    {
        if (g_Streams[i] == NULL)
        {
            g_Streams[i] = (PNTCRT_FILEX)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, sizeof(NTCRT_FILEX));
            if (g_Streams[i] == NULL)
            {
                break;
            }
            RtlInitializeCriticalSection(&g_Streams[i]->Lock);
        }
        if (!_NtCRT_InUse(&g_Streams[i]->File))
        {
            RtlEnterCriticalSection(&g_Streams[i]->Lock);
            if (_NtCRT_InUse(&g_Streams[i]->File))
            {
                RtlLeaveCriticalSection(&g_Streams[i]->Lock);
                continue;
            }
            Stream = &g_Streams[i]->File;
        }
    }
    if (Stream != NULL)
    {
        Stream->_ptr = Stream->_base = NULL;
        Stream->_cnt = 0;
        Stream->_file = -1;
        Stream->_charbuf = 0;
        Stream->_bufsiz = 0;
        Stream->_tmpfname = NULL;
        /* Reserve the slot until the caller sets the real flags */
        Stream->_flag = _IORW;
    } else
    {
        NTCRT_ERRNO = EMFILE;
    }
    _NtCRT_unlock(_IOB_SCAN_LOCK);
    return Stream;
}

/* Allocate the buffer of a stream */
static
void
_NtCRT_getbuf(
    _In_ FILE* Stream)
{
    if (Stream->_file == 2 || _NtCRT_isatty(Stream->_file))
    {
        Stream->_flag |= NTCRT_IOFLUSHUNLOCK;
    }
    if (!(Stream->_flag & _IONBF))
    {
        Stream->_base = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, NTCRT_BUFSIZ);
        if (Stream->_base != NULL)
        {
            Stream->_flag |= _IOMYBUF;
            Stream->_bufsiz = NTCRT_BUFSIZ;
        }
    }
    if (Stream->_base == NULL)
    {
        Stream->_flag |= _IONBF;
        Stream->_base = (char*)&Stream->_charbuf;
        Stream->_bufsiz = 2;
    }
    Stream->_ptr = Stream->_base;
    Stream->_cnt = 0;
}

static
void
_NtCRT_freebuf(
    _In_ FILE* Stream)
{
    if ((Stream->_flag & _IOMYBUF) && Stream->_base != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Stream->_base);
    }
    Stream->_flag &= ~(_IOMYBUF | NTCRT_IOSETVBUF | NTCRT_IOYOURBUF);
    Stream->_ptr = Stream->_base = NULL;
    Stream->_cnt = 0;
    Stream->_bufsiz = 0;
}

/* Write the pending output, returns 0 or EOF */
static
int
_NtCRT_flush_nolock(
    FILE* Stream)
{
    int Result = 0;

    if ((Stream->_flag & (_IOREAD | _IOWRT)) == _IOWRT && Stream->_base != NULL &&
        !(Stream->_flag & _IONBF))
    {
        int Count = (int)(Stream->_ptr - Stream->_base);
        if (Count > 0)
        {
            if (_NtCRT_write(Stream->_file, Stream->_base, Count) == Count)
            {
                if (Stream->_flag & _IORW)
                {
                    Stream->_flag &= ~_IOWRT;
                }
            } else
            {
                Stream->_flag |= _IOERR;
                Result = EOF;
            }
        }
    }
    Stream->_ptr = Stream->_base;
    Stream->_cnt = 0;
    return Result;
}

EXTERN_C int __cdecl _NtCRT_filbuf(FILE* stream)
{
    int Count;

    if (!(stream->_flag & (_IOREAD | _IORW)) || (stream->_flag & _IOSTRG))
    {
        return EOF;
    }
    if (stream->_flag & _IOWRT)
    {
        stream->_flag |= _IOERR;
        return EOF;
    }
    stream->_flag |= _IOREAD;
    if (stream->_base == NULL)
    {
        _NtCRT_getbuf(stream);
    }
    Count = _NtCRT_read(stream->_file, stream->_base, stream->_bufsiz);
    stream->_ptr = stream->_base;
    if (Count <= 0)
    {
        stream->_flag |= Count == 0 ? _IOEOF : _IOERR;
        stream->_cnt = 0;
        return EOF;
    }
    stream->_cnt = Count - 1;
    return (unsigned char)*stream->_ptr++;
}

/* Flush the buffer and put the bytes of a character, returns 0 or EOF */
static
int
_NtCRT_FlushPut(
    _In_reads_bytes_(Length) const char* Bytes,
    _In_ int Length,
    _Inout_ FILE* stream)
{
    int Count, Written;

    if (!(stream->_flag & (_IOWRT | _IORW)) || (stream->_flag & _IOSTRG))
    {
        stream->_flag |= _IOERR;
        return EOF;
    }
    if (stream->_flag & _IOREAD)
    {
        stream->_cnt = 0;
        if (!(stream->_flag & _IOEOF))
        {
            stream->_flag |= _IOERR;
            return EOF;
        }
        stream->_ptr = stream->_base;
        stream->_flag &= ~_IOREAD;
    }
    stream->_flag |= _IOWRT;
    stream->_flag &= ~_IOEOF;
    stream->_cnt = 0;
    if (stream->_base == NULL)
    {
        _NtCRT_getbuf(stream);
    }
    if (!(stream->_flag & _IONBF))
    {
        Count = (int)(stream->_ptr - stream->_base);
        stream->_ptr = stream->_base + Length;
        stream->_cnt = stream->_bufsiz - Length;
        Written = Count > 0 ? _NtCRT_write(stream->_file, stream->_base, Count) : 0;
        memcpy(stream->_base, Bytes, Length);
    } else
    {
        Count = Length;
        Written = _NtCRT_write(stream->_file, Bytes, Length);
    }
    if (Written != Count)
    {
        stream->_flag |= _IOERR;
        return EOF;
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT_flsbuf(int ch, FILE* stream)
{
    char c = (char)ch;
    return _NtCRT_FlushPut(&c, 1, stream) == 0 ? (unsigned char)c : EOF;
}

/* Character I/O */

static
int
_NtCRT_getc_nolock(
    _In_ FILE* Stream)
{
    return --Stream->_cnt >= 0 ? (unsigned char)*Stream->_ptr++ : _NtCRT_filbuf(Stream);
}

static
int
_NtCRT_putc_nolock(
    _In_ int Ch,
    _In_ FILE* Stream)
{
    return --Stream->_cnt >= 0 ? (unsigned char)(*Stream->_ptr++ = (char)Ch) : _NtCRT_flsbuf(Ch, Stream);
}

static
int
_NtCRT_ungetc_nolock(
    _In_ int Ch,
    _In_ FILE* Stream)
{
    if (Ch == EOF)
    {
        return EOF;
    }
    if (!((Stream->_flag & _IOREAD) || ((Stream->_flag & _IORW) && !(Stream->_flag & _IOWRT))))
    {
        return EOF;
    }
    if (Stream->_base == NULL)
    {
        _NtCRT_getbuf(Stream);
    }
    if (Stream->_ptr == Stream->_base)
    {
        if (Stream->_cnt > 0)
        {
            return EOF;
        }
        Stream->_ptr++;
    }
    if (Stream->_flag & _IOSTRG)
    {
        if (*--Stream->_ptr != (char)Ch)
        {
            Stream->_ptr++;
            return EOF;
        }
    } else
    {
        *--Stream->_ptr = (char)Ch;
    }
    if (Stream->_cnt < 0)
    {
        Stream->_cnt = 0;
    }
    Stream->_cnt++;
    Stream->_flag &= ~_IOEOF;
    Stream->_flag |= _IOREAD;
    return (unsigned char)Ch;
}

/* As msvcrt, narrow character I/O is invalid on a Unicode text mode stream */
#define NTCRT_CHECK_STREAM_ANSI(Stream, Ret) \
    do { \
        if (!((Stream)->_flag & _IOSTRG) && _NtCRT_fd_narrow_invalid((Stream)->_file)) \
        { \
            _NtCRT_InvalidParameter(EINVAL); \
            return (Ret); \
        } \
    } while (0)

#define NTCRT_CHECK_STREAM(Stream, Ret) \
    do { \
        if ((Stream) == NULL) \
        { \
            NTCRT_ERRNO = EINVAL; \
            return (Ret); \
        } \
    } while (0)

EXTERN_C int __cdecl _NtCRT_fgetc(FILE* stream)
{
    int Ch;

    NTCRT_CHECK_STREAM(stream, EOF);
    NTCRT_CHECK_STREAM_ANSI(stream, EOF);
    _NtCRT_lock_file(stream);
    Ch = _NtCRT_getc_nolock(stream);
    _NtCRT_unlock_file(stream);
    return Ch;
}

EXTERN_C int __cdecl _NtCRT_fputc(int ch, FILE* stream)
{
    int Ret;

    NTCRT_CHECK_STREAM(stream, EOF);
    NTCRT_CHECK_STREAM_ANSI(stream, EOF);
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_putc_nolock(ch, stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_ungetc(int ch, FILE* stream)
{
    int Ret;

    NTCRT_CHECK_STREAM(stream, EOF);
    NTCRT_CHECK_STREAM_ANSI(stream, EOF);
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_ungetc_nolock(ch, stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_getchar(void)
{
    return _NtCRT_fgetc(&_NtCRT_iob[0]);
}

EXTERN_C int __cdecl _NtCRT_putchar(int ch)
{
    return _NtCRT_fputc(ch, &_NtCRT_iob[1]);
}

EXTERN_C int __cdecl _NtCRT__fgetc_nolock(FILE* stream)
{
    return _NtCRT_getc_nolock(stream);
}

EXTERN_C int __cdecl _NtCRT__fputc_nolock(int ch, FILE* stream)
{
    return _NtCRT_putc_nolock(ch, stream);
}

EXTERN_C int __cdecl _NtCRT__ungetc_nolock(int ch, FILE* stream)
{
    return _NtCRT_ungetc_nolock(ch, stream);
}

/* Wide character I/O, text mode streams convert as the "C" locale, a byte for each character up to 0xFF */

static
wint_t
_NtCRT_getwc_nolock(
    _In_ FILE* Stream)
{
    int Lo, Hi;

    Lo = _NtCRT_getc_nolock(Stream);
    if (Lo == EOF)
    {
        return WEOF;
    }
    if (_NtCRT_fd_textmode(Stream->_file) == NTCRT_FD_ANSI)
    {
        return (wint_t)Lo;
    }
    Hi = _NtCRT_getc_nolock(Stream);
    if (Hi == EOF)
    {
        return WEOF;
    }
    return (wint_t)(Lo | (Hi << 8));
}

static
wint_t
_NtCRT_putwc_nolock(
    _In_ wchar_t Ch,
    _In_ FILE* Stream)
{
    if (_NtCRT_fd_textmode(Stream->_file) == NTCRT_FD_ANSI)
    {
        if (Ch > 0xFF)
        {
            NTCRT_ERRNO = EILSEQ;
            return WEOF;
        }
        return _NtCRT_putc_nolock(Ch, Stream) == EOF ? WEOF : Ch;
    }

    /* Both bytes at once, a Unicode text mode fd rejects a write of an odd count */
    if ((Stream->_cnt -= sizeof(wchar_t)) >= 0)
    {
        memcpy(Stream->_ptr, &Ch, sizeof(wchar_t));
        Stream->_ptr += sizeof(wchar_t);
        return Ch;
    }
    Stream->_cnt = 0;
    return _NtCRT_FlushPut((const char*)&Ch, sizeof(wchar_t), Stream) == 0 ? Ch : WEOF;
}

static
wint_t
_NtCRT_ungetwc_nolock(
    _In_ wint_t Ch,
    _In_ FILE* Stream)
{
    if (Ch == WEOF)
    {
        return WEOF;
    }
    if (_NtCRT_fd_textmode(Stream->_file) == NTCRT_FD_ANSI)
    {
        if (Ch > 0xFF)
        {
            return WEOF;
        }
        return _NtCRT_ungetc_nolock((int)Ch, Stream) == EOF ? WEOF : Ch;
    }
    if (_NtCRT_ungetc_nolock((Ch >> 8) & 0xFF, Stream) == EOF)
    {
        return WEOF;
    }
    if (_NtCRT_ungetc_nolock(Ch & 0xFF, Stream) == EOF)
    {
        _NtCRT_getc_nolock(Stream);
        return WEOF;
    }
    return Ch;
}

EXTERN_C wint_t __cdecl _NtCRT_fgetwc(FILE* stream)
{
    wint_t Ch;

    NTCRT_CHECK_STREAM(stream, WEOF);
    _NtCRT_lock_file(stream);
    Ch = _NtCRT_getwc_nolock(stream);
    _NtCRT_unlock_file(stream);
    return Ch;
}

EXTERN_C wint_t __cdecl _NtCRT_fputwc(wchar_t ch, FILE* stream)
{
    wint_t Ret;

    NTCRT_CHECK_STREAM(stream, WEOF);
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_putwc_nolock(ch, stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C wint_t __cdecl _NtCRT_ungetwc(wint_t ch, FILE* stream)
{
    wint_t Ret;

    NTCRT_CHECK_STREAM(stream, WEOF);
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_ungetwc_nolock(ch, stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C wint_t __cdecl _NtCRT_getwchar(void)
{
    return _NtCRT_fgetwc(&_NtCRT_iob[0]);
}

EXTERN_C wint_t __cdecl _NtCRT_putwchar(wchar_t ch)
{
    return _NtCRT_fputwc(ch, &_NtCRT_iob[1]);
}

EXTERN_C wint_t __cdecl _NtCRT__fgetwc_nolock(FILE* stream)
{
    return _NtCRT_getwc_nolock(stream);
}

EXTERN_C wint_t __cdecl _NtCRT__fputwc_nolock(wchar_t ch, FILE* stream)
{
    return _NtCRT_putwc_nolock(ch, stream);
}

EXTERN_C wint_t __cdecl _NtCRT__ungetwc_nolock(wint_t ch, FILE* stream)
{
    return _NtCRT_ungetwc_nolock(ch, stream);
}

/* String I/O */

EXTERN_C char* __cdecl _NtCRT_fgets(char* str, int n, FILE* stream)
{
    char* p = str;

    if (str == NULL || n <= 0 || stream == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    NTCRT_CHECK_STREAM_ANSI(stream, NULL);
    _NtCRT_lock_file(stream);
    while (--n > 0)
    {
        int Ch = _NtCRT_getc_nolock(stream);
        if (Ch == EOF)
        {
            if (p == str)
            {
                str = NULL;
            }
            break;
        }
        *p++ = (char)Ch;
        if (Ch == '\n')
        {
            break;
        }
    }
    if (str != NULL)
    {
        *p = '\0';
    }
    _NtCRT_unlock_file(stream);
    return str;
}

EXTERN_C wchar_t* __cdecl _NtCRT_fgetws(wchar_t* str, int n, FILE* stream)
{
    wchar_t* p = str;

    if (str == NULL || n <= 0 || stream == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    _NtCRT_lock_file(stream);
    while (--n > 0)
    {
        wint_t Ch = _NtCRT_getwc_nolock(stream);
        if (Ch == WEOF)
        {
            if (p == str)
            {
                str = NULL;
            }
            break;
        }
        *p++ = (wchar_t)Ch;
        if (Ch == L'\n')
        {
            break;
        }
    }
    if (str != NULL)
    {
        *p = L'\0';
    }
    _NtCRT_unlock_file(stream);
    return str;
}

EXTERN_C char* __cdecl _NtCRT_gets(char* str)
{
    FILE* Stream = &_NtCRT_iob[0];
    char* p = str;
    int Ch;

    if (str == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    NTCRT_CHECK_STREAM_ANSI(Stream, NULL);
    _NtCRT_lock_file(Stream);
    while ((Ch = _NtCRT_getc_nolock(Stream)) != '\n')
    {
        if (Ch == EOF)
        {
            if (p == str)
            {
                str = NULL;
            }
            break;
        }
        *p++ = (char)Ch;
    }
    if (str != NULL)
    {
        *p = '\0';
    }
    _NtCRT_unlock_file(Stream);
    return str;
}

EXTERN_C wchar_t* __cdecl _NtCRT__getws(wchar_t* str)
{
    FILE* Stream = &_NtCRT_iob[0];
    wchar_t* p = str;
    wint_t Ch;

    if (str == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    _NtCRT_lock_file(Stream);
    while ((Ch = _NtCRT_getwc_nolock(Stream)) != L'\n')
    {
        if (Ch == WEOF)
        {
            if (p == str)
            {
                str = NULL;
            }
            break;
        }
        *p++ = (wchar_t)Ch;
    }
    if (str != NULL)
    {
        *p = L'\0';
    }
    _NtCRT_unlock_file(Stream);
    return str;
}

EXTERN_C size_t __cdecl _NtCRT_fwrite(const void* buffer, size_t size, size_t count, FILE* stream);

EXTERN_C int __cdecl _NtCRT_fputs(const char* str, FILE* stream)
{
    size_t Length;

    if (str == NULL || stream == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    NTCRT_CHECK_STREAM_ANSI(stream, EOF);
    Length = strlen(str);
    return _NtCRT_fwrite(str, 1, Length, stream) == Length ? 0 : EOF;
}

EXTERN_C int __cdecl _NtCRT_puts(const char* str)
{
    FILE* Stream = &_NtCRT_iob[1];
    int Ret = 0;

    if (str == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    _NtCRT_lock_file(Stream);
    if (_NtCRT_fputs(str, Stream) == EOF || _NtCRT_putc_nolock('\n', Stream) == EOF)
    {
        Ret = EOF;
    }
    _NtCRT_unlock_file(Stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_fputws(const wchar_t* str, FILE* stream)
{
    int Ret = 0;

    if (str == NULL || stream == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    _NtCRT_lock_file(stream);
    for (; *str != L'\0'; str++)
    {
        if (_NtCRT_putwc_nolock(*str, stream) == WEOF)
        {
            Ret = EOF;
            break;
        }
    }
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__putws(const wchar_t* str)
{
    FILE* Stream = &_NtCRT_iob[1];
    int Ret = 0;

    _NtCRT_lock_file(Stream);
    if (_NtCRT_fputws(str, Stream) == EOF || _NtCRT_putwc_nolock(L'\n', Stream) == WEOF)
    {
        Ret = EOF;
    }
    _NtCRT_unlock_file(Stream);
    return Ret;
}

/* Block I/O */

EXTERN_C size_t __cdecl _NtCRT_fread(void* buffer, size_t size, size_t count, FILE* stream)
{
    char* Dst = (char*)buffer;
    size_t Total, Remaining, BufferSize;

    if (size == 0 || count == 0)
    {
        return 0;
    }
    if (buffer == NULL || stream == NULL || count > SIZE_MAX / size)
    {
        NTCRT_ERRNO = EINVAL;
        return 0;
    }
    Total = Remaining = size * count;
    _NtCRT_lock_file(stream);

    /* As msvcrt, whole buffers are read directly and the rest through the buffer */
    BufferSize = stream->_base != NULL ? (size_t)stream->_bufsiz : NTCRT_BUFSIZ;
    while (Remaining != 0)
    {
        if (stream->_base != NULL && stream->_cnt > 0)
        {
            size_t Chunk = (size_t)stream->_cnt < Remaining ? (size_t)stream->_cnt : Remaining;
            memcpy(Dst, stream->_ptr, Chunk);
            stream->_ptr += Chunk;
            stream->_cnt -= (int)Chunk;
            Dst += Chunk;
            Remaining -= Chunk;
        } else if (Remaining >= BufferSize)
        {
            size_t Chunk = BufferSize != 0 ? Remaining - Remaining % BufferSize : Remaining;
            int Read;

            if (!(stream->_flag & (_IOREAD | _IORW)) || (stream->_flag & _IOWRT))
            {
                stream->_flag |= _IOERR;
                break;
            }
            stream->_flag |= _IOREAD;
            if (Chunk > INT_MAX)
            {
                Chunk = BufferSize != 0 && BufferSize <= INT_MAX ? INT_MAX - INT_MAX % BufferSize : INT_MAX;
            }
            Read = _NtCRT_read(stream->_file, Dst, (unsigned int)Chunk);
            if (Read <= 0)
            {
                stream->_flag |= Read == 0 ? _IOEOF : _IOERR;
                break;
            }
            Dst += Read;
            Remaining -= Read;
        } else
        {
            int Ch = _NtCRT_filbuf(stream);
            if (Ch == EOF)
            {
                break;
            }
            *Dst++ = (char)Ch;
            Remaining--;
            BufferSize = (size_t)stream->_bufsiz;
        }
    }
    _NtCRT_unlock_file(stream);
    return (Total - Remaining) / size;
}

EXTERN_C size_t __cdecl _NtCRT_fwrite(const void* buffer, size_t size, size_t count, FILE* stream)
{
    const char* Src = (const char*)buffer;
    size_t Total, Remaining;

    if (size == 0 || count == 0)
    {
        return 0;
    }
    if (buffer == NULL || stream == NULL || count > SIZE_MAX / size)
    {
        NTCRT_ERRNO = EINVAL;
        return 0;
    }
    Total = Remaining = size * count;
    _NtCRT_lock_file(stream);
    while (Remaining != 0)
    {
        if (stream->_base != NULL && !(stream->_flag & _IONBF) && (stream->_flag & _IOWRT) && stream->_cnt > 0)
        {
            size_t Chunk = (size_t)stream->_cnt < Remaining ? (size_t)stream->_cnt : Remaining;
            memcpy(stream->_ptr, Src, Chunk);
            stream->_ptr += Chunk;
            stream->_cnt -= (int)Chunk;
            Src += Chunk;
            Remaining -= Chunk;
        } else if (stream->_base != NULL && !(stream->_flag & _IONBF) && (stream->_flag & _IOWRT) &&
                   stream->_ptr > stream->_base)
        {
            /* Buffer is full */
            if (_NtCRT_flush_nolock(stream) != 0)
            {
                break;
            }
            /* _NtCRT_flush_nolock cleared _IOWRT of a read/write stream */
            stream->_flag |= _IOWRT;
            stream->_cnt = stream->_bufsiz;
        } else if (Remaining >= NTCRT_BUFSIZ || (stream->_flag & _IONBF))
        {
            int Written;
            unsigned int Chunk;

            if (!(stream->_flag & (_IOWRT | _IORW)) || (stream->_flag & _IOSTRG))
            {
                stream->_flag |= _IOERR;
                break;
            }
            if (stream->_flag & _IOREAD)
            {
                if (!(stream->_flag & _IOEOF))
                {
                    stream->_flag |= _IOERR;
                    break;
                }
                stream->_flag &= ~_IOREAD;
                stream->_ptr = stream->_base;
                stream->_cnt = 0;
            }
            stream->_flag |= _IOWRT;
            stream->_flag &= ~_IOEOF;
            Chunk = Remaining > INT_MAX ? INT_MAX : (unsigned int)Remaining;
            if (!(stream->_flag & _IONBF))
            {
                Chunk -= Chunk % NTCRT_BUFSIZ;
            }
            Written = _NtCRT_write(stream->_file, Src, Chunk);
            if (Written <= 0)
            {
                stream->_flag |= _IOERR;
                break;
            }
            Src += Written;
            Remaining -= Written;
            if ((unsigned int)Written < Chunk)
            {
                stream->_flag |= _IOERR;
                break;
            }
        } else
        {
            /* Let _flsbuf set up the buffer */
            if (_NtCRT_flsbuf((unsigned char)*Src, stream) == EOF)
            {
                break;
            }
            Src++;
            Remaining--;
        }
    }
    _NtCRT_unlock_file(stream);
    return (Total - Remaining) / size;
}

/* Status */

EXTERN_C int __cdecl _NtCRT_feof(FILE* stream)
{
    NTCRT_CHECK_STREAM(stream, 0);
    return stream->_flag & _IOEOF;
}

EXTERN_C int __cdecl _NtCRT_ferror(FILE* stream)
{
    NTCRT_CHECK_STREAM(stream, 0);
    return stream->_flag & _IOERR;
}

EXTERN_C void __cdecl _NtCRT_clearerr(FILE* stream)
{
    if (stream == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return;
    }
    _NtCRT_lock_file(stream);
    stream->_flag &= ~(_IOERR | _IOEOF);
    _NtCRT_unlock_file(stream);
}

EXTERN_C int __cdecl _NtCRT_fileno(FILE* stream)
{
    NTCRT_CHECK_STREAM(stream, -1);
    return stream->_file;
}

/* Flush and positioning */

EXTERN_C int __cdecl _NtCRT_flushall_impl(BOOL FlushOnly);

EXTERN_C int __cdecl _NtCRT_fflush(FILE* stream)
{
    int Ret;

    if (stream == NULL)
    {
        return _NtCRT_flushall_impl(TRUE);
    }
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_flush_nolock(stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__fflush_nolock(FILE* stream)
{
    if (stream == NULL)
    {
        return _NtCRT_flushall_impl(TRUE);
    }
    return _NtCRT_flush_nolock(stream);
}

static
long long
_NtCRT_ftell_nolock(
    _In_ FILE* Stream)
{
    long long Position;

    if (Stream->_cnt < 0)
    {
        Stream->_cnt = 0;
    }
    Position = _NtCRT_lseeki64(Stream->_file, 0, SEEK_CUR);
    if (Position < 0)
    {
        return -1;
    }
    if (Stream->_base == NULL || ((Stream->_flag & _IONBF) && !(Stream->_flag & _IOREAD)))
    {
        return Position;
    }
    if (Stream->_flag & _IOWRT)
    {
        Position += Stream->_ptr - Stream->_base;
        if (_NtCRT_fd_textmode(Stream->_file))
        {
            for (char* p = Stream->_base; p < Stream->_ptr; p++)
            {
                if (*p == '\n')
                {
                    Position++;
                }
            }
        }
    } else if ((Stream->_flag & _IOREAD) && _NtCRT_fd_textmode(Stream->_file) >= NTCRT_FD_UTF8)
    {
        /*
         * As msvcrt, the buffer is taken as the bytes read, a full buffer unless at the end of the file, and each
         * newline byte in it as CRLF
         */
        long long Offset = Stream->_ptr - Stream->_base, Read;

        for (char* p = Stream->_base; p < Stream->_ptr; p++)
        {
            if (*p == '\n')
            {
                Offset++;
            }
        }
        if (Position == 0)
        {
            return Offset;
        }
        if (Stream->_cnt == 0)
        {
            return Position;
        }
        Read = Stream->_cnt + (Stream->_ptr - Stream->_base);
        if (_NtCRT_lseeki64(Stream->_file, 0, SEEK_END) == Position)
        {
            for (char* p = Stream->_base, *End = Stream->_base + Read; p < End; p++)
            {
                if (*p == '\n')
                {
                    Read++;
                }
            }
        } else
        {
            _NtCRT_lseeki64(Stream->_file, Position, SEEK_SET);
            Read = Read <= NTCRT_SMALL_BUFSIZ && (Stream->_flag & _IOMYBUF) && !(Stream->_flag & NTCRT_IOSETVBUF) ?
                   NTCRT_SMALL_BUFSIZ : Stream->_bufsiz;
            if (_NtCRT_fd_crlf(Stream->_file))
            {
                Read++;
            }
        }
        return Position - Read + Offset;
    } else if (Stream->_flag & _IOREAD)
    {
        /* Unread data, a text mode newline is assumed to be CRLF in the file */
        Position -= Stream->_cnt;
        if (_NtCRT_fd_textmode(Stream->_file))
        {
            for (char* p = Stream->_ptr; p < Stream->_ptr + Stream->_cnt; p++)
            {
                if (*p == '\n')
                {
                    Position--;
                }
            }
        }
        if (Position < 0)
        {
            Position = 0;
        }
    }
    return Position;
}

EXTERN_C long long __cdecl _NtCRT_ftelli64(FILE* stream)
{
    long long Ret;

    NTCRT_CHECK_STREAM(stream, -1);
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_ftell_nolock(stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C long __cdecl _NtCRT_ftell(FILE* stream)
{
    long long Ret = _NtCRT_ftelli64(stream);

    if (Ret > LONG_MAX)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return (long)Ret;
}

EXTERN_C int __cdecl _NtCRT_fseeki64(FILE* stream, long long offset, int origin)
{
    int Ret = 0;

    NTCRT_CHECK_STREAM(stream, -1);
    if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_lock_file(stream);
    stream->_flag &= ~_IOEOF;
    if (origin == SEEK_CUR)
    {
        long long Current = _NtCRT_ftell_nolock(stream);
        if (Current < 0)
        {
            Ret = -1;
            goto _Exit;
        }
        offset += Current;
        origin = SEEK_SET;
    }
    _NtCRT_flush_nolock(stream);
    if (stream->_flag & _IORW)
    {
        stream->_flag &= ~(_IOWRT | _IOREAD);
    } else if ((stream->_flag & _IOREAD) && (stream->_flag & _IOMYBUF) && !(stream->_flag & NTCRT_IOSETVBUF))
    {
        /* msvcrt shrinks the buffer of a read stream after seeking */
        stream->_bufsiz = NTCRT_SMALL_BUFSIZ;
    }
    if (_NtCRT_lseeki64(stream->_file, offset, origin) < 0)
    {
        Ret = -1;
    }

_Exit:
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_fseek(FILE* stream, long offset, int origin)
{
    return _NtCRT_fseeki64(stream, offset, origin);
}

EXTERN_C void __cdecl _NtCRT_rewind(FILE* stream)
{
    if (stream == NULL)
    {
        return;
    }
    _NtCRT_lock_file(stream);
    _NtCRT_fseeki64(stream, 0, SEEK_SET);
    stream->_flag &= ~(_IOERR | _IOEOF);
    _NtCRT_unlock_file(stream);
}

EXTERN_C int __cdecl _NtCRT_fgetpos(FILE* stream, fpos_t* pos)
{
    long long Position;

    if (stream == NULL || pos == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Position = _NtCRT_ftelli64(stream);
    if (Position < 0)
    {
        return -1;
    }
    *pos = Position;
    return 0;
}

EXTERN_C int __cdecl _NtCRT_fsetpos(FILE* stream, const fpos_t* pos)
{
    if (stream == NULL || pos == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return _NtCRT_fseeki64(stream, *pos, SEEK_SET);
}

EXTERN_C int __cdecl _NtCRT_setvbuf(FILE* stream, char* buffer, int mode, size_t size)
{
    if (stream == NULL || (mode != _IOFBF && mode != _IOLBF && mode != _IONBF) ||
        (mode != _IONBF && (size < 2 || size > INT_MAX)))
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_lock_file(stream);
    _NtCRT_flush_nolock(stream);
    _NtCRT_freebuf(stream);
    stream->_flag &= ~(_IONBF | NTCRT_IOFLUSHUNLOCK);
    if (stream->_file == 2 || _NtCRT_isatty(stream->_file))
    {
        stream->_flag |= NTCRT_IOFLUSHUNLOCK;
    }
    if (mode == _IONBF)
    {
        stream->_flag |= _IONBF;
        stream->_base = (char*)&stream->_charbuf;
        stream->_bufsiz = 2;
    } else
    {
        /* Line buffering is full buffering, as msvcrt does */
        size &= ~(size_t)1;
        if (buffer == NULL)
        {
            buffer = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, size);
            if (buffer == NULL)
            {
                _NtCRT_unlock_file(stream);
                return -1;
            }
            stream->_flag |= _IOMYBUF | NTCRT_IOSETVBUF;
        } else
        {
            stream->_flag |= NTCRT_IOYOURBUF | NTCRT_IOSETVBUF;
        }
        stream->_base = buffer;
        stream->_bufsiz = (int)size;
    }
    stream->_ptr = stream->_base;
    stream->_cnt = 0;
    _NtCRT_unlock_file(stream);
    return 0;
}

EXTERN_C void __cdecl _NtCRT_setbuf(FILE* stream, char* buffer)
{
    _NtCRT_setvbuf(stream, buffer, buffer != NULL ? _IOFBF : _IONBF, BUFSIZ);
}

/* Open and close */

static
BOOL
_NtCRT_ParseMode(
    _In_ const wchar_t* Mode,
    _Out_ int* OFlag,
    _Out_ int* StreamFlag)
{
    int OpenFlag, Flag;
    BOOL Binary = FALSE, Text = FALSE, Plus = FALSE;

    while (*Mode == L' ')
    {
        Mode++;
    }
    switch (*Mode++)
    {
        case L'r':
            OpenFlag = _O_RDONLY;
            Flag = _IOREAD;
            break;
        case L'w':
            OpenFlag = _O_WRONLY | _O_CREAT | _O_TRUNC;
            Flag = _IOWRT;
            break;
        case L'a':
            OpenFlag = _O_WRONLY | _O_CREAT | _O_APPEND;
            Flag = _IOWRT;
            break;
        default:
            return FALSE;
    }
    for (; *Mode != L'\0' && *Mode != L','; Mode++)
    {
        switch (*Mode)
        {
            case L'+':
                if (Plus)
                {
                    return FALSE;
                }
                Plus = TRUE;
                OpenFlag = (OpenFlag & ~(_O_RDONLY | _O_WRONLY)) | _O_RDWR;
                Flag = _IORW;
                break;
            case L'b':
                if (Text || Binary)
                {
                    return FALSE;
                }
                Binary = TRUE;
                OpenFlag |= _O_BINARY;
                break;
            case L't':
                if (Text || Binary)
                {
                    return FALSE;
                }
                Text = TRUE;
                OpenFlag |= _O_TEXT;
                break;
            case L'x':
                OpenFlag |= _O_EXCL;
                break;
            case L'N':
                OpenFlag |= _O_NOINHERIT;
                break;
            case L'S':
                OpenFlag |= _O_SEQUENTIAL;
                break;
            case L'R':
                OpenFlag |= _O_RANDOM;
                break;
            case L'T':
                OpenFlag |= _O_SHORT_LIVED;
                break;
            case L'D':
                OpenFlag |= _O_TEMPORARY;
                break;
            case L'c':
            case L'n':
            case L' ':
                break;
            default:
                return FALSE;
        }
    }
    if (*Mode == L',')
    {
        /* ", ccs=<encoding>" selects a Unicode text mode */
        static const struct
        {
            const wchar_t* Name;
            int Flag;
        } Encodings[] = { { L"UTF-8", _O_U8TEXT }, { L"UTF-16LE", _O_U16TEXT }, { L"UNICODE", _O_WTEXT } };
        size_t i;

        for (Mode++; *Mode == L' '; Mode++);
        if (wcsncmp(Mode, L"ccs", 3) != 0)
        {
            return FALSE;
        }
        for (Mode += 3; *Mode == L' '; Mode++);
        if (*Mode++ != L'=')
        {
            return FALSE;
        }
        for (; *Mode == L' '; Mode++);
        for (i = 0; i < ARRAYSIZE(Encodings); i++)
        {
            size_t Length = wcslen(Encodings[i].Name);
            if (_wcsnicmp(Mode, Encodings[i].Name, Length) == 0)
            {
                OpenFlag |= Encodings[i].Flag;
                Mode += Length;
                break;
            }
        }
        if (i == ARRAYSIZE(Encodings))
        {
            return FALSE;
        }
        for (; *Mode == L' '; Mode++);
        if (*Mode != L'\0')
        {
            return FALSE;
        }
    }
    *OFlag = OpenFlag;
    *StreamFlag = Flag;
    return TRUE;
}

static
FILE*
_NtCRT_OpenStream(
    _In_ const wchar_t* Path,
    _In_ const wchar_t* Mode,
    _In_ int ShFlag,
    _In_opt_ FILE* Reuse)
{
    int OFlag, Flag, fd;
    FILE* Stream;

    if (Path == NULL || Mode == NULL || !_NtCRT_ParseMode(Mode, &OFlag, &Flag))
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    if (*Path == L'\0')
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    Stream = Reuse != NULL ? Reuse : _NtCRT_AllocStream();
    if (Stream == NULL)
    {
        return NULL;
    }
    fd = _NtCRT_wsopen(Path, OFlag, ShFlag, _S_IREAD | _S_IWRITE);
    if (fd < 0)
    {
        if (Reuse == NULL)
        {
            Stream->_flag = 0;
            _NtCRT_unlock_file(Stream);
        }
        return NULL;
    }
    Stream->_file = fd;
    Stream->_flag = Flag;
    Stream->_ptr = Stream->_base = NULL;
    Stream->_cnt = 0;
    Stream->_bufsiz = 0;
    Stream->_tmpfname = NULL;
    if (Reuse == NULL)
    {
        _NtCRT_unlock_file(Stream);
    }
    return Stream;
}

static
FILE*
_NtCRT_OpenStreamA(
    _In_ const char* Path,
    _In_ const char* Mode,
    _In_ int ShFlag,
    _In_opt_ FILE* Reuse)
{
    PWSTR WidePath, WideMode;
    FILE* Stream = NULL;

    if (Path == NULL || Mode == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    WidePath = _NtCRT_AnsiToWide(Path);
    if (WidePath == NULL)
    {
        return NULL;
    }
    WideMode = _NtCRT_AnsiToWide(Mode);
    if (WideMode != NULL)
    {
        Stream = _NtCRT_OpenStream(WidePath, WideMode, ShFlag, Reuse);
        RtlFreeHeap(NTCRT_HEAP, 0, WideMode);
    }
    RtlFreeHeap(NTCRT_HEAP, 0, WidePath);
    return Stream;
}

EXTERN_C FILE* __cdecl _NtCRT_fopen(const char* path, const char* mode)
{
    return _NtCRT_OpenStreamA(path, mode, _SH_DENYNO, NULL);
}

EXTERN_C FILE* __cdecl _NtCRT__wfopen(const wchar_t* path, const wchar_t* mode)
{
    return _NtCRT_OpenStream(path, mode, _SH_DENYNO, NULL);
}

EXTERN_C FILE* __cdecl _NtCRT__fsopen(const char* path, const char* mode, int shflag)
{
    return _NtCRT_OpenStreamA(path, mode, shflag, NULL);
}

EXTERN_C FILE* __cdecl _NtCRT__wfsopen(const wchar_t* path, const wchar_t* mode, int shflag)
{
    return _NtCRT_OpenStream(path, mode, shflag, NULL);
}

EXTERN_C int __cdecl _NtCRT_fopen_s(FILE** pfile, const char* path, const char* mode)
{
    if (pfile == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    *pfile = _NtCRT_OpenStreamA(path, mode, _SH_SECURE, NULL);
    return *pfile == NULL ? NTCRT_ERRNO : 0;
}

EXTERN_C int __cdecl _NtCRT__wfopen_s(FILE** pfile, const wchar_t* path, const wchar_t* mode)
{
    if (pfile == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    *pfile = _NtCRT_OpenStream(path, mode, _SH_SECURE, NULL);
    return *pfile == NULL ? NTCRT_ERRNO : 0;
}

static
FILE*
_NtCRT_OpenFdStream(
    _In_ int fd,
    _In_ const wchar_t* Mode)
{
    int OFlag, Flag;
    FILE* Stream;

    if (Mode == NULL || !_NtCRT_ParseMode(Mode, &OFlag, &Flag))
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    if (_NtCRT_get_osfhandle(fd) == -1)
    {
        return NULL;
    }
    Stream = _NtCRT_AllocStream();
    if (Stream == NULL)
    {
        return NULL;
    }
    Stream->_file = fd;
    Stream->_flag = Flag;
    _NtCRT_unlock_file(Stream);
    return Stream;
}

EXTERN_C FILE* __cdecl _NtCRT__wfdopen(int fd, const wchar_t* mode)
{
    return _NtCRT_OpenFdStream(fd, mode);
}

EXTERN_C FILE* __cdecl _NtCRT__fdopen(int fd, const char* mode)
{
    WCHAR WideMode[32];
    unsigned int i;

    if (mode == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    for (i = 0; mode[i] != '\0' && i < ARRAYSIZE(WideMode) - 1; i++)
    {
        WideMode[i] = (unsigned char)mode[i];
    }
    WideMode[i] = L'\0';
    return _NtCRT_OpenFdStream(fd, WideMode);
}

EXTERN_C int __cdecl _NtCRT_remove(const char* path);

static
int
_NtCRT_fclose_nolock(
    _In_ FILE* Stream)
{
    int Ret = EOF;

    if (_NtCRT_InUse(Stream))
    {
        Ret = _NtCRT_flush_nolock(Stream);
        _NtCRT_freebuf(Stream);
        if (_NtCRT_close(Stream->_file) < 0)
        {
            Ret = EOF;
        }
        if (Stream->_tmpfname != NULL)
        {
            _NtCRT_remove(Stream->_tmpfname);
            RtlFreeHeap(NTCRT_HEAP, 0, Stream->_tmpfname);
            Stream->_tmpfname = NULL;
        }
    }
    Stream->_flag = 0;
    Stream->_file = -1;
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_fclose(FILE* stream)
{
    int Ret;

    NTCRT_CHECK_STREAM(stream, EOF);
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_fclose_nolock(stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C FILE* __cdecl _NtCRT__wfreopen(const wchar_t* path, const wchar_t* mode, FILE* stream)
{
    FILE* Ret;

    NTCRT_CHECK_STREAM(stream, NULL);
    _NtCRT_lock_file(stream);
    _NtCRT_fclose_nolock(stream);
    Ret = _NtCRT_OpenStream(path, mode, _SH_DENYNO, stream);
    if (Ret == NULL)
    {
        stream->_flag = 0;
    }
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C FILE* __cdecl _NtCRT_freopen(const char* path, const char* mode, FILE* stream)
{
    FILE* Ret;

    NTCRT_CHECK_STREAM(stream, NULL);
    _NtCRT_lock_file(stream);
    _NtCRT_fclose_nolock(stream);
    Ret = _NtCRT_OpenStreamA(path, mode, _SH_DENYNO, stream);
    if (Ret == NULL)
    {
        stream->_flag = 0;
    }
    _NtCRT_unlock_file(stream);
    return Ret;
}

/* Iterate all streams in use */
static
int
_NtCRT_ForEachStream(
    _In_ int (*Callback)(FILE*, int),
    _In_ int Context)
{
    int Count = 0;

    _NtCRT_EnsureStdio();
    _NtCRT_lock(_IOB_SCAN_LOCK);
    for (int i = 0; i < NTCRT_IOB_ENTRIES; i++)
    {
        Count += Callback(&_NtCRT_iob[i], Context);
    }
    for (unsigned int i = 0; i < ARRAYSIZE(g_Streams) && g_Streams[i] != NULL; i++)
    {
        Count += Callback(&g_Streams[i]->File, Context);
    }
    _NtCRT_unlock(_IOB_SCAN_LOCK);
    return Count;
}

/* Context: 1 for flush returning error count, 0 for flush returning stream count */
static
int
_NtCRT_FlushCallback(
    _In_ FILE* Stream,
    _In_ int ErrorCount)
{
    int Ret = 0;

    if (!_NtCRT_InUse(Stream))
    {
        return 0;
    }
    _NtCRT_lock_file(Stream);
    if (_NtCRT_InUse(Stream))
    {
        if (_NtCRT_flush_nolock(Stream) != 0)
        {
            Ret = ErrorCount ? 1 : 0;
        } else
        {
            Ret = ErrorCount ? 0 : 1;
        }
    }
    _NtCRT_unlock_file(Stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_flushall_impl(BOOL FlushOnly)
{
    int Ret = _NtCRT_ForEachStream(_NtCRT_FlushCallback, FlushOnly);
    return FlushOnly ? (Ret != 0 ? EOF : 0) : Ret;
}

EXTERN_C int __cdecl _NtCRT__flushall(void)
{
    return _NtCRT_flushall_impl(FALSE);
}

EXTERN_C void _NtCRT_FlushAll(void)
{
    _NtCRT_flushall_impl(TRUE);
}

static
int
_NtCRT_CloseCallback(
    _In_ FILE* Stream,
    _In_ int Context)
{
    int Ret = 0;

    UNREFERENCED_PARAMETER(Context);
    if (Stream >= _NtCRT_iob && Stream < _NtCRT_iob + 3)
    {
        return 0;
    }
    if (!_NtCRT_InUse(Stream))
    {
        return 0;
    }
    _NtCRT_lock_file(Stream);
    if (_NtCRT_InUse(Stream) && _NtCRT_fclose_nolock(Stream) != EOF)
    {
        Ret = 1;
    }
    _NtCRT_unlock_file(Stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__fcloseall(void)
{
    return _NtCRT_ForEachStream(_NtCRT_CloseCallback, 0);
}

EXTERN_C void _NtCRT_CloseAll(void)
{
    _NtCRT__fcloseall();
}

EXTERN_C int __cdecl _NtCRT__getmaxstdio(void)
{
    return NTCRT_NSTREAM;
}

EXTERN_C int __cdecl _NtCRT__setmaxstdio(int newmax)
{
    if (newmax < NTCRT_IOB_ENTRIES || newmax > NTCRT_NSTREAM)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return newmax;
}

EXTERN_C FILE* __cdecl _NtCRT___iob_func(void)
{
    return _NtCRT_iob;
}

EXTERN_C FILE* __cdecl _NtCRT___acrt_iob_func(unsigned index)
{
    return &_NtCRT_iob[index];
}

/* File system operations */

EXTERN_C int __cdecl _NtCRT__wremove(const wchar_t* path)
{
    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (!_K32_DeleteFileW(path))
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return -1;
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT_remove(const char* path)
{
    PWSTR WidePath;
    int Ret;

    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    WidePath = _NtCRT_AnsiToWide(path);
    if (WidePath == NULL)
    {
        return -1;
    }
    Ret = _NtCRT__wremove(WidePath);
    RtlFreeHeap(NTCRT_HEAP, 0, WidePath);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__wrename(const wchar_t* oldname, const wchar_t* newname)
{
    if (oldname == NULL || newname == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (!_K32_MoveFileExW(oldname, newname, MOVEFILE_COPY_ALLOWED))
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return -1;
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT_rename(const char* oldname, const char* newname)
{
    PWSTR WideOld, WideNew;
    int Ret = -1;

    if (oldname == NULL || newname == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    WideOld = _NtCRT_AnsiToWide(oldname);
    if (WideOld == NULL)
    {
        return -1;
    }
    WideNew = _NtCRT_AnsiToWide(newname);
    if (WideNew != NULL)
    {
        Ret = _NtCRT__wrename(WideOld, WideNew);
        RtlFreeHeap(NTCRT_HEAP, 0, WideNew);
    }
    RtlFreeHeap(NTCRT_HEAP, 0, WideOld);
    return Ret;
}

/* Temporary files */

static volatile LONG g_TmpCounter;

/* Make a unique name in the temporary directory, returns a heap string */
static
PWSTR
_NtCRT_MakeTempName(
    _In_opt_ PCWSTR Directory,
    _In_opt_ PCWSTR Prefix)
{
    WCHAR TempPath[MAX_PATH + 1];
    PWSTR Name;
    size_t Length;

    if (Directory == NULL)
    {
        DWORD TempLength = _K32_GetTempPathW(ARRAYSIZE(TempPath), TempPath);
        if (TempLength == 0 || TempLength >= ARRAYSIZE(TempPath))
        {
            NTCRT_ERRNO = ENOENT;
            return NULL;
        }
        Directory = TempPath;
    }
    if (Prefix == NULL)
    {
        Prefix = L"t";
    }
    Length = wcslen(Directory) + wcslen(Prefix) + 32;
    Name = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, Length * sizeof(WCHAR));
    if (Name == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    for (int Try = 0; Try < 0x10000; Try++)
    {
        size_t DirLength = wcslen(Directory);
        LONG Counter = _InterlockedIncrement(&g_TmpCounter);

        _NtCRT__snwprintf(
            Name,
            Length,
            L"%ls%ls%ls%lx.%lx",
            Directory,
            DirLength != 0 && Directory[DirLength - 1] != L'\\' && Directory[DirLength - 1] != L'/' ? L"\\" : L"",
            Prefix,
            (ULONG)(ULONG_PTR)NtCurrentProcessId(),
            (ULONG)Counter);
        if (_K32_GetFileAttributesW(Name) == INVALID_FILE_ATTRIBUTES)
        {
            return Name;
        }
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    NTCRT_ERRNO = EEXIST;
    return NULL;
}

EXTERN_C FILE* __cdecl _NtCRT_tmpfile(void)
{
    FILE* Stream = NULL;

    for (int Try = 0; Try < 16 && Stream == NULL; Try++)
    {
        PWSTR Name = _NtCRT_MakeTempName(NULL, L"t");
        int fd;

        if (Name == NULL)
        {
            return NULL;
        }
        fd = _NtCRT_wsopen(Name,
                           _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY | _O_TEMPORARY,
                           _SH_DENYNO,
                           _S_IREAD | _S_IWRITE);
        RtlFreeHeap(NTCRT_HEAP, 0, Name);
        if (fd < 0)
        {
            if (NTCRT_ERRNO == EEXIST)
            {
                continue;
            }
            return NULL;
        }
        Stream = _NtCRT_AllocStream();
        if (Stream == NULL)
        {
            _NtCRT_close(fd);
            return NULL;
        }
        Stream->_file = fd;
        Stream->_flag = _IORW | NTCRT_IOTMPFILE;
        _NtCRT_unlock_file(Stream);
    }
    return Stream;
}

EXTERN_C errno_t __cdecl _NtCRT_tmpfile_s(FILE** pFilePtr)
{
    if (pFilePtr == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pFilePtr = _NtCRT_tmpfile();
    return *pFilePtr != NULL ? 0 : NTCRT_ERRNO;
}

static
int
_NtCRT_RmtmpCallback(
    _In_ FILE* Stream,
    _In_ int Context)
{
    int Ret = 0;

    UNREFERENCED_PARAMETER(Context);
    if (!(Stream->_flag & NTCRT_IOTMPFILE))
    {
        return 0;
    }
    _NtCRT_lock_file(Stream);
    if (_NtCRT_InUse(Stream) && (Stream->_flag & NTCRT_IOTMPFILE) && _NtCRT_fclose_nolock(Stream) != EOF)
    {
        Ret = 1;
    }
    _NtCRT_unlock_file(Stream);
    return Ret;
}

/* Close the streams of tmpfile, which deletes their files, returns the count */
EXTERN_C int __cdecl _NtCRT__rmtmp(void)
{
    return _NtCRT_ForEachStream(_NtCRT_RmtmpCallback, 0);
}

EXTERN_C char* __cdecl _NtCRT_tempnam(const char* dir, const char* prefix)
{
    PWSTR WideDir = NULL, WidePrefix = NULL, Name = NULL;
    char* Ret = NULL;

    if (dir != NULL && (WideDir = _NtCRT_AnsiToWide(dir)) == NULL)
    {
        goto _Exit;
    }
    if (prefix != NULL && (WidePrefix = _NtCRT_AnsiToWide(prefix)) == NULL)
    {
        goto _Exit;
    }
    Name = _NtCRT_MakeTempName(WideDir, WidePrefix);
    if (Name != NULL)
    {
        Ret = _NtCRT_WideToAnsi(Name);
    }

_Exit:
    RtlFreeHeap(NTCRT_HEAP, 0, WideDir);
    RtlFreeHeap(NTCRT_HEAP, 0, WidePrefix);
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    return Ret;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wtempnam(const wchar_t* dir, const wchar_t* prefix)
{
    return _NtCRT_MakeTempName(dir, prefix);
}

EXTERN_C char* __cdecl _NtCRT_tmpnam(char* str)
{
    static char Buffer[MAX_PATH];
    char* Name = _NtCRT_tempnam(NULL, "s");

    if (Name == NULL)
    {
        return NULL;
    }
    if (str == NULL)
    {
        str = Buffer;
    }
    strncpy(str, Name, L_tmpnam - 1);
    str[L_tmpnam - 1] = '\0';
    if (strlen(Name) >= L_tmpnam)
    {
        /* Caller buffer is too small for an absolute path, use the static buffer */
        if (str != Buffer)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, Name);
            NTCRT_ERRNO = ERANGE;
            return NULL;
        }
        strncpy(Buffer, Name, sizeof(Buffer) - 1);
        Buffer[sizeof(Buffer) - 1] = '\0';
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    return str;
}

/* Like tmpnam, with a static buffer if str is NULL */
EXTERN_C wchar_t* __cdecl _NtCRT__wtmpnam(wchar_t* str)
{
    static wchar_t Buffer[MAX_PATH];
    wchar_t* Name = _NtCRT_MakeTempName(NULL, L"s");

    if (Name == NULL)
    {
        return NULL;
    }
    if (str == NULL)
    {
        str = Buffer;
    }
    if (wcslen(Name) >= (str == Buffer ? ARRAYSIZE(Buffer) : L_tmpnam))
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Name);
        NTCRT_ERRNO = ERANGE;
        return NULL;
    }
    wcscpy(str, Name);
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    return str;
}

template<typename TChar>
static
errno_t
_NtCRT_TmpnamS(
    _Out_writes_z_(Size) TChar* Str,
    _In_ size_t Size,
    _In_ TChar* Name)
{
    size_t Length;

    if (Name == NULL)
    {
        Str[0] = 0;
        return NTCRT_ERRNO;
    }
    for (Length = 0; Name[Length] != 0; Length++)
    {
    }
    if (Length >= Size)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Name);
        Str[0] = 0;
        return _NtCRT_InvalidParameter(ERANGE);
    }
    memcpy(Str, Name, (Length + 1) * sizeof(TChar));
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT_tmpnam_s(char* str, size_t sizeInChars)
{
    if (str == NULL || sizeInChars == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    return _NtCRT_TmpnamS(str, sizeInChars, _NtCRT_tempnam(NULL, "s"));
}

EXTERN_C errno_t __cdecl _NtCRT__wtmpnam_s(wchar_t* str, size_t sizeInChars)
{
    if (str == NULL || sizeInChars == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    return _NtCRT_TmpnamS(str, sizeInChars, _NtCRT_MakeTempName(NULL, L"s"));
}

EXTERN_C void __cdecl _NtCRT_perror(const char* str)
{
    const char* Message = _NtCRT_strerror(NTCRT_ERRNO);
    FILE* Stream = &_NtCRT_iob[2];

    _NtCRT_lock_file(Stream);
    _NtCRT_flush_nolock(Stream);
    if (str != NULL && *str != '\0')
    {
        _NtCRT_write(2, str, (unsigned int)strlen(str));
        _NtCRT_write(2, ": ", 2);
    }
    _NtCRT_write(2, Message, (unsigned int)strlen(Message));
    _NtCRT_write(2, "\n", 1);
    _NtCRT_unlock_file(Stream);
}

/* The prefix is converted to the CRT code page */
EXTERN_C void __cdecl _NtCRT__wperror(const wchar_t* str)
{
    char* Narrow = NULL;

    if (str != NULL && *str != L'\0')
    {
        Narrow = _NtCRT_WideToAnsi(str);
    }
    _NtCRT_perror(Narrow);
    RtlFreeHeap(NTCRT_HEAP, 0, Narrow);
}

/* Characters of stdin and stdout */
EXTERN_C int __cdecl _NtCRT__fgetchar(void)
{
    return _NtCRT_getchar();
}

EXTERN_C int __cdecl _NtCRT__fputchar(int ch)
{
    return _NtCRT_putchar(ch);
}

EXTERN_C wint_t __cdecl _NtCRT__fgetwchar(void)
{
    return _NtCRT_getwchar();
}

EXTERN_C wint_t __cdecl _NtCRT__fputwchar(wchar_t ch)
{
    return _NtCRT_putwchar(ch);
}

/* The bytes of an int */
EXTERN_C int __cdecl _NtCRT__getw(FILE* stream)
{
    int Word;
    unsigned char* p = (unsigned char*)&Word;

    NTCRT_CHECK_STREAM(stream, EOF);
    _NtCRT_lock_file(stream);
    for (size_t i = 0; i < sizeof(Word); i++)
    {
        p[i] = (unsigned char)_NtCRT__fgetc_nolock(stream);
    }
    if (stream->_flag & (_IOEOF | _IOERR))
    {
        Word = EOF;
    }
    _NtCRT_unlock_file(stream);
    return Word;
}

EXTERN_C int __cdecl _NtCRT__putw(int word, FILE* stream)
{
    const unsigned char* p = (const unsigned char*)&word;

    NTCRT_CHECK_STREAM(stream, EOF);
    _NtCRT_lock_file(stream);
    for (size_t i = 0; i < sizeof(word); i++)
    {
        _NtCRT__fputc_nolock(p[i], stream);
    }
    if (stream->_flag & _IOERR)
    {
        word = EOF;
    }
    _NtCRT_unlock_file(stream);
    return word;
}

EXTERN_C errno_t __cdecl _NtCRT_clearerr_s(FILE* stream)
{
    if (stream == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    _NtCRT_clearerr(stream);
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT_freopen_s(FILE** pFile, const char* path, const char* mode, FILE* stream)
{
    if (pFile == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pFile = _NtCRT_freopen(path, mode, stream);
    return *pFile != NULL ? 0 : NTCRT_ERRNO;
}

EXTERN_C errno_t __cdecl _NtCRT__wfreopen_s(FILE** pFile, const wchar_t* path, const wchar_t* mode, FILE* stream)
{
    if (pFile == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pFile = _NtCRT__wfreopen(path, mode, stream);
    return *pFile != NULL ? 0 : NTCRT_ERRNO;
}

/*
 * Formatted I/O
 *
 * The narrow functions use the C99 implementation of mingw-w64, which accepts the format extensions of msvcrt.
 * The wide functions of msvcrt treat %s and %c as wide, the format is rewritten for mingw-w64 before use.
 */

static
PCWSTR
_NtCRT_TranslateWideFormat(
    _In_ PCWSTR Format,
    _Out_writes_(BufferCount) PWSTR Buffer,
    _In_ size_t BufferCount,
    _In_ BOOL Scan,
    _Outptr_result_maybenull_ PWSTR* Allocated)
{
    size_t Length = wcslen(Format);
    size_t Capacity = Length * 2 + 1;
    PWSTR Out, o;
    PCWSTR f = Format;

    *Allocated = NULL;
    if (wcschr(Format, L'%') == NULL)
    {
        return Format;
    }
    if (Capacity <= BufferCount)
    {
        Out = Buffer;
    } else
    {
        Out = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, Capacity * sizeof(WCHAR));
        if (Out == NULL)
        {
            return Format;
        }
        *Allocated = Out;
    }
    o = Out;
    while (*f != L'\0')
    {
        BOOL Long = FALSE, Short = FALSE;
        WCHAR Conv;

        if (*f != L'%')
        {
            *o++ = *f++;
            continue;
        }
        *o++ = *f++;
        if (*f == L'%')
        {
            *o++ = *f++;
            continue;
        }
        /* Flags, width, precision and argument suppression */
        while (*f != L'\0' && wcschr(L"-+ #0123456789.*$'", *f) != NULL)
        {
            *o++ = *f++;
        }
        /* Length modifiers */
        for (;;)
        {
            if (*f == L'h')
            {
                Short = TRUE;
                f++;
            } else if (*f == L'l' || *f == L'w')
            {
                if (Long)
                {
                    /* ll */
                    *o++ = L'l';
                    *o++ = L'l';
                    Long = FALSE;
                    f++;
                    continue;
                }
                Long = TRUE;
                f++;
            } else if (*f == L'I')
            {
                *o++ = *f++;
                if ((f[0] == L'6' && f[1] == L'4') || (f[0] == L'3' && f[1] == L'2'))
                {
                    *o++ = *f++;
                    *o++ = *f++;
                }
            } else if (*f != L'\0' && wcschr(L"Lqjzt", *f) != NULL)
            {
                *o++ = *f++;
            } else
            {
                break;
            }
        }
        Conv = *f;
        if (Conv == L's' || Conv == L'c' || (Scan && Conv == L'['))
        {
            /* Wide unless h is specified */
            if (!Short)
            {
                *o++ = L'l';
            }
        } else if (Conv == L'S' || Conv == L'C')
        {
            /* Narrow unless l is specified */
            if (Long)
            {
                *o++ = L'l';
            }
            Conv = Conv == L'S' ? L's' : L'c';
        } else
        {
            if (Short)
            {
                *o++ = L'h';
            }
            if (Long)
            {
                *o++ = L'l';
            }
        }
        if (Conv == L'\0')
        {
            break;
        }
        *o++ = Conv;
        f++;
        if (Scan && Conv == L'[')
        {
            /* Copy the scan set */
            if (*f == L'^')
            {
                *o++ = *f++;
            }
            if (*f == L']')
            {
                *o++ = *f++;
            }
            while (*f != L'\0' && *f != L']')
            {
                *o++ = *f++;
            }
            if (*f == L']')
            {
                *o++ = *f++;
            }
        }
    }
    *o = L'\0';
    return Out;
}

#define NTCRT_WIDE_FORMAT(Format, Scan)                                                                                \
    WCHAR _FormatBuffer[256];                                                                                          \
    PWSTR _FormatAllocated;                                                                                            \
    PCWSTR _Format =                                                                                                   \
        _NtCRT_TranslateWideFormat((Format), _FormatBuffer, ARRAYSIZE(_FormatBuffer), (Scan), &_FormatAllocated)

#define NTCRT_WIDE_FORMAT_FREE() \
    do { \
        if (_FormatAllocated != NULL) \
        { \
            RtlFreeHeap(NTCRT_HEAP, 0, _FormatAllocated); \
        } \
    } while (0)

/* Narrow printf */

EXTERN_C int __cdecl _NtCRT_vfprintf(FILE* stream, const char* format, va_list ap)
{
    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return __mingw_vfprintf(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT_fprintf(FILE* stream, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfprintf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vprintf(const char* format, va_list ap)
{
    return _NtCRT_vfprintf(&_NtCRT_iob[1], format, ap);
}

EXTERN_C int __cdecl _NtCRT_printf(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfprintf(&_NtCRT_iob[1], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vsprintf(char* buffer, const char* format, va_list ap)
{
    if (buffer == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return __mingw_vsprintf(buffer, format, ap);
}

EXTERN_C int __cdecl _NtCRT_sprintf(char* buffer, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vsprintf(buffer, format, ap);
    va_end(ap);
    return Ret;
}

/* msvcrt semantics: no terminator and -1 returned on truncation */
EXTERN_C int __cdecl _NtCRT__vsnprintf(char* buffer, size_t count, const char* format, va_list ap)
{
    va_list Copy;
    int Length;

    if (format == NULL || (buffer == NULL && count != 0))
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    va_copy(Copy, ap);
    Length = __mingw_vsnprintf(buffer, count, format, ap);
    if (Length >= 0 && (size_t)Length >= count && count != 0)
    {
        /* C99 truncation dropped the last character for the terminator, format again in full */
        char* Full = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, (size_t)Length + 1);
        if (Full != NULL)
        {
            __mingw_vsnprintf(Full, (size_t)Length + 1, format, Copy);
            memcpy(buffer, Full, count);
            RtlFreeHeap(NTCRT_HEAP, 0, Full);
        }
        if ((size_t)Length > count)
        {
            Length = -1;
        }
    } else if (Length >= 0 && count == 0 && buffer != NULL)
    {
        Length = Length == 0 ? 0 : -1;
    }
    va_end(Copy);
    return Length;
}

EXTERN_C int __cdecl _NtCRT__snprintf(char* buffer, size_t count, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vsnprintf(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

/* C99 semantics */
EXTERN_C int __cdecl _NtCRT_vsnprintf(char* buffer, size_t count, const char* format, va_list ap)
{
    return __mingw_vsnprintf(buffer, count, format, ap);
}

EXTERN_C int __cdecl _NtCRT_snprintf(char* buffer, size_t count, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = __mingw_vsnprintf(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vscprintf(const char* format, va_list ap)
{
    if (format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return __mingw_vsnprintf(NULL, 0, format, ap);
}

EXTERN_C int __cdecl _NtCRT__scprintf(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vscprintf(format, ap);
    va_end(ap);
    return Ret;
}

/* Wide printf */

EXTERN_C int __cdecl _NtCRT_vfwprintf(FILE* stream, const wchar_t* format, va_list ap)
{
    int Ret;

    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    NTCRT_WIDE_FORMAT(format, FALSE);
    Ret = __mingw_vfwprintf(stream, _Format, ap);
    NTCRT_WIDE_FORMAT_FREE();
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_fwprintf(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwprintf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vwprintf(const wchar_t* format, va_list ap)
{
    return _NtCRT_vfwprintf(&_NtCRT_iob[1], format, ap);
}

EXTERN_C int __cdecl _NtCRT_wprintf(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwprintf(&_NtCRT_iob[1], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vsnwprintf(wchar_t* buffer, size_t count, const wchar_t* format, va_list ap)
{
    va_list Copy;
    int Length;

    if (format == NULL || (buffer == NULL && count != 0))
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    NTCRT_WIDE_FORMAT(format, FALSE);
    va_copy(Copy, ap);
    Length = __mingw_vsnwprintf(buffer, count, _Format, ap);
    if (Length >= 0 && (size_t)Length >= count && count != 0)
    {
        wchar_t* Full = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, ((size_t)Length + 1) * sizeof(wchar_t));
        if (Full != NULL)
        {
            __mingw_vsnwprintf(Full, (size_t)Length + 1, _Format, Copy);
            memcpy(buffer, Full, count * sizeof(wchar_t));
            RtlFreeHeap(NTCRT_HEAP, 0, Full);
        }
        if ((size_t)Length > count)
        {
            Length = -1;
        }
    } else if (Length >= 0 && count == 0 && buffer != NULL)
    {
        Length = Length == 0 ? 0 : -1;
    }
    va_end(Copy);
    NTCRT_WIDE_FORMAT_FREE();
    return Length;
}

EXTERN_C int __cdecl _NtCRT__snwprintf(wchar_t* buffer, size_t count, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vsnwprintf(buffer, count, format, ap);
    va_end(ap);
    return Ret;
}

/* Legacy swprintf of msvcrt without a buffer size */
EXTERN_C int __cdecl _NtCRT_vswprintf(wchar_t* buffer, const wchar_t* format, va_list ap)
{
    return _NtCRT__vsnwprintf(buffer, INT_MAX / sizeof(wchar_t), format, ap);
}

EXTERN_C int __cdecl _NtCRT_swprintf(wchar_t* buffer, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vswprintf(buffer, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vscwprintf(const wchar_t* format, va_list ap)
{
    int Ret;

    if (format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    NTCRT_WIDE_FORMAT(format, FALSE);
    Ret = __mingw_vsnwprintf(NULL, 0, _Format, ap);
    NTCRT_WIDE_FORMAT_FREE();
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__scwprintf(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vscwprintf(format, ap);
    va_end(ap);
    return Ret;
}

/*
 * mingw-w64 wide pformat writes strings to a FILE with __ms_fwprintf and "%[-]*.*s" / "%[-]*.*S",
 * so it must not call back into pformat. Only string and character conversions are supported.
 */
/* As msvcrt, a character that the "C" locale cannot write to a text mode stream is dropped and still counted */
static
bool
_NtCRT_MsPutWideChar(
    _In_ FILE* Stream,
    _In_ wchar_t Ch)
{
    if (Ch > 0xFF && _NtCRT_fd_textmode(Stream->_file) == NTCRT_FD_ANSI)
    {
        NTCRT_ERRNO = EILSEQ;
        return true;
    }
    return _NtCRT_putwc_nolock(Ch, Stream) != WEOF;
}

static
int
_NtCRT_MsPutWide(
    _In_ FILE* Stream,
    _In_reads_(Count) const wchar_t* Str,
    _In_ int Count,
    _In_ int Width,
    _In_ BOOL Left)
{
    int Pad = Width > Count ? Width - Count : 0;

    for (int i = 0; !Left && i < Pad; i++)
    {
        if (!_NtCRT_MsPutWideChar(Stream, L' '))
        {
            return -1;
        }
    }
    for (int i = 0; i < Count; i++)
    {
        if (!_NtCRT_MsPutWideChar(Stream, Str[i]))
        {
            return -1;
        }
    }
    for (int i = 0; Left && i < Pad; i++)
    {
        if (!_NtCRT_MsPutWideChar(Stream, L' '))
        {
            return -1;
        }
    }
    return Count + Pad;
}

EXTERN_C int __cdecl _NtCRT___ms_vfwprintf(FILE* stream, const wchar_t* format, va_list ap)
{
    int Total = 0;

    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_lock_file(stream);
    for (const wchar_t* f = format; *f != L'\0'; f++)
    {
        BOOL Left = FALSE, Narrow;
        int Width = 0, Precision = -1, Written;
        WCHAR Modifier = 0;

        if (*f != L'%')
        {
            if (!_NtCRT_MsPutWideChar(stream, *f))
            {
                goto _Fail;
            }
            Total++;
            continue;
        }
        f++;
        if (*f == L'%')
        {
            if (!_NtCRT_MsPutWideChar(stream, L'%'))
            {
                goto _Fail;
            }
            Total++;
            continue;
        }
        if (*f == L'-')
        {
            Left = TRUE;
            f++;
        }
        if (*f == L'*')
        {
            Width = va_arg(ap, int);
            if (Width < 0)
            {
                Left = TRUE;
                Width = -Width;
            }
            f++;
        } else
        {
            while (*f >= L'0' && *f <= L'9')
            {
                Width = Width * 10 + (*f++ - L'0');
            }
        }
        if (*f == L'.')
        {
            f++;
            Precision = 0;
            if (*f == L'*')
            {
                Precision = va_arg(ap, int);
                f++;
            } else
            {
                while (*f >= L'0' && *f <= L'9')
                {
                    Precision = Precision * 10 + (*f++ - L'0');
                }
            }
        }
        if (*f == L'h' || *f == L'l' || *f == L'w')
        {
            Modifier = *f++;
        }
        switch (*f)
        {
            case L's':
            case L'S':
                Narrow = Modifier == L'h' || (*f == L'S' && Modifier == 0);
                if (Narrow)
                {
                    const char* Str = va_arg(ap, const char*);
                    int Count;
                    PWSTR Wide;

                    if (Str == NULL)
                    {
                        Str = "(null)";
                    }
                    /* Bytes of the "C" locale */
                    Count = Precision >= 0 ? (int)strnlen(Str, Precision) : (int)strlen(Str);
                    Wide = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, (Count + 1) * sizeof(WCHAR));
                    if (Wide == NULL)
                    {
                        goto _Fail;
                    }
                    for (int i = 0; i < Count; i++)
                    {
                        Wide[i] = (unsigned char)Str[i];
                    }
                    Written = _NtCRT_MsPutWide(stream, Wide, Count, Width, Left);
                    RtlFreeHeap(NTCRT_HEAP, 0, Wide);
                } else
                {
                    const wchar_t* Str = va_arg(ap, const wchar_t*);
                    if (Str == NULL)
                    {
                        Str = L"(null)";
                    }
                    Written = _NtCRT_MsPutWide(stream,
                                               Str,
                                               Precision >= 0 ? (int)wcsnlen(Str, Precision) : (int)wcslen(Str),
                                               Width,
                                               Left);
                }
                break;
            case L'c':
            case L'C':
            {
                WCHAR Ch;
                if (Modifier == L'h' || (*f == L'C' && Modifier == 0))
                {
                    Ch = (unsigned char)va_arg(ap, int);
                } else
                {
                    Ch = (WCHAR)va_arg(ap, int);
                }
                Written = _NtCRT_MsPutWide(stream, &Ch, 1, Width, Left);
                break;
            }
            default:
                NTCRT_ERRNO = EINVAL;
                goto _Fail;
        }
        if (Written < 0)
        {
            goto _Fail;
        }
        Total += Written;
    }
    _NtCRT_unlock_file(stream);
    return Total;

_Fail:
    _NtCRT_unlock_file(stream);
    return -1;
}

EXTERN_C int __cdecl _NtCRT___ms_fwprintf(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT___ms_vfwprintf(stream, format, ap);
    va_end(ap);
    return Ret;
}

/*
 * The internal wide output of msvcrt, UTF-16 units are put into the stream buffer regardless of the text mode,
 * and it fails on a stream without a buffer
 */
EXTERN_C int __cdecl _NtCRT__woutput_s(FILE* stream, const wchar_t* format, va_list ap)
{
    va_list Copy;
    int Length;
    wchar_t* Buffer;

    if (stream == NULL || format == NULL)
    {
        _NtCRT_InvalidParameter(EINVAL);
        return -1;
    }
    va_copy(Copy, ap);
    Length = _NtCRT__vscwprintf(format, Copy);
    va_end(Copy);
    if (Length < 0)
    {
        return -1;
    }
    Buffer = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, ((size_t)Length + 1) * sizeof(wchar_t));
    if (Buffer == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return -1;
    }
    _NtCRT__vsnwprintf(Buffer, (size_t)Length + 1, format, ap);
    for (int i = 0; i < Length; i++)
    {
        if ((stream->_cnt -= sizeof(wchar_t)) >= 0)
        {
            memcpy(stream->_ptr, &Buffer[i], sizeof(wchar_t));
            stream->_ptr += sizeof(wchar_t);
        } else if (stream->_base == NULL || _NtCRT_FlushPut((const char*)&Buffer[i], sizeof(wchar_t), stream) != 0)
        {
            stream->_flag |= _IOERR;
            Length = -1;
            break;
        }
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Buffer);
    return Length;
}

/* Narrow scanf */

EXTERN_C int __cdecl _NtCRT_vfscanf(FILE* stream, const char* format, va_list ap)
{
    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    return __mingw_vfscanf(stream, format, ap);
}

EXTERN_C int __cdecl _NtCRT_fscanf(FILE* stream, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfscanf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vscanf(const char* format, va_list ap)
{
    return _NtCRT_vfscanf(&_NtCRT_iob[0], format, ap);
}

EXTERN_C int __cdecl _NtCRT_scanf(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfscanf(&_NtCRT_iob[0], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vsscanf(const char* str, const char* format, va_list ap)
{
    if (str == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    return __mingw_vsscanf(str, format, ap);
}

EXTERN_C int __cdecl _NtCRT_sscanf(const char* str, const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vsscanf(str, format, ap);
    va_end(ap);
    return Ret;
}

/* Wide scanf */

EXTERN_C int __cdecl _NtCRT_vfwscanf(FILE* stream, const wchar_t* format, va_list ap)
{
    int Ret;

    if (stream == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    NTCRT_WIDE_FORMAT(format, TRUE);
    Ret = __mingw_vfwscanf(stream, _Format, ap);
    NTCRT_WIDE_FORMAT_FREE();
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_fwscanf(FILE* stream, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwscanf(stream, format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vwscanf(const wchar_t* format, va_list ap)
{
    return _NtCRT_vfwscanf(&_NtCRT_iob[0], format, ap);
}

EXTERN_C int __cdecl _NtCRT_wscanf(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vfwscanf(&_NtCRT_iob[0], format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_vswscanf(const wchar_t* str, const wchar_t* format, va_list ap)
{
    int Ret;

    if (str == NULL || format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EOF;
    }
    NTCRT_WIDE_FORMAT(format, TRUE);
    Ret = __mingw_vswscanf(str, _Format, ap);
    NTCRT_WIDE_FORMAT_FREE();
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_swscanf(const wchar_t* str, const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT_vswscanf(str, format, ap);
    va_end(ap);
    return Ret;
}

NTCRT_IMPORT_DATA(_iob, _NtCRT_iob);
NTCRT_IMPORT(__iob_func, _NtCRT___iob_func);
NTCRT_IMPORT(__acrt_iob_func, _NtCRT___acrt_iob_func);
NTCRT_IMPORT(_lock_file, _NtCRT_lock_file);
NTCRT_IMPORT(_unlock_file, _NtCRT_unlock_file);
NTCRT_IMPORT(_filbuf, _NtCRT_filbuf);
NTCRT_IMPORT(_flsbuf, _NtCRT_flsbuf);
NTCRT_IMPORT(fgetc, _NtCRT_fgetc);
NTCRT_IMPORT(getc, _NtCRT_fgetc);
NTCRT_IMPORT(fputc, _NtCRT_fputc);
NTCRT_IMPORT(putc, _NtCRT_fputc);
NTCRT_IMPORT(ungetc, _NtCRT_ungetc);
NTCRT_IMPORT(getchar, _NtCRT_getchar);
NTCRT_IMPORT(putchar, _NtCRT_putchar);
NTCRT_IMPORT(_fgetc_nolock, _NtCRT__fgetc_nolock);
NTCRT_IMPORT(_fputc_nolock, _NtCRT__fputc_nolock);
NTCRT_IMPORT(_ungetc_nolock, _NtCRT__ungetc_nolock);
NTCRT_IMPORT(fgetwc, _NtCRT_fgetwc);
NTCRT_IMPORT(getwc, _NtCRT_fgetwc);
NTCRT_IMPORT(fputwc, _NtCRT_fputwc);
NTCRT_IMPORT(putwc, _NtCRT_fputwc);
NTCRT_IMPORT(ungetwc, _NtCRT_ungetwc);
NTCRT_IMPORT(getwchar, _NtCRT_getwchar);
NTCRT_IMPORT(putwchar, _NtCRT_putwchar);
NTCRT_IMPORT(_fgetwc_nolock, _NtCRT__fgetwc_nolock);
NTCRT_IMPORT(_fputwc_nolock, _NtCRT__fputwc_nolock);
NTCRT_IMPORT(_ungetwc_nolock, _NtCRT__ungetwc_nolock);
NTCRT_IMPORT(fgets, _NtCRT_fgets);
NTCRT_IMPORT(fgetws, _NtCRT_fgetws);
NTCRT_IMPORT(gets, _NtCRT_gets);
NTCRT_IMPORT(_getws, _NtCRT__getws);
NTCRT_IMPORT(fputs, _NtCRT_fputs);
NTCRT_IMPORT(puts, _NtCRT_puts);
NTCRT_IMPORT(fputws, _NtCRT_fputws);
NTCRT_IMPORT(_putws, _NtCRT__putws);
NTCRT_IMPORT(fread, _NtCRT_fread);
NTCRT_IMPORT(fwrite, _NtCRT_fwrite);
NTCRT_IMPORT(feof, _NtCRT_feof);
NTCRT_IMPORT(ferror, _NtCRT_ferror);
NTCRT_IMPORT(clearerr, _NtCRT_clearerr);
NTCRT_IMPORT(_fileno, _NtCRT_fileno);
NTCRT_IMPORT(fileno, _NtCRT_fileno);
NTCRT_IMPORT(fflush, _NtCRT_fflush);
NTCRT_IMPORT(_fflush_nolock, _NtCRT__fflush_nolock);
NTCRT_IMPORT(_ftelli64, _NtCRT_ftelli64);
NTCRT_IMPORT(ftell, _NtCRT_ftell);
NTCRT_IMPORT(_fseeki64, _NtCRT_fseeki64);
NTCRT_IMPORT(fseek, _NtCRT_fseek);
NTCRT_IMPORT(rewind, _NtCRT_rewind);
NTCRT_IMPORT(fgetpos, _NtCRT_fgetpos);
NTCRT_IMPORT(fsetpos, _NtCRT_fsetpos);
NTCRT_IMPORT(setvbuf, _NtCRT_setvbuf);
NTCRT_IMPORT(setbuf, _NtCRT_setbuf);
NTCRT_IMPORT(fopen, _NtCRT_fopen);
NTCRT_IMPORT(_wfopen, _NtCRT__wfopen);
NTCRT_IMPORT(_fsopen, _NtCRT__fsopen);
NTCRT_IMPORT(_wfsopen, _NtCRT__wfsopen);
NTCRT_IMPORT(fopen_s, _NtCRT_fopen_s);
NTCRT_IMPORT(_wfopen_s, _NtCRT__wfopen_s);
NTCRT_IMPORT(_fdopen, _NtCRT__fdopen);
NTCRT_IMPORT(fdopen, _NtCRT__fdopen);
NTCRT_IMPORT(_wfdopen, _NtCRT__wfdopen);
NTCRT_IMPORT(fclose, _NtCRT_fclose);
NTCRT_IMPORT(freopen, _NtCRT_freopen);
NTCRT_IMPORT(_wfreopen, _NtCRT__wfreopen);
NTCRT_IMPORT(_flushall, _NtCRT__flushall);
NTCRT_IMPORT(_fcloseall, _NtCRT__fcloseall);
NTCRT_IMPORT(_getmaxstdio, _NtCRT__getmaxstdio);
NTCRT_IMPORT(_setmaxstdio, _NtCRT__setmaxstdio);
NTCRT_IMPORT(remove, _NtCRT_remove);
NTCRT_IMPORT(_wremove, _NtCRT__wremove);
NTCRT_IMPORT(_unlink, _NtCRT_remove);
NTCRT_IMPORT(unlink, _NtCRT_remove);
NTCRT_IMPORT(_wunlink, _NtCRT__wremove);
NTCRT_IMPORT(rename, _NtCRT_rename);
NTCRT_IMPORT(_wrename, _NtCRT__wrename);
NTCRT_IMPORT(tmpfile, _NtCRT_tmpfile);
NTCRT_IMPORT(_tempnam, _NtCRT_tempnam);
NTCRT_IMPORT(tempnam, _NtCRT_tempnam);
NTCRT_IMPORT(_wtempnam, _NtCRT__wtempnam);
NTCRT_IMPORT(tmpnam, _NtCRT_tmpnam);
NTCRT_IMPORT(perror, _NtCRT_perror);
NTCRT_IMPORT(_wperror, _NtCRT__wperror);
NTCRT_IMPORT(tmpfile_s, _NtCRT_tmpfile_s);
NTCRT_IMPORT(_rmtmp, _NtCRT__rmtmp);
NTCRT_IMPORT(rmtmp, _NtCRT__rmtmp);
NTCRT_IMPORT(_wtmpnam, _NtCRT__wtmpnam);
NTCRT_IMPORT(tmpnam_s, _NtCRT_tmpnam_s);
NTCRT_IMPORT(_wtmpnam_s, _NtCRT__wtmpnam_s);
NTCRT_IMPORT(_fgetchar, _NtCRT__fgetchar);
NTCRT_IMPORT(fgetchar, _NtCRT__fgetchar);
NTCRT_IMPORT(_fputchar, _NtCRT__fputchar);
NTCRT_IMPORT(fputchar, _NtCRT__fputchar);
NTCRT_IMPORT(_fgetwchar, _NtCRT__fgetwchar);
NTCRT_IMPORT(fgetwchar, _NtCRT__fgetwchar);
NTCRT_IMPORT(_fputwchar, _NtCRT__fputwchar);
NTCRT_IMPORT(fputwchar, _NtCRT__fputwchar);
NTCRT_IMPORT(_getw, _NtCRT__getw);
NTCRT_IMPORT(getw, _NtCRT__getw);
NTCRT_IMPORT(_putw, _NtCRT__putw);
NTCRT_IMPORT(putw, _NtCRT__putw);
NTCRT_IMPORT(clearerr_s, _NtCRT_clearerr_s);
NTCRT_IMPORT(freopen_s, _NtCRT_freopen_s);
NTCRT_IMPORT(_wfreopen_s, _NtCRT__wfreopen_s);
NTCRT_IMPORT(vfprintf, _NtCRT_vfprintf);
NTCRT_IMPORT(fprintf, _NtCRT_fprintf);
NTCRT_IMPORT(vprintf, _NtCRT_vprintf);
NTCRT_IMPORT(printf, _NtCRT_printf);
NTCRT_IMPORT(vsprintf, _NtCRT_vsprintf);
NTCRT_IMPORT(sprintf, _NtCRT_sprintf);
NTCRT_IMPORT(_vsnprintf, _NtCRT__vsnprintf);
NTCRT_IMPORT(_snprintf, _NtCRT__snprintf);
NTCRT_IMPORT(_vscprintf, _NtCRT__vscprintf);
NTCRT_IMPORT(_scprintf, _NtCRT__scprintf);
NTCRT_IMPORT(vfwprintf, _NtCRT_vfwprintf);
NTCRT_IMPORT(fwprintf, _NtCRT_fwprintf);
NTCRT_IMPORT(__ms_fwprintf, _NtCRT___ms_fwprintf);
NTCRT_IMPORT(_woutput_s, _NtCRT__woutput_s);
NTCRT_IMPORT(__ms_vfwprintf, _NtCRT___ms_vfwprintf);
NTCRT_IMPORT(vwprintf, _NtCRT_vwprintf);
NTCRT_IMPORT(wprintf, _NtCRT_wprintf);
NTCRT_IMPORT(_vsnwprintf, _NtCRT__vsnwprintf);
NTCRT_IMPORT(_snwprintf, _NtCRT__snwprintf);
NTCRT_IMPORT(snwprintf, _NtCRT__snwprintf);
NTCRT_IMPORT(vsnwprintf, _NtCRT__vsnwprintf);
NTCRT_IMPORT(vswprintf, _NtCRT_vswprintf);
NTCRT_IMPORT(swprintf, _NtCRT_swprintf);
NTCRT_IMPORT(_vswprintf, _NtCRT_vswprintf);
NTCRT_IMPORT(_swprintf, _NtCRT_swprintf);
NTCRT_IMPORT(_vscwprintf, _NtCRT__vscwprintf);
NTCRT_IMPORT(_scwprintf, _NtCRT__scwprintf);
NTCRT_IMPORT_ONLY(vfscanf, _NtCRT_vfscanf);
NTCRT_IMPORT(fscanf, _NtCRT_fscanf);
NTCRT_IMPORT_ONLY(vscanf, _NtCRT_vscanf);
NTCRT_IMPORT(scanf, _NtCRT_scanf);
NTCRT_IMPORT_ONLY(vsscanf, _NtCRT_vsscanf);
NTCRT_IMPORT(sscanf, _NtCRT_sscanf);
NTCRT_IMPORT_ONLY(vfwscanf, _NtCRT_vfwscanf);
NTCRT_IMPORT(fwscanf, _NtCRT_fwscanf);
NTCRT_IMPORT_ONLY(vwscanf, _NtCRT_vwscanf);
NTCRT_IMPORT(wscanf, _NtCRT_wscanf);
NTCRT_IMPORT_ONLY(vswscanf, _NtCRT_vswscanf);
NTCRT_IMPORT(swscanf, _NtCRT_swscanf);
