/* The streams of Stdio.cpp, shared with the other Stdio*.cpp */

#pragma once

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

/* Multibyte state of the wide character I/O of a text stream in "C.UTF-8" */
typedef struct _NTCRT_STREAM_MB
{
    mbstate_t PutState;
    mbstate_t GetState;
    wchar_t Pushed; /* A character pushed back by ungetwc that is not a byte, 0 if none */
} NTCRT_STREAM_MB, *PNTCRT_STREAM_MB;

typedef struct _NTCRT_FILEX
{
    FILE File;
    RTL_CRITICAL_SECTION Lock;
    NTCRT_STREAM_MB Mb;
} NTCRT_FILEX, *PNTCRT_FILEX;

/* The streams of _iob, their locks and multibyte states, and the streams beyond _iob allocated on use */
extern "C" RTL_CRITICAL_SECTION _NtCRT_IobLocks[NTCRT_IOB_ENTRIES];
extern "C" NTCRT_STREAM_MB _NtCRT_IobMb[NTCRT_IOB_ENTRIES];
extern "C" PNTCRT_FILEX _NtCRT_Streams[NTCRT_NSTREAM - NTCRT_IOB_ENTRIES];

/* Initialize the locks of _iob once */
extern "C" void _NtCRT_EnsureStdio(void);
/* Allocate the buffer of a stream */
extern "C" void _NtCRT_getbuf(_In_ FILE* Stream);
extern "C" void _NtCRT_freebuf(_In_ FILE* Stream);
/* Write the pending output, returns 0 or EOF */
extern "C" int _NtCRT_flush_nolock(FILE* Stream);
/* Iterate all streams: Callback for each one under _IOB_SCAN_LOCK, returns the sum of its results */
extern "C" int _NtCRT_ForEachStream(_In_ int (*Callback)(FILE* Stream, int Context), _In_ int Context);
/* Flush the streams in use: 0 or EOF if FlushOnly (fflush(NULL)), else the count of the streams (_flushall) */
extern "C" int __cdecl _NtCRT_flushall_impl(BOOL FlushOnly);
extern "C" int __cdecl _NtCRT_filbuf(FILE* stream);
extern "C" int __cdecl _NtCRT_flsbuf(int ch, FILE* stream);
extern "C" int _NtCRT_ungetc_nolock(_In_ int Ch, _In_ FILE* Stream);
/* Allocate a stream, returned locked, see StdioOpen.cpp */
extern "C" FILE* _NtCRT_AllocStream(void);
extern "C" int _NtCRT_fclose_nolock(_In_ FILE* Stream);

static inline
PNTCRT_STREAM_MB
_NtCRT_StreamMb(
    _In_ FILE* Stream)
{
    if (Stream >= _NtCRT_iob && Stream < _NtCRT_iob + NTCRT_IOB_ENTRIES)
    {
        return &_NtCRT_IobMb[Stream - _NtCRT_iob];
    }
    return &CONTAINING_RECORD(Stream, NTCRT_FILEX, File)->Mb;
}

static inline
BOOL
_NtCRT_InUse(
    _In_ FILE* Stream)
{
    return (Stream->_flag & (_IOREAD | _IOWRT | _IORW)) != 0;
}

/* Character I/O */

static inline
int
_NtCRT_getc_nolock(
    _In_ FILE* Stream)
{
    return --Stream->_cnt >= 0 ? (unsigned char)*Stream->_ptr++ : _NtCRT_filbuf(Stream);
}

static inline
int
_NtCRT_putc_nolock(
    _In_ int Ch,
    _In_ FILE* Stream)
{
    return --Stream->_cnt >= 0 ? (unsigned char)(*Stream->_ptr++ = (char)Ch) : _NtCRT_flsbuf(Ch, Stream);
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
