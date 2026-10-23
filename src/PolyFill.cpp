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

/*
 * NTCRT_K32_FUNCTIONS and the module of the system messages, resolved at startup on Windows 7 (NTCRT_K32_EAGER_ALWAYS:
 * on all), whose LdrGetDllHandleEx and LdrGetProcedureAddress take the loader lock: looked up later, a call would
 * wait for a thread that holds it in its DLL_THREAD_DETACH while that one waits for the caller (a lock it holds).
 * Before the other initializers (.CRT$XIAA, _NtCRT_InitLowIO is in .CRT$XIB), under tcc with them after
 * __getmainargs and the constructors of the program. An export kernel32 lacks stays NULL: kernel32 is always loaded
 * in a Win32 process.
 */
#define NTCRT_K32_NAME(Name) #Name,
static const PCSTR g_Kernel32Names[NTCRT_K32_COUNT] = { NTCRT_K32_FUNCTIONS(NTCRT_K32_NAME) };
#undef NTCRT_K32_NAME
static PVOID g_Kernel32Functions[NTCRT_K32_COUNT];
static PVOID g_MessageModule;
static LONG g_Kernel32Resolved;

static
PVOID
Kernel32Lookup(
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

/* Message table resources of kernelbase.dll (or kernel32.dll before Win7) contain the system messages */
static
PVOID
MessageModuleLookup(VOID)
{
    PVOID Module = _Inline_GetModuleHandleW(L"kernelbase.dll");

    return Module != NULL ? Module : _Inline_GetModuleHandleW(L"kernel32.dll");
}

static int __cdecl ResolveKernel32(void)
{
#ifndef NTCRT_K32_EAGER_ALWAYS
    if (IS_NT_VERSION_GE(NT_VERSION_WIN8))
    {
        return 0;
    }
#endif
    for (ULONG i = 0; i < NTCRT_K32_COUNT; i++)
    {
        g_Kernel32Functions[i] = Kernel32Lookup(g_Kernel32Names[i]);
    }
    __atomic_store_n(&g_MessageModule, MessageModuleLookup(), __ATOMIC_RELEASE);
    __atomic_store_n(&g_Kernel32Resolved, 1, __ATOMIC_RELEASE);
    return 0;
}
NTCRT_INITIALIZER(".CRT$XIAA", _NtCRT_ResolveKernel32Ptr, ResolveKernel32);

EXTERN_C
PVOID
_NtCRT_GetKernel32Function(
    _In_ NTCRT_K32_FUNCTION Function)
{
    if (__atomic_load_n(&g_Kernel32Resolved, __ATOMIC_ACQUIRE))
    {
        return g_Kernel32Functions[Function];
    }
    return Kernel32Lookup(g_Kernel32Names[Function]);
}

EXTERN_C
PVOID
_NtCRT_GetSystemMessageModule(VOID)
{
    PVOID Module = __atomic_load_n(&g_MessageModule, __ATOMIC_ACQUIRE);

    if (Module == NULL)
    {
        Module = MessageModuleLookup();
        __atomic_store_n(&g_MessageModule, Module, __ATOMIC_RELEASE);
    }
    return Module;
}

static
PCRT_GET_CONSOLE_MODE
__cdecl
try_get_console_mode(VOID) throw()
{
    return (PCRT_GET_CONSOLE_MODE)_NtCRT_GetKernel32Function(NTCRT_K32_GetConsoleMode);
}

static
PCRT_GET_CONSOLE_OUTPUT_CP
__cdecl
try_get_console_output_cp(VOID) throw()
{
    return (PCRT_GET_CONSOLE_OUTPUT_CP)_NtCRT_GetKernel32Function(NTCRT_K32_GetConsoleOutputCP);
}

static
PCRT_VERIFY_CONSOLE_IO_HANDLE
__cdecl
try_get_verify_console_io_handle(VOID) throw()
{
    return (PCRT_VERIFY_CONSOLE_IO_HANDLE)_NtCRT_GetKernel32Function(NTCRT_K32_VerifyConsoleIoHandle);
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
        Previous = RtlEncodePointer(_NtCRT_GetKernel32Function(NTCRT_K32_UnhandledExceptionFilter));
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
NTCRT_IMPORT(SetUnhandledExceptionFilter, _K32_SetUnhandledExceptionFilter);
NTCRT_IMPORT(GetFileType, _K32_GetFileType);
NTCRT_IMPORT(GetConsoleMode, _K32_GetConsoleMode);
NTCRT_IMPORT(GetConsoleOutputCP, _K32_GetConsoleOutputCP);
