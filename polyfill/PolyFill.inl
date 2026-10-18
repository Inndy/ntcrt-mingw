#pragma once

#include <KNSoft/NDK/NDK.inl>

#include "Process.inl"
#include "CodePage.inl"
#include "Console.inl"

EXTERN_C
DWORD
WINAPI
_CRT_GetFileType(
    _In_ HANDLE hFile)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_FS_DEVICE_INFORMATION FsInformation;

    if (hFile == NULL)
    {
        goto _Invalid_Handle;
    }

    if ((DWORD)(ULONG_PTR)hFile == STD_INPUT_HANDLE)
    {
        hFile = NtCurrentPeb()->ProcessParameters->StandardInput;
    } else if ((DWORD)(ULONG_PTR)hFile == STD_OUTPUT_HANDLE)
    {
        hFile = NtCurrentPeb()->ProcessParameters->StandardOutput;
    } else if ((DWORD)(ULONG_PTR)hFile == STD_ERROR_HANDLE)
    {
        hFile = NtCurrentPeb()->ProcessParameters->StandardError;
    }

    if (hFile == NULL)
    {
_Invalid_Handle:
        _Inline_BaseSetLastNTError(STATUS_INVALID_HANDLE);
        return FILE_TYPE_UNKNOWN;
    }

    Status = NtQueryVolumeInformationFile(hFile,
                                          &IoStatusBlock,
                                          &FsInformation,
                                          sizeof(FsInformation),
                                          FileFsDeviceInformation);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FILE_TYPE_UNKNOWN;
    }

    switch (FsInformation.DeviceType)
    {
        case FILE_DEVICE_CD_ROM:
        case FILE_DEVICE_CD_ROM_FILE_SYSTEM:
        case FILE_DEVICE_CONTROLLER:
        case FILE_DEVICE_DATALINK:
        case FILE_DEVICE_DFS:
        case FILE_DEVICE_DISK:
        case FILE_DEVICE_DISK_FILE_SYSTEM:
        case FILE_DEVICE_VIRTUAL_DISK:
            return FILE_TYPE_DISK;

        case FILE_DEVICE_KEYBOARD:
        case FILE_DEVICE_MOUSE:
        case FILE_DEVICE_NULL:
        case FILE_DEVICE_PARALLEL_PORT:
        case FILE_DEVICE_PRINTER:
        case FILE_DEVICE_SERIAL_PORT:
        case FILE_DEVICE_SCREEN:
        case FILE_DEVICE_SOUND:
        case FILE_DEVICE_MODEM:
            return FILE_TYPE_CHAR;

        case FILE_DEVICE_NAMED_PIPE:
            return FILE_TYPE_PIPE;

        case FILE_DEVICE_CONSOLE:
            return FILE_TYPE_CHAR;
    }

    _Inline_SetLastError(ERROR_SUCCESS);
    return FILE_TYPE_UNKNOWN;
}

/* Use inline implementations by KNSoft.NDK */

#define GetProcessHeap _Inline_GetProcessHeap
#define HeapAlloc _Inline_HeapAlloc
#define HeapReAlloc _Inline_HeapReAlloc
#define HeapFree _Inline_HeapFree
#define HeapSize _Inline_HeapSize
#define HeapQueryInformation _Inline_HeapQueryInformation
#define HeapValidate _Inline_HeapValidate
#define HeapCompact _Inline_HeapCompact

#define GetStdHandle _Inline_GetStdHandle
#define GetFileType _CRT_GetFileType
#define GetConsoleMode _CRT_GetConsoleMode
#define GetConsoleOutputCP _CRT_GetConsoleOutputCP
#define SetStdHandle _Inline_SetStdHandle
#define WriteFile _Inline_WriteFile
#define FlushFileBuffers _Inline_FlushFileBuffers
#define SetFilePointerEx _Inline_SetFilePointerEx
#define GetFileSizeEx _Inline_GetFileSizeEx
#define CloseHandle _Inline_CloseHandle

#define IsProcessorFeaturePresent _Inline_IsProcessorFeaturePresent
#define InitializeSListHead _Inline_InitializeSListHead
#define InterlockedPushEntrySList _Inline_InterlockedPushEntrySList
#define InterlockedFlushSList _Inline_InterlockedFlushSList


