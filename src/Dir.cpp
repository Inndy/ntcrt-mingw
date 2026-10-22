/* msvcrt directory, file search and file time functions */

#include "NtCRT.h"
#include "FindMask.inl"

#include <stdio.h>
#include <direct.h>
#include <errno.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/utime.h>

#define NTCRT_EPOCH_DIFFERENCE 116444736000000000LL
#define NTCRT_TICKS_PER_SECOND 10000000LL

EXTERN_C BOOL WINAPI _K32_RemoveDirectoryW(LPCWSTR lpPathName);
EXTERN_C BOOL WINAPI _K32_CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
EXTERN_C BOOL WINAPI _K32_SetFileAttributesW(LPCWSTR lpFileName, DWORD dwFileAttributes);
EXTERN_C BOOL WINAPI _K32_SetCurrentDirectoryW(LPCWSTR lpPathName);
EXTERN_C HANDLE WINAPI _K32_CreateFileW(LPCWSTR lpFileName,
                                        DWORD dwDesiredAccess,
                                        DWORD dwShareMode,
                                        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                                        DWORD dwCreationDisposition,
                                        DWORD dwFlagsAndAttributes,
                                        HANDLE hTemplateFile);

/* Call a wide function with a converted narrow path */
#define NTCRT_WIDE_PATH_CALL(Path, Call) \
    do { \
        PWSTR WidePath; \
        int Ret; \
        if ((Path) == NULL) \
        { \
            NTCRT_ERRNO = EINVAL; \
            return -1; \
        } \
        WidePath = _NtCRT_FileToWide(Path); \
        if (WidePath == NULL) \
        { \
            return -1; \
        } \
        Ret = Call; \
        RtlFreeHeap(NTCRT_HEAP, 0, WidePath); \
        return Ret; \
    } while (0)

static
int
_NtCRT_Win32Result(
    _In_ BOOL Success)
{
    if (!Success)
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return -1;
    }
    return 0;
}

/* Directories */

EXTERN_C int __cdecl _NtCRT__wmkdir(const wchar_t* path)
{
    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return _NtCRT_Win32Result(_K32_CreateDirectoryW(path, NULL));
}

EXTERN_C int __cdecl _NtCRT__mkdir(const char* path)
{
    NTCRT_WIDE_PATH_CALL(path, _NtCRT__wmkdir(WidePath));
}

EXTERN_C int __cdecl _NtCRT__wrmdir(const wchar_t* path)
{
    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return _NtCRT_Win32Result(_K32_RemoveDirectoryW(path));
}

EXTERN_C int __cdecl _NtCRT__rmdir(const char* path)
{
    NTCRT_WIDE_PATH_CALL(path, _NtCRT__wrmdir(WidePath));
}

EXTERN_C int __cdecl _NtCRT__wchdir(const wchar_t* path)
{
    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return _NtCRT_Win32Result(_K32_SetCurrentDirectoryW(path));
}

EXTERN_C int __cdecl _NtCRT__chdir(const char* path)
{
    NTCRT_WIDE_PATH_CALL(path, _NtCRT__wchdir(WidePath));
}

EXTERN_C wchar_t* __cdecl _NtCRT__wgetcwd(wchar_t* buffer, int maxlen)
{
    ULONG Bytes;
    wchar_t* Allocated = NULL;

    if (buffer != NULL && maxlen <= 0)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    Bytes = RtlGetCurrentDirectory_U(0, NULL);
    if (Bytes == 0)
    {
        NTCRT_ERRNO = ENOENT;
        return NULL;
    }
    if (buffer == NULL)
    {
        size_t Count = Bytes / sizeof(wchar_t);
        if (maxlen > 0 && (size_t)maxlen > Count)
        {
            Count = maxlen;
        }
        buffer = Allocated = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, Count * sizeof(wchar_t));
        if (buffer == NULL)
        {
            NTCRT_ERRNO = ENOMEM;
            return NULL;
        }
        maxlen = (int)Count;
    }
    Bytes = RtlGetCurrentDirectory_U((ULONG)maxlen * sizeof(wchar_t), buffer);
    if (Bytes == 0 || Bytes > (ULONG)maxlen * sizeof(wchar_t))
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Allocated);
        NTCRT_ERRNO = ERANGE;
        return NULL;
    }
    return buffer;
}

