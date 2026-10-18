/* msvcrt "_s" functions of strings, integer conversion, multibyte conversion and environment */

#include "NtCRT.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

EXTERN_C int __cdecl _NtCRT__putenv(const char* envstring);
EXTERN_C size_t __cdecl _NtCRT_mbstowcs(wchar_t* dest, const char* src, size_t count);
EXTERN_C size_t __cdecl _NtCRT_wcstombs(char* dest, const wchar_t* src, size_t count);
EXTERN_C int __cdecl _NtCRT__wputenv(const wchar_t* envstring);

template<typename TChar>
static
size_t
_NtCRT_StrLen(
    _In_z_ const TChar* Str)
{
    const TChar* p = Str;

    while (*p != 0)
    {
        p++;
    }
    return p - Str;
}

/* Length of Str within Size, or Size if there is no terminator */
template<typename TChar>
static
size_t
_NtCRT_StrNLen(
    _In_reads_or_z_(Size) const TChar* Str,
    _In_ size_t Size)
{
    size_t i = 0;

    while (i < Size && Str[i] != 0)
    {
        i++;
    }
    return i;
}

/* Empty Dest and report Errno */
template<typename TChar>
static
errno_t
_NtCRT_ResetAndFail(
    _Out_ TChar* Dest,
    _In_ errno_t Errno)
{
    *Dest = 0;
    return _NtCRT_InvalidParameter(Errno);
}

/*
 * Copy at most Count characters of Src after Offset characters of Dest, _TRUNCATE truncates to the size. As msvcrt,
 * the characters are copied before the size is checked and only the first one is reset on failure.
 */
template<typename TChar>
static
errno_t
_NtCRT_CopyN(
    _Inout_updates_z_(Size) TChar* Dest,
    _In_ size_t Size,
    _In_ size_t Offset,
    _In_z_ const TChar* Src,
    _In_ size_t Count)
{
    TChar* p = Dest + Offset;
    size_t Available = Size - Offset;

    if (Count == _TRUNCATE)
    {
        while ((*p++ = *Src++) != 0 && --Available > 0)
        {
        }
    } else
    {
        while ((*p++ = *Src++) != 0 && --Available > 0 && --Count > 0)
        {
        }
        if (Count == 0)
        {
            *p = 0;
        }
    }
    if (Available == 0)
    {
        if (Count == _TRUNCATE)
        {
            Dest[Size - 1] = 0;
            return STRUNCATE;
        }
        return _NtCRT_ResetAndFail(Dest, ERANGE);
    }
    return 0;
}

