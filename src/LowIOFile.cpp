/* _close, _lseek, _tell, _filelength, _chsize, _eof, _commit and _setmode of the file descriptors of LowIO.cpp */

#include "NtCRT.h"
#include "LowIO.inl"

#include <errno.h>
#include <fcntl.h>
#include <io.h>

EXTERN_C int __cdecl _NtCRT_close(int fd)
{
    PNTCRT_FD Entry = _NtCRT_LockFd(fd);
    HANDLE Handle;
    int Result = 0;

    if (Entry == NULL)
    {
        return -1;
    }
    Handle = Entry->Handle;
    if (Entry->ConsoleText != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Entry->ConsoleText);
        Entry->ConsoleText = NULL;
    }
    if (fd <= 2)
    {
        _Inline_SetStdHandle(fd == 0 ? STD_INPUT_HANDLE : fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE, NULL);
    }
    /*
     * The fd is free last, under the table lock, under which _open takes a free fd without its lock: the fd it took
     * and set is not cleared after
     */
    RtlAcquireSRWLockExclusive(&_NtCRT_FdTableLock);
    Entry->Handle = INVALID_HANDLE_VALUE;
    Entry->Flags = 0;
    RtlReleaseSRWLockExclusive(&_NtCRT_FdTableLock);
    _NtCRT_UnlockFd(Entry);
    if (Handle != NTCRT_NO_CONSOLE_HANDLE && Handle != INVALID_HANDLE_VALUE && Handle != NULL)
    {
        NTSTATUS Status = _NtCRT_CloseHandle(Handle);
        if (!NT_SUCCESS(Status))
        {
            _NtCRT_SetErrnoFromNtStatus(Status);
            Result = -1;
        }
    }
    return Result;
}

EXTERN_C long long __cdecl _NtCRT_lseeki64(int fd, long long offset, int origin)
{
    PNTCRT_FD Entry = _NtCRT_LockFd(fd);
    long long Result, Ahead;

    if (Entry == NULL)
    {
        return -1;
    }
    /* A UTF-8 file keeps only the low surrogate of a character returned by halves, the position is before it */
    Ahead = origin == SEEK_CUR ? Entry->PendingCount * (long long)sizeof(WCHAR) : 0;
    if (origin == SEEK_CUR && offset == 0)
    {
        /* A query keeps the state of reading */
        Result = _NtCRT_SeekHandle(Entry->Handle, 0, SEEK_CUR);
        if (Result >= 0)
        {
            Result = Result > Ahead ? Result - Ahead : 0;
        }
        _NtCRT_UnlockFd(Entry);
        return Result;
    }
    Result = _NtCRT_SeekHandle(Entry->Handle, offset - Ahead, origin);
    if (Result >= 0)
    {
        Entry->Flags &= ~FEOFLAG;
        Entry->HasPipeChar = FALSE;
        Entry->HasPipeWide = FALSE;
        Entry->CarryLength = 0;
        Entry->PendingCount = 0;
    }
    _NtCRT_UnlockFd(Entry);
    return Result;
}

EXTERN_C long __cdecl _NtCRT_lseek(int fd, long offset, int origin)
{
    long long Result = _NtCRT_lseeki64(fd, offset, origin);

    if (Result > LONG_MAX)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    return (long)Result;
}

EXTERN_C long long __cdecl _NtCRT_telli64(int fd)
{
    return _NtCRT_lseeki64(fd, 0, SEEK_CUR);
}

EXTERN_C long __cdecl _NtCRT_tell(int fd)
{
    return _NtCRT_lseek(fd, 0, SEEK_CUR);
}

EXTERN_C long long __cdecl _NtCRT_filelengthi64(int fd)
{
    PNTCRT_FD Entry = _NtCRT_LockFd(fd);
    LARGE_INTEGER Size;
    long long Result = -1;

    if (Entry == NULL)
    {
        return -1;
    }
    if (_Inline_GetFileSizeEx(Entry->Handle, &Size))
    {
        Result = Size.QuadPart;
    } else
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
    }
    _NtCRT_UnlockFd(Entry);
    return Result;
}

EXTERN_C long __cdecl _NtCRT_filelength(int fd)
{
    long long Result = _NtCRT_filelengthi64(fd);

    if (Result > LONG_MAX)
    {
        NTCRT_ERRNO = EFBIG;
        return -1;
    }
    return (long)Result;
}

