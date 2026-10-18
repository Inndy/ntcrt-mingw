/* Win32 APIs implemented by KNSoft.NtCRT PolyFill, shared with the MSVC build */

#include "NtCRT.h"

/* Shared sources are written for MSVC */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Warray-bounds"
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "../polyfill/PolyFill.inl"
#pragma GCC diagnostic pop

/* Win7 fallbacks used by PolyFill/Console.inl, kernel32 is always loaded in a Win32 process */

EXTERN_C
PVOID
_NtCRT_GetKernel32Function(
    _In_z_ PCSTR Name)
{
    static WCHAR Kernel32Name[] = L"kernel32.dll";
    UNICODE_STRING Kernel32 = { sizeof(Kernel32Name) - sizeof(WCHAR), sizeof(Kernel32Name), Kernel32Name };
    ANSI_STRING ProcName;
    PVOID DllHandle, Address;

    if (!NT_SUCCESS(_Inline_LdrGetDllHandle(NULL, NULL, &Kernel32, &DllHandle)))
    {
        return NULL;
    }
    _Inline_RtlInitAnsiString(&ProcName, Name);
    return NT_SUCCESS(LdrGetProcedureAddress(DllHandle, &ProcName, 0, &Address)) ? Address : NULL;
}

static
PCRT_GET_CONSOLE_MODE
__cdecl
try_get_console_mode(VOID) throw()
{
    return (PCRT_GET_CONSOLE_MODE)_NtCRT_GetKernel32Function("GetConsoleMode");
}

static
PCRT_GET_CONSOLE_OUTPUT_CP
__cdecl
try_get_console_output_cp(VOID) throw()
{
    return (PCRT_GET_CONSOLE_OUTPUT_CP)_NtCRT_GetKernel32Function("GetConsoleOutputCP");
}

/* WriteConsoleW through condrv, the request carries the message and the text as two input buffers */

typedef struct _NTCRT_CONSOLE_WRITE_IO
{
    CD_USER_DEFINED_IO Header;
    CD_IO_BUFFER DataBuffer;
    CD_IO_BUFFER ResultBuffer;
} NTCRT_CONSOLE_WRITE_IO;

typedef BOOL(WINAPI* PNTCRT_WRITE_CONSOLE_W)(HANDLE, const VOID*, DWORD, LPDWORD, LPVOID);

EXTERN_C
BOOL
WINAPI
_NtCRT_WriteConsoleW(
    _In_ HANDLE hConsoleOutput,
    _In_reads_(nNumberOfCharsToWrite) const VOID* lpBuffer,
    _In_ DWORD nNumberOfCharsToWrite,
    _Out_opt_ LPDWORD lpNumberOfCharsWritten)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    NTCRT_CONSOLE_WRITE_IO Request;
    CONSOLE_MSG_L1 Message;
    HANDLE ConsoleServerHandle;

    if (lpNumberOfCharsWritten != NULL)
    {
        *lpNumberOfCharsWritten = 0;
    }
    hConsoleOutput = _Inline_BasepMapStdHandle(hConsoleOutput);

    if (!IS_NT_VERSION_GE(NT_VERSION_WIN8))
    {
        auto const WriteConsoleW = (PNTCRT_WRITE_CONSOLE_W)_NtCRT_GetKernel32Function("WriteConsoleW");
        if (WriteConsoleW == NULL)
        {
            _Inline_SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
            return FALSE;
        }
        return WriteConsoleW(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten, NULL);
    }

    ConsoleServerHandle = NtCurrentPeb()->ProcessParameters->ConsoleHandle;
    if ((LONG_PTR)ConsoleServerHandle <= 0)
    {
        _Inline_BaseSetLastNTError(STATUS_INVALID_HANDLE);
        return FALSE;
    }
    if (nNumberOfCharsToWrite > MAXULONG / sizeof(WCHAR))
    {
        _Inline_BaseSetLastNTError(STATUS_INVALID_PARAMETER);
        return FALSE;
    }

    RtlZeroMemory(&Message, sizeof(Message));
    Message.ApiNumber = ConsolepWriteConsole;
    Message.ApiDescriptorSize = sizeof(Message.u.WriteConsole);
    Message.u.WriteConsole.NumBytes = nNumberOfCharsToWrite * sizeof(WCHAR);
    Message.u.WriteConsole.Unicode = TRUE;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Header.Client = hConsoleOutput;
    Request.Header.InputCount = 2;
    Request.Header.OutputCount = 1;
    Request.Header.Buffers[0].Size = sizeof(CONSOLE_MSG_HEADER) + sizeof(Message.u.WriteConsole);
    Request.Header.Buffers[0].Buffer = &Message;
    Request.DataBuffer.Size = nNumberOfCharsToWrite * sizeof(WCHAR);
    Request.DataBuffer.Buffer = (PVOID)lpBuffer;
    Request.ResultBuffer.Size = sizeof(Message.u.WriteConsole);
    Request.ResultBuffer.Buffer = &Message.u;

    Status = NtDeviceIoControlFile(ConsoleServerHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_CONDRV_ISSUE_USER_IO,
                                   &Request,
                                   sizeof(Request),
                                   NULL,
                                   0);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    if (lpNumberOfCharsWritten != NULL)
    {
        *lpNumberOfCharsWritten = Message.u.WriteConsole.NumBytes / sizeof(WCHAR);
    }
    return TRUE;
}

