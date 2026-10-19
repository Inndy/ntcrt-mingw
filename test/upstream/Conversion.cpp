#include <KNSoft/NDK/NDK.inl>

extern "C"
int
__cdecl
__acrt_MultiByteToWideChar(
    _In_ UINT CodePage,
    _In_ DWORD dwFlags,
    _In_NLS_string_(cbMultiByte) LPCCH lpMultiByteStr,
    _In_ int cbMultiByte,
    _Out_writes_opt_(cchWideChar) LPWSTR lpWideCharStr,
    _In_ int cchWideChar);

extern "C"
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
    _Out_opt_ LPBOOL lpUsedDefaultChar);

typedef int (WINAPI *PFN_MULTI_BYTE_TO_WIDE_CHAR)(
    _In_ UINT CodePage,
    _In_ DWORD dwFlags,
    _In_NLS_string_(cbMultiByte) LPCCH lpMultiByteStr,
    _In_ int cbMultiByte,
    _Out_writes_opt_(cchWideChar) LPWSTR lpWideCharStr,
    _In_ int cchWideChar);

typedef int (WINAPI *PFN_WIDE_CHAR_TO_MULTI_BYTE)(
    _In_ UINT CodePage,
    _In_ DWORD dwFlags,
    _In_NLS_string_(cchWideChar) LPCWCH lpWideCharStr,
    _In_ int cchWideChar,
    _Out_writes_opt_(cbMultiByte) LPSTR lpMultiByteStr,
    _In_ int cbMultiByte,
    _In_opt_ LPCCH lpDefaultChar,
    _Out_opt_ LPBOOL lpUsedDefaultChar);

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
CompareMultiByteToWideCharCall(
    _In_ PFN_MULTI_BYTE_TO_WIDE_CHAR SystemMultiByteToWideChar,
    _In_ UINT CodePage,
    _In_ DWORD Flags,
    _In_NLS_string_(InputSize) LPCCH Input,
    _In_ int InputSize,
    _Out_writes_opt_(OutputSize) LPWSTR Output,
    _In_ int OutputSize)
{
    WCHAR Expected[128];
    WCHAR Actual[128];
    LPWSTR ExpectedOutput;
    LPWSTR ActualOutput;
    DWORD ExpectedError;
    DWORD ActualError;
    int ExpectedResult;
    int ActualResult;

    RtlFillMemory(Expected, sizeof(Expected), 0xCC);
    RtlFillMemory(Actual, sizeof(Actual), 0xCC);
    ExpectedOutput = Output == NULL ? NULL : Expected;
    ActualOutput = Output == NULL ? NULL : Actual;

    _Inline_SetLastError(0);
    ExpectedResult = SystemMultiByteToWideChar(CodePage, Flags, Input, InputSize, ExpectedOutput, OutputSize);
    ExpectedError = _Inline_GetLastError();

    _Inline_SetLastError(0);
    ActualResult = __acrt_MultiByteToWideChar(CodePage, Flags, Input, InputSize, ActualOutput, OutputSize);
    ActualError = _Inline_GetLastError();

    if (ActualResult != ExpectedResult || ActualError != ExpectedError)
    {
        return FALSE;
    }

    if (ExpectedResult != 0 &&
        ExpectedOutput != NULL &&
        !CompareMemory(Actual, Expected, ExpectedResult * sizeof(WCHAR)))
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOL
CompareWideCharToMultiByteCall(
    _In_ PFN_WIDE_CHAR_TO_MULTI_BYTE SystemWideCharToMultiByte,
    _In_ UINT CodePage,
    _In_ DWORD Flags,
    _In_NLS_string_(InputSize) LPCWCH Input,
    _In_ int InputSize,
    _Out_writes_opt_(OutputSize) LPSTR Output,
    _In_ int OutputSize)
{
    CHAR Expected[256];
    CHAR Actual[256];
    LPSTR ExpectedOutput;
    LPSTR ActualOutput;
    DWORD ExpectedError;
    DWORD ActualError;
    int ExpectedResult;
    int ActualResult;

    RtlFillMemory(Expected, sizeof(Expected), 0xCC);
    RtlFillMemory(Actual, sizeof(Actual), 0xCC);
    ExpectedOutput = Output == NULL ? NULL : Expected;
    ActualOutput = Output == NULL ? NULL : Actual;

    _Inline_SetLastError(0);
    ExpectedResult = SystemWideCharToMultiByte(CodePage, Flags, Input, InputSize, ExpectedOutput, OutputSize, NULL, NULL);
    ExpectedError = _Inline_GetLastError();

    _Inline_SetLastError(0);
    ActualResult = __acrt_WideCharToMultiByte(CodePage, Flags, Input, InputSize, ActualOutput, OutputSize, NULL, NULL);
    ActualError = _Inline_GetLastError();

    if (ActualResult != ExpectedResult || ActualError != ExpectedError)
    {
        return FALSE;
    }

    if (ExpectedResult != 0 &&
        ExpectedOutput != NULL &&
        !CompareMemory(Actual, Expected, ExpectedResult))
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOL
CompareWideCharToMultiByteDefaultUsed(
    _In_ PFN_WIDE_CHAR_TO_MULTI_BYTE SystemWideCharToMultiByte,
    _In_ UINT CodePage,
    _In_NLS_string_(InputSize) LPCWCH Input,
    _In_ int InputSize,
    _In_ int OutputSize)
{
    CHAR Expected[32];
    CHAR Actual[32];
    BOOL ExpectedUsed;
    BOOL ActualUsed;
    DWORD ExpectedError;
    DWORD ActualError;
    int ExpectedResult;
    int ActualResult;

    RtlFillMemory(Expected, sizeof(Expected), 0xCC);
    RtlFillMemory(Actual, sizeof(Actual), 0xCC);
    ExpectedUsed = (BOOL)0xCC;
    ActualUsed = (BOOL)0xCC;

    _Inline_SetLastError(0);
    ExpectedResult = SystemWideCharToMultiByte(CodePage,
                                               0,
                                               Input,
                                               InputSize,
                                               OutputSize != 0 ? Expected : NULL,
                                               OutputSize,
                                               NULL,
                                               &ExpectedUsed);
    ExpectedError = _Inline_GetLastError();

    _Inline_SetLastError(0);
    ActualResult = __acrt_WideCharToMultiByte(CodePage,
                                              0,
                                              Input,
                                              InputSize,
                                              OutputSize != 0 ? Actual : NULL,
                                              OutputSize,
                                              NULL,
                                              &ActualUsed);
    ActualError = _Inline_GetLastError();

    if (ActualResult != ExpectedResult || ActualError != ExpectedError || ActualUsed != ExpectedUsed)
    {
        return FALSE;
    }

    if (ExpectedResult != 0 &&
        OutputSize != 0 &&
        !CompareMemory(Actual, Expected, ExpectedResult))
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOL
CompareMultiByteToWideChar(
    _In_ PFN_MULTI_BYTE_TO_WIDE_CHAR SystemMultiByteToWideChar,
    _In_ UINT CodePage,
    _In_ DWORD Flags,
    _In_NLS_string_(InputSize) LPCCH Input,
    _In_ int InputSize)
{
    int RequiredSize;
    WCHAR Buffer[128];

    if (!CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, CodePage, Flags, Input, InputSize, NULL, 0))
    {
        return FALSE;
    }

    RequiredSize = __acrt_MultiByteToWideChar(CodePage, Flags, Input, InputSize, NULL, 0);
    if (RequiredSize <= 0 || RequiredSize > _countof(Buffer))
    {
        return FALSE;
    }

    return
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, CodePage, Flags, Input, InputSize, Buffer, RequiredSize) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, CodePage, Flags, Input, InputSize, Buffer, RequiredSize - 1);
}

