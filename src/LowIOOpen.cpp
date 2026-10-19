/* File descriptors opened from paths and handles, _dup and _dup2, inherited by child processes */

#include "NtCRT.h"
#include "LowIO.inl"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <sys/types.h>

/* The standard handle of fd 0, 1 or 2 */
static __attribute__((noinline)) void _NtCRT_SetStdFd(int fd, HANDLE Handle)
{
    PRTL_USER_PROCESS_PARAMETERS ProcParam = NtCurrentPeb()->ProcessParameters;

    (fd == 0 ? ProcParam->StandardInput : fd == 1 ? ProcParam->StandardOutput : ProcParam->StandardError) = Handle;
}

/* Allocate a free fd at or above MinFd, returns it locked with FOPEN set */
static
int
_NtCRT_AllocFd(
    _In_ int MinFd,
    _In_ HANDLE Handle,
    _In_ UCHAR Flags)
{
    int fd = -1;

    RtlAcquireSRWLockExclusive(&_NtCRT_FdTableLock);
    for (unsigned int b = MinFd / NTCRT_FD_BLOCK_SIZE; b < ARRAYSIZE(_NtCRT_FdBlocks) && fd < 0; b++)
    {
        PNTCRT_FD Block = _NtCRT_FdBlocks[b];
        if (Block == NULL && (Block = _NtCRT_AllocBlock(b)) == NULL)
        {
            break;
        }
        for (int i = 0; i < NTCRT_FD_BLOCK_SIZE; i++)
        {
            int Candidate = b * NTCRT_FD_BLOCK_SIZE + i;
            if (Candidate >= MinFd && !(Block[i].Flags & FOPEN))
            {
                _NtCRT_InitFdState(&Block[i], Handle, Flags);
                fd = Candidate;
                break;
            }
        }
    }
    RtlReleaseSRWLockExclusive(&_NtCRT_FdTableLock);
    if (fd < 0)
    {
        NTCRT_ERRNO = EMFILE;
        _NtCRT_GetPtd()->Doserrno = 0;
    } else if (fd <= 2)
    {
        /* As _close and _dup2, fds 0 to 2 are the standard handles */
        _NtCRT_SetStdFd(fd, Handle);
    }
    return fd;
}

/* Build lpReserved2 of STARTUPINFO for a child process in the format parsed above, free by RtlFreeHeap */
EXTERN_C PUCHAR _NtCRT_BuildInheritBlock(_Out_ PUSHORT Size)
{
    int Count = 0;
    PUCHAR Block, Flags;
    UNALIGNED intptr_t* Handles;

    RtlAcquireSRWLockShared(&_NtCRT_FdTableLock);
    for (int i = 0; i < NTCRT_FD_MAX; i++)
    {
        PNTCRT_FD Entry = _NtCRT_FdEntry(i);
        if (Entry != NULL && (Entry->Flags & FOPEN))
        {
            Count = i + 1;
        }
    }
    *Size = (USHORT)(sizeof(int) + Count * (1 + sizeof(intptr_t)));
    Block = (PUCHAR)RtlAllocateHeap(NTCRT_HEAP, 0, *Size);
    if (Block != NULL)
    {
        *(UNALIGNED int*)Block = Count;
        Flags = Block + sizeof(int);
        Handles = (UNALIGNED intptr_t*)(Flags + Count);
        for (int i = 0; i < Count; i++)
        {
            PNTCRT_FD Entry = _NtCRT_FdEntry(i);
            if (Entry != NULL && (Entry->Flags & (FOPEN | FNOINHERIT)) == FOPEN)
            {
                Flags[i] = Entry->Flags;
                Handles[i] = (intptr_t)Entry->Handle;
            } else
            {
                Flags[i] = 0;
                Handles[i] = (intptr_t)INVALID_HANDLE_VALUE;
            }
        }
    }
    RtlReleaseSRWLockShared(&_NtCRT_FdTableLock);
    return Block;
}

EXTERN_C int __cdecl _NtCRT_open_handle(HANDLE Handle, int flags)
{
    UCHAR Flags = 0;
    int fd;

    if (flags & _O_APPEND)
    {
        Flags |= FAPPEND;
    }
    if (flags & _O_TEXT)
    {
        Flags |= FTEXT;
    }
    if (flags & _O_NOINHERIT)
    {
        Flags |= FNOINHERIT;
    }
    Flags |= _NtCRT_DeviceFlags(Handle);
    fd = _NtCRT_AllocFd(0, Handle, Flags);
    return fd;
}