/* Exports */

EXTERN_C
int
WINAPI
_NtCRT_MultiByteToWideChar(
    _In_ UINT CodePage,
    _In_ DWORD dwFlags,
    _In_ LPCCH lpMultiByteStr,
    _In_ int cbMultiByte,
    _Out_opt_ LPWSTR lpWideCharStr,
    _In_ int cchWideChar)
{
    return __acrt_MultiByteToWideChar(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
}

EXTERN_C
int
WINAPI
_NtCRT_WideCharToMultiByte(
    _In_ UINT CodePage,
    _In_ DWORD dwFlags,
    _In_ LPCWCH lpWideCharStr,
    _In_ int cchWideChar,
    _Out_opt_ LPSTR lpMultiByteStr,
    _In_ int cbMultiByte,
    _In_opt_ LPCCH lpDefaultChar,
    _Out_opt_ LPBOOL lpUsedDefaultChar)
{
    return __acrt_WideCharToMultiByte(CodePage,
                                      dwFlags,
                                      lpWideCharStr,
                                      cchWideChar,
                                      lpMultiByteStr,
                                      cbMultiByte,
                                      lpDefaultChar,
                                      lpUsedDefaultChar);
}

NTCRT_API UINT WINAPI _K32_GetACP(VOID) { return _CRT_GetACP(); }
NTCRT_API UINT WINAPI _K32_GetOEMCP(VOID) { return _CRT_GetOEMCP(); }

NTCRT_API
BOOL
WINAPI
_K32_IsDBCSLeadByteEx(
    _In_ UINT CodePage,
    _In_ BYTE TestChar)
{
    CPINFO Info;

    if (!_CRT_GetCPInfo(CodePage, &Info))
    {
        return FALSE;
    }
    for (UINT i = 0; i + 1 < MAX_LEADBYTES && Info.LeadByte[i] != 0; i += 2)
    {
        if (TestChar >= Info.LeadByte[i] && TestChar <= Info.LeadByte[i + 1])
        {
            return TRUE;
        }
    }
    return FALSE;
}

NTCRT_API BOOL WINAPI _K32_IsDBCSLeadByte(_In_ BYTE TestChar) { return _K32_IsDBCSLeadByteEx(CP_ACP, TestChar); }

NTCRT_API
HMODULE
WINAPI
_K32_LoadLibraryExW(
    _In_ LPCWSTR lpLibFileName,
    _Reserved_ HANDLE hFile,
    _In_ DWORD dwFlags)
{
    return _CRT_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
}

NTCRT_API
HMODULE
WINAPI
_K32_LoadLibraryW(
    _In_ LPCWSTR lpLibFileName)
{
    return _CRT_LoadLibraryExW(lpLibFileName, NULL, 0);
}

NTCRT_API
HMODULE
WINAPI
_K32_LoadLibraryExA(
    _In_ LPCSTR lpLibFileName,
    _Reserved_ HANDLE hFile,
    _In_ DWORD dwFlags)
{
    HMODULE Module;
    PWSTR Name = _NtCRT_AnsiToWide(lpLibFileName);

    if (Name == NULL)
    {
        return NULL;
    }
    Module = _CRT_LoadLibraryExW(Name, hFile, dwFlags);
    RtlFreeHeap(RtlProcessHeap(), 0, Name);
    return Module;
}

NTCRT_API
HMODULE
WINAPI
_K32_LoadLibraryA(
    _In_ LPCSTR lpLibFileName)
{
    return _K32_LoadLibraryExA(lpLibFileName, NULL, 0);
}

NTCRT_API
FARPROC
WINAPI
_K32_GetProcAddress(
    _In_ HMODULE hModule,
    _In_ LPCSTR lpProcName)
{
    return _CRT_GetProcAddress(hModule, lpProcName);
}

NTCRT_API
BOOL
WINAPI
_K32_FreeLibrary(
    _In_ HMODULE hLibModule)
{
    if (LDR_IS_RESOURCE(hLibModule))
    {
        _Inline_SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    NTSTATUS Status = LdrUnloadDll(hLibModule);
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

/*
 * Install the filter to ntdll directly as the original NtCRT does. kernel32!UnhandledExceptionFilter is returned as
 * the previous filter at the first time, so MinGW-w64's _gnu_exception_handler still chains to the default behavior.
 */
NTCRT_API
LPTOP_LEVEL_EXCEPTION_FILTER
WINAPI
_K32_SetUnhandledExceptionFilter(
    _In_opt_ LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
    static PVOID Previous = NULL;
    static BOOL Initialized = FALSE;
    LPTOP_LEVEL_EXCEPTION_FILTER Old;

    if (!Initialized)
    {
        Previous = RtlEncodePointer(_NtCRT_GetKernel32Function("UnhandledExceptionFilter"));
        Initialized = TRUE;
    }
    Old = (LPTOP_LEVEL_EXCEPTION_FILTER)RtlDecodePointer(Previous);
    Previous = RtlEncodePointer((PVOID)lpTopLevelExceptionFilter);
    _CRT_SetUnhandledExceptionFilter(lpTopLevelExceptionFilter);
    return Old;
}

NTCRT_API DWORD WINAPI _K32_GetFileType(_In_ HANDLE hFile) { return _CRT_GetFileType(hFile); }
NTCRT_API BOOL WINAPI _K32_GetConsoleMode(_In_ HANDLE h, _Out_ LPDWORD m) { return _CRT_GetConsoleMode(h, m); }
NTCRT_API UINT WINAPI _K32_GetConsoleOutputCP(VOID) { return _CRT_GetConsoleOutputCP(); }

NTCRT_API
BOOL
WINAPI
_K32_WriteConsoleW(
    _In_ HANDLE hConsoleOutput,
    _In_reads_(nNumberOfCharsToWrite) const VOID* lpBuffer,
    _In_ DWORD nNumberOfCharsToWrite,
    _Out_opt_ LPDWORD lpNumberOfCharsWritten,
    _Reserved_ LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(lpReserved);
    return _NtCRT_WriteConsoleW(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten);
}

/* UCRT internal of KNSoft.NtCRT UCRT/Locale.inl, used by test/upstream */
NTCRT_API
_Success_(return)
BOOL
__cdecl
__acrt_GetStringTypeW(
    _In_ DWORD _DWInfoType,
    _In_NLS_string_(_CchSrc) PCWCH _LpSrcStr,
    _In_ int _CchSrc,
    _Out_ LPWORD _LpCharType)
{
    return _CRT_GetStringTypeW(_DWInfoType, _LpSrcStr, _CchSrc, _LpCharType);
}

NTCRT_IMPORT(MultiByteToWideChar, _NtCRT_MultiByteToWideChar);
NTCRT_IMPORT(WideCharToMultiByte, _NtCRT_WideCharToMultiByte);
NTCRT_IMPORT(GetACP, _K32_GetACP);
NTCRT_IMPORT(GetOEMCP, _K32_GetOEMCP);
NTCRT_IMPORT(GetCPInfo, _CRT_GetCPInfo);
NTCRT_IMPORT(IsValidCodePage, _CRT_IsValidCodePage);
NTCRT_IMPORT(GetStringTypeW, _CRT_GetStringTypeW);
NTCRT_IMPORT(IsDBCSLeadByteEx, _K32_IsDBCSLeadByteEx);
NTCRT_IMPORT(IsDBCSLeadByte, _K32_IsDBCSLeadByte);
NTCRT_IMPORT(LoadLibraryExW, _K32_LoadLibraryExW);
NTCRT_IMPORT(LoadLibraryW, _K32_LoadLibraryW);
NTCRT_IMPORT(LoadLibraryExA, _K32_LoadLibraryExA);
NTCRT_IMPORT(LoadLibraryA, _K32_LoadLibraryA);
NTCRT_IMPORT(GetProcAddress, _K32_GetProcAddress);
NTCRT_IMPORT(FreeLibrary, _K32_FreeLibrary);
NTCRT_IMPORT(SetUnhandledExceptionFilter, _K32_SetUnhandledExceptionFilter);
NTCRT_IMPORT(GetFileType, _K32_GetFileType);
NTCRT_IMPORT(GetConsoleMode, _K32_GetConsoleMode);
NTCRT_IMPORT(GetConsoleOutputCP, _K32_GetConsoleOutputCP);
NTCRT_IMPORT(WriteConsoleW, _K32_WriteConsoleW);
