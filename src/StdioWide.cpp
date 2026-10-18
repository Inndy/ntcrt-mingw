/* stdio output of wide characters and strings, UTF-16 or the multibyte characters of the locale */

#include "NtCRT.h"
#include "Stdio.inl"

#include <errno.h>

/*
 * Wide character I/O, text mode streams convert by the multibyte characters of the locale: a byte for each
 * character up to 0xFF in the "C" locale, UTF-8 with surrogate pairs in "C.UTF-8".
 */

EXTERN_C
wint_t
_NtCRT_putwc_nolock(
    _In_ wchar_t Ch,
    _In_ FILE* Stream)
{
    if (_NtCRT_fd_textmode(Stream->_file) == NTCRT_FD_ANSI)
    {
        char Bytes[MB_LEN_MAX];
        size_t Length;

        if (_NtCRT_CurrentCodePage() != CP_UTF8)
        {
            if (Ch > 0xFF)
            {
                NTCRT_ERRNO = EILSEQ;
                return WEOF;
            }
            return _NtCRT_putc_nolock(Ch, Stream) == EOF ? WEOF : Ch;
        }
        Length = wcrtomb(Bytes, Ch, &_NtCRT_StreamMb(Stream)->PutState);
        if (Length == (size_t)-1)
        {
            Stream->_flag |= _IOERR;
            return WEOF;
        }
        for (size_t i = 0; i < Length; i++)
        {
            if (_NtCRT_putc_nolock((unsigned char)Bytes[i], Stream) == EOF)
            {
                return WEOF;
            }
        }
        return Ch;
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

EXTERN_C wint_t __cdecl _NtCRT_fputwc(wchar_t ch, FILE* stream)
{
    wint_t Ret;

    NTCRT_CHECK_STREAM(stream, WEOF);
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_putwc_nolock(ch, stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C wint_t __cdecl _NtCRT_putwchar(wchar_t ch)
{
    return _NtCRT_fputwc(ch, &_NtCRT_iob[1]);
}

EXTERN_C wint_t __cdecl _NtCRT__fputwc_nolock(wchar_t ch, FILE* stream)
{
    return _NtCRT_putwc_nolock(ch, stream);
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

EXTERN_C wint_t __cdecl _NtCRT__fputwchar(wchar_t ch)
{
    return _NtCRT_putwchar(ch);
}

NTCRT_IMPORT(fputwc, _NtCRT_fputwc);
NTCRT_IMPORT(putwc, _NtCRT_fputwc);
NTCRT_IMPORT(putwchar, _NtCRT_putwchar);
NTCRT_IMPORT(_fputwc_nolock, _NtCRT__fputwc_nolock);
NTCRT_IMPORT(fputws, _NtCRT_fputws);
NTCRT_IMPORT(_putws, _NtCRT__putws);
NTCRT_IMPORT(_fputwchar, _NtCRT__fputwchar);
NTCRT_IMPORT(fputwchar, _NtCRT__fputwchar);