static
BOOL
CompareWideCharToMultiByte(
    _In_ PFN_WIDE_CHAR_TO_MULTI_BYTE SystemWideCharToMultiByte,
    _In_ UINT CodePage,
    _In_ DWORD Flags,
    _In_NLS_string_(InputSize) LPCWCH Input,
    _In_ int InputSize)
{
    int RequiredSize;
    CHAR Buffer[256];

    if (!CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, CodePage, Flags, Input, InputSize, NULL, 0))
    {
        return FALSE;
    }

    RequiredSize = __acrt_WideCharToMultiByte(CodePage, Flags, Input, InputSize, NULL, 0, NULL, NULL);
    if (RequiredSize <= 0 || RequiredSize > _countof(Buffer))
    {
        return FALSE;
    }

    return
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, CodePage, Flags, Input, InputSize, Buffer, RequiredSize) &&
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, CodePage, Flags, Input, InputSize, Buffer, RequiredSize - 1);
}

static
BOOL
TestMultiByteToWideCharErrors(
    _In_ PFN_MULTI_BYTE_TO_WIDE_CHAR SystemMultiByteToWideChar)
{
    static const CHAR InvalidDbcs[] = { (CHAR)0x81 };
    CHAR SameBuffer[8] = "A";
    WCHAR Buffer[8];

    return
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, CP_ACP, 0, NULL, 1, Buffer, _countof(Buffer)) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, CP_ACP, 0, "A", 0, Buffer, _countof(Buffer)) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, CP_ACP, 0, "A", 1, Buffer, -1) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, CP_ACP, 0, "A", 1, NULL, _countof(Buffer)) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, 1252, 0, "A", -1, Buffer, 0) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, CP_UTF8, 0, "A", -1, Buffer, 0) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, CP_ACP, 0, SameBuffer, 1, (LPWSTR)SameBuffer, 1) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, CP_UTF8, MB_PRECOMPOSED, "A", 1, Buffer, _countof(Buffer)) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, 932, MB_ERR_INVALID_CHARS, InvalidDbcs, sizeof(InvalidDbcs), NULL, 0) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, 932, MB_ERR_INVALID_CHARS, InvalidDbcs, sizeof(InvalidDbcs), Buffer, _countof(Buffer)) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, 1200, 0, "A", 1, Buffer, _countof(Buffer)) &&
        CompareMultiByteToWideCharCall(SystemMultiByteToWideChar, 60000, 0, "A", 1, Buffer, _countof(Buffer));
}

