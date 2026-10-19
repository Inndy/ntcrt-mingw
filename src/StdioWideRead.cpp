/* stdio input of wide characters and strings, see StdioWide.cpp for the conversions */

#include "NtCRT.h"
#include "Stdio.inl"

#include <errno.h>

static
wint_t
_NtCRT_getwc_nolock(
    _In_ FILE* Stream)
{
    PNTCRT_STREAM_MB Mb;
    int Lo, Hi;

    if (_NtCRT_fd_textmode(Stream->_file) == NTCRT_FD_ANSI && _NtCRT_CurrentCodePage() == CP_UTF8)
    {
        Mb = _NtCRT_StreamMb(Stream);
        if (Mb->Pushed != 0)
        {
            wchar_t Ch = Mb->Pushed;

            Mb->Pushed = 0;
            return Ch;
        }
        for (;;)
        {
            char Byte;
            wchar_t Ch;
            size_t Ret;

            Lo = _NtCRT_getc_nolock(Stream);
            if (Lo == EOF)
            {
                if (!mbsinit(&Mb->GetState))
                {
                    /* A truncated sequence is an encoding error as an invalid one */
                    Mb->GetState = 0;
                    Stream->_flag |= _IOERR;
                    NTCRT_ERRNO = EILSEQ;
                }
                return WEOF;
            }
            Byte = (char)Lo;
            Ret = mbrtowc(&Ch, &Byte, 1, &Mb->GetState);
            if (Ret == (size_t)-1)
            {
                Stream->_flag |= _IOERR;
                return WEOF;
            }
            if (Ret != (size_t)-2)
            {
                return Ch;
            }
        }
    }
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
        if (_NtCRT_CurrentCodePage() == CP_UTF8 && Ch >= 0x80)
        {
            PNTCRT_STREAM_MB Mb = _NtCRT_StreamMb(Stream);

            if (Mb->Pushed != 0)
            {
                return WEOF;
            }
            Mb->Pushed = (wchar_t)Ch;
            Stream->_flag &= ~_IOEOF;
            return Ch;
        }
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

EXTERN_C wint_t __cdecl _NtCRT__fgetwc_nolock(FILE* stream)
{
    return _NtCRT_getwc_nolock(stream);
}

EXTERN_C wint_t __cdecl _NtCRT__ungetwc_nolock(wint_t ch, FILE* stream)
{
    return _NtCRT_ungetwc_nolock(ch, stream);
}

EXTERN_C wchar_t* __cdecl _NtCRT_fgetws(wchar_t* str, int n, FILE* stream)
{
    wchar_t* p = str;
    int Error;

    if (str == NULL || n <= 0 || stream == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    _NtCRT_lock_file(stream);
    /* A read or encoding error in this call fails it, also after some characters */
    Error = stream->_flag & _IOERR;
    stream->_flag &= ~_IOERR;
    while (--n > 0)
    {
        wint_t Ch = _NtCRT_getwc_nolock(stream);
        if (Ch == WEOF)
        {
            if (p == str || (stream->_flag & _IOERR))
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
    stream->_flag |= Error;
    _NtCRT_unlock_file(stream);
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

EXTERN_C wint_t __cdecl _NtCRT__fgetwchar(void)
{
    return _NtCRT_getwchar();
}

NTCRT_IMPORT(fgetwc, _NtCRT_fgetwc);
NTCRT_IMPORT(getwc, _NtCRT_fgetwc);
NTCRT_IMPORT(ungetwc, _NtCRT_ungetwc);
NTCRT_IMPORT(getwchar, _NtCRT_getwchar);
NTCRT_IMPORT(_fgetwc_nolock, _NtCRT__fgetwc_nolock);
NTCRT_IMPORT(_ungetwc_nolock, _NtCRT__ungetwc_nolock);
NTCRT_IMPORT(fgetws, _NtCRT_fgetws);
NTCRT_IMPORT(_getws, _NtCRT__getws);
NTCRT_IMPORT(_fgetwchar, _NtCRT__fgetwchar);
NTCRT_IMPORT(fgetwchar, _NtCRT__fgetwchar);
