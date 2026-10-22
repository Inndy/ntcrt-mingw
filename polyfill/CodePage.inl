#pragma once

#include <KNSoft/NDK/NDK.inl>

/* Use code page data in PEB instead of calling GetACP and GetOEMCP */

FORCEINLINE
UINT
WINAPI
_CRT_GetACP(void)
{
    return _Inline_GetACP();
}

FORCEINLINE
UINT
WINAPI
_CRT_GetOEMCP(void)
{
    return _Inline_GetOEMCP();
}

FORCEINLINE
DWORD
WINAPI
_CRT_GetLocaleDefaultCodePage(
    _In_ LCID Locale,
    _Out_opt_ PUSHORT DefaultAnsiCodePage,
    _Out_opt_ PUSHORT DefaultMacCodePage)
{
    NTSTATUS Status;
    PVOID BaseAddress;
    LCID DefaultLocaleId;
    LARGE_INTEGER DefaultCasingTableSize;
    PCNLS_LOCALE_FILE_HEADER LocaleFileHeader;
    PCNLS_LOCALE_HEADER LocaleHeader;
    PCNLS_LOCALE_LCID_INDEX LocaleIdIndex;
    PCNLS_LOCALE_DATA LocaleData;
    USHORT i;

    Status = RtlGetLocaleFileMappingAddress(&BaseAddress, &DefaultLocaleId, &DefaultCasingTableSize);
    if (!NT_SUCCESS(Status))
    {
        return _Inline_RtlNtStatusToDosError(Status);
    }

    if (Locale == LOCALE_NEUTRAL)
    {
        Locale = DefaultLocaleId;
    }

    LocaleFileHeader = (PCNLS_LOCALE_FILE_HEADER)BaseAddress;
    LocaleHeader = (PCNLS_LOCALE_HEADER)Add2Ptr(BaseAddress, LocaleFileHeader->LocaleTableOffset);
    LocaleIdIndex = (PCNLS_LOCALE_LCID_INDEX)Add2Ptr(LocaleHeader, LocaleHeader->LocaleIdIndexOffset);
    LocaleData = (PCNLS_LOCALE_DATA)Add2Ptr(LocaleHeader, LocaleHeader->LocaleDataOffset);

    for (i = 0; i < LocaleHeader->LocaleIdCount; ++i)
    {
        if (LocaleIdIndex[i].LocaleId == Locale)
        {
            LocaleData = (PCNLS_LOCALE_DATA)Add2Ptr(LocaleData,
                                                    LocaleIdIndex[i].LocaleIndex * LocaleHeader->LocaleDataSize);
            if (DefaultAnsiCodePage != NULL)
            {
                *DefaultAnsiCodePage = LocaleData->IDefaultAnsiCodePage;
            }
            if (DefaultMacCodePage != NULL)
            {
                *DefaultMacCodePage = LocaleData->IDefaultMacCodePage;
            }
            return ERROR_SUCCESS;
        }
    }

    return ERROR_FILE_NOT_FOUND;
}

/*
 * The .nls data of the code pages that NtGetNlsSectionPtr mapped, which maps a new view at each call: a code page
 * is mapped once and stays. A list published by compare and exchange, the thread that loses unmaps its view.
 */
typedef struct _CRT_CODEPAGE_DATA
{
    struct _CRT_CODEPAGE_DATA* Next;
    UINT CodePage;
    PVOID Data;
} CRT_CODEPAGE_DATA;

static CRT_CODEPAGE_DATA* _CRT_CodePageData;

static
CRT_CODEPAGE_DATA*
_CRT_FindCodePageData(
    _In_opt_ CRT_CODEPAGE_DATA* Entry,
    _In_ UINT CodePage)
{
    while (Entry != NULL && Entry->CodePage != CodePage)
    {
        Entry = Entry->Next;
    }
    return Entry;
}

