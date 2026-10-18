#include <KNSoft/NDK/NDK.inl>

#include <string.h>

extern "C"
BOOL
__cdecl
__acrt_GetStringTypeW(
    _In_ DWORD _DWInfoType,
    _In_NLS_string_(_CchSrc) PCWCH _LpSrcStr,
    _In_ int _CchSrc,
    _Out_ LPWORD _LpCharType);

typedef BOOL (WINAPI *PFN_GET_STRING_TYPE_W)(
    _In_ DWORD dwInfoType,
    _In_NLS_string_(cchSrc) LPCWCH lpSrcStr,
    _In_ int cchSrc,
    _Out_ LPWORD lpCharType);

static
PFN_GET_STRING_TYPE_W
GetSystemGetStringTypeW(VOID)
{
    ANSI_STRING ProcedureName;
    HMODULE Kernel32;
    PVOID ProcedureAddress;
    NTSTATUS Status;

    Kernel32 = _Inline_GetModuleHandleW(L"kernel32.dll");
    if (!Kernel32)
    {
        return NULL;
    }

    _Inline_RtlInitAnsiString(&ProcedureName, "GetStringTypeW");
    Status = LdrGetProcedureAddress(Kernel32, &ProcedureName, 0, &ProcedureAddress);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return NULL;
    }

    return (PFN_GET_STRING_TYPE_W)ProcedureAddress;
}

static
int
GetActualCount(
    _In_z_ LPCWCH String,
    _In_ int Count)
{
    int ActualCount;

    if (Count >= 0)
    {
        return Count;
    }

    for (ActualCount = 1; *String++; ActualCount++)
    {
    }

    return ActualCount;
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
CompareSuccess(
    _In_ PFN_GET_STRING_TYPE_W SystemGetStringTypeW,
    _In_ DWORD InfoType,
    _In_NLS_string_(Count) LPCWCH String,
    _In_ int Count)
{
    WORD Expected[32];
    WORD Actual[32];
    int ActualCount;

    memset(Expected, 0xCC, sizeof(Expected));
    memset(Actual, 0xCC, sizeof(Actual));

    if (!SystemGetStringTypeW(InfoType, String, Count, Expected))
    {
        return FALSE;
    }

    if (!__acrt_GetStringTypeW(InfoType, String, Count, Actual))
    {
        return FALSE;
    }

    ActualCount = GetActualCount(String, Count);
    if (ActualCount < 0 || ActualCount > _countof(Expected))
    {
        return FALSE;
    }

    return CompareMemory(Actual, Expected, ActualCount * sizeof(WORD));
}

static
BOOL
CompareFailure(
    _In_ PFN_GET_STRING_TYPE_W SystemGetStringTypeW,
    _In_ DWORD InfoType,
    _In_NLS_string_(Count) LPCWCH String,
    _In_ int Count,
    _Out_opt_ LPWORD CharType)
{
    DWORD ExpectedError;
    DWORD ActualError;
    BOOL ExpectedResult;
    BOOL ActualResult;

    _Inline_SetLastError(0);
    ExpectedResult = SystemGetStringTypeW(InfoType, String, Count, CharType);
    ExpectedError = _Inline_GetLastError();

    _Inline_SetLastError(0);
    ActualResult = __acrt_GetStringTypeW(InfoType, String, Count, CharType);
    ActualError = _Inline_GetLastError();

    return !ExpectedResult && ActualResult == ExpectedResult && ActualError == ExpectedError;
}

static
BOOL
CompareOverlap(
    _In_ PFN_GET_STRING_TYPE_W SystemGetStringTypeW)
{
    WCHAR ExpectedBuffer[] = { L'A', L'B', L'C', 0 };
    WCHAR ActualBuffer[] = { L'A', L'B', L'C', 0 };

    if (SystemGetStringTypeW(CT_CTYPE1, ExpectedBuffer, 2, (LPWORD)(ExpectedBuffer + 1)) !=
        __acrt_GetStringTypeW(CT_CTYPE1, ActualBuffer, 2, (LPWORD)(ActualBuffer + 1)))
    {
        return FALSE;
    }

    return CompareMemory(ActualBuffer, ExpectedBuffer, sizeof(ExpectedBuffer));
}

extern "C"
BOOL
TestGetStringTypeW(VOID)
{
    static const WCHAR Sample[] = {
        L'A', L'a', L'0', L' ', L'\t', L'\r', L'\n', L'_',
        0x4E2D, 0x3042, 0x30A2, 0x0640, 0xD800, 0xDC00, 0
    };
    static const WCHAR NullTerminated[] = L"Az";
    PFN_GET_STRING_TYPE_W SystemGetStringTypeW;
    WORD Types[_countof(Sample)];
    WCHAR SameBuffer[] = L"A";

    SystemGetStringTypeW = GetSystemGetStringTypeW();
    if (!SystemGetStringTypeW)
    {
        return FALSE;
    }

    return
        CompareSuccess(SystemGetStringTypeW, CT_CTYPE1, Sample, _countof(Sample) - 1) &&
        CompareSuccess(SystemGetStringTypeW, CT_CTYPE2, Sample, _countof(Sample) - 1) &&
        CompareSuccess(SystemGetStringTypeW, CT_CTYPE3, Sample, _countof(Sample) - 1) &&
        CompareSuccess(SystemGetStringTypeW, CT_CTYPE1, NullTerminated, -1) &&
        CompareSuccess(SystemGetStringTypeW, CT_CTYPE1, NullTerminated, -2) &&
        CompareOverlap(SystemGetStringTypeW) &&
        CompareFailure(SystemGetStringTypeW, 0, Sample, 1, Types) &&
        CompareFailure(SystemGetStringTypeW, 0, NullTerminated, -1, Types) &&
        CompareFailure(SystemGetStringTypeW, 0, NullTerminated, -2, Types) &&
        CompareFailure(SystemGetStringTypeW, CT_CTYPE1, NULL, 1, Types) &&
        CompareFailure(SystemGetStringTypeW, CT_CTYPE1, Sample, 0, Types) &&
        CompareFailure(SystemGetStringTypeW, CT_CTYPE1, Sample, 1, NULL) &&
        CompareFailure(SystemGetStringTypeW, CT_CTYPE1, SameBuffer, 1, (LPWORD)SameBuffer);
}
