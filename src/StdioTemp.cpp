/* Temporary files and names of stdio: tmpfile, _rmtmp, tmpnam and tempnam */

#include "NtCRT.h"
#include "Stdio.inl"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

static volatile LONG g_TmpCounter;

/* Make a unique name in the temporary directory, returns a heap string */
/* Append a number in lowercase hexadecimal */
static
void
_NtCRT_AppendHex(
    _Inout_ PWSTR Buffer,
    _In_ ULONG Value)
{
    WCHAR Digits[9];
    PWSTR p = Digits + 8;

    *p = L'\0';
    do
    {
        *--p = L"0123456789abcdef"[Value & 0xF];
        Value >>= 4;
    } while (Value != 0);
    wcscat(Buffer, p);
}

static
PWSTR
_NtCRT_MakeTempName(
    _In_opt_ PCWSTR Directory,
    _In_opt_ PCWSTR Prefix)
{
    WCHAR TempPath[MAX_PATH + 1];
    PWSTR Name;
    size_t Length;

    if (Directory == NULL)
    {
        DWORD TempLength = _K32_GetTempPathW(ARRAYSIZE(TempPath), TempPath);
        if (TempLength == 0 || TempLength >= ARRAYSIZE(TempPath))
        {
            NTCRT_ERRNO = ENOENT;
            return NULL;
        }
        Directory = TempPath;
    }
    if (Prefix == NULL)
    {
        Prefix = L"t";
    }
    Length = wcslen(Directory) + wcslen(Prefix) + 32;
    Name = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, Length * sizeof(WCHAR));
    if (Name == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    for (int Try = 0; Try < 0x10000; Try++)
    {
        size_t DirLength = wcslen(Directory);
        LONG Counter = _InterlockedIncrement(&g_TmpCounter);

        /* Directory\PrefixPID.COUNTER, hexadecimal numbers */
        wcscpy(Name, Directory);
        if (DirLength != 0 && Directory[DirLength - 1] != L'\\' && Directory[DirLength - 1] != L'/')
        {
            wcscat(Name, L"\\");
        }
        wcscat(Name, Prefix);
        _NtCRT_AppendHex(Name, (ULONG)(ULONG_PTR)NtCurrentProcessId());
        wcscat(Name, L".");
        _NtCRT_AppendHex(Name, (ULONG)Counter);
        if (_K32_GetFileAttributesW(Name) == INVALID_FILE_ATTRIBUTES)
        {
            return Name;
        }
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    NTCRT_ERRNO = EEXIST;
    return NULL;
}

EXTERN_C FILE* __cdecl _NtCRT_tmpfile(void)
{
    FILE* Stream = NULL;

    for (int Try = 0; Try < 16 && Stream == NULL; Try++)
    {
        PWSTR Name = _NtCRT_MakeTempName(NULL, L"t");
        int fd;

        if (Name == NULL)
        {
            return NULL;
        }
        fd = _NtCRT_wsopen(Name,
                           _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY | _O_TEMPORARY,
                           _SH_DENYNO,
                           _S_IREAD | _S_IWRITE);
        RtlFreeHeap(NTCRT_HEAP, 0, Name);
        if (fd < 0)
        {
            if (NTCRT_ERRNO == EEXIST)
            {
                continue;
            }
            return NULL;
        }
        Stream = _NtCRT_AllocStream();
        if (Stream == NULL)
        {
            _NtCRT_close(fd);
            return NULL;
        }
        Stream->_file = fd;
        Stream->_flag = _IORW | NTCRT_IOTMPFILE;
        _NtCRT_unlock_file(Stream);
    }
    return Stream;
}

EXTERN_C errno_t __cdecl _NtCRT_tmpfile_s(FILE** pFilePtr)
{
    if (pFilePtr == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    *pFilePtr = _NtCRT_tmpfile();
    return *pFilePtr != NULL ? 0 : NTCRT_ERRNO;
}

static
int
_NtCRT_RmtmpCallback(
    _In_ FILE* Stream,
    _In_ int Context)
{
    int Ret = 0;

    UNREFERENCED_PARAMETER(Context);
    if (!(Stream->_flag & NTCRT_IOTMPFILE))
    {
        return 0;
    }
    _NtCRT_lock_file(Stream);
    if (_NtCRT_InUse(Stream) && (Stream->_flag & NTCRT_IOTMPFILE) && _NtCRT_fclose_nolock(Stream) != EOF)
    {
        Ret = 1;
    }
    _NtCRT_unlock_file(Stream);
    return Ret;
}

/* Close the streams of tmpfile, which deletes their files, returns the count */
EXTERN_C int __cdecl _NtCRT__rmtmp(void)
{
    return _NtCRT_ForEachStream(_NtCRT_RmtmpCallback, 0);
}

EXTERN_C char* __cdecl _NtCRT_tempnam(const char* dir, const char* prefix)
{
    PWSTR WideDir = NULL, WidePrefix = NULL, Name = NULL;
    char* Ret = NULL;

    if (dir != NULL && (WideDir = _NtCRT_FileToWide(dir)) == NULL)
    {
        goto _Exit;
    }
    if (prefix != NULL && (WidePrefix = _NtCRT_FileToWide(prefix)) == NULL)
    {
        goto _Exit;
    }
    Name = _NtCRT_MakeTempName(WideDir, WidePrefix);
    if (Name != NULL)
    {
        Ret = _NtCRT_WideToFile(Name);
    }

_Exit:
    RtlFreeHeap(NTCRT_HEAP, 0, WideDir);
    RtlFreeHeap(NTCRT_HEAP, 0, WidePrefix);
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    return Ret;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wtempnam(const wchar_t* dir, const wchar_t* prefix)
{
    return _NtCRT_MakeTempName(dir, prefix);
}

EXTERN_C char* __cdecl _NtCRT_tmpnam(char* str)
{
    static char Buffer[MAX_PATH];
    char* Name = _NtCRT_tempnam(NULL, "s");

    if (Name == NULL)
    {
        return NULL;
    }
    if (str == NULL)
    {
        str = Buffer;
    }
    strncpy(str, Name, L_tmpnam - 1);
    str[L_tmpnam - 1] = '\0';
    if (strlen(Name) >= L_tmpnam)
    {
        /* Caller buffer is too small for an absolute path, use the static buffer */
        if (str != Buffer)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, Name);
            NTCRT_ERRNO = ERANGE;
            return NULL;
        }
        strncpy(Buffer, Name, sizeof(Buffer) - 1);
        Buffer[sizeof(Buffer) - 1] = '\0';
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    return str;
}

/* Like tmpnam, with a static buffer if str is NULL */
EXTERN_C wchar_t* __cdecl _NtCRT__wtmpnam(wchar_t* str)
{
    static wchar_t Buffer[MAX_PATH];
    wchar_t* Name = _NtCRT_MakeTempName(NULL, L"s");

    if (Name == NULL)
    {
        return NULL;
    }
    if (str == NULL)
    {
        str = Buffer;
    }
    if (wcslen(Name) >= (str == Buffer ? ARRAYSIZE(Buffer) : L_tmpnam))
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Name);
        NTCRT_ERRNO = ERANGE;
        return NULL;
    }
    wcscpy(str, Name);
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    return str;
}

