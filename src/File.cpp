/* File system APIs of kernel32, see FilePath.cpp for the shared helpers */

#include "NtCRT.h"

/* kernel32 */

NTCRT_API
HANDLE
WINAPI
_K32_CreateFileW(
    _In_ LPCWSTR lpFileName,
    _In_ DWORD dwDesiredAccess,
    _In_ DWORD dwShareMode,
    _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    _In_ DWORD dwCreationDisposition,
    _In_ DWORD dwFlagsAndAttributes,
    _In_opt_ HANDLE hTemplateFile)
{
    NTSTATUS Status;
    UNICODE_STRING NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    ULONG Disposition, Options = 0;

    if (_NtCRT_IsLegacyConsoleName(lpFileName, dwDesiredAccess))
    {
        auto const CreateFileW = (decltype(&_K32_CreateFileW))_NtCRT_GetKernel32Function("CreateFileW");

        if (CreateFileW != NULL)
        {
            return CreateFileW(lpFileName,
                               dwDesiredAccess,
                               dwShareMode,
                               lpSecurityAttributes,
                               dwCreationDisposition,
                               dwFlagsAndAttributes,
                               hTemplateFile);
        }
    }
    switch (dwCreationDisposition)
    {
        case CREATE_NEW: Disposition = FILE_CREATE; break;
        case CREATE_ALWAYS: Disposition = FILE_OVERWRITE_IF; break;
        case OPEN_EXISTING: Disposition = FILE_OPEN; break;
        case OPEN_ALWAYS: Disposition = FILE_OPEN_IF; break;
        case TRUNCATE_EXISTING: Disposition = FILE_OVERWRITE; break;
        default:
            _Inline_SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_HANDLE_VALUE;
    }
    if (!(dwFlagsAndAttributes & FILE_FLAG_OVERLAPPED))
    {
        Options |= FILE_SYNCHRONOUS_IO_NONALERT;
    }
    Options |=
        (dwFlagsAndAttributes & FILE_FLAG_BACKUP_SEMANTICS) ? FILE_OPEN_FOR_BACKUP_INTENT : FILE_NON_DIRECTORY_FILE;
    if (dwFlagsAndAttributes & FILE_FLAG_WRITE_THROUGH)
    {
        Options |= FILE_WRITE_THROUGH;
    }
    if (dwFlagsAndAttributes & FILE_FLAG_NO_BUFFERING)
    {
        Options |= FILE_NO_INTERMEDIATE_BUFFERING;
    }
    if (dwFlagsAndAttributes & FILE_FLAG_RANDOM_ACCESS)
    {
        Options |= FILE_RANDOM_ACCESS;
    }
    if (dwFlagsAndAttributes & FILE_FLAG_SEQUENTIAL_SCAN)
    {
        Options |= FILE_SEQUENTIAL_ONLY;
    }
    if (dwFlagsAndAttributes & FILE_FLAG_DELETE_ON_CLOSE)
    {
        Options |= FILE_DELETE_ON_CLOSE;
        dwDesiredAccess |= DELETE;
    }
    if (dwFlagsAndAttributes & FILE_FLAG_OPEN_REPARSE_POINT)
    {
        Options |= FILE_OPEN_REPARSE_POINT;
    }

    Status = RtlDosPathNameToNtPathName_U_WithStatus(lpFileName, &NtPath, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return INVALID_HANDLE_VALUE;
    }
    InitializeObjectAttributes(
        &ObjectAttributes,
        &NtPath,
        OBJ_CASE_INSENSITIVE | (lpSecurityAttributes != NULL && lpSecurityAttributes->bInheritHandle ? OBJ_INHERIT : 0),
        NULL,
        lpSecurityAttributes != NULL ? lpSecurityAttributes->lpSecurityDescriptor : NULL);
    Status = NtCreateFile(&FileHandle,
                          dwDesiredAccess | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          dwFlagsAndAttributes & (FILE_ATTRIBUTE_VALID_FLAGS & ~FILE_ATTRIBUTE_DIRECTORY),
                          dwShareMode,
                          Disposition,
                          Options,
                          NULL,
                          0);
    RtlFreeUnicodeString(&NtPath);
    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_OBJECT_NAME_COLLISION && Disposition == FILE_CREATE)
        {
            _Inline_SetLastError(ERROR_FILE_EXISTS);
        } else if (Status == STATUS_FILE_IS_A_DIRECTORY)
        {
            _Inline_SetLastError(ERROR_ACCESS_DENIED);
        } else
        {
            _Inline_BaseSetLastNTError(Status);
        }
        return INVALID_HANDLE_VALUE;
    }
    _Inline_SetLastError((dwCreationDisposition == CREATE_ALWAYS || dwCreationDisposition == OPEN_ALWAYS) &&
                         (IoStatusBlock.Information == FILE_OPENED || IoStatusBlock.Information == FILE_OVERWRITTEN) ?
                         ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return FileHandle;
}

