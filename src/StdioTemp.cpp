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

/*
 * The names of tmpnam fit in L_tmpnam of the headers, 14 with _P_tmpdir "\" (the root of the current drive), as in
 * msvcrt: "\s", the process id and a counter below TMP_MAX + 1 in base 32, at most 2 + 7 + 1 + 3 characters. UCRT
 * names a file in the temporary directory, its headers have L_tmpnam 260. Returns the length, 0 if all names exist.
 */
static volatile LONG g_TmpnamCounter;

static
size_t
_NtCRT_MakeTmpnam(
    _Out_writes_z_(L_tmpnam) PWSTR Name)
{
    for (int Try = 0; Try <= TMP_MAX; Try++)
    {
        ULONG Values[2] = { (ULONG)(ULONG_PTR)NtCurrentProcessId(),
                            (ULONG)_InterlockedIncrement(&g_TmpnamCounter) & TMP_MAX };
        WCHAR Digits[8];
        size_t Length = 2;

        Name[0] = L'\\';
        Name[1] = L's';
        for (int i = 0; i < 2; i++)
        {
            PWSTR p = Digits + ARRAYSIZE(Digits);
            ULONG Value = Values[i];

            do
            {
                *--p = L"0123456789abcdefghijklmnopqrstuv"[Value & 31];
                Value >>= 5;
            } while (Value != 0);
            if (i == 1)
            {
                Name[Length++] = L'.';
            }
            while (p < Digits + ARRAYSIZE(Digits))
            {
                Name[Length++] = *p++;
            }
        }
        Name[Length] = L'\0';
        if (_K32_GetFileAttributesW(Name) == INVALID_FILE_ATTRIBUTES)
        {
            return Length;
        }
    }
    NTCRT_ERRNO = EEXIST;
    return 0;
}

/* The name in Str (of L_tmpnam if Size is 0), or "" and an error */
template<typename TChar>
static
errno_t
_NtCRT_Tmpnam(
    _Out_writes_z_(Size) TChar* Str,
    _In_ size_t Size)
{
    WCHAR Name[L_tmpnam];
    size_t Length = _NtCRT_MakeTmpnam(Name);

    if (Length == 0)
    {
        Str[0] = 0;
        return NTCRT_ERRNO;
    }
    if (Size != 0 && Length >= Size)
    {
        Str[0] = 0;
        return _NtCRT_InvalidParameter(ERANGE);
    }
    for (size_t i = 0; i <= Length; i++)
    {
        Str[i] = (TChar)Name[i];
    }
    return 0;
}

/* A static buffer if str is NULL */
EXTERN_C char* __cdecl _NtCRT_tmpnam(char* str)
{
    static char Buffer[L_tmpnam];

    if (str == NULL)
    {
        str = Buffer;
    }
    return _NtCRT_Tmpnam(str, 0) == 0 ? str : NULL;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wtmpnam(wchar_t* str)
{
    static wchar_t Buffer[L_tmpnam];

    if (str == NULL)
    {
        str = Buffer;
    }
    return _NtCRT_Tmpnam(str, 0) == 0 ? str : NULL;
}

EXTERN_C errno_t __cdecl _NtCRT_tmpnam_s(char* str, size_t sizeInChars)
{
    if (str == NULL || sizeInChars == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    return _NtCRT_Tmpnam(str, sizeInChars);
}

EXTERN_C errno_t __cdecl _NtCRT__wtmpnam_s(wchar_t* str, size_t sizeInChars)
{
    if (str == NULL || sizeInChars == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    return _NtCRT_Tmpnam(str, sizeInChars);
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