EXTERN_C char* __cdecl _NtCRT__getcwd(char* buffer, int maxlen)
{
    wchar_t* Wide;
    char* Narrow;
    size_t Length;

    if (buffer != NULL && maxlen <= 0)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    Wide = _NtCRT__wgetcwd(NULL, 0);
    if (Wide == NULL)
    {
        return NULL;
    }
    Narrow = _NtCRT_WideToFile(Wide);
    RtlFreeHeap(NTCRT_HEAP, 0, Wide);
    if (Narrow == NULL)
    {
        return NULL;
    }
    Length = strlen(Narrow) + 1;
    if (buffer == NULL)
    {
        /* The buffer allocated has maxlen bytes at least */
        if (maxlen > 0 && (size_t)maxlen > Length)
        {
            buffer = (char*)RtlReAllocateHeap(NTCRT_HEAP, 0, Narrow, (size_t)maxlen);
            if (buffer == NULL)
            {
                RtlFreeHeap(NTCRT_HEAP, 0, Narrow);
                NTCRT_ERRNO = ENOMEM;
            }
            return buffer;
        }
        return Narrow;
    }
    if (Length > (size_t)maxlen)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Narrow);
        NTCRT_ERRNO = ERANGE;
        return NULL;
    }
    memcpy(buffer, Narrow, Length);
    RtlFreeHeap(NTCRT_HEAP, 0, Narrow);
    return buffer;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wfullpath(wchar_t* absPath, const wchar_t* relPath, size_t maxLength)
{
    ULONG Bytes;
    wchar_t* Allocated = NULL;

    if (relPath == NULL || *relPath == L'\0')
    {
        return _NtCRT__wgetcwd(absPath, (int)maxLength);
    }
    if (absPath == NULL)
    {
        Bytes = RtlGetFullPathName_U(relPath, 0, NULL, NULL);
        if (Bytes == 0)
        {
            NTCRT_ERRNO = EINVAL;
            return NULL;
        }
        maxLength = Bytes / sizeof(wchar_t);
        absPath = Allocated = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, Bytes);
        if (absPath == NULL)
        {
            NTCRT_ERRNO = ENOMEM;
            return NULL;
        }
    }
    if (maxLength > ULONG_MAX / sizeof(wchar_t))
    {
        maxLength = ULONG_MAX / sizeof(wchar_t);
    }
    Bytes = RtlGetFullPathName_U(relPath, (ULONG)(maxLength * sizeof(wchar_t)), absPath, NULL);
    if (Bytes == 0 || Bytes >= maxLength * sizeof(wchar_t))
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Allocated);
        NTCRT_ERRNO = Bytes == 0 ? EINVAL : ERANGE;
        return NULL;
    }
    return absPath;
}

EXTERN_C char* __cdecl _NtCRT__fullpath(char* absPath, const char* relPath, size_t maxLength)
{
    PWSTR WideRel = NULL, WideAbs;
    char* Narrow;
    size_t Length;

    if (relPath != NULL && *relPath != '\0')
    {
        WideRel = _NtCRT_FileToWide(relPath);
        if (WideRel == NULL)
        {
            return NULL;
        }
    }
    WideAbs = _NtCRT__wfullpath(NULL, WideRel, 0);
    RtlFreeHeap(NTCRT_HEAP, 0, WideRel);
    if (WideAbs == NULL)
    {
        return NULL;
    }
    Narrow = _NtCRT_WideToFile(WideAbs);
    RtlFreeHeap(NTCRT_HEAP, 0, WideAbs);
    if (Narrow == NULL || absPath == NULL)
    {
        return Narrow;
    }
    Length = strlen(Narrow) + 1;
    if (Length > maxLength)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Narrow);
        NTCRT_ERRNO = ERANGE;
        return NULL;
    }
    memcpy(absPath, Narrow, Length);
    RtlFreeHeap(NTCRT_HEAP, 0, Narrow);
    return absPath;
}

/* Access and permission */