static
BOOL
TestWideCharToMultiByteErrors(
    _In_ PFN_WIDE_CHAR_TO_MULTI_BYTE SystemWideCharToMultiByte)
{
    WCHAR SameBuffer[8] = L"A";
    CHAR Buffer[8];

    return
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, CP_ACP, 0, NULL, 1, Buffer, _countof(Buffer)) &&
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, CP_ACP, 0, L"A", 0, Buffer, _countof(Buffer)) &&
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, CP_ACP, 0, L"A", 1, Buffer, -1) &&
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, CP_ACP, 0, L"A", 1, NULL, _countof(Buffer)) &&
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, 1252, 0, L"A", -1, Buffer, 0) &&
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, CP_UTF8, 0, L"A", -1, Buffer, 0) &&
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, CP_ACP, 0, SameBuffer, 1, (LPSTR)SameBuffer, 1) &&
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, CP_UTF8, WC_COMPOSITECHECK, L"A", 1, Buffer, _countof(Buffer)) &&
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, 1200, 0, L"A", 1, Buffer, _countof(Buffer)) &&
        CompareWideCharToMultiByteCall(SystemWideCharToMultiByte, 60000, 0, L"A", 1, Buffer, _countof(Buffer));
}

static
BOOL
TestWideCharToMultiByteDefaultUsed(
    _In_ PFN_WIDE_CHAR_TO_MULTI_BYTE SystemWideCharToMultiByte)
{
    static const WCHAR Representable[] = L"A";
    static const WCHAR NotRepresentable[] = { 0x4E2D, 0 };

    return
        CompareWideCharToMultiByteDefaultUsed(SystemWideCharToMultiByte, CP_UTF8, Representable, 1, 4) &&
        CompareWideCharToMultiByteDefaultUsed(SystemWideCharToMultiByte, CP_UTF8, NotRepresentable, 1, 8) &&
        CompareWideCharToMultiByteDefaultUsed(SystemWideCharToMultiByte, 1252, Representable, 1, 1) &&
        CompareWideCharToMultiByteDefaultUsed(SystemWideCharToMultiByte, 1252, NotRepresentable, 1, 1) &&
        CompareWideCharToMultiByteDefaultUsed(SystemWideCharToMultiByte, 1252, NotRepresentable, -1, 2);
}

