/* Code pages of the system, of the locale and of file names, and strings converted between them and UTF-16 */

#include "NtCRT.h"

#include <errno.h>

extern "C" unsigned int _NtCRT_lc_codepage;

/* "_l" functions running with the code page of a locale, see NTCRT_LOCALE_SCOPE */
static volatile LONG g_LocaleScopes;

EXTERN_C UINT _NtCRT_CodePage(void)
{
    return NtCurrentPeb()->ActiveCodePage;
}

EXTERN_C UINT _NtCRT_CurrentCodePage(void)
{
    if (g_LocaleScopes != 0)
    {
        unsigned int CodePage = _NtCRT_GetPtd()->LocaleCodePage;

        if (CodePage != 0)
        {
            return CodePage - 1;
        }
    }
    return _NtCRT_lc_codepage;
}

EXTERN_C UINT _NtCRT_LocaleCodePage(_locale_t Locale)
{
    return Locale != NULL ? Locale->locinfo->lc_codepage : _NtCRT_CurrentCodePage();
}

EXTERN_C UINT _NtCRT_FileCodePage(void)
{
    return _NtCRT_lc_codepage == CP_UTF8 ? CP_UTF8 : _NtCRT_CodePage();
}

NTCRT_LOCALE_SCOPE::NTCRT_LOCALE_SCOPE(_locale_t Locale)
{
    m_Ptd = NULL;
    if (Locale != NULL)
    {
        m_Ptd = _NtCRT_GetPtd();
        m_Saved = m_Ptd->LocaleCodePage;
        _InterlockedIncrement(&g_LocaleScopes);
        m_Ptd->LocaleCodePage = Locale->locinfo->lc_codepage + 1;
    }
}

NTCRT_LOCALE_SCOPE::~NTCRT_LOCALE_SCOPE()
{
    if (m_Ptd != NULL)
    {
        m_Ptd->LocaleCodePage = m_Saved;
        _InterlockedDecrement(&g_LocaleScopes);
    }
}

/* Strings */

static
PWSTR
_NtCRT_ToWide(
    _In_ UINT CodePage,
    _In_ DWORD Flags,
    _In_z_ const char* Str)
{
    int Length;
    PWSTR Buffer;

    Length = _NtCRT_MultiByteToWideChar(CodePage, Flags, Str, -1, NULL, 0);
    if (Length <= 0)
    {
        ULONG Error = _Inline_GetLastError();

        _NtCRT_SetErrnoFromWin32(Error);
        if (Error == ERROR_NO_UNICODE_TRANSLATION)
        {
            NTCRT_ERRNO = EILSEQ;
        }
        return NULL;
    }
    Buffer = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, Length * sizeof(WCHAR));
    if (Buffer == NULL)
    {
        _Inline_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    _NtCRT_MultiByteToWideChar(CodePage, Flags, Str, -1, Buffer, Length);
    return Buffer;
}

static
char*
_NtCRT_ToNarrow(
    _In_ UINT CodePage,
    _In_z_ const wchar_t* Str)
{
    int Length;
    char* Buffer;

    Length = _NtCRT_WideCharToMultiByte(CodePage, 0, Str, -1, NULL, 0, NULL, NULL);
    if (Length <= 0)
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return NULL;
    }
    Buffer = (char*)RtlAllocateHeap(NTCRT_HEAP, 0, Length);
    if (Buffer == NULL)
    {
        _Inline_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    _NtCRT_WideCharToMultiByte(CodePage, 0, Str, -1, Buffer, Length, NULL, NULL);
    return Buffer;
}

EXTERN_C PWSTR _NtCRT_AnsiToWide(const char* Str)
{
    return _NtCRT_ToWide(_NtCRT_CodePage(), 0, Str);
}

EXTERN_C char* _NtCRT_WideToAnsi(const wchar_t* Str)
{
    return _NtCRT_ToNarrow(_NtCRT_CodePage(), Str);
}

/* An invalid multibyte character fails with EILSEQ, instead of a default character naming another file */
EXTERN_C PWSTR _NtCRT_FileToWide(const char* Str)
{
    return _NtCRT_ToWide(_NtCRT_FileCodePage(), MB_ERR_INVALID_CHARS, Str);
}

EXTERN_C char* _NtCRT_WideToFile(const wchar_t* Str)
{
    return _NtCRT_ToNarrow(_NtCRT_FileCodePage(), Str);
}