/* Not inline: each conversion function calls it */
static __attribute__((noinline))
DWORD
WINAPI
_CRT_GetCodePageTable(
    _In_ UINT CodePage,
    _Out_ PCPTABLEINFO CodePageTable)
{
    NTSTATUS Status;
    SIZE_T SectionSize;
    PVOID SectionPointer;
    PPEB Peb;
    CRT_CODEPAGE_DATA *Entry, *Other;

    if (CodePage >= 50000 && CodePage <= 59999)
    {
        return ERROR_NOT_SUPPORTED;
    }

    /* The .nls data that the process has mapped (Windows 7, not Windows 11 24H2) */
    Peb = NtCurrentPeb();
    if (Peb->AnsiCodePageData != NULL && CodePage == _CRT_GetACP())
    {
        RtlInitCodePageTable((PUSHORT)Peb->AnsiCodePageData, CodePageTable);
        return ERROR_SUCCESS;
    }

    if (Peb->OemCodePageData != NULL && CodePage == _CRT_GetOEMCP())
    {
        RtlInitCodePageTable((PUSHORT)Peb->OemCodePageData, CodePageTable);
        return ERROR_SUCCESS;
    }

    Entry = _CRT_FindCodePageData(__atomic_load_n(&_CRT_CodePageData, __ATOMIC_ACQUIRE), CodePage);
    if (Entry == NULL)
    {
        Status = NtGetNlsSectionPtr(11, CodePage, NULL, &SectionPointer, &SectionSize); // Codepage NLS section.
        if (!NT_SUCCESS(Status))
        {
            return _Inline_RtlNtStatusToDosError(Status);
        }
        Entry = (CRT_CODEPAGE_DATA*)RtlAllocateHeap(RtlProcessHeap(), 0, sizeof(*Entry));
        if (Entry == NULL)
        {
            NtUnmapViewOfSection(NtCurrentProcess(), SectionPointer);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        Entry->CodePage = CodePage;
        Entry->Data = SectionPointer;
        Entry->Next = __atomic_load_n(&_CRT_CodePageData, __ATOMIC_ACQUIRE);
        while (!__atomic_compare_exchange_n(&_CRT_CodePageData, &Entry->Next, Entry, false, __ATOMIC_RELEASE,
                                            __ATOMIC_ACQUIRE))
        {
            /* Another thread added entries, maybe this code page */
            Other = _CRT_FindCodePageData(Entry->Next, CodePage);
            if (Other != NULL)
            {
                NtUnmapViewOfSection(NtCurrentProcess(), Entry->Data);
                RtlFreeHeap(RtlProcessHeap(), 0, Entry);
                Entry = Other;
                break;
            }
        }
    }

    RtlInitCodePageTable((PUSHORT)Entry->Data, CodePageTable);
    return ERROR_SUCCESS;
}

/* Not inline: each conversion function calls it */
static __attribute__((noinline))
DWORD
WINAPI
_CRT_ResolveCodePage(
    _In_ UINT CodePage,
    _Out_ PUINT ResolvedCodePage)
{
    DWORD Error;
    USHORT LocaleCodePage;

    if (CodePage == CP_ACP)
    {
        *ResolvedCodePage = _CRT_GetACP();
    } else if (CodePage == CP_OEMCP)
    {
        *ResolvedCodePage = _CRT_GetOEMCP();
    } else if (CodePage == CP_MACCP)
    {
        *ResolvedCodePage = (_CRT_GetLocaleDefaultCodePage(LOCALE_NEUTRAL, NULL, &LocaleCodePage) == ERROR_SUCCESS &&
                             LocaleCodePage != 0) ? LocaleCodePage : 10000; // Mac Roman.
    } else if (CodePage == CP_THREAD_ACP)
    {
        Error = _CRT_GetLocaleDefaultCodePage(NtCurrentTeb()->CurrentLocale, &LocaleCodePage, NULL);
        if (Error == ERROR_SUCCESS)
        {
            *ResolvedCodePage = LocaleCodePage > CP_THREAD_ACP ? LocaleCodePage : CP_UTF8;
        }
        return Error;
    } else if (CodePage == CP_SYMBOL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    // [MS-UCODEREF]
    // https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-ucoderef/28fefe92-d66c-4b03-90a9-97b473223d43
    else if (CodePage == 51949)
    {
        // 51949 uses [CODEPAGEFILES] 20949.txt
        *ResolvedCodePage = 20949;
    } else if (CodePage == 38598)
    {
        // 38598 uses [CODEPAGEFILES] 28598.txt
        *ResolvedCodePage = 28598;
    } else
    {
        *ResolvedCodePage = CodePage;
    }

    return ERROR_SUCCESS;
}

FORCEINLINE
BOOL
WINAPI
_CRT_IsInvalidCodePage(
    _In_ UINT CodePage)
{
    return
        CodePage == 1200 ||
        CodePage == 1201 ||
        CodePage == 12000 ||
        CodePage == 12001 ||
        CodePage >= 60000;
}

FORCEINLINE
DWORD
WINAPI
_CRT_ConversionStatusToError(
    _In_ NTSTATUS Status)
{
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        return ERROR_INSUFFICIENT_BUFFER;
    }

    if (Status == STATUS_BUFFER_OVERFLOW)
    {
        return ERROR_INSUFFICIENT_BUFFER;
    }

    if (Status == STATUS_SOME_NOT_MAPPED)
    {
        return ERROR_NO_UNICODE_TRANSLATION;
    }

    return ERROR_INVALID_PARAMETER;
}

FORCEINLINE
BOOL
WINAPI
_CRT_IsLeadByte(
    _In_ const BYTE LeadByte[MAXIMUM_LEADBYTES],
    _In_ BYTE Byte)
{
    UINT i;

    for (i = 0; i + 1 < MAXIMUM_LEADBYTES && LeadByte[i] != 0 && LeadByte[i + 1] != 0; i += 2)
    {
        if (Byte >= LeadByte[i] && Byte <= LeadByte[i + 1])
        {
            return TRUE;
        }
    }

    return FALSE;
}

FORCEINLINE
ULONG
WINAPI
_CRT_GetCustomUnicodeSize(
    _In_ PCPTABLEINFO CodePageTable,
    _In_reads_bytes_(BytesInCustomCPString) LPCCH CustomCPString,
    _In_ ULONG BytesInCustomCPString)
{
    ULONG i;
    ULONG Characters;

    Characters = 0;
    for (i = 0; i < BytesInCustomCPString; ++i)
    {
        ++Characters;
        if (CodePageTable->MaximumCharacterSize > 1 &&
            _CRT_IsLeadByte(CodePageTable->LeadByte, (BYTE)CustomCPString[i]) &&
            i + 1 < BytesInCustomCPString)
        {
            ++i;
        }
    }

    return Characters * sizeof(WCHAR);
}

FORCEINLINE
ULONG
WINAPI
_CRT_GetDefaultChar(
    _In_ PCPTABLEINFO CodePageTable,
    _Out_writes_(2) PCHAR DefaultChar)
{
    if (HIBYTE(CodePageTable->DefaultChar))
    {
        DefaultChar[0] = HIBYTE(CodePageTable->DefaultChar);
        DefaultChar[1] = LOBYTE(CodePageTable->DefaultChar);
        return 2;
    }

    DefaultChar[0] = LOBYTE(CodePageTable->DefaultChar);
    return 1;
}

FORCEINLINE
BOOL
WINAPI
_CRT_IsCustomMultiByteInvalid(
    _In_ PCPTABLEINFO CodePageTable,
    _In_reads_bytes_(BytesInCustomCPString) LPCCH CustomCPString,
    _In_ ULONG BytesInCustomCPString)
{
    ULONG i;

    for (i = 0; i < BytesInCustomCPString; ++i)
    {
        NTSTATUS Status;
        WCHAR UnicodeChar;
        ULONG BytesInUnicodeChar;
        ULONG MultiByteCharSize;

        MultiByteCharSize = 1;
        if (CodePageTable->MaximumCharacterSize > 1 &&
            _CRT_IsLeadByte(CodePageTable->LeadByte, (BYTE)CustomCPString[i]))
        {
            if (i + 1 == BytesInCustomCPString)
            {
                return TRUE;
            }
            MultiByteCharSize = 2;
        }

        Status = RtlCustomCPToUnicodeN(CodePageTable,
                                       &UnicodeChar,
                                       sizeof(UnicodeChar),
                                       &BytesInUnicodeChar,
                                       (PCH)(CustomCPString + i),
                                       MultiByteCharSize);
        if (!NT_SUCCESS(Status) || BytesInUnicodeChar != sizeof(WCHAR))
        {
            return TRUE;
        }

        /*
         * Undefined characters are translated to the Unicode default character, unless it is the translation of
         * the default character itself (0x8145 of code page 932), or to U+F880 - U+F8FF for undefined bytes
         */
        if ((UnicodeChar == CodePageTable->UniDefaultChar &&
             (MultiByteCharSize == 1 ? (BYTE)CustomCPString[i] :
                                       MAKEWORD(CustomCPString[i + 1], CustomCPString[i])) !=
                 CodePageTable->TransUniDefaultChar) ||
            (UnicodeChar >= 0xF880 && UnicodeChar <= 0xF8FF))
        {
            return TRUE;
        }

        i += MultiByteCharSize - 1;
    }

    return FALSE;
}

FORCEINLINE
BOOL
WINAPI
_CRT_IsDefaultCharUsed(
    _In_ PCPTABLEINFO CodePageTable,
    _In_reads_(cchWideChar) LPCWCH lpWideCharStr,
    _In_ int cchWideChar)
{
    CHAR DefaultChar[2];
    CHAR MultiByteChar[MB_LEN_MAX];
    ULONG DefaultCharSize;
    int i;

    DefaultCharSize = _CRT_GetDefaultChar(CodePageTable, DefaultChar);
    for (i = 0; i < cchWideChar; ++i)
    {
        NTSTATUS Status;
        ULONG BytesInMultiByteChar;

        if (lpWideCharStr[i] == CodePageTable->UniDefaultChar)
        {
            continue;
        }

        Status = RtlUnicodeToCustomCPN(CodePageTable,
                                       MultiByteChar,
                                       sizeof(MultiByteChar),
                                       &BytesInMultiByteChar,
                                       (PWCH)(lpWideCharStr + i),
                                       sizeof(WCHAR));
        if (NT_SUCCESS(Status) &&
            BytesInMultiByteChar == DefaultCharSize &&
            RtlEqualMemory(MultiByteChar, DefaultChar, DefaultCharSize))
        {
            return TRUE;
        }
    }

    return FALSE;
}

EXTERN_C
int
__cdecl
__acrt_MultiByteToWideChar(
    _In_ UINT CodePage,
    _In_ DWORD dwFlags,
    _In_NLS_string_(cbMultiByte) LPCCH lpMultiByteStr,
    _In_ int cbMultiByte,
    _Out_writes_opt_(cchWideChar) LPWSTR lpWideCharStr,
    _In_ int cchWideChar)
{
    CPTABLEINFO CodePageTable;
    NTSTATUS Status;
    ULONG BytesInUnicodeString;
    DWORD Error;
    UINT ResolvedCodePage;
    ULONG MaxBytesInUnicodeString;

    if (cbMultiByte == 0 ||
        cbMultiByte < -1 ||
        cchWideChar < 0 ||
        lpMultiByteStr == NULL ||
        (cchWideChar != 0 && lpWideCharStr == NULL) ||
        (cchWideChar != 0 && lpMultiByteStr == (LPCCH)lpWideCharStr))
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    /* A size of 0 asks for the size, the buffer is not used */
    if (cchWideChar == 0)
    {
        lpWideCharStr = NULL;
    }

    Error = _CRT_ResolveCodePage(CodePage, &ResolvedCodePage);
    if (Error != ERROR_SUCCESS)
    {
        _Inline_SetLastError(Error);
        return 0;
    }

    if (cbMultiByte == -1)
    {
        cbMultiByte = (int)(strlen(lpMultiByteStr) + 1);
    }

    MaxBytesInUnicodeString = (ULONG)cchWideChar * sizeof(WCHAR);

    if (ResolvedCodePage == CP_UTF8)
    {
        if ((dwFlags & ~(MB_PRECOMPOSED | MB_COMPOSITE | MB_USEGLYPHCHARS | MB_ERR_INVALID_CHARS)) != 0 ||
            (dwFlags & (MB_PRECOMPOSED | MB_COMPOSITE)) == (MB_PRECOMPOSED | MB_COMPOSITE))
        {
            _Inline_SetLastError(ERROR_INVALID_FLAGS);
            return 0;
        }

        Status = RtlUTF8ToUnicodeN(lpWideCharStr,
                                   MaxBytesInUnicodeString,
                                   &BytesInUnicodeString,
                                   lpMultiByteStr,
                                   (ULONG)cbMultiByte);
    } else if (ResolvedCodePage == CP_UTF7)
    {
        _Inline_SetLastError(dwFlags == 0 ? ERROR_NOT_SUPPORTED : ERROR_INVALID_FLAGS);
        return 0;
    } else
    {
        if ((dwFlags & ~(MB_PRECOMPOSED | MB_COMPOSITE | MB_USEGLYPHCHARS | MB_ERR_INVALID_CHARS)) != 0 ||
            (dwFlags & (MB_PRECOMPOSED | MB_COMPOSITE)) == (MB_PRECOMPOSED | MB_COMPOSITE))
        {
            _Inline_SetLastError(ERROR_INVALID_FLAGS);
            return 0;
        }

        if ((dwFlags & (MB_COMPOSITE | MB_USEGLYPHCHARS)) != 0)
        {
            _Inline_SetLastError(ERROR_NOT_SUPPORTED);
            return 0;
        }

        if (_CRT_IsInvalidCodePage(ResolvedCodePage))
        {
            _Inline_SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }

        Error = _CRT_GetCodePageTable(ResolvedCodePage, &CodePageTable);
        if (Error != ERROR_SUCCESS)
        {
            _Inline_SetLastError(Error);
            return 0;
        }

        if ((dwFlags & MB_ERR_INVALID_CHARS) != 0 &&
            _CRT_IsCustomMultiByteInvalid(&CodePageTable, lpMultiByteStr, (ULONG)cbMultiByte))
        {
            _Inline_SetLastError(ERROR_NO_UNICODE_TRANSLATION);
            return 0;
        }

        if (cchWideChar == 0)
        {
            if ((dwFlags & MB_ERR_INVALID_CHARS) != 0)
            {
                if ((ULONG)cbMultiByte > MAXULONG / sizeof(WCHAR))
                {
                    _Inline_SetLastError(ERROR_INVALID_PARAMETER);
                    return 0;
                }

                MaxBytesInUnicodeString = (ULONG)cbMultiByte * sizeof(WCHAR);
                lpWideCharStr = (LPWSTR)RtlAllocateHeap(NtCurrentPeb()->ProcessHeap,
                                                        0,
                                                        MaxBytesInUnicodeString);
                if (lpWideCharStr == NULL)
                {
                    _Inline_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return 0;
                }

                Status = RtlCustomCPToUnicodeN(&CodePageTable,
                                               lpWideCharStr,
                                               MaxBytesInUnicodeString,
                                               &BytesInUnicodeString,
                                               (PCH)lpMultiByteStr,
                                               (ULONG)cbMultiByte);
                RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, lpWideCharStr);
            } else
            {
                Status = STATUS_SUCCESS;
                BytesInUnicodeString = _CRT_GetCustomUnicodeSize(&CodePageTable,
                                                                 lpMultiByteStr,
                                                                 (ULONG)cbMultiByte);
            }
        } else
        {
            Status = RtlCustomCPToUnicodeN(&CodePageTable,
                                           lpWideCharStr,
                                           MaxBytesInUnicodeString,
                                           &BytesInUnicodeString,
                                           (PCH)lpMultiByteStr,
                                           (ULONG)cbMultiByte);
        }
    }

    if (!NT_SUCCESS(Status) || ((dwFlags & MB_ERR_INVALID_CHARS) != 0 && Status == STATUS_SOME_NOT_MAPPED))
    {
        _Inline_SetLastError(_CRT_ConversionStatusToError(Status));
        return 0;
    }

    return (int)(BytesInUnicodeString / sizeof(WCHAR));
}

