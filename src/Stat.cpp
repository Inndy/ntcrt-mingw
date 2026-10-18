/* stat and fstat of msvcrt */

#include "NtCRT.h"
#include "LowIO.inl"

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#define NTCRT_EPOCH_DIFFERENCE 116444736000000000LL

static
__time64_t
_NtCRT_FileTimeToTime64(
    _In_ LONGLONG Time)
{
    if (Time <= NTCRT_EPOCH_DIFFERENCE)
    {
        return 0;
    }
    return (Time - NTCRT_EPOCH_DIFFERENCE) / 10000000LL;
}

static
unsigned short
_NtCRT_ModeFromAttributes(
    _In_ ULONG Attributes,
    _In_opt_ PCWSTR Path)
{
    unsigned short Mode;

    if (Attributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        Mode = _S_IFDIR | _S_IEXEC;
    } else
    {
        Mode = _S_IFREG;
        if (Path != NULL)
        {
            static const WCHAR Executables[][5] = { L".exe", L".com", L".bat", L".cmd" };
            PCWSTR Ext = wcsrchr(Path, L'.');

            for (size_t i = 0; Ext != NULL && wcslen(Ext) == 4 && i < ARRAYSIZE(Executables); i++)
            {
                size_t k = 1;

                /* ASCII case-insensitive */
                while (k < 4 && (Ext[k] | 0x20) == Executables[i][k])
                {
                    k++;
                }
                if (k == 4)
                {
                    Mode |= _S_IEXEC;
                    break;
                }
            }
        }
    }
    Mode |= _S_IREAD;
    if (!(Attributes & FILE_ATTRIBUTE_READONLY))
    {
        Mode |= _S_IWRITE;
    }
    /* Copy the user permissions to group and others */
    Mode |= (Mode & 0700) >> 3;
    Mode |= (Mode & 0700) >> 6;
    return Mode;
}

EXTERN_C int __cdecl _NtCRT_wstat64(const wchar_t* path, struct _stat64* buffer)
{
    NTSTATUS Status;
    HANDLE Handle;
    UNICODE_STRING NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_BASIC_INFORMATION Basic;
    FILE_STANDARD_INFORMATION Standard;
    int Drive;

    if (path == NULL || buffer == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    /* Wildcards are not allowed */
    if (wcspbrk(path, L"?*") != NULL)
    {
        NTCRT_ERRNO = ENOENT;
        return -1;
    }
    Status = RtlDosPathNameToNtPathName_U_WithStatus(path, &NtPath, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }
    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle,
                        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
    RtlFreeUnicodeString(&NtPath);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }
    Status = NtQueryInformationFile(Handle, &IoStatusBlock, &Basic, sizeof(Basic), FileBasicInformation);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryInformationFile(Handle, &IoStatusBlock, &Standard, sizeof(Standard), FileStandardInformation);
    }
    NtClose(Handle);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }

    if (path[0] != L'\0' && path[1] == L':')
    {
        Drive = (path[0] | 0x20) - L'a';
    } else
    {
        WCHAR Current[MAX_PATH];
        Drive = RtlGetCurrentDirectory_U(sizeof(Current), Current) >= 2 * sizeof(WCHAR) && Current[1] == L':' ?
            (Current[0] | 0x20) - L'a' :
            0;
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->st_dev = buffer->st_rdev = (_dev_t)Drive;
    buffer->st_mode = _NtCRT_ModeFromAttributes(Basic.FileAttributes, path);
    buffer->st_nlink = (short)Standard.NumberOfLinks;
    buffer->st_size = (Basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : Standard.EndOfFile.QuadPart;
    buffer->st_atime = _NtCRT_FileTimeToTime64(Basic.LastAccessTime.QuadPart);
    buffer->st_mtime = _NtCRT_FileTimeToTime64(Basic.LastWriteTime.QuadPart);
    buffer->st_ctime = _NtCRT_FileTimeToTime64(Basic.CreationTime.QuadPart);
    return 0;
}

EXTERN_C int __cdecl _NtCRT_stat64(const char* path, struct _stat64* buffer)
{
    int Result;
    PWSTR WidePath;

    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    WidePath = _NtCRT_FileToWide(path);
    if (WidePath == NULL)
    {
        return -1;
    }
    Result = _NtCRT_wstat64(WidePath, buffer);
    RtlFreeHeap(NTCRT_HEAP, 0, WidePath);
    return Result;
}

EXTERN_C int __cdecl _NtCRT_fstat64(int fd, struct _stat64* buffer)
{
    PNTCRT_FD Entry;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_BASIC_INFORMATION Basic;
    FILE_STANDARD_INFORMATION Standard;
    NTSTATUS Status;
    int Result = 0;

    if (buffer == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Entry = _NtCRT_LockFd(fd);
    if (Entry == NULL)
    {
        return -1;
    }
    memset(buffer, 0, sizeof(*buffer));
    /* As msvcrt and UCRT: st_dev is the fd for a device or a pipe, 0 for a file, a pipe has no permissions */
    if (Entry->Flags & (FDEV | FPIPE))
    {
        buffer->st_mode = (Entry->Flags & FDEV) ? _S_IFCHR : _S_IFIFO;
        buffer->st_nlink = 1;
        buffer->st_dev = buffer->st_rdev = (_dev_t)fd;
        goto _Exit;
    }
    Status = NtQueryInformationFile(Entry->Handle, &IoStatusBlock, &Basic, sizeof(Basic), FileBasicInformation);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryInformationFile(Entry->Handle,
                                        &IoStatusBlock,
                                        &Standard,
                                        sizeof(Standard),
                                        FileStandardInformation);
    }
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        Result = -1;
        goto _Exit;
    }
    buffer->st_mode = _NtCRT_ModeFromAttributes(Basic.FileAttributes, NULL);
    buffer->st_nlink = (short)Standard.NumberOfLinks;
    buffer->st_size = Standard.EndOfFile.QuadPart;
    buffer->st_atime = _NtCRT_FileTimeToTime64(Basic.LastAccessTime.QuadPart);
    buffer->st_mtime = _NtCRT_FileTimeToTime64(Basic.LastWriteTime.QuadPart);
    buffer->st_ctime = _NtCRT_FileTimeToTime64(Basic.CreationTime.QuadPart);

_Exit:
    _NtCRT_UnlockFd(Entry);
    return Result;
}