EXTERN_C int __cdecl _NtCRT_chsize_s(int fd, long long size)
{
    PNTCRT_FD Entry;
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_END_OF_FILE_INFORMATION EndOfFile;

    if (size < 0)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    Entry = _NtCRT_LockFd(fd);
    if (Entry == NULL)
    {
        return EBADF;
    }
    EndOfFile.EndOfFile.QuadPart = size;
    Status = _NtCRT_IsLegacyConsoleHandle(Entry->Handle) ? STATUS_INVALID_HANDLE
                                                          : NtSetInformationFile(Entry->Handle,
                                                                                 &IoStatusBlock,
                                                                                 &EndOfFile,
                                                                                 sizeof(EndOfFile),
                                                                                 FileEndOfFileInformation);
    _NtCRT_UnlockFd(Entry);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_SetErrnoFromNtStatus(Status);
        if (NTCRT_ERRNO == EACCES)
        {
            NTCRT_ERRNO = EBADF;
        }
        return NTCRT_ERRNO;
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT_chsize(int fd, long size)
{
    return _NtCRT_chsize_s(fd, size) == 0 ? 0 : -1;
}

/*
 * ftruncate64 of libmingwex (std::filesystem::resize_file, truncate) checks the free space of the volume by
 * kernel32 functions NtCRT does not have. The specs wrap it to this, lld takes "ftruncate64" of libntcrt, which
 * precedes libmingwex in ntcrt.cfg, before it applies --wrap.
 */
EXTERN_C int __cdecl __wrap_ftruncate64(int fd, long long length)
{
    return _NtCRT_chsize_s(fd, length) == 0 ? 0 : -1;
}

EXTERN_C int __cdecl ftruncate64(int fd, long long length) __attribute__((alias("__wrap_ftruncate64")));

EXTERN_C int __cdecl _NtCRT_eof(int fd)
{
    long long Current, End;

    Current = _NtCRT_lseeki64(fd, 0, SEEK_CUR);
    if (Current < 0)
    {
        return -1;
    }
    End = _NtCRT_lseeki64(fd, 0, SEEK_END);
    if (End < 0)
    {
        return -1;
    }
    if (Current == End)
    {
        return 1;
    }
    _NtCRT_lseeki64(fd, Current, SEEK_SET);
    return 0;
}

EXTERN_C int __cdecl _NtCRT_commit(int fd)
{
    PNTCRT_FD Entry = _NtCRT_LockFd(fd);
    int Result = 0;

    if (Entry == NULL)
    {
        return -1;
    }
    if (_NtCRT_IsLegacyConsoleHandle(Entry->Handle))
    {
        NTCRT_ERRNO = EBADF;
        Result = -1;
    } else if (!_Inline_FlushFileBuffers(Entry->Handle))
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        Result = -1;
    }
    _NtCRT_UnlockFd(Entry);
    return Result;
}

EXTERN_C int __cdecl _NtCRT_setmode(int fd, int mode)
{
    PNTCRT_FD Entry;
    int Old;

    if (mode != _O_TEXT && mode != _O_BINARY && mode != _O_WTEXT && mode != _O_U8TEXT && mode != _O_U16TEXT)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Entry = _NtCRT_LockFd(fd);
    if (Entry == NULL)
    {
        return -1;
    }
    Old = !(Entry->Flags & FTEXT) ? _O_BINARY : Entry->TextMode == NTCRT_TM_ANSI ? _O_TEXT : _O_WTEXT;
    if (mode == _O_BINARY)
    {
        /* The text mode is kept */
        Entry->Flags &= ~FTEXT;
    } else
    {
        Entry->Flags |= FTEXT;
        Entry->TextMode = mode == _O_TEXT ? NTCRT_TM_ANSI : mode == _O_U8TEXT ? NTCRT_TM_UTF8 : NTCRT_TM_UTF16LE;
    }
    _NtCRT_UnlockFd(Entry);
    return Old;
}

NTCRT_IMPORT(_close, _NtCRT_close);
NTCRT_IMPORT(_lseeki64, _NtCRT_lseeki64);
/* An alias of _lseeki64 in libmsvcrt.a of MinGW-w64 13 and later, of libmingwex.a before */
NTCRT_IMPORT(lseek64, _NtCRT_lseeki64);
NTCRT_IMPORT(_lseek, _NtCRT_lseek);
NTCRT_IMPORT(_telli64, _NtCRT_telli64);
NTCRT_IMPORT(_tell, _NtCRT_tell);
NTCRT_IMPORT(_filelengthi64, _NtCRT_filelengthi64);
NTCRT_IMPORT(_filelength, _NtCRT_filelength);
NTCRT_IMPORT(_chsize_s, _NtCRT_chsize_s);
NTCRT_IMPORT(_chsize, _NtCRT_chsize);
NTCRT_IMPORT(_eof, _NtCRT_eof);
NTCRT_IMPORT(_commit, _NtCRT_commit);
NTCRT_IMPORT(_setmode, _NtCRT_setmode);

/* POSIX names */
NTCRT_IMPORT(close, _NtCRT_close);
NTCRT_IMPORT(lseek, _NtCRT_lseek);
NTCRT_IMPORT(tell, _NtCRT_tell);
NTCRT_IMPORT(chsize, _NtCRT_chsize);
NTCRT_IMPORT(eof, _NtCRT_eof);
NTCRT_IMPORT(filelength, _NtCRT_filelength);
NTCRT_IMPORT(setmode, _NtCRT_setmode);
