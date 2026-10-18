/* stdio streams opened and closed, remove */

#include "NtCRT.h"
#include "Stdio.inl"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

EXTERN_C
FILE*
_NtCRT_AllocStream(void)
{
    FILE* Stream = NULL;

    _NtCRT_EnsureStdio();
    _NtCRT_lock(_IOB_SCAN_LOCK);
    for (int i = 3; i < NTCRT_IOB_ENTRIES; i++)
    {
        if (!_NtCRT_InUse(&_NtCRT_iob[i]))
        {
            RtlEnterCriticalSection(&_NtCRT_IobLocks[i]);
            if (_NtCRT_InUse(&_NtCRT_iob[i]))
            {
                RtlLeaveCriticalSection(&_NtCRT_IobLocks[i]);
                continue;
            }
            Stream = &_NtCRT_iob[i];
            break;
        }
    }
    for (unsigned int i = 0; Stream == NULL && i < ARRAYSIZE(_NtCRT_Streams); i++)
    {
        if (_NtCRT_Streams[i] == NULL)
        {
            _NtCRT_Streams[i] = (PNTCRT_FILEX)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, sizeof(NTCRT_FILEX));
            if (_NtCRT_Streams[i] == NULL)
            {
                break;
            }
            RtlInitializeCriticalSection(&_NtCRT_Streams[i]->Lock);
        }
        if (!_NtCRT_InUse(&_NtCRT_Streams[i]->File))
        {
            RtlEnterCriticalSection(&_NtCRT_Streams[i]->Lock);
            if (_NtCRT_InUse(&_NtCRT_Streams[i]->File))
            {
                RtlLeaveCriticalSection(&_NtCRT_Streams[i]->Lock);
                continue;
            }
            Stream = &_NtCRT_Streams[i]->File;
        }
    }
    if (Stream != NULL)
    {
        Stream->_ptr = Stream->_base = NULL;
        Stream->_cnt = 0;
        Stream->_file = -1;
        Stream->_charbuf = 0;
        Stream->_bufsiz = 0;
        Stream->_tmpfname = NULL;
        /* Reserve the slot until the caller sets the real flags */
        Stream->_flag = _IORW;
    } else
    {
        NTCRT_ERRNO = EMFILE;
    }
    _NtCRT_unlock(_IOB_SCAN_LOCK);
    return Stream;
}

/* Open and close */