EXTERN_C int __cdecl _NtCRT_open_osfhandle(intptr_t osfhandle, int flags)
{
    HANDLE Handle = (HANDLE)osfhandle;

    if (_CRT_GetFileType(Handle) == FILE_TYPE_UNKNOWN && _Inline_GetLastError() != ERROR_SUCCESS)
    {
        NTCRT_ERRNO = EBADF;
        return -1;
    }
    return _NtCRT_open_handle(Handle, flags);
}

EXTERN_C int __cdecl _NtCRT_dup2(int fd1, int fd2)
{
    PNTCRT_FD Entry;
    HANDLE NewHandle;
    UCHAR Flags, TextMode;
    BOOLEAN WideOnly;
    NTSTATUS Status;

    if (fd1 == fd2)
    {
        return _NtCRT_FdEntry(fd1) != NULL && (_NtCRT_FdEntry(fd1)->Flags & FOPEN) ? 0 : (NTCRT_ERRNO = EBADF, -1);
    }
    if ((unsigned int)fd2 >= NTCRT_FD_MAX)
    {
        NTCRT_ERRNO = EBADF;
        return -1;
    }
    Entry = _NtCRT_LockFd(fd1);
    if (Entry == NULL)
    {
        return -1;
    }
    Flags = Entry->Flags & ~FNOINHERIT;
    TextMode = Entry->TextMode;
    WideOnly = Entry->WideOnly;
    Status = NtDuplicateObject(NtCurrentProcess(),
                               Entry->Handle,
                               NtCurrentProcess(),
                               &NewHandle,
                               0,
                               OBJ_INHERIT,
                               DUPLICATE_SAME_ACCESS);
    _NtCRT_UnlockFd(Entry);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }
    /* EBUSY: the process ends and a terminated thread holds the lock of fd2, which stays unusable */
    if (_NtCRT_FdEntry(fd2) != NULL && (_NtCRT_FdEntry(fd2)->Flags & FOPEN) && _NtCRT_close(fd2) != 0 &&
        NTCRT_ERRNO == EBUSY)
    {
        NtClose(NewHandle);
        return -1;
    }
    _NtCRT_SetFd(fd2, NewHandle, Flags);
    _NtCRT_FdEntry(fd2)->TextMode = TextMode;
    _NtCRT_FdEntry(fd2)->WideOnly = WideOnly;
    if (fd2 <= 2)
    {
        _NtCRT_SetStdFd(fd2, NewHandle);
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT_dup(int fd)
{
    PNTCRT_FD Entry;
    HANDLE NewHandle;
    UCHAR Flags, TextMode;
    BOOLEAN WideOnly;
    NTSTATUS Status;
    int NewFd;

    Entry = _NtCRT_LockFd(fd);
    if (Entry == NULL)
    {
        return -1;
    }
    Flags = Entry->Flags & ~FNOINHERIT;
    TextMode = Entry->TextMode;
    WideOnly = Entry->WideOnly;
    Status = NtDuplicateObject(NtCurrentProcess(),
                               Entry->Handle,
                               NtCurrentProcess(),
                               &NewHandle,
                               0,
                               OBJ_INHERIT,
                               DUPLICATE_SAME_ACCESS);
    _NtCRT_UnlockFd(Entry);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        return -1;
    }
    NewFd = _NtCRT_AllocFd(0, NewHandle, Flags);
    if (NewFd < 0)
    {
        NtClose(NewHandle);
        return -1;
    }
    _NtCRT_FdEntry(NewFd)->TextMode = TextMode;
    _NtCRT_FdEntry(NewFd)->WideOnly = WideOnly;
    return NewFd;
}

