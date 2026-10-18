#include <KNSoft/NDK/NDK.inl>

extern "C"
BOOL
WINAPI
_CRT_GetConsoleMode(
    _In_ HANDLE hConsoleHandle,
    _Out_ LPDWORD lpMode);

extern "C"
UINT
WINAPI
_CRT_GetConsoleOutputCP(VOID);

typedef BOOL(WINAPI* PFN_GET_CONSOLE_MODE)(
    _In_ HANDLE hConsoleHandle,
    _Out_ LPDWORD lpMode);

typedef UINT(WINAPI* PFN_GET_CONSOLE_OUTPUT_CP)(VOID);

static
PVOID
GetKernel32Procedure(
    _In_ PCSTR ProcedureNameString)
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
CompareConsoleMode(
    _In_ PFN_GET_CONSOLE_MODE SystemGetConsoleMode,
    _In_ HANDLE ConsoleHandle)
{
    DWORD ExpectedError;
    DWORD ActualError;
    DWORD ExpectedMode;
    DWORD ActualMode;
    BOOL ExpectedResult;
    BOOL ActualResult;

    ExpectedMode = 0xcccccccc;
    ActualMode = 0xcccccccc;

    _Inline_SetLastError(0);
    ExpectedResult = SystemGetConsoleMode(ConsoleHandle, &ExpectedMode);
    ExpectedError = _Inline_GetLastError();

    _Inline_SetLastError(0);
    ActualResult = _CRT_GetConsoleMode(ConsoleHandle, &ActualMode);
    ActualError = _Inline_GetLastError();

    return
        ActualResult == ExpectedResult &&
        ActualError == ExpectedError &&
        (!ActualResult || ActualMode == ExpectedMode);
}

static
BOOL
TestGetConsoleMode(VOID)
{
    PFN_GET_CONSOLE_MODE SystemGetConsoleMode;
    RTL_USER_PROCESS_PARAMETERS* ProcessParameters;

    SystemGetConsoleMode = (PFN_GET_CONSOLE_MODE)GetKernel32Procedure("GetConsoleMode");
    if (SystemGetConsoleMode == NULL)
    {
        return FALSE;
    }

    ProcessParameters = NtCurrentPeb()->ProcessParameters;

    return
        CompareConsoleMode(SystemGetConsoleMode, NULL) &&
        CompareConsoleMode(SystemGetConsoleMode, INVALID_HANDLE_VALUE) &&
        CompareConsoleMode(SystemGetConsoleMode, ProcessParameters->StandardInput) &&
        CompareConsoleMode(SystemGetConsoleMode, ProcessParameters->StandardOutput) &&
        CompareConsoleMode(SystemGetConsoleMode, ProcessParameters->StandardError);
}

static
BOOL
TestGetConsoleOutputCP(VOID)
{
    PFN_GET_CONSOLE_OUTPUT_CP SystemGetConsoleOutputCP;
    DWORD ExpectedError;
    DWORD ActualError;
    UINT ExpectedCodePage;
    UINT ActualCodePage;

    SystemGetConsoleOutputCP = (PFN_GET_CONSOLE_OUTPUT_CP)GetKernel32Procedure("GetConsoleOutputCP");
    if (SystemGetConsoleOutputCP == NULL)
    {
        return FALSE;
    }

    _Inline_SetLastError(0);
    ExpectedCodePage = SystemGetConsoleOutputCP();
    ExpectedError = _Inline_GetLastError();

    _Inline_SetLastError(0);
    ActualCodePage = _CRT_GetConsoleOutputCP();
    ActualError = _Inline_GetLastError();

    return ActualCodePage == ExpectedCodePage && ActualError == ExpectedError;
}

extern "C"
BOOL
TestConsole(VOID)
{
    return TestGetConsoleMode() && TestGetConsoleOutputCP();
}