EXTERN_C int __cdecl _NtCRT__waccess(const wchar_t* path, int mode)
{
    DWORD Attributes;

    if (path == NULL || (mode & ~6) != 0)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Attributes = _K32_GetFileAttributesW(path);
    if (Attributes == INVALID_FILE_ATTRIBUTES)
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return -1;
    }
    if ((mode & 2) && !(Attributes & FILE_ATTRIBUTE_DIRECTORY) && (Attributes & FILE_ATTRIBUTE_READONLY))
    {
        NTCRT_ERRNO = EACCES;
        return -1;
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT__access(const char* path, int mode)
{
    NTCRT_WIDE_PATH_CALL(path, _NtCRT__waccess(WidePath, mode));
}

EXTERN_C int __cdecl _NtCRT__waccess_s(const wchar_t* path, int mode)
{
    return _NtCRT__waccess(path, mode) == 0 ? 0 : NTCRT_ERRNO;
}

EXTERN_C int __cdecl _NtCRT__access_s(const char* path, int mode)
{
    return _NtCRT__access(path, mode) == 0 ? 0 : NTCRT_ERRNO;
}

EXTERN_C int __cdecl _NtCRT__wchmod(const wchar_t* path, int mode)
{
    DWORD Attributes;

    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Attributes = _K32_GetFileAttributesW(path);
    if (Attributes == INVALID_FILE_ATTRIBUTES)
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return -1;
    }
    if (mode & _S_IWRITE)
    {
        Attributes &= ~FILE_ATTRIBUTE_READONLY;
    } else
    {
        Attributes |= FILE_ATTRIBUTE_READONLY;
    }
    return _NtCRT_Win32Result(_K32_SetFileAttributesW(path, Attributes));
}

EXTERN_C int __cdecl _NtCRT__chmod(const char* path, int mode)
{
    NTCRT_WIDE_PATH_CALL(path, _NtCRT__wchmod(WidePath, mode));
}

EXTERN_C int __cdecl _NtCRT__umask(int mode)
{
    int Old = _NtCRT_Umask;
    _NtCRT_Umask = mode & (_S_IREAD | _S_IWRITE);
    return Old;
}