static
BOOL
_NtCRT_ParseMode(
    _In_ const wchar_t* Mode,
    _Out_ int* OFlag,
    _Out_ int* StreamFlag)
{
    int OpenFlag, Flag;
    BOOL Binary = FALSE, Text = FALSE, Plus = FALSE;

    while (*Mode == L' ')
    {
        Mode++;
    }
    switch (*Mode++)
    {
        case L'r':
            OpenFlag = _O_RDONLY;
            Flag = _IOREAD;
            break;
        case L'w':
            OpenFlag = _O_WRONLY | _O_CREAT | _O_TRUNC;
            Flag = _IOWRT;
            break;
        case L'a':
            OpenFlag = _O_WRONLY | _O_CREAT | _O_APPEND;
            Flag = _IOWRT;
            break;
        default:
            return FALSE;
    }
    for (; *Mode != L'\0' && *Mode != L','; Mode++)
    {
        switch (*Mode)
        {
            case L'+':
                if (Plus)
                {
                    return FALSE;
                }
                Plus = TRUE;
                OpenFlag = (OpenFlag & ~(_O_RDONLY | _O_WRONLY)) | _O_RDWR;
                Flag = _IORW;
                break;
            case L'b':
                if (Text || Binary)
                {
                    return FALSE;
                }
                Binary = TRUE;
                OpenFlag |= _O_BINARY;
                break;
            case L't':
                if (Text || Binary)
                {
                    return FALSE;
                }
                Text = TRUE;
                OpenFlag |= _O_TEXT;
                break;
            case L'x':
                OpenFlag |= _O_EXCL;
                break;
            case L'N':
                OpenFlag |= _O_NOINHERIT;
                break;
            case L'S':
                OpenFlag |= _O_SEQUENTIAL;
                break;
            case L'R':
                OpenFlag |= _O_RANDOM;
                break;
            case L'T':
                OpenFlag |= _O_SHORT_LIVED;
                break;
            case L'D':
                OpenFlag |= _O_TEMPORARY;
                break;
            case L'c':
            case L'n':
            case L' ':
                break;
            default:
                return FALSE;
        }
    }
    if (*Mode == L',')
    {
        /* ", ccs=<encoding>" selects a Unicode text mode */
        static const struct
        {
            const wchar_t* Name;
            int Flag;
        } Encodings[] = { { L"UTF-8", _O_U8TEXT }, { L"UTF-16LE", _O_U16TEXT }, { L"UNICODE", _O_WTEXT } };
        size_t i;

        for (Mode++; *Mode == L' '; Mode++);
        if (wcsncmp(Mode, L"ccs", 3) != 0)
        {
            return FALSE;
        }
        for (Mode += 3; *Mode == L' '; Mode++);
        if (*Mode++ != L'=')
        {
            return FALSE;
        }
        for (; *Mode == L' '; Mode++);
        for (i = 0; i < ARRAYSIZE(Encodings); i++)
        {
            size_t Length = 0;

            /* ASCII case-insensitive, the names are upper case */
            while (Encodings[i].Name[Length] != L'\0' &&
                   (Mode[Length] >= L'a' && Mode[Length] <= L'z' ? Mode[Length] - (L'a' - L'A') : Mode[Length]) ==
                       Encodings[i].Name[Length])
            {
                Length++;
            }
            if (Encodings[i].Name[Length] == L'\0')
            {
                OpenFlag |= Encodings[i].Flag;
                Mode += Length;
                break;
            }
        }
        if (i == ARRAYSIZE(Encodings))
        {
            return FALSE;
        }
        for (; *Mode == L' '; Mode++);
        if (*Mode != L'\0')
        {
            return FALSE;
        }
    }
    *OFlag = OpenFlag;
    *StreamFlag = Flag;
    return TRUE;
}

static
FILE*
_NtCRT_OpenStream(
    _In_ const wchar_t* Path,
    _In_ const wchar_t* Mode,
    _In_ int ShFlag,
    _In_opt_ FILE* Reuse)
{
    int OFlag, Flag, fd;
    FILE* Stream;

    if (Path == NULL || Mode == NULL || !_NtCRT_ParseMode(Mode, &OFlag, &Flag))
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    if (*Path == L'\0')
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    Stream = Reuse != NULL ? Reuse : _NtCRT_AllocStream();
    if (Stream == NULL)
    {
        return NULL;
    }
    fd = _NtCRT_wsopen(Path, OFlag, ShFlag, _S_IREAD | _S_IWRITE);
    if (fd < 0)
    {
        if (Reuse == NULL)
        {
            Stream->_flag = 0;
            _NtCRT_unlock_file(Stream);
        }
        return NULL;
    }
    Stream->_file = fd;
    Stream->_flag = Flag;
    Stream->_ptr = Stream->_base = NULL;
    Stream->_cnt = 0;
    Stream->_bufsiz = 0;
    Stream->_tmpfname = NULL;
    if (Reuse == NULL)
    {
        _NtCRT_unlock_file(Stream);
    }
    return Stream;
}