extern "C"
BOOL
TestConversion(VOID)
{
    static const CHAR AnsiString[] = "Az09";
    static const CHAR Utf8String[] = "\xE4\xB8\xAD";
    static const CHAR Utf8Languages[] = {
        (CHAR)0xE4, (CHAR)0xB8, (CHAR)0xAD, (CHAR)0xE6, (CHAR)0x96, (CHAR)0x87, 0x20,
        (CHAR)0xE6, (CHAR)0x97, (CHAR)0xA5, (CHAR)0xE6, (CHAR)0x9C, (CHAR)0xAC,
        (CHAR)0xE8, (CHAR)0xAA, (CHAR)0x9E, 0x20,
        (CHAR)0xED, (CHAR)0x95, (CHAR)0x9C, (CHAR)0xEA, (CHAR)0xB5, (CHAR)0xAD,
        (CHAR)0xEC, (CHAR)0x96, (CHAR)0xB4, 0x20,
        (CHAR)0xD0, (CHAR)0xA0, (CHAR)0xD1, (CHAR)0x83, (CHAR)0xD1, (CHAR)0x81,
        (CHAR)0xD1, (CHAR)0x81, (CHAR)0xD0, (CHAR)0xBA, (CHAR)0xD0, (CHAR)0xB8,
        (CHAR)0xD0, (CHAR)0xB9, 0x20,
        (CHAR)0xD8, (CHAR)0xA7, (CHAR)0xD9, (CHAR)0x84, (CHAR)0xD8, (CHAR)0xB9,
        (CHAR)0xD8, (CHAR)0xB1, (CHAR)0xD8, (CHAR)0xA8, (CHAR)0xD9, (CHAR)0x8A,
        (CHAR)0xD8, (CHAR)0xA9, 0x20,
        (CHAR)0xCE, (CHAR)0x95, (CHAR)0xCE, (CHAR)0xBB, (CHAR)0xCE, (CHAR)0xBB,
        (CHAR)0xCE, (CHAR)0xB7, (CHAR)0xCE, (CHAR)0xBD, (CHAR)0xCE, (CHAR)0xB9,
        (CHAR)0xCE, (CHAR)0xBA, (CHAR)0xCE, (CHAR)0xAC, 0x20,
        (CHAR)0xD7, (CHAR)0xA2, (CHAR)0xD7, (CHAR)0x91, (CHAR)0xD7, (CHAR)0xA8,
        (CHAR)0xD7, (CHAR)0x99, (CHAR)0xD7, (CHAR)0xAA, 0x20,
        (CHAR)0xE0, (CHAR)0xA4, (CHAR)0xB9, (CHAR)0xE0, (CHAR)0xA4, (CHAR)0xBF,
        (CHAR)0xE0, (CHAR)0xA4, (CHAR)0xA8, (CHAR)0xE0, (CHAR)0xA5, (CHAR)0x8D,
        (CHAR)0xE0, (CHAR)0xA4, (CHAR)0xA6, (CHAR)0xE0, (CHAR)0xA5, (CHAR)0x80, 0x20,
        (CHAR)0xE0, (CHAR)0xB9, (CHAR)0x84, (CHAR)0xE0, (CHAR)0xB8, (CHAR)0x97,
        (CHAR)0xE0, (CHAR)0xB8, (CHAR)0xA2, 0x20,
        (CHAR)0xF0, (CHAR)0x9F, (CHAR)0x98, (CHAR)0x80, 0
    };
    static const WCHAR WideString[] = L"Az09";
    static const WCHAR Utf8WideString[] = { 0x4E2D, 0 };
    static const WCHAR WideLanguages[] = {
        0x4E2D, 0x6587, 0x0020,
        0x65E5, 0x672C, 0x8A9E, 0x0020,
        0xD55C, 0xAD6D, 0xC5B4, 0x0020,
        0x0420, 0x0443, 0x0441, 0x0441, 0x043A, 0x0438, 0x0439, 0x0020,
        0x0627, 0x0644, 0x0639, 0x0631, 0x0628, 0x064A, 0x0629, 0x0020,
        0x0395, 0x03BB, 0x03BB, 0x03B7, 0x03BD, 0x03B9, 0x03BA, 0x03AC, 0x0020,
        0x05E2, 0x05D1, 0x05E8, 0x05D9, 0x05EA, 0x0020,
        0x0939, 0x093F, 0x0928, 0x094D, 0x0926, 0x0940, 0x0020,
        0x0E44, 0x0E17, 0x0E22, 0x0020,
        0xD83D, 0xDE00, 0
    };
    PFN_MULTI_BYTE_TO_WIDE_CHAR SystemMultiByteToWideChar;
    PFN_WIDE_CHAR_TO_MULTI_BYTE SystemWideCharToMultiByte;

    SystemMultiByteToWideChar = (PFN_MULTI_BYTE_TO_WIDE_CHAR)GetKernel32Procedure("MultiByteToWideChar");
    SystemWideCharToMultiByte = (PFN_WIDE_CHAR_TO_MULTI_BYTE)GetKernel32Procedure("WideCharToMultiByte");
    if (SystemMultiByteToWideChar == NULL || SystemWideCharToMultiByte == NULL)
    {
        return FALSE;
    }

    return
        CompareMultiByteToWideChar(SystemMultiByteToWideChar, CP_ACP, MB_PRECOMPOSED, AnsiString, -1) &&
        CompareMultiByteToWideChar(SystemMultiByteToWideChar, CP_OEMCP, 0, AnsiString, _countof(AnsiString) - 1) &&
        CompareMultiByteToWideChar(SystemMultiByteToWideChar, CP_UTF8, 0, Utf8String, -1) &&
        CompareMultiByteToWideChar(SystemMultiByteToWideChar, CP_UTF8, 0, Utf8Languages, -1) &&
        CompareMultiByteToWideChar(SystemMultiByteToWideChar, CP_UTF8, MB_ERR_INVALID_CHARS, Utf8Languages, (int)sizeof(Utf8Languages) - 1) &&
        CompareMultiByteToWideChar(SystemMultiByteToWideChar, NtCurrentPeb()->ActiveCodePage, 0, AnsiString, -1) &&
        CompareWideCharToMultiByte(SystemWideCharToMultiByte, CP_ACP, 0, WideString, -1) &&
        CompareWideCharToMultiByte(SystemWideCharToMultiByte, CP_OEMCP, 0, WideString, _countof(WideString) - 1) &&
        CompareWideCharToMultiByte(SystemWideCharToMultiByte, CP_UTF8, 0, Utf8WideString, -1) &&
        CompareWideCharToMultiByte(SystemWideCharToMultiByte, CP_UTF8, 0, WideLanguages, -1) &&
        CompareWideCharToMultiByte(SystemWideCharToMultiByte, CP_UTF8, WC_ERR_INVALID_CHARS, WideLanguages, _countof(WideLanguages) - 1) &&
        CompareWideCharToMultiByte(SystemWideCharToMultiByte, NtCurrentPeb()->ActiveCodePage, 0, WideString, -1) &&
        TestMultiByteToWideCharErrors(SystemMultiByteToWideChar) &&
        TestWideCharToMultiByteErrors(SystemWideCharToMultiByte) &&
        TestWideCharToMultiByteDefaultUsed(SystemWideCharToMultiByte);
}
