/* LoadLibrary, GetProcAddress and FreeLibrary of kernel32 by KNSoft.NtCRT PolyFill */

#include "NtCRT.h"

/* Shared sources are written for MSVC */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#include "../polyfill/Process.inl"
#pragma GCC diagnostic pop

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

NTCRT_IMPORT(LoadLibraryExW, _K32_LoadLibraryExW);
NTCRT_IMPORT(LoadLibraryW, _K32_LoadLibraryW);
NTCRT_IMPORT(LoadLibraryExA, _K32_LoadLibraryExA);
NTCRT_IMPORT(LoadLibraryA, _K32_LoadLibraryA);
NTCRT_IMPORT(GetProcAddress, _K32_GetProcAddress);
NTCRT_IMPORT(FreeLibrary, _K32_FreeLibrary);