NTCRT_API
HANDLE
WINAPI
_K32_CreateFileA(
    _In_ LPCSTR lpFileName,
    _In_ DWORD dwDesiredAccess,
    _In_ DWORD dwShareMode,
    _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    _In_ DWORD dwCreationDisposition,
    _In_ DWORD dwFlagsAndAttributes,
    _In_opt_ HANDLE hTemplateFile)
{
    HANDLE Handle;
    PWSTR Name = _NtCRT_AnsiToWide(lpFileName);

    if (Name == NULL)
    {
        return INVALID_HANDLE_VALUE;
    }
    Handle = _K32_CreateFileW(Name,
                              dwDesiredAccess,
                              dwShareMode,
                              lpSecurityAttributes,
                              dwCreationDisposition,
                              dwFlagsAndAttributes,
                              hTemplateFile);
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    return Handle;
}

NTCRT_API
BOOL
WINAPI
_K32_CreateDirectoryW(
    _In_ LPCWSTR lpPathName,
    _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    HANDLE Handle;

    UNREFERENCED_PARAMETER(lpSecurityAttributes);
    if (_NtCRT_CreateFile(&Handle,
                          lpPathName,
                          FILE_LIST_DIRECTORY | SYNCHRONIZE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_CREATE,
                          FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL) != ERROR_SUCCESS)
    {
        if (_Inline_GetLastError() == ERROR_FILE_EXISTS)
        {
            _Inline_SetLastError(ERROR_ALREADY_EXISTS);
        }
        return FALSE;
    }
    NtClose(Handle);
    return TRUE;
}