template<typename TChar>
static
errno_t
_NtCRT_StrCpyS(
    _Out_writes_z_(Size) TChar* Dest,
    _In_ size_t Size,
    _In_opt_z_ const TChar* Src,
    _In_ size_t Count,
    _In_ bool Append)
{
    size_t Offset = 0;

    if (Count == 0 && Dest == NULL && Size == 0 && !Append)
    {
        return 0;
    }
    if (Dest == NULL || Size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (Append)
    {
        Offset = _NtCRT_StrNLen(Dest, Size);
        if (Offset == Size)
        {
            return _NtCRT_ResetAndFail(Dest, EINVAL);
        }
    }
    if (Count == 0)
    {
        if (!Append)
        {
            *Dest = 0;
        }
        return 0;
    }
    if (Src == NULL)
    {
        return _NtCRT_ResetAndFail(Dest, EINVAL);
    }
    return _NtCRT_CopyN(Dest, Size, Offset, Src, Count);
}

EXTERN_C errno_t __cdecl _NtCRT_strcpy_s(char* dest, rsize_t size, const char* src)
{
    return _NtCRT_StrCpyS(dest, size, src, SIZE_MAX - 1, false);
}

EXTERN_C errno_t __cdecl _NtCRT_wcscpy_s(wchar_t* dest, rsize_t size, const wchar_t* src)
{
    return _NtCRT_StrCpyS(dest, size, src, SIZE_MAX - 1, false);
}

EXTERN_C errno_t __cdecl _NtCRT_strncpy_s(char* dest, rsize_t size, const char* src, rsize_t count)
{
    return _NtCRT_StrCpyS(dest, size, src, count, false);
}

EXTERN_C errno_t __cdecl _NtCRT_wcsncpy_s(wchar_t* dest, rsize_t size, const wchar_t* src, rsize_t count)
{
    return _NtCRT_StrCpyS(dest, size, src, count, false);
}

EXTERN_C errno_t __cdecl _NtCRT_strcat_s(char* dest, rsize_t size, const char* src)
{
    return _NtCRT_StrCpyS(dest, size, src, SIZE_MAX - 1, true);
}

EXTERN_C errno_t __cdecl _NtCRT_wcscat_s(wchar_t* dest, rsize_t size, const wchar_t* src)
{
    return _NtCRT_StrCpyS(dest, size, src, SIZE_MAX - 1, true);
}

EXTERN_C errno_t __cdecl _NtCRT_strncat_s(char* dest, rsize_t size, const char* src, rsize_t count)
{
    return _NtCRT_StrCpyS(dest, size, src, count, true);
}

EXTERN_C errno_t __cdecl _NtCRT_wcsncat_s(wchar_t* dest, rsize_t size, const wchar_t* src, rsize_t count)
{
    return _NtCRT_StrCpyS(dest, size, src, count, true);
}

/* Case mapping and filling in place, the "C" locale maps ASCII only */

enum class NtCRTStrOp
{
    Lower,
    Upper,
    Set
};

template<typename TChar>
static
errno_t
_NtCRT_StrModifyS(
    _Inout_updates_z_(Size) TChar* Str,
    _In_ size_t Size,
    _In_ NtCRTStrOp Op,
    _In_ TChar Fill,
    _In_ size_t Count)
{
    size_t Length;

    if (Op == NtCRTStrOp::Set && Count == 0 && Str == NULL && Size == 0)
    {
        return 0;
    }
    if (Str == NULL || Size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (Op == NtCRTStrOp::Set)
    {
        /* As msvcrt, the characters are filled before the terminator is checked */
        TChar* p = Str;
        size_t Available = Size;

        while (*p != 0 && Count > 0 && --Available > 0)
        {
            *p++ = Fill;
            Count--;
        }
        if (Count == 0)
        {
            while (*p != 0 && --Available > 0)
            {
                p++;
            }
        }
        return Available == 0 ? _NtCRT_ResetAndFail(Str, EINVAL) : 0;
    }
    Length = _NtCRT_StrNLen(Str, Size);
    if (Length == Size)
    {
        return _NtCRT_ResetAndFail(Str, EINVAL);
    }
    if (Length > Count)
    {
        Length = Count;
    }
    for (size_t i = 0; i < Length; i++)
    {
        switch (Op)
        {
            case NtCRTStrOp::Lower:
                if (sizeof(TChar) == sizeof(wchar_t))
                {
                    Str[i] = (TChar)towlower((wchar_t)Str[i]);
                } else if (Str[i] >= 'A' && Str[i] <= 'Z')
                {
                    Str[i] += 'a' - 'A';
                }
                break;
            case NtCRTStrOp::Upper:
                if (sizeof(TChar) == sizeof(wchar_t))
                {
                    Str[i] = (TChar)towupper((wchar_t)Str[i]);
                } else if (Str[i] >= 'a' && Str[i] <= 'z')
                {
                    Str[i] -= 'a' - 'A';
                }
                break;
            default:
                break;
        }
    }
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT__strlwr_s(char* str, size_t size)
{
    return _NtCRT_StrModifyS(str, size, NtCRTStrOp::Lower, '\0', SIZE_MAX);
}

EXTERN_C errno_t __cdecl _NtCRT__wcslwr_s(wchar_t* str, size_t size)
{
    return _NtCRT_StrModifyS(str, size, NtCRTStrOp::Lower, L'\0', SIZE_MAX);
}

EXTERN_C errno_t __cdecl _NtCRT__strupr_s(char* str, size_t size)
{
    return _NtCRT_StrModifyS(str, size, NtCRTStrOp::Upper, '\0', SIZE_MAX);
}

EXTERN_C errno_t __cdecl _NtCRT__wcsupr_s(wchar_t* str, size_t size)
{
    return _NtCRT_StrModifyS(str, size, NtCRTStrOp::Upper, L'\0', SIZE_MAX);
}

EXTERN_C errno_t __cdecl _NtCRT__strset_s(char* str, size_t size, int c)
{
    return _NtCRT_StrModifyS(str, size, NtCRTStrOp::Set, (char)c, SIZE_MAX);
}

EXTERN_C errno_t __cdecl _NtCRT__wcsset_s(wchar_t* str, size_t size, wchar_t c)
{
    return _NtCRT_StrModifyS(str, size, NtCRTStrOp::Set, c, SIZE_MAX);
}

EXTERN_C errno_t __cdecl _NtCRT__strnset_s(char* str, size_t size, int c, size_t count)
{
    return _NtCRT_StrModifyS(str, size, NtCRTStrOp::Set, (char)c, count);
}

EXTERN_C errno_t __cdecl _NtCRT__wcsnset_s(wchar_t* str, size_t size, wchar_t c, size_t count)
{
    return _NtCRT_StrModifyS(str, size, NtCRTStrOp::Set, c, count);
}

/* Integer to string, a sign is written for radix 10 only, as _itoa */
template<typename TChar>
static
errno_t
_NtCRT_XToAS(
    _In_ unsigned long long Value,
    _In_ bool Negative,
    _Out_writes_z_(Size) TChar* Buffer,
    _In_ size_t Size,
    _In_ int Radix)
{
    TChar Digits[66];
    size_t Count = 0, Length;

    if (Buffer == NULL || Size == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (Radix < 2 || Radix > 36)
    {
        return _NtCRT_ResetAndFail(Buffer, EINVAL);
    }
    if (Negative)
    {
        Value = 0 - Value;
    }
    do
    {
        unsigned int Digit = (unsigned int)(Value % Radix);
        Digits[Count++] = (TChar)(Digit < 10 ? '0' + Digit : 'a' + Digit - 10);
        Value /= Radix;
    } while (Value != 0);
    Length = Count + Negative;
    if (Length >= Size)
    {
        return _NtCRT_ResetAndFail(Buffer, ERANGE);
    }
    if (Negative)
    {
        *Buffer++ = '-';
    }
    while (Count > 0)
    {
        *Buffer++ = Digits[--Count];
    }
    *Buffer = 0;
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT__itoa_s(int value, char* buffer, size_t size, int radix)
{
    return _NtCRT_XToAS(radix == 10 ? (unsigned long long)(long long)value : (unsigned int)value,
                        radix == 10 && value < 0,
                        buffer,
                        size,
                        radix);
}

EXTERN_C errno_t __cdecl _NtCRT__itow_s(int value, wchar_t* buffer, size_t size, int radix)
{
    return _NtCRT_XToAS(radix == 10 ? (unsigned long long)(long long)value : (unsigned int)value,
                        radix == 10 && value < 0,
                        buffer,
                        size,
                        radix);
}

EXTERN_C errno_t __cdecl _NtCRT__ltoa_s(long value, char* buffer, size_t size, int radix)
{
    return _NtCRT_XToAS(radix == 10 ? (unsigned long long)(long long)value : (unsigned long)value,
                        radix == 10 && value < 0,
                        buffer,
                        size,
                        radix);
}

EXTERN_C errno_t __cdecl _NtCRT__ltow_s(long value, wchar_t* buffer, size_t size, int radix)
{
    return _NtCRT_XToAS(radix == 10 ? (unsigned long long)(long long)value : (unsigned long)value,
                        radix == 10 && value < 0,
                        buffer,
                        size,
                        radix);
}

EXTERN_C errno_t __cdecl _NtCRT__ultoa_s(unsigned long value, char* buffer, size_t size, int radix)
{
    return _NtCRT_XToAS(value, false, buffer, size, radix);
}

EXTERN_C errno_t __cdecl _NtCRT__ultow_s(unsigned long value, wchar_t* buffer, size_t size, int radix)
{
    return _NtCRT_XToAS(value, false, buffer, size, radix);
}

EXTERN_C errno_t __cdecl _NtCRT__i64toa_s(long long value, char* buffer, size_t size, int radix)
{
    return _NtCRT_XToAS((unsigned long long)value, radix == 10 && value < 0, buffer, size, radix);
}

EXTERN_C errno_t __cdecl _NtCRT__i64tow_s(long long value, wchar_t* buffer, size_t size, int radix)
{
    return _NtCRT_XToAS((unsigned long long)value, radix == 10 && value < 0, buffer, size, radix);
}

EXTERN_C errno_t __cdecl _NtCRT__ui64toa_s(unsigned long long value, char* buffer, size_t size, int radix)
{
    return _NtCRT_XToAS(value, false, buffer, size, radix);
}

EXTERN_C errno_t __cdecl _NtCRT__ui64tow_s(unsigned long long value, wchar_t* buffer, size_t size, int radix)
{
    return _NtCRT_XToAS(value, false, buffer, size, radix);
}

/*
 * Multibyte conversion by mbstowcs and wcstombs.
 * *pReturnValue counts the terminator, Count is the maximum to store without it or _TRUNCATE, ignored without a buffer.
 */

EXTERN_C errno_t __cdecl _NtCRT_mbstowcs_s(
    size_t* pReturnValue, wchar_t* wcstr, size_t sizeInWords, const char* mbstr, size_t count)
{
    size_t Length;
    errno_t Ret = 0;

    if (pReturnValue != NULL)
    {
        *pReturnValue = 0;
    }
    if ((wcstr == NULL && sizeInWords > 0) || (wcstr != NULL && sizeInWords == 0))
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (wcstr != NULL)
    {
        *wcstr = L'\0';
    }
    if (mbstr == NULL)
    {
        return count == 0 ? 0 : _NtCRT_InvalidParameter(EINVAL);
    }
    Length = _NtCRT_mbstowcs(NULL, mbstr, 0);
    if (Length == (size_t)-1)
    {
        NTCRT_ERRNO = EILSEQ;
        return EILSEQ;
    }
    if (wcstr != NULL && count != _TRUNCATE && count < Length)
    {
        Length = count;
    }
    if (wcstr != NULL)
    {
        if (Length >= sizeInWords)
        {
            if (count != _TRUNCATE)
            {
                return _NtCRT_InvalidParameter(ERANGE);
            }
            Length = sizeInWords - 1;
            Ret = STRUNCATE;
        }
        Length = _NtCRT_mbstowcs(wcstr, mbstr, Length);
        wcstr[Length] = L'\0';
    }
    if (pReturnValue != NULL)
    {
        *pReturnValue = Length + 1;
    }
    return Ret;
}

EXTERN_C errno_t __cdecl _NtCRT_wcstombs_s(
    size_t* pReturnValue, char* mbstr, size_t sizeInBytes, const wchar_t* wcstr, size_t count)
{
    size_t Length;
    errno_t Ret = 0;

    if (pReturnValue != NULL)
    {
        *pReturnValue = 0;
    }
    if ((mbstr == NULL && sizeInBytes > 0) || (mbstr != NULL && sizeInBytes == 0))
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (mbstr != NULL)
    {
        *mbstr = '\0';
    }
    if (wcstr == NULL)
    {
        return count == 0 ? 0 : _NtCRT_InvalidParameter(EINVAL);
    }
    Length = _NtCRT_wcstombs(NULL, wcstr, 0);
    if (Length == (size_t)-1)
    {
        NTCRT_ERRNO = EILSEQ;
        return EILSEQ;
    }
    if (mbstr != NULL && count != _TRUNCATE && count < Length)
    {
        Length = count;
    }
    if (mbstr != NULL)
    {
        if (Length >= sizeInBytes)
        {
            if (count != _TRUNCATE)
            {
                return _NtCRT_InvalidParameter(ERANGE);
            }
            Length = sizeInBytes - 1;
            Ret = STRUNCATE;
        }
        Length = _NtCRT_wcstombs(mbstr, wcstr, Length);
        mbstr[Length] = '\0';
    }
    if (pReturnValue != NULL)
    {
        *pReturnValue = Length + 1;
    }
    return Ret;
}

/* Restartable conversions, by the C99 functions of mingw-w64 */

EXTERN_C errno_t __cdecl _NtCRT_wcrtomb_s(
    size_t* pReturnValue, char* mbchar, size_t sizeInBytes, wchar_t wchar, mbstate_t* mbstate)
{
    char Buffer[MB_LEN_MAX];
    size_t Length;

    if (pReturnValue != NULL)
    {
        *pReturnValue = (size_t)-1;
    }
    if ((mbchar == NULL && sizeInBytes > 0) || (mbchar != NULL && sizeInBytes == 0))
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (mbchar == NULL)
    {
        Length = wcrtomb(Buffer, L'\0', mbstate);
    } else
    {
        Length = wcrtomb(Buffer, wchar, mbstate);
    }
    if (Length == (size_t)-1)
    {
        if (mbchar != NULL)
        {
            *mbchar = '\0';
        }
        NTCRT_ERRNO = EILSEQ;
        return EILSEQ;
    }
    if (mbchar != NULL)
    {
        if (Length > sizeInBytes)
        {
            *mbchar = '\0';
            return _NtCRT_InvalidParameter(ERANGE);
        }
        memcpy(mbchar, Buffer, Length);
    }
    if (pReturnValue != NULL)
    {
        *pReturnValue = Length;
    }
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT_mbsrtowcs_s(
    size_t* pReturnValue, wchar_t* wcstr, size_t sizeInWords, const char** mbstr, size_t count, mbstate_t* mbstate)
{
    size_t Length, Limit;
    errno_t Ret = 0;

    if (pReturnValue != NULL)
    {
        *pReturnValue = 0;
    }
    if ((wcstr == NULL && sizeInWords > 0) || (wcstr != NULL && sizeInWords == 0) || mbstr == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (wcstr != NULL)
    {
        *wcstr = L'\0';
    }
    if (*mbstr == NULL)
    {
        return count == 0 ? 0 : _NtCRT_InvalidParameter(EINVAL);
    }
    if (wcstr == NULL)
    {
        mbstate_t State = mbstate != NULL ? *mbstate : mbstate_t();
        const char* Src = *mbstr;
        Length = mbsrtowcs(NULL, &Src, 0, &State);
    } else
    {
        Limit = count == _TRUNCATE || count >= sizeInWords ? sizeInWords - 1 : count;
        Length = mbsrtowcs(wcstr, mbstr, Limit, mbstate);
        if (Length != (size_t)-1)
        {
            wcstr[Length] = L'\0';
            if (Length == Limit && *mbstr != NULL && **mbstr != '\0' && Limit < count)
            {
                if (count != _TRUNCATE)
                {
                    *wcstr = L'\0';
                    return _NtCRT_InvalidParameter(ERANGE);
                }
                Ret = STRUNCATE;
            }
        }
    }
    if (Length == (size_t)-1)
    {
        NTCRT_ERRNO = EILSEQ;
        return EILSEQ;
    }
    if (pReturnValue != NULL)
    {
        *pReturnValue = Length + 1;
    }
    return Ret;
}

EXTERN_C errno_t __cdecl _NtCRT_wcsrtombs_s(
    size_t* pReturnValue, char* mbstr, size_t sizeInBytes, const wchar_t** wcstr, size_t count, mbstate_t* mbstate)
{
    size_t Length, Limit;
    errno_t Ret = 0;

    if (pReturnValue != NULL)
    {
        *pReturnValue = 0;
    }
    if ((mbstr == NULL && sizeInBytes > 0) || (mbstr != NULL && sizeInBytes == 0) || wcstr == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (mbstr != NULL)
    {
        *mbstr = '\0';
    }
    if (*wcstr == NULL)
    {
        return count == 0 ? 0 : _NtCRT_InvalidParameter(EINVAL);
    }
    if (mbstr == NULL)
    {
        mbstate_t State = mbstate != NULL ? *mbstate : mbstate_t();
        const wchar_t* Src = *wcstr;
        Length = wcsrtombs(NULL, &Src, 0, &State);
    } else
    {
        Limit = count == _TRUNCATE || count >= sizeInBytes ? sizeInBytes - 1 : count;
        Length = wcsrtombs(mbstr, wcstr, Limit, mbstate);
        if (Length != (size_t)-1)
        {
            mbstr[Length] = '\0';
            if (*wcstr != NULL && **wcstr != L'\0' && Limit < count)
            {
                if (count != _TRUNCATE)
                {
                    *mbstr = '\0';
                    return _NtCRT_InvalidParameter(ERANGE);
                }
                Ret = STRUNCATE;
            }
        }
    }
    if (Length == (size_t)-1)
    {
        NTCRT_ERRNO = EILSEQ;
        return EILSEQ;
    }
    if (pReturnValue != NULL)
    {
        *pReturnValue = Length + 1;
    }
    return Ret;
}

/* Memory copying, the destination is emptied when the source is missing or too large */

template<typename TChar>
static
errno_t
_NtCRT_MemCopyS(
    _Out_writes_(Size) TChar* Dest,
    _In_ size_t Size,
    _In_reads_opt_(Count) const TChar* Src,
    _In_ size_t Count,
    _In_ bool Overlap,
    _In_ bool Empty)
{
    if (Count == 0)
    {
        return 0;
    }
    if (Dest == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (Src == NULL || Size < Count)
    {
        if (Empty)
        {
            memset(Dest, 0, Size * sizeof(TChar));
        }
        return _NtCRT_InvalidParameter(Src == NULL ? EINVAL : ERANGE);
    }
    if (Overlap)
    {
        memmove(Dest, Src, Count * sizeof(TChar));
    } else
    {
        memcpy(Dest, Src, Count * sizeof(TChar));
    }
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT_memcpy_s(void* dest, size_t destSize, const void* src, size_t count)
{
    return _NtCRT_MemCopyS((char*)dest, destSize, (const char*)src, count, false, true);
}

EXTERN_C errno_t __cdecl _NtCRT_memmove_s(void* dest, size_t destSize, const void* src, size_t count)
{
    return _NtCRT_MemCopyS((char*)dest, destSize, (const char*)src, count, true, false);
}

/* Not in msvcrt, both empty the destination as the emulation of mingw-w64 */

EXTERN_C errno_t __cdecl _NtCRT_wmemcpy_s(wchar_t* dest, size_t destSize, const wchar_t* src, size_t count)
{
    return _NtCRT_MemCopyS(dest, destSize, src, count, false, true);
}

EXTERN_C errno_t __cdecl _NtCRT_wmemmove_s(wchar_t* dest, size_t destSize, const wchar_t* src, size_t count)
{
    return _NtCRT_MemCopyS(dest, destSize, src, count, true, true);
}

/* Environment */

template<typename TChar>
static
errno_t
_NtCRT_GetEnvS(
    _Out_ size_t* pReturnValue,
    _Out_writes_opt_z_(Size) TChar* Buffer,
    _In_ size_t Size,
    _In_z_ const TChar* Name,
    _In_ const TChar* Value)
{
    size_t Length;

    if (pReturnValue == NULL || (Buffer == NULL && Size > 0) || Name == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pReturnValue = 0;
    if (Buffer != NULL)
    {
        *Buffer = 0;
    }
    if (Value == NULL)
    {
        return 0;
    }
    Length = _NtCRT_StrLen(Value) + 1;
    *pReturnValue = Length;
    if (Size == 0)
    {
        return 0;
    }
    if (Length > Size)
    {
        /* msvcrt leaves errno */
        return ERANGE;
    }
    memcpy(Buffer, Value, Length * sizeof(TChar));
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT_getenv_s(size_t* pReturnValue, char* buffer, rsize_t size, const char* varname)
{
    errno_t Ret;

    _NtCRT_lock(_ENV_LOCK);
    Ret = _NtCRT_GetEnvS(pReturnValue, buffer, size, varname, varname != NULL ? getenv(varname) : NULL);
    _NtCRT_unlock(_ENV_LOCK);
    return Ret;
}

EXTERN_C errno_t __cdecl _NtCRT__wgetenv_s(size_t* pReturnValue, wchar_t* buffer, size_t size, const wchar_t* varname)
{
    errno_t Ret;

    _NtCRT_lock(_ENV_LOCK);
    Ret = _NtCRT_GetEnvS(pReturnValue, buffer, size, varname, varname != NULL ? _wgetenv(varname) : NULL);
    _NtCRT_unlock(_ENV_LOCK);
    return Ret;
}

/* "name=value" for _putenv, an empty value removes the variable */
template<typename TChar>
static
TChar*
_NtCRT_JoinEnv(
    _In_z_ const TChar* Name,
    _In_z_ const TChar* Value)
{
    size_t NameLength = _NtCRT_StrLen(Name), ValueLength = _NtCRT_StrLen(Value);
    TChar* Str = (TChar*)RtlAllocateHeap(NTCRT_HEAP, 0, (NameLength + ValueLength + 2) * sizeof(TChar));

    if (Str != NULL)
    {
        memcpy(Str, Name, NameLength * sizeof(TChar));
        Str[NameLength] = '=';
        memcpy(Str + NameLength + 1, Value, (ValueLength + 1) * sizeof(TChar));
    }
    return Str;
}

EXTERN_C errno_t __cdecl _NtCRT__putenv_s(const char* name, const char* value)
{
    char* Str;
    int Ret;

    if (name == NULL || value == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    Str = _NtCRT_JoinEnv(name, value);
    if (Str == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return ENOMEM;
    }
    Ret = _NtCRT__putenv(Str);
    RtlFreeHeap(NTCRT_HEAP, 0, Str);
    return Ret == 0 ? 0 : NTCRT_ERRNO;
}

EXTERN_C errno_t __cdecl _NtCRT__wputenv_s(const wchar_t* name, const wchar_t* value)
{
    wchar_t* Str;
    int Ret;

    if (name == NULL || value == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    Str = _NtCRT_JoinEnv(name, value);
    if (Str == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return ENOMEM;
    }
    Ret = _NtCRT__wputenv(Str);
    RtlFreeHeap(NTCRT_HEAP, 0, Str);
    return Ret == 0 ? 0 : NTCRT_ERRNO;
}

NTCRT_IMPORT(strcpy_s, _NtCRT_strcpy_s);
NTCRT_IMPORT(memcpy_s, _NtCRT_memcpy_s);
NTCRT_IMPORT(memmove_s, _NtCRT_memmove_s);
NTCRT_IMPORT(wmemcpy_s, _NtCRT_wmemcpy_s);
NTCRT_IMPORT(wmemmove_s, _NtCRT_wmemmove_s);
NTCRT_IMPORT(wcscpy_s, _NtCRT_wcscpy_s);
NTCRT_IMPORT(strncpy_s, _NtCRT_strncpy_s);
NTCRT_IMPORT(wcsncpy_s, _NtCRT_wcsncpy_s);
NTCRT_IMPORT(strcat_s, _NtCRT_strcat_s);
NTCRT_IMPORT(wcscat_s, _NtCRT_wcscat_s);
NTCRT_IMPORT(strncat_s, _NtCRT_strncat_s);
NTCRT_IMPORT(wcsncat_s, _NtCRT_wcsncat_s);
NTCRT_IMPORT(_strlwr_s, _NtCRT__strlwr_s);
NTCRT_IMPORT(_wcslwr_s, _NtCRT__wcslwr_s);
NTCRT_IMPORT(_strupr_s, _NtCRT__strupr_s);
NTCRT_IMPORT(_wcsupr_s, _NtCRT__wcsupr_s);
NTCRT_IMPORT(_strset_s, _NtCRT__strset_s);
NTCRT_IMPORT(_wcsset_s, _NtCRT__wcsset_s);
NTCRT_IMPORT(_strnset_s, _NtCRT__strnset_s);
NTCRT_IMPORT(_wcsnset_s, _NtCRT__wcsnset_s);
NTCRT_IMPORT(_itoa_s, _NtCRT__itoa_s);
NTCRT_IMPORT(_itow_s, _NtCRT__itow_s);
NTCRT_IMPORT(_ltoa_s, _NtCRT__ltoa_s);
NTCRT_IMPORT(_ltow_s, _NtCRT__ltow_s);
NTCRT_IMPORT(_ultoa_s, _NtCRT__ultoa_s);
NTCRT_IMPORT(_ultow_s, _NtCRT__ultow_s);
NTCRT_IMPORT(_i64toa_s, _NtCRT__i64toa_s);
NTCRT_IMPORT(_i64tow_s, _NtCRT__i64tow_s);
NTCRT_IMPORT(_ui64toa_s, _NtCRT__ui64toa_s);
NTCRT_IMPORT(_ui64tow_s, _NtCRT__ui64tow_s);
NTCRT_IMPORT(mbstowcs_s, _NtCRT_mbstowcs_s);
NTCRT_IMPORT(wcstombs_s, _NtCRT_wcstombs_s);
NTCRT_IMPORT(wcrtomb_s, _NtCRT_wcrtomb_s);
NTCRT_IMPORT(mbsrtowcs_s, _NtCRT_mbsrtowcs_s);
NTCRT_IMPORT(wcsrtombs_s, _NtCRT_wcsrtombs_s);
NTCRT_IMPORT(getenv_s, _NtCRT_getenv_s);
NTCRT_IMPORT(_wgetenv_s, _NtCRT__wgetenv_s);
NTCRT_IMPORT(_putenv_s, _NtCRT__putenv_s);
NTCRT_IMPORT(_wputenv_s, _NtCRT__wputenv_s);
