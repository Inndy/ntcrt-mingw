/* msvcrt stdio: FILE streams in the layout of msvcrt _iobuf */

#include "NtCRT.h"
#include "Stdio.inl"

#include <errno.h>

extern "C"
{
    FILE _NtCRT_iob[NTCRT_IOB_ENTRIES] = {
        { NULL, 0, NULL, _IOREAD, 0, 0, 0, NULL },
        { NULL, 0, NULL, _IOWRT, 1, 0, 0, NULL },
        { NULL, 0, NULL, _IOWRT, 2, 0, 0, NULL },
    };
}

RTL_CRITICAL_SECTION _NtCRT_IobLocks[NTCRT_IOB_ENTRIES];
NTCRT_STREAM_MB _NtCRT_IobMb[NTCRT_IOB_ENTRIES];
PNTCRT_FILEX _NtCRT_Streams[NTCRT_NSTREAM - NTCRT_IOB_ENTRIES];
static volatile LONG g_StdioState;

EXTERN_C
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
            RtlInitializeCriticalSection(&_NtCRT_IobLocks[i]);
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
        return &_NtCRT_IobLocks[Stream - _NtCRT_iob];
    }
    return &CONTAINING_RECORD(Stream, NTCRT_FILEX, File)->Lock;
}

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

EXTERN_C
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

EXTERN_C
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

EXTERN_C
int
_NtCRT_flush_nolock(
    FILE* Stream)
{
    int Result = 0;

    if ((Stream->_flag & (_IOREAD | _IOWRT)) == _IOWRT)
    {
        int Count = Stream->_base != NULL && !(Stream->_flag & _IONBF) ? (int)(Stream->_ptr - Stream->_base) : 0;

        if (Count > 0 && _NtCRT_write(Stream->_file, Stream->_base, Count) != Count)
        {
            Stream->_flag |= _IOERR;
            Result = EOF;
        } else if (Stream->_flag & _IORW)
        {
            /* Flushed, a read/write stream may read next */
            Stream->_flag &= ~_IOWRT;
        }
    }
    Stream->_ptr = Stream->_base;
    Stream->_cnt = 0;
    return Result;
}

/* Flush the buffer and put the bytes of a character, returns 0 or EOF */
EXTERN_C
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

EXTERN_C int __cdecl _NtCRT_putchar(int ch)
{
    return _NtCRT_fputc(ch, &_NtCRT_iob[1]);
}

EXTERN_C int __cdecl _NtCRT__fputc_nolock(int ch, FILE* stream)
{
    return _NtCRT_putc_nolock(ch, stream);
}

EXTERN_C size_t __cdecl _NtCRT_fwrite(const void* buffer, size_t size, size_t count, FILE* stream);

/* The output of printf (Printf.cpp) to a narrow stream, 0 or -1 */
EXTERN_C int _NtCRT_StreamWrite(FILE* stream, const char* Bytes, size_t Length)
{
    NTCRT_CHECK_STREAM_ANSI(stream, -1);
    return _NtCRT_fwrite(Bytes, 1, Length, stream) == Length ? 0 : -1;
}

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

EXTERN_C
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
    for (unsigned int i = 0; i < ARRAYSIZE(_NtCRT_Streams) && _NtCRT_Streams[i] != NULL; i++)
    {
        Count += Callback(&_NtCRT_Streams[i]->File, Context);
    }
    _NtCRT_unlock(_IOB_SCAN_LOCK);
    return Count;
}

/*
 * Context: 1 for flush returning error count (fflush(NULL), output streams only), 0 for flush returning stream count
 * (_flushall, input buffers are discarded), and 2 when the process ends without exit: a lock that a terminated thread
 * held is never released and entering its critical section would end the process, that stream is left
 */
static
int
_NtCRT_FlushCallback(
    _In_ FILE* Stream,
    _In_ int Context)
{
    int ErrorCount = Context & 1;
    int Ret = 0;

    if (!_NtCRT_InUse(Stream))
    {
        return 0;
    }
    if ((Context & 2) == 0)
    {
        _NtCRT_lock_file(Stream);
    } else if (!RtlTryEnterCriticalSection(_NtCRT_StreamLock(Stream)))
    {
        return 0;
    }
    if (_NtCRT_InUse(Stream) && (!ErrorCount || (Stream->_flag & _IOWRT)))
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

EXTERN_C void _NtCRT_FlushAll(bool Terminating)
{
    /*
     * Nothing was written if the streams are not initialized (a terminated thread may have left that unfinished). The
     * scan lock is entered as msvcrt does, the process ends there if a terminated thread held it.
     */
    if (Terminating && __atomic_load_n(&g_StdioState, __ATOMIC_ACQUIRE) != 2)
    {
        return;
    }
    _NtCRT_ForEachStream(_NtCRT_FlushCallback, Terminating ? 3 : 1);
}

static
int
__cdecl
_NtCRT_InitFlushAtExit(void)
{
    _NtCRT_FlushAllAtExit = _NtCRT_FlushAll;
    return 0;
}

extern "C" __attribute__((section(".CRT$XIC"), used)) int(__cdecl* const _NtCRT_InitFlushAtExitPtr)(void) =
    _NtCRT_InitFlushAtExit;

EXTERN_C FILE* __cdecl _NtCRT___iob_func(void)
{
    return _NtCRT_iob;
}

EXTERN_C FILE* __cdecl _NtCRT___acrt_iob_func(unsigned index)
{
    return &_NtCRT_iob[index];
}

EXTERN_C int __cdecl _NtCRT__fputchar(int ch)
{
    return _NtCRT_putchar(ch);
}

NTCRT_IMPORT_DATA(_iob, _NtCRT_iob);
NTCRT_IMPORT(__iob_func, _NtCRT___iob_func);
NTCRT_IMPORT(__acrt_iob_func, _NtCRT___acrt_iob_func);
NTCRT_IMPORT(_lock_file, _NtCRT_lock_file);
NTCRT_IMPORT(_unlock_file, _NtCRT_unlock_file);
NTCRT_IMPORT(_flsbuf, _NtCRT_flsbuf);
NTCRT_IMPORT(fputc, _NtCRT_fputc);
NTCRT_IMPORT(putc, _NtCRT_fputc);
NTCRT_IMPORT(putchar, _NtCRT_putchar);
NTCRT_IMPORT(_fputc_nolock, _NtCRT__fputc_nolock);
NTCRT_IMPORT(fputs, _NtCRT_fputs);
NTCRT_IMPORT(puts, _NtCRT_puts);
NTCRT_IMPORT(fwrite, _NtCRT_fwrite);
NTCRT_IMPORT(_fputchar, _NtCRT__fputchar);
NTCRT_IMPORT(fputchar, _NtCRT__fputchar);