/* Set rename or link information, the target is a DOS path */
static
BOOL
_NtCRT_SetLinkOrRename(
    _In_ PCWSTR Source,
    _In_ PCWSTR Target,
    _In_ BOOLEAN ReplaceIfExists,
    _In_ FILE_INFORMATION_CLASS InfoClass)
{
    NTSTATUS Status;
    HANDLE FileHandle;
    UNICODE_STRING NtTarget;
    IO_STATUS_BLOCK IoStatusBlock;
    PFILE_RENAME_INFORMATION Info;
    ULONG InfoLength;

    Status = RtlDosPathNameToNtPathName_U_WithStatus(Target, &NtTarget, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        goto _Exit;
    }
    InfoLength = FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + NtTarget.Length;
    Info = (PFILE_RENAME_INFORMATION)RtlAllocateHeap(NTCRT_HEAP, 0, InfoLength);
    if (Info == NULL)
    {
        Status = STATUS_NO_MEMORY;
        RtlFreeUnicodeString(&NtTarget);
        goto _Exit;
    }
    Info->ReplaceIfExists = ReplaceIfExists;
    Info->RootDirectory = NULL;
    Info->FileNameLength = NtTarget.Length;
    memcpy(Info->FileName, NtTarget.Buffer, NtTarget.Length);
    RtlFreeUnicodeString(&NtTarget);

    Status = _NtCRT_OpenPath(&FileHandle,
                             Source,
                             (InfoClass == FileRenameInformation ? DELETE : FILE_WRITE_ATTRIBUTES),
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
    if (NT_SUCCESS(Status))
    {
        Status = NtSetInformationFile(FileHandle, &IoStatusBlock, Info, InfoLength, InfoClass);
        NtClose(FileHandle);
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Info);

_Exit:
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    if (Status == STATUS_NOT_SAME_DEVICE)
    {
        _Inline_RtlNtStatusToDosError(Status);
        _Inline_SetLastError(ERROR_NOT_SAME_DEVICE);
        return FALSE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_API
BOOL
WINAPI
_K32_MoveFileExW(
    _In_ LPCWSTR lpExistingFileName,
    _In_opt_ LPCWSTR lpNewFileName,
    _In_ DWORD dwFlags)
{
    if (lpNewFileName == NULL)
    {
        return _NtCRT_DeletePath(lpExistingFileName, 0);
    }
    /* Copying across volumes (MOVEFILE_COPY_ALLOWED) is not supported */
    return _NtCRT_SetLinkOrRename(lpExistingFileName,
                                  lpNewFileName,
                                  (dwFlags & MOVEFILE_REPLACE_EXISTING) != 0,
                                  FileRenameInformation);
}

NTCRT_API BOOL WINAPI _K32_MoveFileW(_In_ LPCWSTR lpExistingFileName, _In_ LPCWSTR lpNewFileName)
{
    return _K32_MoveFileExW(lpExistingFileName, lpNewFileName, 0);
}

NTCRT_API
BOOL
WINAPI
_K32_CreateHardLinkW(
    _In_ LPCWSTR lpFileName,
    _In_ LPCWSTR lpExistingFileName,
    _Reserved_ LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    UNREFERENCED_PARAMETER(lpSecurityAttributes);
    return _NtCRT_SetLinkOrRename(lpExistingFileName, lpFileName, FALSE, FileLinkInformation);
}

NTCRT_API
DWORD
WINAPI
_K32_GetFileAttributesW(
    _In_ LPCWSTR lpFileName)
{
    NTSTATUS Status;
    UNICODE_STRING NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_BASIC_INFORMATION Info;

    Status = RtlDosPathNameToNtPathName_U_WithStatus(lpFileName, &NtPath, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return INVALID_FILE_ATTRIBUTES;
    }
    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtQueryAttributesFile(&ObjectAttributes, &Info);
    RtlFreeUnicodeString(&NtPath);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return INVALID_FILE_ATTRIBUTES;
    }
    return Info.FileAttributes;
}

NTCRT_API
DWORD
WINAPI
_K32_GetFileAttributesA(
    _In_ LPCSTR lpFileName)
{
    DWORD Attributes;
    PWSTR Name = _NtCRT_AnsiToWide(lpFileName);

    if (Name == NULL)
    {
        return INVALID_FILE_ATTRIBUTES;
    }
    Attributes = _K32_GetFileAttributesW(Name);
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    return Attributes;
}

NTCRT_API
BOOL
WINAPI
_K32_GetFileAttributesExW(
    _In_ LPCWSTR lpFileName,
    _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
    _Out_ LPVOID lpFileInformation)
{
    NTSTATUS Status;
    UNICODE_STRING NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_NETWORK_OPEN_INFORMATION Info;
    LPWIN32_FILE_ATTRIBUTE_DATA Data = (LPWIN32_FILE_ATTRIBUTE_DATA)lpFileInformation;

    if (fInfoLevelId != GetFileExInfoStandard)
    {
        _Inline_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Status = RtlDosPathNameToNtPathName_U_WithStatus(lpFileName, &NtPath, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtQueryFullAttributesFile(&ObjectAttributes, &Info);
    RtlFreeUnicodeString(&NtPath);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    Data->dwFileAttributes = Info.FileAttributes;
    Data->ftCreationTime.dwLowDateTime = Info.CreationTime.LowPart;
    Data->ftCreationTime.dwHighDateTime = Info.CreationTime.HighPart;
    Data->ftLastAccessTime.dwLowDateTime = Info.LastAccessTime.LowPart;
    Data->ftLastAccessTime.dwHighDateTime = Info.LastAccessTime.HighPart;
    Data->ftLastWriteTime.dwLowDateTime = Info.LastWriteTime.LowPart;
    Data->ftLastWriteTime.dwHighDateTime = Info.LastWriteTime.HighPart;
    Data->nFileSizeHigh = Info.EndOfFile.HighPart;
    Data->nFileSizeLow = Info.EndOfFile.LowPart;
    return TRUE;
}

NTCRT_API
BOOL
WINAPI
_K32_SetFileAttributesW(
    _In_ LPCWSTR lpFileName,
    _In_ DWORD dwFileAttributes)
{
    NTSTATUS Status;
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_BASIC_INFORMATION Info;

    Status = _NtCRT_OpenPath(&FileHandle,
                             lpFileName,
                             FILE_WRITE_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Info, sizeof(Info));
        Info.FileAttributes = (dwFileAttributes & FILE_ATTRIBUTE_VALID_SET_FLAGS) | FILE_ATTRIBUTE_NORMAL;
        Status = NtSetInformationFile(FileHandle, &IoStatusBlock, &Info, sizeof(Info), FileBasicInformation);
        NtClose(FileHandle);
    }
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_API
BOOL
WINAPI
_K32_GetFileInformationByHandle(
    _In_ HANDLE hFile,
    _Out_ LPBY_HANDLE_FILE_INFORMATION lpFileInformation)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_BASIC_INFORMATION Basic;
    FILE_STANDARD_INFORMATION Standard;
    FILE_INTERNAL_INFORMATION Internal;
    union
    {
        FILE_FS_VOLUME_INFORMATION Info;
        UCHAR Buffer[sizeof(FILE_FS_VOLUME_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
    } Volume;

    NTCRT_LEGACY_CONSOLE_CALL(hFile, FALSE, GetFileInformationByHandle, hFile, lpFileInformation);
    Status = NtQueryVolumeInformationFile(hFile, &IoStatusBlock, &Volume, sizeof(Volume), FileFsVolumeInformation);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
    {
        goto _Fail;
    }
    Status = NtQueryInformationFile(hFile, &IoStatusBlock, &Basic, sizeof(Basic), FileBasicInformation);
    if (!NT_SUCCESS(Status))
    {
        goto _Fail;
    }
    Status = NtQueryInformationFile(hFile, &IoStatusBlock, &Standard, sizeof(Standard), FileStandardInformation);
    if (!NT_SUCCESS(Status))
    {
        goto _Fail;
    }
    Status = NtQueryInformationFile(hFile, &IoStatusBlock, &Internal, sizeof(Internal), FileInternalInformation);
    if (!NT_SUCCESS(Status))
    {
        goto _Fail;
    }
    lpFileInformation->dwFileAttributes = Basic.FileAttributes;
    lpFileInformation->ftCreationTime.dwLowDateTime = Basic.CreationTime.LowPart;
    lpFileInformation->ftCreationTime.dwHighDateTime = Basic.CreationTime.HighPart;
    lpFileInformation->ftLastAccessTime.dwLowDateTime = Basic.LastAccessTime.LowPart;
    lpFileInformation->ftLastAccessTime.dwHighDateTime = Basic.LastAccessTime.HighPart;
    lpFileInformation->ftLastWriteTime.dwLowDateTime = Basic.LastWriteTime.LowPart;
    lpFileInformation->ftLastWriteTime.dwHighDateTime = Basic.LastWriteTime.HighPart;
    lpFileInformation->dwVolumeSerialNumber = Volume.Info.VolumeSerialNumber;
    lpFileInformation->nFileSizeHigh = Standard.EndOfFile.HighPart;
    lpFileInformation->nFileSizeLow = Standard.EndOfFile.LowPart;
    lpFileInformation->nNumberOfLinks = Standard.NumberOfLinks;
    lpFileInformation->nFileIndexHigh = Internal.IndexNumber.HighPart;
    lpFileInformation->nFileIndexLow = Internal.IndexNumber.LowPart;
    return TRUE;

_Fail:
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_API
BOOL
WINAPI
_K32_SetEndOfFile(
    _In_ HANDLE hFile)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_POSITION_INFORMATION Position;
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    FILE_ALLOCATION_INFORMATION Allocation;

    NTCRT_LEGACY_CONSOLE_CALL(hFile, FALSE, SetEndOfFile, hFile);
    Status = NtQueryInformationFile(hFile, &IoStatusBlock, &Position, sizeof(Position), FilePositionInformation);
    if (NT_SUCCESS(Status))
    {
        EndOfFile.EndOfFile = Position.CurrentByteOffset;
        Status = NtSetInformationFile(hFile, &IoStatusBlock, &EndOfFile, sizeof(EndOfFile), FileEndOfFileInformation);
        if (NT_SUCCESS(Status))
        {
            Allocation.AllocationSize = Position.CurrentByteOffset;
            Status = NtSetInformationFile(hFile,
                                          &IoStatusBlock,
                                          &Allocation,
                                          sizeof(Allocation),
                                          FileAllocationInformation);
        }
    }
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_API
DWORD
WINAPI
_K32_SetFilePointer(
    _In_ HANDLE hFile,
    _In_ LONG lDistanceToMove,
    _Inout_opt_ PLONG lpDistanceToMoveHigh,
    _In_ DWORD dwMoveMethod)
{
    LARGE_INTEGER Distance, NewPointer;

    NTCRT_LEGACY_CONSOLE_CALL(_Inline_BasepMapStdHandle(hFile),
                              INVALID_SET_FILE_POINTER,
                              SetFilePointer,
                              hFile,
                              lDistanceToMove,
                              lpDistanceToMoveHigh,
                              dwMoveMethod);
    if (lpDistanceToMoveHigh != NULL)
    {
        Distance.LowPart = (ULONG)lDistanceToMove;
        Distance.HighPart = *lpDistanceToMoveHigh;
    } else
    {
        Distance.QuadPart = lDistanceToMove;
    }
    if (!_Inline_SetFilePointerEx(hFile, Distance, &NewPointer, dwMoveMethod))
    {
        return INVALID_SET_FILE_POINTER;
    }
    if (lpDistanceToMoveHigh != NULL)
    {
        *lpDistanceToMoveHigh = NewPointer.HighPart;
    } else if (NewPointer.HighPart != 0)
    {
        _Inline_SetLastError(ERROR_NEGATIVE_SEEK);
        return INVALID_SET_FILE_POINTER;
    }
    _Inline_SetLastError(ERROR_SUCCESS);
    return NewPointer.LowPart;
}

NTCRT_API
BOOL
WINAPI
_K32_SetFilePointerEx(
    _In_ HANDLE hFile,
    _In_ LARGE_INTEGER liDistanceToMove,
    _Out_opt_ PLARGE_INTEGER lpNewFilePointer,
    _In_ DWORD dwMoveMethod)
{
    NTCRT_LEGACY_CONSOLE_CALL(_Inline_BasepMapStdHandle(hFile),
                              FALSE,
                              SetFilePointerEx,
                              hFile,
                              liDistanceToMove,
                              lpNewFilePointer,
                              dwMoveMethod);
    return _Inline_SetFilePointerEx(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
}

NTCRT_API BOOL WINAPI _K32_GetFileSizeEx(_In_ HANDLE hFile, _Out_ PLARGE_INTEGER lpFileSize)
{
    NTCRT_LEGACY_CONSOLE_CALL(_Inline_BasepMapStdHandle(hFile), FALSE, GetFileSizeEx, hFile, lpFileSize);
    return _Inline_GetFileSizeEx(hFile, lpFileSize);
}

NTCRT_API
DWORD
WINAPI
_K32_GetFileSize(
    _In_ HANDLE hFile,
    _Out_opt_ LPDWORD lpFileSizeHigh)
{
    LARGE_INTEGER Size;

    NTCRT_LEGACY_CONSOLE_CALL(
        _Inline_BasepMapStdHandle(hFile), INVALID_FILE_SIZE, GetFileSize, hFile, lpFileSizeHigh);
    if (!_Inline_GetFileSizeEx(hFile, &Size))
    {
        return INVALID_FILE_SIZE;
    }
    if (lpFileSizeHigh != NULL)
    {
        *lpFileSizeHigh = Size.HighPart;
    }
    _Inline_SetLastError(ERROR_SUCCESS);
    return Size.LowPart;
}

NTCRT_API
BOOL
WINAPI
_K32_ReadFile(
    _In_ HANDLE hFile,
    _Out_ LPVOID lpBuffer,
    _In_ DWORD nNumberOfBytesToRead,
    _Out_opt_ LPDWORD lpNumberOfBytesRead,
    _Inout_opt_ LPOVERLAPPED lpOverlapped)
{
    NTCRT_LEGACY_CONSOLE_CALL(_Inline_BasepMapStdHandle(hFile),
                              FALSE,
                              ReadFile,
                              hFile,
                              lpBuffer,
                              nNumberOfBytesToRead,
                              lpNumberOfBytesRead,
                              lpOverlapped);
    return _Inline_ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

NTCRT_API
BOOL
WINAPI
_K32_WriteFile(
    _In_ HANDLE hFile,
    _In_ LPCVOID lpBuffer,
    _In_ DWORD nNumberOfBytesToWrite,
    _Out_opt_ LPDWORD lpNumberOfBytesWritten,
    _Inout_opt_ LPOVERLAPPED lpOverlapped)
{
    NTCRT_LEGACY_CONSOLE_CALL(_Inline_BasepMapStdHandle(hFile),
                              FALSE,
                              WriteFile,
                              hFile,
                              lpBuffer,
                              nNumberOfBytesToWrite,
                              lpNumberOfBytesWritten,
                              lpOverlapped);
    return _Inline_WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
}

NTCRT_API BOOL WINAPI _K32_FlushFileBuffers(_In_ HANDLE hFile)
{
    NTCRT_LEGACY_CONSOLE_CALL(_Inline_BasepMapStdHandle(hFile), FALSE, FlushFileBuffers, hFile);
    return _Inline_FlushFileBuffers(hFile);
}

NTCRT_API
DWORD
WINAPI
_K32_GetFullPathNameW(
    _In_ LPCWSTR lpFileName,
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength, return + 1) LPWSTR lpBuffer,
    _Outptr_opt_ LPWSTR* lpFilePart)
{
    ULONG Bytes;
    ULONG BufferBytes = nBufferLength > MAXUSHORT / sizeof(WCHAR) ? MAXUSHORT & ~1 : nBufferLength * sizeof(WCHAR);

    /* Returns the length without NUL, or the required size including NUL if the buffer is too small */
    Bytes = RtlGetFullPathName_U(lpFileName, BufferBytes, lpBuffer, lpFilePart);
    if (Bytes == 0)
    {
        _Inline_SetLastError(ERROR_INVALID_NAME);
        return 0;
    }
    return Bytes / sizeof(WCHAR);
}

NTCRT_API
DWORD
WINAPI
_K32_GetEnvironmentVariableW(
    _In_opt_ LPCWSTR lpName,
    _Out_writes_to_opt_(nSize, return + 1) LPWSTR lpBuffer,
    _In_ DWORD nSize)
{
    NTSTATUS Status;
    UNICODE_STRING Name, Value;

    Status = _Inline_RtlInitUnicodeStringEx(&Name, lpName);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return 0;
    }
    Value.Length = 0;
    Value.MaximumLength = (USHORT)(nSize > MAXUSHORT / sizeof(WCHAR) ? MAXUSHORT & ~1 : nSize * sizeof(WCHAR));
    Value.Buffer = lpBuffer;
    Status = RtlQueryEnvironmentVariable_U(NULL, &Name, &Value);
    if (NT_SUCCESS(Status))
    {
        if (Value.Length + sizeof(WCHAR) > Value.MaximumLength)
        {
            return Value.Length / sizeof(WCHAR) + 1;
        }
        lpBuffer[Value.Length / sizeof(WCHAR)] = UNICODE_NULL;
        _Inline_SetLastError(ERROR_SUCCESS);
        return Value.Length / sizeof(WCHAR);
    }
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        return Value.Length / sizeof(WCHAR) + 1;
    }
    _Inline_BaseSetLastNTError(Status);
    return 0;
}

NTCRT_API
BOOL
WINAPI
_K32_SetEnvironmentVariableW(
    _In_ LPCWSTR lpName,
    _In_opt_ LPCWSTR lpValue)
{
    return _Inline_SetEnvironmentVariableW(lpName, lpValue);
}

NTCRT_API
DWORD
WINAPI
_K32_GetTempPathW(
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength, return + 1) LPWSTR lpBuffer)
{
    static const WCHAR* const Variables[] = { L"TMP", L"TEMP", L"USERPROFILE" };
    WCHAR Path[MAX_PATH + 1];
    WCHAR Full[MAX_PATH + 2];
    DWORD Length = 0;

    for (UINT i = 0; i < ARRAYSIZE(Variables); i++)
    {
        Length = _K32_GetEnvironmentVariableW(Variables[i], Path, ARRAYSIZE(Path));
        if (Length != 0 && Length < ARRAYSIZE(Path))
        {
            break;
        }
        Length = 0;
    }
    if (Length == 0)
    {
        wcsncpy(Path, SharedUserData->NtSystemRoot, MAX_PATH);
        Path[MAX_PATH] = UNICODE_NULL;
    }
    Length = _K32_GetFullPathNameW(Path, MAX_PATH, Full, NULL);
    if (Length == 0 || Length >= MAX_PATH)
    {
        return 0;
    }
    if (Full[Length - 1] != L'\\')
    {
        Full[Length++] = L'\\';
        Full[Length] = UNICODE_NULL;
    }
    if (Length + 1 > nBufferLength)
    {
        return Length + 1;
    }
    memcpy(lpBuffer, Full, (Length + 1) * sizeof(WCHAR));
    return Length;
}

/* Volume information, lpRootPathName must be a directory */
static
NTSTATUS
_NtCRT_OpenVolumeRoot(
    _Out_ PHANDLE Handle,
    _In_opt_ LPCWSTR lpRootPathName)
{
    WCHAR Root[MAX_PATH];

    if (lpRootPathName == NULL)
    {
        ULONG Bytes = RtlGetCurrentDirectory_U(sizeof(Root), Root);
        if (Bytes < 3 * sizeof(WCHAR) || Bytes >= sizeof(Root))
        {
            return STATUS_OBJECT_PATH_INVALID;
        }
        Root[3] = UNICODE_NULL;
        lpRootPathName = Root;
    }
    return _NtCRT_OpenPath(Handle,
                           lpRootPathName,
                           FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT);
}

NTCRT_API
BOOL
WINAPI
_K32_GetDiskFreeSpaceExW(
    _In_opt_ LPCWSTR lpDirectoryName,
    _Out_opt_ PULARGE_INTEGER lpFreeBytesAvailableToCaller,
    _Out_opt_ PULARGE_INTEGER lpTotalNumberOfBytes,
    _Out_opt_ PULARGE_INTEGER lpTotalNumberOfFreeBytes)
{
    NTSTATUS Status;
    HANDLE Handle;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_FS_FULL_SIZE_INFORMATION Info;
    ULONGLONG UnitSize;

    Status = _NtCRT_OpenVolumeRoot(&Handle, lpDirectoryName);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryVolumeInformationFile(Handle, &IoStatusBlock, &Info, sizeof(Info), FileFsFullSizeInformation);
        NtClose(Handle);
    }
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return FALSE;
    }
    UnitSize = (ULONGLONG)Info.SectorsPerAllocationUnit * Info.BytesPerSector;
    if (lpFreeBytesAvailableToCaller != NULL)
    {
        lpFreeBytesAvailableToCaller->QuadPart = Info.CallerAvailableAllocationUnits.QuadPart * UnitSize;
    }
    if (lpTotalNumberOfBytes != NULL)
    {
        lpTotalNumberOfBytes->QuadPart = Info.TotalAllocationUnits.QuadPart * UnitSize;
    }
    if (lpTotalNumberOfFreeBytes != NULL)
    {
        lpTotalNumberOfFreeBytes->QuadPart = Info.ActualAvailableAllocationUnits.QuadPart * UnitSize;
    }
    return TRUE;
}

NTCRT_API
BOOL
WINAPI
_K32_GetVolumeInformationW(
    _In_opt_ LPCWSTR lpRootPathName,
    _Out_writes_opt_(nVolumeNameSize) LPWSTR lpVolumeNameBuffer,
    _In_ DWORD nVolumeNameSize,
    _Out_opt_ LPDWORD lpVolumeSerialNumber,
    _Out_opt_ LPDWORD lpMaximumComponentLength,
    _Out_opt_ LPDWORD lpFileSystemFlags,
    _Out_writes_opt_(nFileSystemNameSize) LPWSTR lpFileSystemNameBuffer,
    _In_ DWORD nFileSystemNameSize)
{
    NTSTATUS Status;
    HANDLE Handle;
    IO_STATUS_BLOCK IoStatusBlock;
    union
    {
        FILE_FS_VOLUME_INFORMATION Info;
        UCHAR Buffer[sizeof(FILE_FS_VOLUME_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
    } Volume;
    union
    {
        FILE_FS_ATTRIBUTE_INFORMATION Info;
        UCHAR Buffer[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
    } Attribute;
    ULONG Length;

    Status = _NtCRT_OpenVolumeRoot(&Handle, lpRootPathName);
    if (!NT_SUCCESS(Status))
    {
        goto _Fail;
    }
    Status = NtQueryVolumeInformationFile(Handle, &IoStatusBlock, &Volume, sizeof(Volume), FileFsVolumeInformation);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryVolumeInformationFile(Handle,
                                              &IoStatusBlock,
                                              &Attribute,
                                              sizeof(Attribute),
                                              FileFsAttributeInformation);
    }
    NtClose(Handle);
    if (!NT_SUCCESS(Status))
    {
        goto _Fail;
    }
    if (lpVolumeNameBuffer != NULL)
    {
        Length = Volume.Info.VolumeLabelLength / sizeof(WCHAR);
        if (Length >= nVolumeNameSize)
        {
            _Inline_SetLastError(ERROR_BAD_LENGTH);
            return FALSE;
        }
        memcpy(lpVolumeNameBuffer, Volume.Info.VolumeLabel, Length * sizeof(WCHAR));
        lpVolumeNameBuffer[Length] = UNICODE_NULL;
    }
    if (lpVolumeSerialNumber != NULL)
    {
        *lpVolumeSerialNumber = Volume.Info.VolumeSerialNumber;
    }
    if (lpMaximumComponentLength != NULL)
    {
        *lpMaximumComponentLength = Attribute.Info.MaximumComponentNameLength;
    }
    if (lpFileSystemFlags != NULL)
    {
        *lpFileSystemFlags = Attribute.Info.FileSystemAttributes;
    }
    if (lpFileSystemNameBuffer != NULL)
    {
        Length = Attribute.Info.FileSystemNameLength / sizeof(WCHAR);
        if (Length >= nFileSystemNameSize)
        {
            _Inline_SetLastError(ERROR_BAD_LENGTH);
            return FALSE;
        }
        memcpy(lpFileSystemNameBuffer, Attribute.Info.FileSystemName, Length * sizeof(WCHAR));
        lpFileSystemNameBuffer[Length] = UNICODE_NULL;
    }
    return TRUE;

_Fail:
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_API
DWORD
WINAPI
_K32_GetCurrentDirectoryW(
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength, return + 1) LPWSTR lpBuffer)
{
    ULONG Bytes = RtlGetCurrentDirectory_U(nBufferLength > MAXUSHORT / sizeof(WCHAR) ?
                                               MAXUSHORT & ~1 :
                                               nBufferLength * sizeof(WCHAR),
                                           lpBuffer);
    /* Same as RtlGetFullPathName_U */
    return Bytes / sizeof(WCHAR);
}

NTCRT_API
BOOL
WINAPI
_K32_SetCurrentDirectoryW(
    _In_ LPCWSTR lpPathName)
{
    UNICODE_STRING Path;
    NTSTATUS Status = _Inline_RtlInitUnicodeStringEx(&Path, lpPathName);

    if (NT_SUCCESS(Status))
    {
        Status = RtlSetCurrentDirectory_U(&Path);
    }
    if (NT_SUCCESS(Status))
    {
        return TRUE;
    }
    _Inline_BaseSetLastNTError(Status);
    return FALSE;
}

NTCRT_IMPORT(CreateFileW, _K32_CreateFileW);
NTCRT_IMPORT(CreateFileA, _K32_CreateFileA);
NTCRT_IMPORT(CreateDirectoryW, _K32_CreateDirectoryW);
NTCRT_IMPORT(MoveFileExW, _K32_MoveFileExW);
NTCRT_IMPORT(MoveFileW, _K32_MoveFileW);
NTCRT_IMPORT(CreateHardLinkW, _K32_CreateHardLinkW);
NTCRT_IMPORT(GetFileAttributesW, _K32_GetFileAttributesW);
NTCRT_IMPORT(GetFileAttributesA, _K32_GetFileAttributesA);
NTCRT_IMPORT(GetFileAttributesExW, _K32_GetFileAttributesExW);
NTCRT_IMPORT(SetFileAttributesW, _K32_SetFileAttributesW);
NTCRT_IMPORT(GetFileInformationByHandle, _K32_GetFileInformationByHandle);
NTCRT_IMPORT(SetEndOfFile, _K32_SetEndOfFile);
NTCRT_IMPORT(SetFilePointer, _K32_SetFilePointer);
NTCRT_IMPORT(SetFilePointerEx, _K32_SetFilePointerEx);
NTCRT_IMPORT(GetFileSizeEx, _K32_GetFileSizeEx);
NTCRT_IMPORT(GetFileSize, _K32_GetFileSize);
NTCRT_IMPORT(ReadFile, _K32_ReadFile);
NTCRT_IMPORT(WriteFile, _K32_WriteFile);
NTCRT_IMPORT(FlushFileBuffers, _K32_FlushFileBuffers);
NTCRT_IMPORT(GetFullPathNameW, _K32_GetFullPathNameW);
NTCRT_IMPORT(GetEnvironmentVariableW, _K32_GetEnvironmentVariableW);
NTCRT_IMPORT(SetEnvironmentVariableW, _K32_SetEnvironmentVariableW);
NTCRT_IMPORT(GetTempPathW, _K32_GetTempPathW);
NTCRT_IMPORT(GetDiskFreeSpaceExW, _K32_GetDiskFreeSpaceExW);
NTCRT_IMPORT(GetVolumeInformationW, _K32_GetVolumeInformationW);
NTCRT_IMPORT(GetCurrentDirectoryW, _K32_GetCurrentDirectoryW);
NTCRT_IMPORT(SetCurrentDirectoryW, _K32_SetCurrentDirectoryW);