/* Other stat variants convert from _stat64 */

#define NTCRT_STAT_CONVERT(Dst, Src) \
    do { \
        (Dst)->st_dev = (Src).st_dev; \
        (Dst)->st_ino = (Src).st_ino; \
        (Dst)->st_mode = (Src).st_mode; \
        (Dst)->st_nlink = (Src).st_nlink; \
        (Dst)->st_uid = (Src).st_uid; \
        (Dst)->st_gid = (Src).st_gid; \
        (Dst)->st_rdev = (Src).st_rdev; \
        (Dst)->st_size = (Src).st_size; \
        (Dst)->st_atime = (Src).st_atime; \
        (Dst)->st_mtime = (Src).st_mtime; \
        (Dst)->st_ctime = (Src).st_ctime; \
    } while (0)

EXTERN_C int __cdecl _NtCRT_fstati64(int fd, struct _stati64* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_fstat64(fd, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

EXTERN_C int __cdecl _NtCRT_stati64(const char* path, struct _stati64* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_stat64(path, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

EXTERN_C int __cdecl _NtCRT_wstati64(const wchar_t* path, struct _stati64* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_wstat64(path, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

EXTERN_C int __cdecl _NtCRT_fstat32(int fd, struct _stat32* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_fstat64(fd, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

EXTERN_C int __cdecl _NtCRT_stat32(const char* path, struct _stat32* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_stat64(path, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

EXTERN_C int __cdecl _NtCRT_wstat32(const wchar_t* path, struct _stat32* buffer)
{
    struct _stat64 Stat;
    int Result = _NtCRT_wstat64(path, &Stat);
    if (Result == 0)
    {
        NTCRT_STAT_CONVERT(buffer, Stat);
    }
    return Result;
}

NTCRT_IMPORT(_wstat64, _NtCRT_wstat64);
NTCRT_IMPORT(_stat64, _NtCRT_stat64);
NTCRT_IMPORT(_fstat64, _NtCRT_fstat64);
NTCRT_IMPORT(_fstati64, _NtCRT_fstati64);
NTCRT_IMPORT(_stati64, _NtCRT_stati64);
NTCRT_IMPORT(_wstati64, _NtCRT_wstati64);
NTCRT_IMPORT(_fstat32, _NtCRT_fstat32);
NTCRT_IMPORT(_stat32, _NtCRT_stat32);
NTCRT_IMPORT(_wstat32, _NtCRT_wstat32);
NTCRT_IMPORT(_fstat, _NtCRT_fstat32);
NTCRT_IMPORT(_stat, _NtCRT_stat32);
NTCRT_IMPORT(_wstat, _NtCRT_wstat32);