/* The old mask is returned even if the new one is invalid */
EXTERN_C errno_t __cdecl _NtCRT__umask_s(int mode, int* oldmode)
{
    if (oldmode == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *oldmode = _NtCRT_Umask;
    if (mode & ~(_S_IREAD | _S_IWRITE))
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    _NtCRT_Umask = mode;
    return 0;
}

/* File times */

static
NTSTATUS
_NtCRT_SetFileTimes(
    _In_ HANDLE Handle,
    _In_opt_ const struct __utimbuf64* Times)
{
    FILE_BASIC_INFORMATION Basic;
    IO_STATUS_BLOCK IoStatusBlock;
    long long Access, Modify;

    if (Times == NULL)
    {
        FILETIME Now;
        _Inline_GetSystemTimeAsFileTime(&Now);
        Access = Modify = ((long long)Now.dwHighDateTime << 32) | Now.dwLowDateTime;
    } else
    {
        if (Times->actime < 0 || Times->modtime < 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Access = Times->actime * NTCRT_TICKS_PER_SECOND + NTCRT_EPOCH_DIFFERENCE;
        Modify = Times->modtime * NTCRT_TICKS_PER_SECOND + NTCRT_EPOCH_DIFFERENCE;
    }
    memset(&Basic, 0, sizeof(Basic));
    Basic.LastAccessTime.QuadPart = Access;
    Basic.LastWriteTime.QuadPart = Modify;
    return NtSetInformationFile(Handle, &IoStatusBlock, &Basic, sizeof(Basic), FileBasicInformation);
}

EXTERN_C int __cdecl _NtCRT__futime64(int fd, struct __utimbuf64* times)
{
    intptr_t Handle = _NtCRT_get_osfhandle(fd);
    NTSTATUS Status;

    if (Handle == -1)
    {
        return -1;
    }
    Status = _NtCRT_SetFileTimes((HANDLE)Handle, times);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT__wutime64(const wchar_t* path, struct __utimbuf64* times)
{
    HANDLE Handle;
    NTSTATUS Status;

    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Handle = _K32_CreateFileW(path,
                              FILE_WRITE_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL,
                              OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS,
                              NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return -1;
    }
    Status = _NtCRT_SetFileTimes(Handle, times);
    NtClose(Handle);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT__utime64(const char* path, struct __utimbuf64* times)
{
    NTCRT_WIDE_PATH_CALL(path, _NtCRT__wutime64(WidePath, times));
}

static
struct __utimbuf64*
_NtCRT_Utimbuf32To64(
    _In_opt_ const struct __utimbuf32* Times,
    _Out_ struct __utimbuf64* Buffer)
{
    if (Times == NULL)
    {
        return NULL;
    }
    Buffer->actime = Times->actime;
    Buffer->modtime = Times->modtime;
    return Buffer;
}

EXTERN_C int __cdecl _NtCRT__futime32(int fd, struct __utimbuf32* times)
{
    struct __utimbuf64 Buffer;
    return _NtCRT__futime64(fd, _NtCRT_Utimbuf32To64(times, &Buffer));
}

EXTERN_C int __cdecl _NtCRT__wutime32(const wchar_t* path, struct __utimbuf32* times)
{
    struct __utimbuf64 Buffer;
    return _NtCRT__wutime64(path, _NtCRT_Utimbuf32To64(times, &Buffer));
}

EXTERN_C int __cdecl _NtCRT__utime32(const char* path, struct __utimbuf32* times)
{
    struct __utimbuf64 Buffer;
    return _NtCRT__utime64(path, _NtCRT_Utimbuf32To64(times, &Buffer));
}

/* File search */

typedef struct _NTCRT_FIND
{
    HANDLE Directory;
    UNICODE_STRING Mask;
    ULONG Offset;
    BOOLEAN HasData;
    ULONG_PTR Buffer[0x2000 / sizeof(ULONG_PTR)];
} NTCRT_FIND, *PNTCRT_FIND;

typedef struct _NTCRT_FIND_DATA
{
    unsigned Attrib;
    long long CreateTime;
    long long AccessTime;
    long long WriteTime;
    long long Size;
    PCWSTR Name;
    ULONG NameLength;
} NTCRT_FIND_DATA, *PNTCRT_FIND_DATA;

static
long long
_NtCRT_FindTime(
    _In_ LONGLONG Time)
{
    return Time < NTCRT_EPOCH_DIFFERENCE ? -1 : (Time - NTCRT_EPOCH_DIFFERENCE) / NTCRT_TICKS_PER_SECOND;
}

static
void
_NtCRT_FindClose(
    _In_ PNTCRT_FIND Find)
{
    NtClose(Find->Directory);
    RtlFreeHeap(NTCRT_HEAP, 0, Find->Mask.Buffer);
    RtlFreeHeap(NTCRT_HEAP, 0, Find);
}

/* Get the next entry, returns FALSE with errno set at the end */
static
BOOL
_NtCRT_FindNextEntry(
    _In_ PNTCRT_FIND Find,
    _Out_ PNTCRT_FIND_DATA Data,
    _In_ BOOL First)
{
    PFILE_DIRECTORY_INFORMATION Info;

    if (!Find->HasData)
    {
        IO_STATUS_BLOCK IoStatusBlock;
        NTSTATUS Status = NtQueryDirectoryFile(Find->Directory,
                                               NULL,
                                               NULL,
                                               NULL,
                                               &IoStatusBlock,
                                               Find->Buffer,
                                               sizeof(Find->Buffer),
                                               FileDirectoryInformation,
                                               FALSE,
                                               First ? &Find->Mask : NULL,
                                               (BOOLEAN)First);
        if (!NT_SUCCESS(Status))
        {
            /* The last error as FindNextFileW, readdir of libmingwex expects ERROR_NO_MORE_FILES at the end */
            RtlSetLastWin32Error(RtlNtStatusToDosError(Status));
            NTCRT_ERRNO = ENOENT;
            return FALSE;
        }
        Find->HasData = TRUE;
        Find->Offset = 0;
    }
    Info = (PFILE_DIRECTORY_INFORMATION)((PUCHAR)Find->Buffer + Find->Offset);
    if (Info->NextEntryOffset == 0)
    {
        Find->HasData = FALSE;
    } else
    {
        Find->Offset += Info->NextEntryOffset;
    }
    Data->Attrib = Info->FileAttributes == FILE_ATTRIBUTE_NORMAL ? 0 : Info->FileAttributes;
    Data->CreateTime = _NtCRT_FindTime(Info->CreationTime.QuadPart);
    Data->AccessTime = _NtCRT_FindTime(Info->LastAccessTime.QuadPart);
    Data->WriteTime = _NtCRT_FindTime(Info->LastWriteTime.QuadPart);
    Data->Size = Info->EndOfFile.QuadPart;
    Data->Name = Info->FileName;
    Data->NameLength = Info->FileNameLength / sizeof(WCHAR);
    return TRUE;
}

static
PNTCRT_FIND
_NtCRT_FindFirst(
    _In_ PCWSTR Pattern,
    _Out_ PNTCRT_FIND_DATA Data)
{
    PCWSTR FileName;
    PWSTR DirectoryPath;
    size_t DirectoryLength, MaskLength;
    PNTCRT_FIND Find;
    UNICODE_STRING NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    if (Pattern == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    FileName = _NtCRT_FindFileName(Pattern);
    if (*FileName == L'\0')
    {
        NTCRT_ERRNO = ENOENT;
        return NULL;
    }

    Find = (PNTCRT_FIND)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, sizeof(NTCRT_FIND));
    if (Find == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }

    /* Convert the mask to DOS wildcards as FindFirstFileW does */
    MaskLength = wcslen(FileName);
    Find->Mask.Buffer = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, (MaskLength + 1) * sizeof(WCHAR));
    if (Find->Mask.Buffer == NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Find);
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    MaskLength = _NtCRT_FindMask(FileName, Find->Mask.Buffer);
    Find->Mask.Length = Find->Mask.MaximumLength = (USHORT)(MaskLength * sizeof(WCHAR));

    /* Open the directory */
    DirectoryLength = FileName - Pattern;
    DirectoryPath = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, (DirectoryLength + 2) * sizeof(WCHAR));
    if (DirectoryPath == NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Find->Mask.Buffer);
        RtlFreeHeap(NTCRT_HEAP, 0, Find);
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    if (DirectoryLength == 0)
    {
        DirectoryPath[0] = L'.';
        DirectoryPath[1] = L'\0';
    } else
    {
        memcpy(DirectoryPath, Pattern, DirectoryLength * sizeof(WCHAR));
        DirectoryPath[DirectoryLength] = L'\0';
    }
    Status = RtlDosPathNameToNtPathName_U_WithStatus(DirectoryPath, &NtPath, NULL, NULL);
    RtlFreeHeap(NTCRT_HEAP, 0, DirectoryPath);
    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
        Status = NtOpenFile(&Find->Directory,
                            FILE_LIST_DIRECTORY | SYNCHRONIZE,
                            &ObjectAttributes,
                            &IoStatusBlock,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
        RtlFreeUnicodeString(&NtPath);
    }
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        RtlFreeHeap(NTCRT_HEAP, 0, Find->Mask.Buffer);
        RtlFreeHeap(NTCRT_HEAP, 0, Find);
        return NULL;
    }
    if (!_NtCRT_FindNextEntry(Find, Data, TRUE))
    {
        _NtCRT_FindClose(Find);
        return NULL;
    }
    return Find;
}

template<typename TFindData>
static
void
_NtCRT_FillFindDataW(
    _Out_ TFindData* FindData,
    _In_ const NTCRT_FIND_DATA* Data)
{
    ULONG Length = Data->NameLength < MAX_PATH - 1 ? Data->NameLength : MAX_PATH - 1;

    FindData->attrib = Data->Attrib;
    FindData->time_create = Data->CreateTime;
    FindData->time_access = Data->AccessTime;
    FindData->time_write = Data->WriteTime;
    FindData->size = (decltype(FindData->size))Data->Size;
    memcpy(FindData->name, Data->Name, Length * sizeof(WCHAR));
    FindData->name[Length] = L'\0';
}

template<typename TFindData>
static
void
_NtCRT_FillFindDataA(
    _Out_ TFindData* FindData,
    _In_ const NTCRT_FIND_DATA* Data)
{
    int Length;

    FindData->attrib = Data->Attrib;
    FindData->time_create = Data->CreateTime;
    FindData->time_access = Data->AccessTime;
    FindData->time_write = Data->WriteTime;
    FindData->size = (decltype(FindData->size))Data->Size;
    Length = _NtCRT_WideCharToMultiByte(_NtCRT_FileCodePage(),
                                        0,
                                        Data->Name,
                                        (int)Data->NameLength,
                                        FindData->name,
                                        MAX_PATH - 1,
                                        NULL,
                                        NULL);
    FindData->name[Length > 0 ? Length : 0] = '\0';
}

template<typename TFindData>
static
intptr_t
_NtCRT_FindFirstW(
    _In_ const wchar_t* Pattern,
    _Out_ TFindData* FindData)
{
    NTCRT_FIND_DATA Data;
    PNTCRT_FIND Find;

    if (FindData == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Find = _NtCRT_FindFirst(Pattern, &Data);
    if (Find == NULL)
    {
        return -1;
    }
    _NtCRT_FillFindDataW(FindData, &Data);
    return (intptr_t)Find;
}

template<typename TFindData>
static
intptr_t
_NtCRT_FindFirstA(
    _In_ const char* Pattern,
    _Out_ TFindData* FindData)
{
    NTCRT_FIND_DATA Data;
    PNTCRT_FIND Find;
    PWSTR WidePattern;

    if (Pattern == NULL || FindData == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    WidePattern = _NtCRT_FileToWide(Pattern);
    if (WidePattern == NULL)
    {
        return -1;
    }
    Find = _NtCRT_FindFirst(WidePattern, &Data);
    RtlFreeHeap(NTCRT_HEAP, 0, WidePattern);
    if (Find == NULL)
    {
        return -1;
    }
    _NtCRT_FillFindDataA(FindData, &Data);
    return (intptr_t)Find;
}

template<typename TFindData>
static
int
_NtCRT_FindNextW(
    _In_ intptr_t Handle,
    _Out_ TFindData* FindData)
{
    NTCRT_FIND_DATA Data;

    if (Handle == -1 || Handle == 0 || FindData == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (!_NtCRT_FindNextEntry((PNTCRT_FIND)Handle, &Data, FALSE))
    {
        return -1;
    }
    _NtCRT_FillFindDataW(FindData, &Data);
    return 0;
}

template<typename TFindData>
static
int
_NtCRT_FindNextA(
    _In_ intptr_t Handle,
    _Out_ TFindData* FindData)
{
    NTCRT_FIND_DATA Data;

    if (Handle == -1 || Handle == 0 || FindData == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (!_NtCRT_FindNextEntry((PNTCRT_FIND)Handle, &Data, FALSE))
    {
        return -1;
    }
    _NtCRT_FillFindDataA(FindData, &Data);
    return 0;
}

EXTERN_C int __cdecl _NtCRT__findclose(intptr_t handle)
{
    if (handle == -1 || handle == 0)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_FindClose((PNTCRT_FIND)handle);
    return 0;
}

EXTERN_C intptr_t __cdecl _NtCRT__findfirst64(const char* pattern, struct __finddata64_t* data)
{
    return _NtCRT_FindFirstA(pattern, data);
}

EXTERN_C int __cdecl _NtCRT__findnext64(intptr_t handle, struct __finddata64_t* data)
{
    return _NtCRT_FindNextA(handle, data);
}

EXTERN_C intptr_t __cdecl _NtCRT__findfirst64i32(const char* pattern, struct _finddata64i32_t* data)
{
    return _NtCRT_FindFirstA(pattern, data);
}

EXTERN_C int __cdecl _NtCRT__findnext64i32(intptr_t handle, struct _finddata64i32_t* data)
{
    return _NtCRT_FindNextA(handle, data);
}

EXTERN_C intptr_t __cdecl _NtCRT__findfirst32(const char* pattern, struct _finddata32_t* data)
{
    return _NtCRT_FindFirstA(pattern, data);
}

EXTERN_C int __cdecl _NtCRT__findnext32(intptr_t handle, struct _finddata32_t* data)
{
    return _NtCRT_FindNextA(handle, data);
}

EXTERN_C intptr_t __cdecl _NtCRT__wfindfirst64(const wchar_t* pattern, struct _wfinddata64_t* data)
{
    return _NtCRT_FindFirstW(pattern, data);
}

EXTERN_C int __cdecl _NtCRT__wfindnext64(intptr_t handle, struct _wfinddata64_t* data)
{
    return _NtCRT_FindNextW(handle, data);
}

EXTERN_C intptr_t __cdecl _NtCRT__wfindfirst64i32(const wchar_t* pattern, struct _wfinddata64i32_t* data)
{
    return _NtCRT_FindFirstW(pattern, data);
}

EXTERN_C int __cdecl _NtCRT__wfindnext64i32(intptr_t handle, struct _wfinddata64i32_t* data)
{
    return _NtCRT_FindNextW(handle, data);
}

EXTERN_C intptr_t __cdecl _NtCRT__wfindfirst32(const wchar_t* pattern, struct _wfinddata32_t* data)
{
    return _NtCRT_FindFirstW(pattern, data);
}

EXTERN_C int __cdecl _NtCRT__wfindnext32(intptr_t handle, struct _wfinddata32_t* data)
{
    return _NtCRT_FindNextW(handle, data);
}

/* The _findfirsti64 of msvcrt on x86, with a 32-bit time_t and a 64-bit size, x86 only */
EXTERN_C intptr_t __cdecl _NtCRT__findfirst32i64(const char* pattern, struct _finddata32i64_t* data);
EXTERN_C int __cdecl _NtCRT__findnext32i64(intptr_t handle, struct _finddata32i64_t* data);
EXTERN_C intptr_t __cdecl _NtCRT__wfindfirst32i64(const wchar_t* pattern, struct _wfinddata32i64_t* data);
EXTERN_C int __cdecl _NtCRT__wfindnext32i64(intptr_t handle, struct _wfinddata32i64_t* data);

#ifndef _WIN64
EXTERN_C intptr_t __cdecl _NtCRT__findfirst32i64(const char* pattern, struct _finddata32i64_t* data)
{
    return _NtCRT_FindFirstA(pattern, data);
}

EXTERN_C int __cdecl _NtCRT__findnext32i64(intptr_t handle, struct _finddata32i64_t* data)
{
    return _NtCRT_FindNextA(handle, data);
}

EXTERN_C intptr_t __cdecl _NtCRT__wfindfirst32i64(const wchar_t* pattern, struct _wfinddata32i64_t* data)
{
    return _NtCRT_FindFirstW(pattern, data);
}

EXTERN_C int __cdecl _NtCRT__wfindnext32i64(intptr_t handle, struct _wfinddata32i64_t* data)
{
    return _NtCRT_FindNextW(handle, data);
}
#endif

/* Process */

EXTERN_C int __cdecl _NtCRT__getpid(void)
{
    return (int)(ULONG_PTR)NtCurrentProcessId();
}

/*
 * The names of msvcrt without a size: a 32-bit time_t on x86, 64 bits on x64, with a 32-bit size for _finddata_t and
 * a 64-bit one for _finddatai64_t
 */
NTCRT_ARCH_ALIAS(_NtCRT__futime, _NtCRT__futime32, _NtCRT__futime64);
NTCRT_ARCH_ALIAS(_NtCRT__wutime, _NtCRT__wutime32, _NtCRT__wutime64);
NTCRT_ARCH_ALIAS(_NtCRT__utime, _NtCRT__utime32, _NtCRT__utime64);
NTCRT_ARCH_ALIAS(_NtCRT__findfirst, _NtCRT__findfirst32, _NtCRT__findfirst64i32);
NTCRT_ARCH_ALIAS(_NtCRT__findnext, _NtCRT__findnext32, _NtCRT__findnext64i32);
NTCRT_ARCH_ALIAS(_NtCRT__wfindfirst, _NtCRT__wfindfirst32, _NtCRT__wfindfirst64i32);
NTCRT_ARCH_ALIAS(_NtCRT__wfindnext, _NtCRT__wfindnext32, _NtCRT__wfindnext64i32);
#ifdef _WIN64
NTCRT_ARCH_ALIAS(_NtCRT__findfirsti64, _NtCRT__findfirst64, _NtCRT__findfirst64);
NTCRT_ARCH_ALIAS(_NtCRT__findnexti64, _NtCRT__findnext64, _NtCRT__findnext64);
NTCRT_ARCH_ALIAS(_NtCRT__wfindfirsti64, _NtCRT__wfindfirst64, _NtCRT__wfindfirst64);
NTCRT_ARCH_ALIAS(_NtCRT__wfindnexti64, _NtCRT__wfindnext64, _NtCRT__wfindnext64);
#else
NTCRT_ARCH_ALIAS(_NtCRT__findfirsti64, _NtCRT__findfirst32i64, _NtCRT__findfirst32i64);
NTCRT_ARCH_ALIAS(_NtCRT__findnexti64, _NtCRT__findnext32i64, _NtCRT__findnext32i64);
NTCRT_ARCH_ALIAS(_NtCRT__wfindfirsti64, _NtCRT__wfindfirst32i64, _NtCRT__wfindfirst32i64);
NTCRT_ARCH_ALIAS(_NtCRT__wfindnexti64, _NtCRT__wfindnext32i64, _NtCRT__wfindnext32i64);
#endif

NTCRT_IMPORT(_mkdir, _NtCRT__mkdir);
NTCRT_IMPORT(mkdir, _NtCRT__mkdir);
NTCRT_IMPORT(_wmkdir, _NtCRT__wmkdir);
NTCRT_IMPORT(_rmdir, _NtCRT__rmdir);
NTCRT_IMPORT(rmdir, _NtCRT__rmdir);
NTCRT_IMPORT(_wrmdir, _NtCRT__wrmdir);
NTCRT_IMPORT(_chdir, _NtCRT__chdir);
NTCRT_IMPORT(chdir, _NtCRT__chdir);
NTCRT_IMPORT(_wchdir, _NtCRT__wchdir);
NTCRT_IMPORT(_getcwd, _NtCRT__getcwd);
NTCRT_IMPORT(getcwd, _NtCRT__getcwd);
NTCRT_IMPORT(_wgetcwd, _NtCRT__wgetcwd);
NTCRT_IMPORT(_fullpath, _NtCRT__fullpath);
NTCRT_IMPORT(_wfullpath, _NtCRT__wfullpath);
NTCRT_IMPORT(_access, _NtCRT__access);
NTCRT_IMPORT(access, _NtCRT__access);
NTCRT_IMPORT(_waccess, _NtCRT__waccess);
NTCRT_IMPORT(_access_s, _NtCRT__access_s);
NTCRT_IMPORT(_waccess_s, _NtCRT__waccess_s);
NTCRT_IMPORT(_chmod, _NtCRT__chmod);
NTCRT_IMPORT(chmod, _NtCRT__chmod);
NTCRT_IMPORT(_wchmod, _NtCRT__wchmod);
NTCRT_IMPORT(_umask, _NtCRT__umask);
NTCRT_IMPORT(_umask_s, _NtCRT__umask_s);
NTCRT_IMPORT(umask, _NtCRT__umask);
NTCRT_IMPORT(_futime64, _NtCRT__futime64);
NTCRT_IMPORT(_futime, _NtCRT__futime);
NTCRT_IMPORT(_wutime64, _NtCRT__wutime64);
NTCRT_IMPORT(_wutime, _NtCRT__wutime);
NTCRT_IMPORT(_utime64, _NtCRT__utime64);
NTCRT_IMPORT(_utime, _NtCRT__utime);
NTCRT_IMPORT(utime, _NtCRT__utime);
NTCRT_IMPORT(_futime32, _NtCRT__futime32);
NTCRT_IMPORT(_wutime32, _NtCRT__wutime32);
NTCRT_IMPORT(_utime32, _NtCRT__utime32);
NTCRT_IMPORT(_findclose, _NtCRT__findclose);
NTCRT_IMPORT(_findfirst64, _NtCRT__findfirst64);
NTCRT_IMPORT(_findnext64, _NtCRT__findnext64);
NTCRT_IMPORT(_findfirsti64, _NtCRT__findfirsti64);
NTCRT_IMPORT(_findnexti64, _NtCRT__findnexti64);
NTCRT_IMPORT_ONLY(_findfirst64i32, _NtCRT__findfirst64i32);
NTCRT_IMPORT_ONLY(_findnext64i32, _NtCRT__findnext64i32);
NTCRT_IMPORT(_findfirst, _NtCRT__findfirst);
NTCRT_IMPORT(_findnext, _NtCRT__findnext);
NTCRT_IMPORT(_findfirst32, _NtCRT__findfirst32);
NTCRT_IMPORT(_findnext32, _NtCRT__findnext32);
NTCRT_IMPORT(_wfindfirst64, _NtCRT__wfindfirst64);
NTCRT_IMPORT(_wfindnext64, _NtCRT__wfindnext64);
NTCRT_IMPORT(_wfindfirsti64, _NtCRT__wfindfirsti64);
NTCRT_IMPORT(_wfindnexti64, _NtCRT__wfindnexti64);
#ifndef _WIN64
NTCRT_IMPORT(_findfirst32i64, _NtCRT__findfirst32i64);
NTCRT_IMPORT(_findnext32i64, _NtCRT__findnext32i64);
NTCRT_IMPORT(_wfindfirst32i64, _NtCRT__wfindfirst32i64);
NTCRT_IMPORT(_wfindnext32i64, _NtCRT__wfindnext32i64);
#endif
NTCRT_IMPORT_ONLY(_wfindfirst64i32, _NtCRT__wfindfirst64i32);
NTCRT_IMPORT_ONLY(_wfindnext64i32, _NtCRT__wfindnext64i32);
NTCRT_IMPORT(_wfindfirst, _NtCRT__wfindfirst);
NTCRT_IMPORT(_wfindnext, _NtCRT__wfindnext);
NTCRT_IMPORT(_wfindfirst32, _NtCRT__wfindfirst32);
NTCRT_IMPORT(_wfindnext32, _NtCRT__wfindnext32);
NTCRT_IMPORT(_getpid, _NtCRT__getpid);
NTCRT_IMPORT(getpid, _NtCRT__getpid);
