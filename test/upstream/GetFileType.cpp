#include <KNSoft/NDK/NDK.inl>

extern "C"
DWORD
WINAPI
_CRT_GetFileType(
    _In_ HANDLE hFile);

typedef DWORD (WINAPI *PFN_GET_FILE_TYPE)(
    _In_ HANDLE hFile);

static
PFN_GET_FILE_TYPE
GetSystemGetFileType(VOID)
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

    _Inline_RtlInitAnsiString(&ProcedureName, "GetFileType");
    Status = LdrGetProcedureAddress(Kernel32, &ProcedureName, 0, &ProcedureAddress);
    if (!NT_SUCCESS(Status))
    {
        _Inline_BaseSetLastNTError(Status);
        return NULL;
    }

    return (PFN_GET_FILE_TYPE)ProcedureAddress;
}

static
BOOL
CompareFileType(
    _In_ PFN_GET_FILE_TYPE SystemGetFileType,
    _In_ HANDLE FileHandle)
{
    DWORD ExpectedError;
    DWORD ActualError;
    DWORD ExpectedType;
    DWORD ActualType;

    _Inline_SetLastError(0);
    ExpectedType = SystemGetFileType(FileHandle);
    ExpectedError = _Inline_GetLastError();

    _Inline_SetLastError(0);
    ActualType = _CRT_GetFileType(FileHandle);
    ActualError = _Inline_GetLastError();

    return ActualType == ExpectedType && ActualError == ExpectedError;
}

static
BOOL
TestImageFileType(
    _In_ PFN_GET_FILE_TYPE SystemGetFileType)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING NtPath;
    HANDLE FileHandle;
    NTSTATUS Status;
    BOOL Result;

    Status = RtlDosPathNameToNtPathName_U_WithStatus(NtCurrentPeb()->ProcessParameters->ImagePathName.Buffer,
                                                     &NtPath,
                                                     NULL,
                                                     NULL);
    if (!NT_SUCCESS(Status))
    {
        return FALSE;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &NtPath,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    RtlFreeUnicodeString(&NtPath);
    if (!NT_SUCCESS(Status))
    {
        return FALSE;
    }

    Result = CompareFileType(SystemGetFileType, FileHandle);
    NtClose(FileHandle);
    return Result;
}

extern "C"
BOOL
TestGetFileType(VOID)
{
    PFN_GET_FILE_TYPE SystemGetFileType;
    RTL_USER_PROCESS_PARAMETERS* ProcessParameters;

    SystemGetFileType = GetSystemGetFileType();
    if (!SystemGetFileType)
    {
        return FALSE;
    }

    ProcessParameters = NtCurrentPeb()->ProcessParameters;

    return
        CompareFileType(SystemGetFileType, NULL) &&
        CompareFileType(SystemGetFileType, (HANDLE)(ULONG_PTR)STD_INPUT_HANDLE) &&
        CompareFileType(SystemGetFileType, (HANDLE)(ULONG_PTR)STD_OUTPUT_HANDLE) &&
        CompareFileType(SystemGetFileType, (HANDLE)(ULONG_PTR)STD_ERROR_HANDLE) &&
        CompareFileType(SystemGetFileType, ProcessParameters->StandardInput) &&
        CompareFileType(SystemGetFileType, ProcessParameters->StandardOutput) &&
        CompareFileType(SystemGetFileType, ProcessParameters->StandardError) &&
        TestImageFileType(SystemGetFileType);
}