EXTERN_C
int
__cdecl
__acrt_WideCharToMultiByte(
    _In_ UINT CodePage,
    _In_ DWORD dwFlags,
    _In_NLS_string_(cchWideChar) LPCWCH lpWideCharStr,
    _In_ int cchWideChar,
    _Out_writes_opt_(cbMultiByte) LPSTR lpMultiByteStr,
    _In_ int cbMultiByte,
    _In_opt_ LPCCH lpDefaultChar,
    _Out_opt_ LPBOOL lpUsedDefaultChar)
{
    CPTABLEINFO CodePageTable;
    NTSTATUS Status;
    ULONG BytesInMultiByteString;
    DWORD Error;
    UINT ResolvedCodePage;
    BOOL DefaultCharUsed;

    if (cchWideChar == 0 ||
        cchWideChar < -1 ||
        cbMultiByte < 0 ||
        lpWideCharStr == NULL ||
        (cbMultiByte != 0 && lpMultiByteStr == NULL) ||
        (cbMultiByte != 0 && lpWideCharStr == (LPCWCH)lpMultiByteStr))
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    /* A size of 0 asks for the size, the buffer is not used */
    if (cbMultiByte == 0)
    {
        lpMultiByteStr = NULL;
    }

    Error = _CRT_ResolveCodePage(CodePage, &ResolvedCodePage);
    if (Error != ERROR_SUCCESS)
    {
        _Inline_SetLastError(Error);
        return 0;
    }

    if (cchWideChar == -1)
    {
        cchWideChar = (int)(wcslen(lpWideCharStr) + 1);
    }

    DefaultCharUsed = FALSE;

    if (ResolvedCodePage == CP_UTF8)
    {
        if ((dwFlags & ~(WC_DISCARDNS | WC_SEPCHARS | WC_DEFAULTCHAR |
                         WC_ERR_INVALID_CHARS | WC_COMPOSITECHECK | WC_NO_BEST_FIT_CHARS)) != 0 ||
            lpDefaultChar != NULL)
        {
            _Inline_SetLastError(lpDefaultChar != NULL ? ERROR_INVALID_PARAMETER : ERROR_INVALID_FLAGS);
            return 0;
        }

        Status = RtlUnicodeToUTF8N(lpMultiByteStr,
                                   (ULONG)cbMultiByte,
                                   &BytesInMultiByteString,
                                   lpWideCharStr,
                                   (ULONG)cchWideChar * sizeof(WCHAR));
    } else if (ResolvedCodePage == CP_UTF7)
    {
        _Inline_SetLastError(dwFlags == 0 && lpDefaultChar == NULL && lpUsedDefaultChar == NULL ?
                             ERROR_NOT_SUPPORTED :
                             ERROR_INVALID_PARAMETER);
        return 0;
    } else
    {
        PCHAR MultiByteStringBuffer;
        ULONG MaxBytesInMultiByteString;

        if ((dwFlags & ~(WC_DISCARDNS | WC_SEPCHARS | WC_DEFAULTCHAR |
                         WC_ERR_INVALID_CHARS | WC_COMPOSITECHECK | WC_NO_BEST_FIT_CHARS)) != 0)
        {
            _Inline_SetLastError(ERROR_INVALID_FLAGS);
            return 0;
        }

        if (dwFlags != 0 || lpDefaultChar != NULL)
        {
            _Inline_SetLastError(ERROR_NOT_SUPPORTED);
            return 0;
        }

        if (_CRT_IsInvalidCodePage(ResolvedCodePage))
        {
            _Inline_SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }

        Error = _CRT_GetCodePageTable(ResolvedCodePage, &CodePageTable);
        if (Error != ERROR_SUCCESS)
        {
            _Inline_SetLastError(Error);
            return 0;
        }

        if (lpUsedDefaultChar != NULL)
        {
            DefaultCharUsed = _CRT_IsDefaultCharUsed(&CodePageTable, lpWideCharStr, cchWideChar);
        }

        MultiByteStringBuffer = lpMultiByteStr;
        MaxBytesInMultiByteString = (ULONG)cbMultiByte;
        if (cbMultiByte == 0)
        {
            if ((ULONG)cchWideChar > MAXULONG / CodePageTable.MaximumCharacterSize)
            {
                _Inline_SetLastError(ERROR_INVALID_PARAMETER);
                return 0;
            }

            MaxBytesInMultiByteString = (ULONG)cchWideChar * CodePageTable.MaximumCharacterSize;
            MultiByteStringBuffer = (PCHAR)RtlAllocateHeap(NtCurrentPeb()->ProcessHeap,
                                                           0,
                                                           MaxBytesInMultiByteString);
            if (MultiByteStringBuffer == NULL)
            {
                _Inline_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return 0;
            }
        }

        Status = RtlUnicodeToCustomCPN(&CodePageTable,
                                       MultiByteStringBuffer,
                                       MaxBytesInMultiByteString,
                                       &BytesInMultiByteString,
                                       (PWCH)lpWideCharStr,
                                       (ULONG)cchWideChar * sizeof(WCHAR));
        if (lpMultiByteStr == NULL)
        {
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, MultiByteStringBuffer);
        }
    }

    if (!NT_SUCCESS(Status) || ((dwFlags & WC_ERR_INVALID_CHARS) != 0 && Status == STATUS_SOME_NOT_MAPPED))
    {
        _Inline_SetLastError(_CRT_ConversionStatusToError(Status));
        return 0;
    }

    if (lpUsedDefaultChar != NULL)
    {
        *lpUsedDefaultChar = Status == STATUS_SOME_NOT_MAPPED || DefaultCharUsed;
    }

    return (int)BytesInMultiByteString;
}