/* Set the text mode of a newly opened text fd by oflag, _fmode and the BOM of the file as msvcrt */
static
int
_NtCRT_SetUnicodeMode(
    _In_ int fd,
    _In_ int oflag,
    _In_ ULONG Disposition)
{
    const int UnicodeModes = _O_WTEXT | _O_U16TEXT | _O_U8TEXT;
    PNTCRT_FD Entry = _NtCRT_FdEntry(fd);
    UCHAR TextMode = NTCRT_TM_ANSI;
    BOOL CheckBom = FALSE, WriteBom = FALSE;
    int Access = oflag & (_O_RDONLY | _O_WRONLY | _O_RDWR);

    if ((oflag & (_O_TEXT | UnicodeModes)) == 0)
    {
        oflag |= (_NtCRT_fmode & (_O_TEXT | UnicodeModes)) != 0 ? _NtCRT_fmode & (_O_TEXT | UnicodeModes) : _O_TEXT;
    }
    if (oflag & _O_U8TEXT)
    {
        TextMode = NTCRT_TM_UTF8;
    } else if ((oflag & _O_U16TEXT) ||
               ((oflag & _O_WTEXT) && (oflag & (_O_WRONLY | _O_CREAT | _O_TRUNC)) == (_O_WRONLY | _O_CREAT | _O_TRUNC)))
    {
        TextMode = NTCRT_TM_UTF16LE;
    }

    if ((oflag & UnicodeModes) && !(Entry->Flags & FDEV))
    {
        BOOL Opening = Disposition == FILE_OPEN || Disposition == FILE_OPEN_IF;

        if (Access == _O_RDONLY)
        {
            CheckBom = TRUE;
        } else if (Opening)
        {
            long long Size = _NtCRT_lseeki64(fd, 0, SEEK_END);

            if (Size == -1)
            {
                return -1;
            }
            if (Size == 0)
            {
                WriteBom = TRUE;
            } else if (_NtCRT_lseeki64(fd, 0, SEEK_SET) == -1)
            {
                return -1;
            } else
            {
                CheckBom = Access == _O_RDWR;
            }
        } else
        {
            WriteBom = TRUE;
        }
    }
    if (CheckBom)
    {
        ULONG Bom = 0;
        int Count = _NtCRT_read(fd, &Bom, 3);
        long long Position = 0;

        if (Count == -1)
        {
            return -1;
        }
        if (Count == 3 && Bom == 0xBFBBEF)
        {
            TextMode = NTCRT_TM_UTF8;
            Position = -1;
        } else if (Count >= 2 && (Bom & 0xFFFF) == 0xFFFE)
        {
            NTCRT_ERRNO = EINVAL;
            return -1;
        } else if (Count >= 2 && (Bom & 0xFFFF) == 0xFEFF)
        {
            TextMode = NTCRT_TM_UTF16LE;
            Position = 2;
        }
        if (Position != -1 && _NtCRT_lseeki64(fd, Position, SEEK_SET) == -1)
        {
            return -1;
        }
    }
    if (WriteBom && TextMode != NTCRT_TM_ANSI)
    {
        static const UCHAR Utf8Bom[] = { 0xEF, 0xBB, 0xBF }, Utf16Bom[] = { 0xFF, 0xFE };
        const UCHAR* Bom = TextMode == NTCRT_TM_UTF8 ? Utf8Bom : Utf16Bom;
        int Length = TextMode == NTCRT_TM_UTF8 ? sizeof(Utf8Bom) : sizeof(Utf16Bom), Written = 0;

        while (Written < Length)
        {
            int Result = _NtCRT_write(fd, Bom + Written, (unsigned int)(Length - Written));
            if (Result == -1)
            {
                return -1;
            }
            Written += Result;
        }
    }
    Entry->TextMode = TextMode;
    Entry->WideOnly = (oflag & _O_WTEXT) != 0;
    return 0;
}

