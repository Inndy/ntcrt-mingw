/* Files opened and deleted by DOS path: the helpers of File.cpp, _open, remove, DeleteFileW, RemoveDirectoryW */

#include "NtCRT.h"

EXTERN_C
NTSTATUS
_NtCRT_OpenPath(
    _Out_ PHANDLE FileHandle,
    _In_ PCWSTR FileName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG ShareAccess,
    _In_ ULONG OpenOptions)
{
    NTSTATUS Status;
    UNICODE_STRING NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;

    Status = RtlDosPathNameToNtPathName_U_WithStatus(FileName, &NtPath, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(FileHandle,
                        DesiredAccess | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        ShareAccess,
                        OpenOptions | FILE_SYNCHRONOUS_IO_NONALERT);
    RtlFreeUnicodeString(&NtPath);
    return Status;
}

EXTERN_C
ULONG
_NtCRT_CreateFile(
    _Out_ PHANDLE FileHandle,
    _In_ PCWSTR FileName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG ShareAccess,
    _In_ ULONG CreateDisposition,
    _In_ ULONG CreateOptions,
    _In_ ULONG FileAttributes,
    _Out_opt_ PULONG_PTR Information)
{
    NTSTATUS Status;
    UNICODE_STRING NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;

    *FileHandle = INVALID_HANDLE_VALUE;
    Status = RtlDosPathNameToNtPathName_U_WithStatus(FileName, &NtPath, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        return _Inline_BaseSetLastNTError(Status);
    }
    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    IoStatusBlock.Information = 0;
    Status = NtCreateFile(FileHandle,
                          DesiredAccess,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          FileAttributes,
                          ShareAccess,
                          CreateDisposition,
                          CreateOptions,
                          NULL,
                          0);
    RtlFreeUnicodeString(&NtPath);
    if (Information != NULL)
    {
        *Information = IoStatusBlock.Information;
    }
    if (!NT_SUCCESS(Status))
    {
        *FileHandle = INVALID_HANDLE_VALUE;
        if (Status == STATUS_OBJECT_NAME_COLLISION && CreateDisposition == FILE_CREATE)
        {
            _Inline_RtlNtStatusToDosError(Status);
            _Inline_SetLastError(ERROR_FILE_EXISTS);
            return ERROR_FILE_EXISTS;
        }
        if (Status == STATUS_FILE_IS_A_DIRECTORY)
        {
            _Inline_RtlNtStatusToDosError(Status);
            _Inline_SetLastError(ERROR_ACCESS_DENIED);
            return ERROR_ACCESS_DENIED;
        }
        return _Inline_BaseSetLastNTError(Status);
    }
    _Inline_SetLastError(ERROR_SUCCESS);
    return ERROR_SUCCESS;
}

EXTERN_C
BOOL
_NtCRT_DeletePath(
    _In_ PCWSTR Path,
    _In_ ULONG Options)
{
    NTSTATUS Status;
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_DISPOSITION_INFORMATION Disposition = { TRUE };

    Status = _NtCRT_OpenPath(&FileHandle,
                             Path,
                             DELETE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             Options | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
    if (NT_SUCCESS(Status))
    {
        Status = NtSetInformationFile(FileHandle,
                                      &IoStatusBlock,
                                      &Disposition,
                                      sizeof(Disposition),
                                      FileDispositionInformation);
        NtClose(FileHandle);
    }
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_API BOOL WINAPI _K32_DeleteFileW(_In_ LPCWSTR lpFileName)
{
    return _NtCRT_DeletePath(lpFileName, FILE_NON_DIRECTORY_FILE);
}

NTCRT_API BOOL WINAPI _K32_RemoveDirectoryW(_In_ LPCWSTR lpPathName)
{
    return _NtCRT_DeletePath(lpPathName, FILE_DIRECTORY_FILE);
}

NTCRT_IMPORT(DeleteFileW, _K32_DeleteFileW);
NTCRT_IMPORT(RemoveDirectoryW, _K32_RemoveDirectoryW);