EXTERN_C
BOOL
WINAPI
_CRT_GetCPInfo(
    _In_ UINT CodePage,
    _Out_ LPCPINFO lpCPInfo)
{
    CPTABLEINFO CodePageTable;
    DWORD Error;
    UINT ResolvedCodePage;

    if (lpCPInfo == NULL)
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Error = _CRT_ResolveCodePage(CodePage, &ResolvedCodePage);
    if (Error != ERROR_SUCCESS)
    {
        _Inline_SetLastError(Error);
        return FALSE;
    }

    if (ResolvedCodePage == CP_UTF7 || ResolvedCodePage == CP_UTF8)
    {
        RtlZeroMemory(lpCPInfo, sizeof(*lpCPInfo));
        lpCPInfo->MaxCharSize = ResolvedCodePage == CP_UTF7 ? 5 : 4;
        lpCPInfo->DefaultChar[0] = '?';
        return TRUE;
    }

    if (_CRT_IsInvalidCodePage(ResolvedCodePage))
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Error = _CRT_GetCodePageTable(ResolvedCodePage, &CodePageTable);
    if (Error != ERROR_SUCCESS)
    {
        _Inline_SetLastError(Error);
        return FALSE;
    }

    lpCPInfo->MaxCharSize = CodePageTable.MaximumCharacterSize;
    if (HIBYTE(CodePageTable.DefaultChar))
    {
        lpCPInfo->DefaultChar[0] = HIBYTE(CodePageTable.DefaultChar);
        lpCPInfo->DefaultChar[1] = LOBYTE(CodePageTable.DefaultChar);
    } else
    {
        *(WORD*)lpCPInfo->DefaultChar = CodePageTable.DefaultChar;
    }
    RtlCopyMemory(lpCPInfo->LeadByte, CodePageTable.LeadByte, sizeof(lpCPInfo->LeadByte));
    return TRUE;
}