EXTERN_C int __cdecl _NtCRT_wsopen(const wchar_t* path, int oflag, int shflag, int pmode)
{
    ACCESS_MASK Access;
    ULONG Share, Disposition, Options, Attributes;
    HANDLE Handle;
    ULONG Error;
    UCHAR Flags = 0;
    int fd;

    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    switch (oflag & (_O_RDONLY | _O_WRONLY | _O_RDWR))
    {
        case _O_RDONLY: Access = GENERIC_READ; break;
        case _O_WRONLY: Access = (oflag & _O_APPEND) ? GENERIC_WRITE | GENERIC_READ : GENERIC_WRITE; break;
        case _O_RDWR: Access = GENERIC_READ | GENERIC_WRITE; break;
        default:
            NTCRT_ERRNO = EINVAL;
            return -1;
    }
    switch (shflag)
    {
        case _SH_DENYRW: Share = 0; break;
        case _SH_DENYWR: Share = FILE_SHARE_READ; break;
        case _SH_DENYRD: Share = FILE_SHARE_WRITE; break;
        case _SH_DENYNO: Share = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
        /* fopen_s: shared read if read-only */
        case _SH_SECURE: Share = Access == GENERIC_READ ? FILE_SHARE_READ : 0; break;
        default:
            NTCRT_ERRNO = EINVAL;
            return -1;
    }
    switch (oflag & (_O_CREAT | _O_EXCL | _O_TRUNC))
    {
        case 0:
        case _O_EXCL: Disposition = FILE_OPEN; break;
        case _O_CREAT: Disposition = FILE_OPEN_IF; break;
        case _O_CREAT | _O_EXCL:
        case _O_CREAT | _O_TRUNC | _O_EXCL: Disposition = FILE_CREATE; break;
        case _O_CREAT | _O_TRUNC: Disposition = FILE_OVERWRITE_IF; break;
        case _O_TRUNC:
        case _O_TRUNC | _O_EXCL: Disposition = FILE_OVERWRITE; break;
        default:
            NTCRT_ERRNO = EINVAL;
            return -1;
    }
    Options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE;
    Attributes = FILE_ATTRIBUTE_NORMAL;
    if ((oflag & _O_CREAT) && !(pmode & _S_IWRITE))
    {
        Attributes = FILE_ATTRIBUTE_READONLY;
    }
    if (oflag & _O_TEMPORARY)
    {
        Options |= FILE_DELETE_ON_CLOSE;
        Access |= DELETE;
        Share |= FILE_SHARE_DELETE;
    }
    if (oflag & _O_SHORT_LIVED)
    {
        Attributes |= FILE_ATTRIBUTE_TEMPORARY;
    }
    if (oflag & _O_SEQUENTIAL)
    {
        Options |= FILE_SEQUENTIAL_ONLY;
    } else if (oflag & _O_RANDOM)
    {
        Options |= FILE_RANDOM_ACCESS;
    }

    Error = _NtCRT_CreateFile(&Handle,
                              path,
                              Access | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                              Share,
                              Disposition,
                              Options,
                              Attributes,
                              NULL);
    if (Error != ERROR_SUCCESS && (Access & GENERIC_READ) && (oflag & _O_APPEND) &&
        (oflag & (_O_RDONLY | _O_WRONLY | _O_RDWR)) == _O_WRONLY)
    {
        /* Write-only file opened for appending, retry without read access */
        Access &= ~GENERIC_READ;
        Error = _NtCRT_CreateFile(&Handle,
                                  path,
                                  Access | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                                  Share,
                                  Disposition,
                                  Options,
                                  Attributes,
                                  NULL);
    }
    if (Error != ERROR_SUCCESS)
    {
        _NtCRT_SetErrnoFromWin32(Error);
        return -1;
    }

    if (oflag & _O_APPEND)
    {
        Flags |= FAPPEND;
    }
    if (oflag & _O_NOINHERIT)
    {
        Flags |= FNOINHERIT;
    } else
    {
        /* msvcrt opens inheritable handles, spawned children receive them by lpReserved2 */
        OBJECT_HANDLE_FLAG_INFORMATION Info;

        if (NT_SUCCESS(NtQueryObject(Handle, ObjectHandleFlagInformation, &Info, sizeof(Info), NULL)))
        {
            Info.Inherit = TRUE;
            NtSetInformationObject(Handle, ObjectHandleFlagInformation, &Info, sizeof(Info));
        }
    }
    if ((oflag & _O_TEXT) ||
        (!(oflag & _O_BINARY) && ((oflag & (_O_WTEXT | _O_U16TEXT | _O_U8TEXT)) || _NtCRT_fmode != _O_BINARY)))
    {
        Flags |= FTEXT;
    }
    Flags |= _NtCRT_DeviceFlags(Handle);
    fd = _NtCRT_AllocFd(0, Handle, Flags);
    if (fd < 0)
    {
        NtClose(Handle);
        return -1;
    }

    /* As msvcrt, a Ctrl+Z at the end of a text file opened for reading and writing is removed for appending */
    if ((Flags & (FTEXT | FDEV | FPIPE)) == FTEXT && (oflag & _O_RDWR))
    {
        long long Position = _NtCRT_lseeki64(fd, -1, SEEK_END);
        char c = 0;

        if (Position == -1)
        {
            /* An empty file, errno is left as EINVAL */
            if (_NtCRT_GetPtd()->Doserrno != ERROR_NEGATIVE_SEEK)
            {
                _NtCRT_close(fd);
                return -1;
            }
        } else if ((_NtCRT_read(fd, &c, 1) == 0 && c == 0x1A && _NtCRT_chsize_s(fd, Position) != 0) ||
                   _NtCRT_lseeki64(fd, 0, SEEK_SET) == -1)
        {
            _NtCRT_close(fd);
            return -1;
        }
    }
    if ((Flags & FTEXT) && _NtCRT_SetUnicodeMode(fd, oflag, Disposition) != 0)
    {
        _NtCRT_close(fd);
        return -1;
    }
    return fd;
}

