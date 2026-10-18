#include <KNSoft/NDK/NDK.inl>

extern "C"
BOOL
WINAPI
_CRT_GetCPInfo(
    _In_ UINT CodePage,
    _Out_ LPCPINFO lpCPInfo);

extern "C"
BOOL
WINAPI
_CRT_IsValidCodePage(
    _In_ UINT CodePage);

typedef BOOL (WINAPI *PFN_GET_CP_INFO)(
    _In_ UINT CodePage,
    _Out_ LPCPINFO lpCPInfo);

typedef BOOL (WINAPI *PFN_IS_VALID_CODE_PAGE)(
    _In_ UINT CodePage);

static
PVOID
GetKernel32Procedure(
    _In_z_ PCSTR ProcedureNameString)
{
    ANSI_STRING ProcedureName;
    HMODULE Kernel32;
    PVOID ProcedureAddress;
    NTSTATUS Status;

    Kernel32 = _Inline_GetModuleHandleW(L"kernel32.dll");
    if (Kernel32 == NULL)
    {
        return NULL;
    }

    _Inline_RtlInitAnsiString(&ProcedureName, ProcedureNameString);
    Status = LdrGetProcedureAddress(Kernel32, &ProcedureName, 0, &ProcedureAddress);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return NULL;
    }

    return ProcedureAddress;
}

static
BOOL
CompareMemory(
    _In_reads_bytes_(Size) const VOID* Buffer1,
    _In_reads_bytes_(Size) const VOID* Buffer2,
    _In_ SIZE_T Size)
{
    const BYTE* p1 = (const BYTE*)Buffer1;
    const BYTE* p2 = (const BYTE*)Buffer2;

    while (Size--)
    {
        if (*p1++ != *p2++)
        {
            return FALSE;
        }
    }

    return TRUE;
}

static
BOOL
CompareCPInfo(
    _In_ const CPINFO* CpInfo1,
    _In_ const CPINFO* CpInfo2)
{
    return
        CpInfo1->MaxCharSize == CpInfo2->MaxCharSize &&
        CompareMemory(CpInfo1->DefaultChar, CpInfo2->DefaultChar, sizeof(CpInfo1->DefaultChar)) &&
        CompareMemory(CpInfo1->LeadByte, CpInfo2->LeadByte, sizeof(CpInfo1->LeadByte));
}

static
BOOL
CompareGetCPInfo(
    _In_ PFN_GET_CP_INFO SystemGetCPInfo,
    _In_ UINT CodePage)
{
    CPINFO Expected;
    CPINFO Actual;
    DWORD ExpectedError;
    DWORD ActualError;
    BOOL ExpectedResult;
    BOOL ActualResult;

    RtlFillMemory(&Expected, sizeof(Expected), 0xCC);
    RtlFillMemory(&Actual, sizeof(Actual), 0xCC);

    _Inline_SetLastError(0);
    ExpectedResult = SystemGetCPInfo(CodePage, &Expected);
    ExpectedError = _Inline_GetLastError();

    _Inline_SetLastError(0);
    ActualResult = _CRT_GetCPInfo(CodePage, &Actual);
    ActualError = _Inline_GetLastError();

    if (ActualResult != ExpectedResult || ActualError != ExpectedError)
    {
        return FALSE;
    }

    if (ExpectedResult != FALSE && !CompareCPInfo(&Actual, &Expected))
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOL
CompareGetCPInfoFailure(
    _In_ PFN_GET_CP_INFO SystemGetCPInfo,
    _In_ UINT CodePage,
    _Out_opt_ LPCPINFO lpCPInfo)
{
    DWORD ExpectedError;
    DWORD ActualError;
    BOOL ExpectedResult;
    BOOL ActualResult;

    _Inline_SetLastError(0);
    ExpectedResult = SystemGetCPInfo(CodePage, lpCPInfo);
    ExpectedError = _Inline_GetLastError();

    _Inline_SetLastError(0);
    ActualResult = _CRT_GetCPInfo(CodePage, lpCPInfo);
    ActualError = _Inline_GetLastError();

    if (!ExpectedResult && ActualResult == ExpectedResult && ActualError == ExpectedError)
    {
        return TRUE;
    }

    return FALSE;
}

static
BOOL
CompareIsValidCodePage(
    _In_ PFN_IS_VALID_CODE_PAGE SystemIsValidCodePage,
    _In_ UINT CodePage)
{
    BOOL ExpectedResult;
    BOOL ActualResult;

    ExpectedResult = SystemIsValidCodePage(CodePage);

    _Inline_SetLastError(0x12345678);
    ActualResult = _CRT_IsValidCodePage(CodePage);

    if (ActualResult == ExpectedResult && _Inline_GetLastError() == 0x12345678)
    {
        return TRUE;
    }

    return FALSE;
}

extern "C"
BOOL
TestCodePage(VOID)
{
    static const UINT CodePages[] = {
        CP_ACP,
        CP_OEMCP,
        CP_MACCP,
        CP_THREAD_ACP,
        CP_UTF7,
        CP_UTF8,
        20949,
        51949,
        28598,
        38598,
    };
    static const UINT InvalidCodePages[] = {
        CP_SYMBOL,
        1200,
        1201,
        12000,
        12001,
        60000,
        65002,
        MAXUINT
    };
    PFN_GET_CP_INFO SystemGetCPInfo;
    PFN_IS_VALID_CODE_PAGE SystemIsValidCodePage;
    CPINFO CpInfo;
    UINT i;

    SystemGetCPInfo = (PFN_GET_CP_INFO)GetKernel32Procedure("GetCPInfo");
    SystemIsValidCodePage = (PFN_IS_VALID_CODE_PAGE)GetKernel32Procedure("IsValidCodePage");
    if (SystemGetCPInfo == NULL || SystemIsValidCodePage == NULL)
    {
        return FALSE;
    }

    for (i = 0; i < _countof(CodePages); ++i)
    {
        if (!CompareGetCPInfo(SystemGetCPInfo, CodePages[i]) ||
            !CompareIsValidCodePage(SystemIsValidCodePage, CodePages[i]))
        {
            return FALSE;
        }
    }

    for (i = 0; i < _countof(InvalidCodePages); ++i)
    {
        if (!CompareGetCPInfo(SystemGetCPInfo, InvalidCodePages[i]) ||
            !CompareIsValidCodePage(SystemIsValidCodePage, InvalidCodePages[i]) ||
            !CompareGetCPInfoFailure(SystemGetCPInfo, InvalidCodePages[i], &CpInfo))
        {
            return FALSE;
        }
    }

    return
        CompareGetCPInfo(SystemGetCPInfo, NtCurrentPeb()->ActiveCodePage) &&
        CompareGetCPInfo(SystemGetCPInfo, NtCurrentPeb()->OemCodePage) &&
        CompareIsValidCodePage(SystemIsValidCodePage, NtCurrentPeb()->ActiveCodePage) &&
        CompareIsValidCodePage(SystemIsValidCodePage, NtCurrentPeb()->OemCodePage) &&
        CompareGetCPInfoFailure(SystemGetCPInfo, CP_ACP, NULL) &&
        CompareGetCPInfoFailure(SystemGetCPInfo, CP_UTF8, NULL);
}