template<typename TChar>
static
errno_t
_NtCRT_TmpnamS(
    _Out_writes_z_(Size) TChar* Str,
    _In_ size_t Size,
    _In_ TChar* Name)
{
    size_t Length;

    if (Name == NULL)
    {
        Str[0] = 0;
        return NTCRT_ERRNO;
    }
    for (Length = 0; Name[Length] != 0; Length++)
    {
    }
    if (Length >= Size)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Name);
        Str[0] = 0;
        return _NtCRT_InvalidParameter(ERANGE);
    }
    memcpy(Str, Name, (Length + 1) * sizeof(TChar));
    RtlFreeHeap(NTCRT_HEAP, 0, Name);
    return 0;
}

EXTERN_C errno_t __cdecl _NtCRT_tmpnam_s(char* str, size_t sizeInChars)
{
    if (str == NULL || sizeInChars == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    return _NtCRT_TmpnamS(str, sizeInChars, _NtCRT_tempnam(NULL, "s"));
}

EXTERN_C errno_t __cdecl _NtCRT__wtmpnam_s(wchar_t* str, size_t sizeInChars)
{
    if (str == NULL || sizeInChars == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    return _NtCRT_TmpnamS(str, sizeInChars, _NtCRT_MakeTempName(NULL, L"s"));
}

NTCRT_IMPORT(tmpfile, _NtCRT_tmpfile);
NTCRT_IMPORT(_tempnam, _NtCRT_tempnam);
NTCRT_IMPORT(tempnam, _NtCRT_tempnam);
NTCRT_IMPORT(_wtempnam, _NtCRT__wtempnam);
NTCRT_IMPORT(tmpnam, _NtCRT_tmpnam);
NTCRT_IMPORT(tmpfile_s, _NtCRT_tmpfile_s);
NTCRT_IMPORT(_rmtmp, _NtCRT__rmtmp);
NTCRT_IMPORT(rmtmp, _NtCRT__rmtmp);
NTCRT_IMPORT(_wtmpnam, _NtCRT__wtmpnam);
NTCRT_IMPORT(tmpnam_s, _NtCRT_tmpnam_s);
NTCRT_IMPORT(_wtmpnam_s, _NtCRT__wtmpnam_s);