EXTERN_C int __cdecl _NtCRT_sopen(const char* path, int oflag, int shflag, ...)
{
    int pmode = 0;
    int fd;
    PWSTR WidePath;

    if (oflag & _O_CREAT)
    {
        va_list ap;
        va_start(ap, shflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
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
    fd = _NtCRT_wsopen(WidePath, oflag, shflag, pmode);
    RtlFreeHeap(NTCRT_HEAP, 0, WidePath);
    return fd;
}

EXTERN_C int __cdecl _NtCRT__wsopen(const wchar_t* path, int oflag, int shflag, ...)
{
    int pmode = 0;

    if (oflag & _O_CREAT)
    {
        va_list ap;
        va_start(ap, shflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    return _NtCRT_wsopen(path, oflag, shflag, pmode);
}

EXTERN_C int __cdecl _NtCRT_open(const char* path, int oflag, ...)
{
    int pmode = 0;

    if (oflag & _O_CREAT)
    {
        va_list ap;
        va_start(ap, oflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    return _NtCRT_sopen(path, oflag, _SH_DENYNO, pmode);
}

EXTERN_C int __cdecl _NtCRT_wopen(const wchar_t* path, int oflag, ...)
{
    int pmode = 0;

    if (oflag & _O_CREAT)
    {
        va_list ap;
        va_start(ap, oflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    return _NtCRT_wsopen(path, oflag, _SH_DENYNO, pmode);
}

EXTERN_C int __cdecl _NtCRT_creat(const char* path, int pmode)
{
    return _NtCRT_sopen(path, _O_CREAT | _O_TRUNC | _O_RDWR, _SH_DENYNO, pmode);
}

EXTERN_C int __cdecl _NtCRT_wcreat(const wchar_t* path, int pmode)
{
    return _NtCRT_wsopen(path, _O_CREAT | _O_TRUNC | _O_RDWR, _SH_DENYNO, pmode);
}

/* The permission of a new file is validated */
EXTERN_C int __cdecl _NtCRT_wsopen_s(int* pfh, const wchar_t* path, int oflag, int shflag, int pmode)
{
    if (pfh == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pfh = -1;
    if ((oflag & _O_CREAT) && (pmode & ~(_S_IREAD | _S_IWRITE)))
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pfh = _NtCRT_wsopen(path, oflag, shflag, pmode);
    return *pfh < 0 ? NTCRT_ERRNO : 0;
}

EXTERN_C int __cdecl _NtCRT__sopen_s(int* pfh, const char* path, int oflag, int shflag, int pmode)
{
    if (pfh == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pfh = -1;
    if ((oflag & _O_CREAT) && (pmode & ~(_S_IREAD | _S_IWRITE)))
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pfh = _NtCRT_sopen(path, oflag, shflag, pmode);
    return *pfh < 0 ? NTCRT_ERRNO : 0;
}

NTCRT_IMPORT(_open_osfhandle, _NtCRT_open_osfhandle);
NTCRT_IMPORT(_dup, _NtCRT_dup);
NTCRT_IMPORT(_dup2, _NtCRT_dup2);
NTCRT_IMPORT(_sopen, _NtCRT_sopen);
NTCRT_IMPORT(_wsopen, _NtCRT__wsopen);
NTCRT_IMPORT(_wsopen_s, _NtCRT_wsopen_s);
NTCRT_IMPORT(_sopen_s, _NtCRT__sopen_s);
NTCRT_IMPORT(_open, _NtCRT_open);
NTCRT_IMPORT(_wopen, _NtCRT_wopen);
NTCRT_IMPORT(_creat, _NtCRT_creat);
NTCRT_IMPORT(_wcreat, _NtCRT_wcreat);

/* POSIX names */
NTCRT_IMPORT(dup, _NtCRT_dup);
NTCRT_IMPORT(dup2, _NtCRT_dup2);
NTCRT_IMPORT(sopen, _NtCRT_sopen);
NTCRT_IMPORT(open, _NtCRT_open);
NTCRT_IMPORT(creat, _NtCRT_creat);