static
FILE*
_NtCRT_OpenStreamA(
    _In_ const char* Path,
    _In_ const char* Mode,
    _In_ int ShFlag,
    _In_opt_ FILE* Reuse)
{
    PWSTR WidePath, WideMode;
    FILE* Stream = NULL;

    if (Path == NULL || Mode == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    WidePath = _NtCRT_FileToWide(Path);
    if (WidePath == NULL)
    {
        return NULL;
    }
    WideMode = _NtCRT_FileToWide(Mode);
    if (WideMode != NULL)
    {
        Stream = _NtCRT_OpenStream(WidePath, WideMode, ShFlag, Reuse);
        RtlFreeHeap(NTCRT_HEAP, 0, WideMode);
    }
    RtlFreeHeap(NTCRT_HEAP, 0, WidePath);
    return Stream;
}

EXTERN_C FILE* __cdecl _NtCRT_fopen(const char* path, const char* mode)
{
    return _NtCRT_OpenStreamA(path, mode, _SH_DENYNO, NULL);
}

EXTERN_C FILE* __cdecl _NtCRT__wfopen(const wchar_t* path, const wchar_t* mode)
{
    return _NtCRT_OpenStream(path, mode, _SH_DENYNO, NULL);
}

EXTERN_C FILE* __cdecl _NtCRT__fsopen(const char* path, const char* mode, int shflag)
{
    return _NtCRT_OpenStreamA(path, mode, shflag, NULL);
}

EXTERN_C FILE* __cdecl _NtCRT__wfsopen(const wchar_t* path, const wchar_t* mode, int shflag)
{
    return _NtCRT_OpenStream(path, mode, shflag, NULL);
}

EXTERN_C int __cdecl _NtCRT_fopen_s(FILE** pfile, const char* path, const char* mode)
{
    if (pfile == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    *pfile = _NtCRT_OpenStreamA(path, mode, _SH_SECURE, NULL);
    return *pfile == NULL ? NTCRT_ERRNO : 0;
}

EXTERN_C int __cdecl _NtCRT__wfopen_s(FILE** pfile, const wchar_t* path, const wchar_t* mode)
{
    if (pfile == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return EINVAL;
    }
    *pfile = _NtCRT_OpenStream(path, mode, _SH_SECURE, NULL);
    return *pfile == NULL ? NTCRT_ERRNO : 0;
}

static
FILE*
_NtCRT_OpenFdStream(
    _In_ int fd,
    _In_ const wchar_t* Mode)
{
    int OFlag, Flag;
    FILE* Stream;

    if (Mode == NULL || !_NtCRT_ParseMode(Mode, &OFlag, &Flag))
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    if (_NtCRT_get_osfhandle(fd) == -1)
    {
        return NULL;
    }
    Stream = _NtCRT_AllocStream();
    if (Stream == NULL)
    {
        return NULL;
    }
    Stream->_file = fd;
    Stream->_flag = Flag;
    _NtCRT_unlock_file(Stream);
    return Stream;
}

EXTERN_C FILE* __cdecl _NtCRT__wfdopen(int fd, const wchar_t* mode)
{
    return _NtCRT_OpenFdStream(fd, mode);
}

EXTERN_C FILE* __cdecl _NtCRT__fdopen(int fd, const char* mode)
{
    WCHAR WideMode[32];
    unsigned int i;

    if (mode == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    for (i = 0; mode[i] != '\0' && i < ARRAYSIZE(WideMode) - 1; i++)
    {
        WideMode[i] = (unsigned char)mode[i];
    }
    WideMode[i] = L'\0';
    return _NtCRT_OpenFdStream(fd, WideMode);
}

EXTERN_C int __cdecl _NtCRT_remove(const char* path);

EXTERN_C
int
_NtCRT_fclose_nolock(
    _In_ FILE* Stream)
{
    int Ret = EOF;

    if (_NtCRT_InUse(Stream))
    {
        Ret = _NtCRT_flush_nolock(Stream);
        _NtCRT_freebuf(Stream);
        if (_NtCRT_close(Stream->_file) < 0)
        {
            Ret = EOF;
        }
        if (Stream->_tmpfname != NULL)
        {
            _NtCRT_remove(Stream->_tmpfname);
            RtlFreeHeap(NTCRT_HEAP, 0, Stream->_tmpfname);
            Stream->_tmpfname = NULL;
        }
    }
    Stream->_flag = 0;
    Stream->_file = -1;
    memset(_NtCRT_StreamMb(Stream), 0, sizeof(NTCRT_STREAM_MB));
    return Ret;
}

EXTERN_C int __cdecl _NtCRT_fclose(FILE* stream)
{
    int Ret;

    NTCRT_CHECK_STREAM(stream, EOF);
    _NtCRT_lock_file(stream);
    Ret = _NtCRT_fclose_nolock(stream);
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C FILE* __cdecl _NtCRT__wfreopen(const wchar_t* path, const wchar_t* mode, FILE* stream)
{
    FILE* Ret;

    NTCRT_CHECK_STREAM(stream, NULL);
    _NtCRT_lock_file(stream);
    _NtCRT_fclose_nolock(stream);
    Ret = _NtCRT_OpenStream(path, mode, _SH_DENYNO, stream);
    if (Ret == NULL)
    {
        stream->_flag = 0;
    }
    _NtCRT_unlock_file(stream);
    return Ret;
}

EXTERN_C FILE* __cdecl _NtCRT_freopen(const char* path, const char* mode, FILE* stream)
{
    FILE* Ret;

    NTCRT_CHECK_STREAM(stream, NULL);
    _NtCRT_lock_file(stream);
    _NtCRT_fclose_nolock(stream);
    Ret = _NtCRT_OpenStreamA(path, mode, _SH_DENYNO, stream);
    if (Ret == NULL)
    {
        stream->_flag = 0;
    }
    _NtCRT_unlock_file(stream);
    return Ret;
}

static
int
_NtCRT_CloseCallback(
    _In_ FILE* Stream,
    _In_ int Context)
{
    int Ret = 0;

    UNREFERENCED_PARAMETER(Context);
    if (Stream >= _NtCRT_iob && Stream < _NtCRT_iob + 3)
    {
        return 0;
    }
    if (!_NtCRT_InUse(Stream))
    {
        return 0;
    }
    _NtCRT_lock_file(Stream);
    if (_NtCRT_InUse(Stream) && _NtCRT_fclose_nolock(Stream) != EOF)
    {
        Ret = 1;
    }
    _NtCRT_unlock_file(Stream);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__fcloseall(void)
{
    return _NtCRT_ForEachStream(_NtCRT_CloseCallback, 0);
}

EXTERN_C void _NtCRT_CloseAll(void)
{
    _NtCRT__fcloseall();
}

/* File system operations */

EXTERN_C int __cdecl _NtCRT__wremove(const wchar_t* path)
{
    if (path == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (!_K32_DeleteFileW(path))
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return -1;
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT_remove(const char* path)
{
    PWSTR WidePath;
    int Ret;

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
    Ret = _NtCRT__wremove(WidePath);
    RtlFreeHeap(NTCRT_HEAP, 0, WidePath);
    return Ret;
}

EXTERN_C errno_t __cdecl _NtCRT_freopen_s(FILE** pFile, const char* path, const char* mode, FILE* stream)
{
    if (pFile == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pFile = _NtCRT_freopen(path, mode, stream);
    return *pFile != NULL ? 0 : NTCRT_ERRNO;
}

EXTERN_C errno_t __cdecl _NtCRT__wfreopen_s(FILE** pFile, const wchar_t* path, const wchar_t* mode, FILE* stream)
{
    if (pFile == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pFile = _NtCRT__wfreopen(path, mode, stream);
    return *pFile != NULL ? 0 : NTCRT_ERRNO;
}

NTCRT_IMPORT(fopen, _NtCRT_fopen);
NTCRT_IMPORT(_wfopen, _NtCRT__wfopen);
NTCRT_IMPORT(_fsopen, _NtCRT__fsopen);
NTCRT_IMPORT(_wfsopen, _NtCRT__wfsopen);
NTCRT_IMPORT(fopen_s, _NtCRT_fopen_s);
NTCRT_IMPORT(_wfopen_s, _NtCRT__wfopen_s);
NTCRT_IMPORT(_fdopen, _NtCRT__fdopen);
NTCRT_IMPORT(fdopen, _NtCRT__fdopen);
NTCRT_IMPORT(_wfdopen, _NtCRT__wfdopen);
NTCRT_IMPORT(fclose, _NtCRT_fclose);
NTCRT_IMPORT(freopen, _NtCRT_freopen);
NTCRT_IMPORT(_wfreopen, _NtCRT__wfreopen);
NTCRT_IMPORT(_fcloseall, _NtCRT__fcloseall);
NTCRT_IMPORT(remove, _NtCRT_remove);
NTCRT_IMPORT(_wremove, _NtCRT__wremove);
NTCRT_IMPORT(_unlink, _NtCRT_remove);
NTCRT_IMPORT(unlink, _NtCRT_remove);
NTCRT_IMPORT(_wunlink, _NtCRT__wremove);
NTCRT_IMPORT(freopen_s, _NtCRT_freopen_s);
NTCRT_IMPORT(_wfreopen_s, _NtCRT__wfreopen_s);