EXTERN_C
BOOL
WINAPI
_CRT_IsValidCodePage(
    _In_ UINT CodePage)
{
    CPTABLEINFO CodePageTable;
    DWORD Error;
    UINT ResolvedCodePage;

    if (CodePage <= CP_THREAD_ACP || CodePage == CP_SYMBOL)
    {
        return FALSE;
    }

    Error = _CRT_ResolveCodePage(CodePage, &ResolvedCodePage);
    if (Error != ERROR_SUCCESS)
    {
        return FALSE;
    }

    if (ResolvedCodePage == _CRT_GetACP() ||
        ResolvedCodePage == _CRT_GetOEMCP() ||
        ResolvedCodePage == CP_UTF7 ||
        ResolvedCodePage == CP_UTF8)
    {
        return TRUE;
    }

    if (_CRT_IsInvalidCodePage(ResolvedCodePage))
    {
        return FALSE;
    }

    return _CRT_GetCodePageTable(ResolvedCodePage, &CodePageTable) == ERROR_SUCCESS;
}

EXTERN_C
BOOL
WINAPI
_CRT_GetStringTypeW(
    _In_ DWORD dwInfoType,
    _In_NLS_string_(cchSrc) LPCWCH lpSrcStr,
    _In_ int cchSrc,
    _Out_ LPWORD lpCharType)
{
    NTSTATUS Status;
    PVOID BaseAddress;
    LCID DefaultLocaleId;
    LARGE_INTEGER DefaultCasingTableSize;
    PCNLS_LOCALE_FILE_HEADER LocaleFileHeader;
    PCNLS_CTYPE_HEADER CTypeHeader;
    const WORD* TypeTable;
    PCUCHAR IndexTable;

    if (lpSrcStr == NULL || cchSrc == 0 || lpCharType == NULL || lpSrcStr == (LPCWCH)lpCharType)
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (cchSrc < 0)
    {
        cchSrc = (int)(wcslen(lpSrcStr) + 1);
    }

    if (dwInfoType != CT_CTYPE1 && dwInfoType != CT_CTYPE2 && dwInfoType != CT_CTYPE3)
    {
        _Inline_SetLastError(ERROR_INVALID_FLAGS);
        return FALSE;
    }

    if (cchSrc <= 0)
    {
        return TRUE;
    }

    Status = RtlGetLocaleFileMappingAddress(&BaseAddress, &DefaultLocaleId, &DefaultCasingTableSize);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }

    LocaleFileHeader = (PCNLS_LOCALE_FILE_HEADER)BaseAddress;
    CTypeHeader = (PCNLS_CTYPE_HEADER)Add2Ptr(LocaleFileHeader, LocaleFileHeader->CTypeTableOffset);
    TypeTable = CTypeHeader->TypeTable;
    IndexTable = (PCUCHAR)Add2Ptr(&CTypeHeader->IndexTableOffset, CTypeHeader->IndexTableOffset);

    while (cchSrc--)
    {
        DWORD LowByte;
        DWORD ClassIndex;
        WCHAR ch;
        PCUCHAR Leaf;

        ch = *lpSrcStr++;
        LowByte = ch & 0xFF;
        ClassIndex = (*(const WORD*)Add2Ptr(IndexTable, (ch >> 8) * sizeof(WORD)) >> 1) + (LowByte >> 4);
        Leaf = (PCUCHAR)Add2Ptr(IndexTable, *(const WORD*)Add2Ptr(IndexTable, ClassIndex * sizeof(WORD)));
        *lpCharType++ = TypeTable[Leaf[LowByte & 0xF] * 3 + dwInfoType / 2];
    }

    return TRUE;
}

#define GetACP _CRT_GetACP
#define GetOEMCP _CRT_GetOEMCP
#define GetCPInfo _CRT_GetCPInfo
#define IsValidCodePage _CRT_IsValidCodePage
#define GetStringTypeW _CRT_GetStringTypeW
