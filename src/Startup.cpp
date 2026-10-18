/* msvcrt startup, termination, errno, locks, signal, threads and locale */

#include "NtCRT.h"

#include <errno.h>
#include <signal.h>
#include <locale.h>
#include <limits.h>
#include <float.h>
#include <new>

/* errno */

/*
 * ntdll!_errno returns the TEB TlsSlots[16] which is reserved by the system for it (TlsBitmapBits has bit 16 set),
 * share it so errno set by the C functions exported by ntdll is visible.
 */
EXTERN_C int* __cdecl _NtCRT_errno(void)
{
    return (int*)&NtCurrentTeb()->TlsSlots[16];
}

static const struct
{
    ULONG Win32Error;
    int Errno;
} g_ErrorTable[] = {
    { ERROR_INVALID_FUNCTION, EINVAL },
    { ERROR_FILE_NOT_FOUND, ENOENT },
    { ERROR_PATH_NOT_FOUND, ENOENT },
    { ERROR_TOO_MANY_OPEN_FILES, EMFILE },
    { ERROR_ACCESS_DENIED, EACCES },
    { ERROR_INVALID_HANDLE, EBADF },
    { ERROR_ARENA_TRASHED, ENOMEM },
    { ERROR_NOT_ENOUGH_MEMORY, ENOMEM },
    { ERROR_INVALID_BLOCK, ENOMEM },
    { ERROR_BAD_ENVIRONMENT, E2BIG },
    { ERROR_BAD_FORMAT, ENOEXEC },
    { ERROR_INVALID_ACCESS, EINVAL },
    { ERROR_INVALID_DATA, EINVAL },
    { ERROR_INVALID_DRIVE, ENOENT },
    { ERROR_CURRENT_DIRECTORY, EACCES },
    { ERROR_NOT_SAME_DEVICE, EXDEV },
    { ERROR_NO_MORE_FILES, ENOENT },
    { ERROR_LOCK_VIOLATION, EACCES },
    { ERROR_SHARING_VIOLATION, EACCES },
    { ERROR_BAD_NETPATH, ENOENT },
    { ERROR_NETWORK_ACCESS_DENIED, EACCES },
    { ERROR_BAD_NET_NAME, ENOENT },
    { ERROR_FILE_EXISTS, EEXIST },
    { ERROR_CANNOT_MAKE, EACCES },
    { ERROR_FAIL_I24, EACCES },
    { ERROR_INVALID_PARAMETER, EINVAL },
    { ERROR_NO_PROC_SLOTS, EAGAIN },
    { ERROR_DRIVE_LOCKED, EACCES },
    { ERROR_BROKEN_PIPE, EPIPE },
    { ERROR_DISK_FULL, ENOSPC },
    { ERROR_INVALID_TARGET_HANDLE, EBADF },
    { ERROR_WAIT_NO_CHILDREN, ECHILD },
    { ERROR_CHILD_NOT_COMPLETE, ECHILD },
    { ERROR_DIRECT_ACCESS_HANDLE, EBADF },
    { ERROR_NEGATIVE_SEEK, EINVAL },
    { ERROR_SEEK_ON_DEVICE, EACCES },
    { ERROR_DIR_NOT_EMPTY, ENOTEMPTY },
    { ERROR_NOT_LOCKED, EACCES },
    { ERROR_BAD_PATHNAME, ENOENT },
    { ERROR_MAX_THRDS_REACHED, EAGAIN },
    { ERROR_LOCK_FAILED, EACCES },
    { ERROR_ALREADY_EXISTS, EEXIST },
    { ERROR_FILENAME_EXCED_RANGE, ENOENT },
    { ERROR_NESTING_NOT_ALLOWED, EAGAIN },
    { ERROR_DIRECTORY, ENOTDIR },
    { ERROR_NOT_ENOUGH_QUOTA, ENOMEM },
    { ERROR_INVALID_NAME, ENOENT },
    { ERROR_DELETE_PENDING, EACCES },
};

EXTERN_C int _NtCRT_Win32ToErrno(ULONG Win32Error)
{
    for (UINT i = 0; i < ARRAYSIZE(g_ErrorTable); i++)
    {
        if (g_ErrorTable[i].Win32Error == Win32Error)
        {
            return g_ErrorTable[i].Errno;
        }
    }
    if (Win32Error >= ERROR_WRITE_PROTECT && Win32Error <= ERROR_SHARING_BUFFER_EXCEEDED)
    {
        return EACCES;
    }
    if (Win32Error >= ERROR_INVALID_STARTING_CODESEG && Win32Error <= ERROR_INFLOOP_IN_RELOC_CHAIN)
    {
        return ENOEXEC;
    }
    return EINVAL;
}

EXTERN_C void _NtCRT_SetErrnoFromWin32(ULONG Win32Error)
{
    _NtCRT_GetPtd()->Doserrno = Win32Error;
    NTCRT_ERRNO = _NtCRT_Win32ToErrno(Win32Error);
}

EXTERN_C void _NtCRT_SetErrnoFromNtStatus(NTSTATUS Status)
{
    _NtCRT_SetErrnoFromWin32(_Inline_BaseSetLastNTError(Status));
}

/* Per-thread data, freed by the TLS callback on thread exit */

static DWORD g_PtdIndex = TLS_OUT_OF_INDEXES;
static NTCRT_PTD g_FallbackPtd;

EXTERN_C PNTCRT_PTD _NtCRT_GetPtd(void)
{
    PNTCRT_PTD Ptd;
    DWORD Index = g_PtdIndex;

    if (Index == TLS_OUT_OF_INDEXES)
    {
        DWORD NewIndex = _Inline_TlsAlloc();
        if (NewIndex == TLS_OUT_OF_INDEXES)
        {
            return &g_FallbackPtd;
        }
        Index = _InterlockedCompareExchange((volatile LONG*)&g_PtdIndex, NewIndex, TLS_OUT_OF_INDEXES);
        if (Index != TLS_OUT_OF_INDEXES)
        {
            _Inline_TlsFree(NewIndex);
        } else
        {
            Index = NewIndex;
        }
    }
    Ptd = (PNTCRT_PTD)_Inline_TlsGetValue2(Index);
    if (Ptd == NULL)
    {
        Ptd = (PNTCRT_PTD)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, sizeof(*Ptd));
        if (Ptd == NULL)
        {
            return &g_FallbackPtd;
        }
        _Inline_TlsSetValue(Index, Ptd);
    }
    return Ptd;
}

EXTERN_C void _NtCRT_ThreadDetach(void)
{
    PNTCRT_PTD Ptd;

    if (g_PtdIndex == TLS_OUT_OF_INDEXES)
    {
        return;
    }
    Ptd = (PNTCRT_PTD)_Inline_TlsGetValue2(g_PtdIndex);
    if (Ptd != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Ptd->TmBuffer);
        RtlFreeHeap(NTCRT_HEAP, 0, Ptd->AsctimeBuffer);
        RtlFreeHeap(NTCRT_HEAP, 0, Ptd->WcserrorBuffer);
        RtlFreeHeap(NTCRT_HEAP, 0, Ptd->CvtBuffer);
        RtlFreeHeap(NTCRT_HEAP, 0, Ptd);
        _Inline_TlsSetValue(g_PtdIndex, NULL);
    }
}

static
VOID
NTAPI
_NtCRT_TlsCallback(
    _In_ PVOID DllHandle,
    _In_ DWORD Reason,
    _In_ PVOID Reserved)
{
    UNREFERENCED_PARAMETER(DllHandle);
    UNREFERENCED_PARAMETER(Reserved);
    if (Reason == DLL_THREAD_DETACH)
    {
        _NtCRT_ThreadDetach();
    }
}

/* .CRT$XLF is included in the TLS callback array provided by MinGW-w64's tlssup.o */
extern "C" __attribute__((section(".CRT$XLF"), used)) const PIMAGE_TLS_CALLBACK _NtCRT_TlsCallbackEntry =
    _NtCRT_TlsCallback;

EXTERN_C unsigned long* __cdecl _NtCRT___doserrno(void)
{
    return &_NtCRT_GetPtd()->Doserrno;
}

EXTERN_C int __cdecl _NtCRT__get_errno(int* Value)
{
    if (Value == NULL)
    {
        return EINVAL;
    }
    *Value = NTCRT_ERRNO;
    return 0;
}

EXTERN_C int __cdecl _NtCRT__set_errno(int Value)
{
    NTCRT_ERRNO = Value;
    return 0;
}

EXTERN_C int __cdecl _NtCRT__get_doserrno(unsigned long* Value)
{
    if (Value == NULL)
    {
        return EINVAL;
    }
    *Value = _NtCRT_GetPtd()->Doserrno;
    return 0;
}

/* Global locks, see _lock / _unlock of msvcrt */

static RTL_CRITICAL_SECTION g_Locks[NTCRT_LOCK_TABLE_SIZE];
static volatile LONG g_LockInitialized[NTCRT_LOCK_TABLE_SIZE];

static
PRTL_CRITICAL_SECTION
_NtCRT_GetLock(
    _In_ int locknum)
{
    PRTL_CRITICAL_SECTION Lock = &g_Locks[locknum];

    /* 0: uninitialized, 1: initializing, 2: initialized */
    if (g_LockInitialized[locknum] != 2)
    {
        if (_InterlockedCompareExchange(&g_LockInitialized[locknum], 1, 0) == 0)
        {
            RtlInitializeCriticalSection(Lock);
            _InterlockedExchange(&g_LockInitialized[locknum], 2);
        } else
        {
            while (g_LockInitialized[locknum] != 2)
            {
                NtYieldExecution();
            }
        }
    }
    return Lock;
}

EXTERN_C void __cdecl _NtCRT_lock(int locknum)
{
    if ((unsigned int)locknum < NTCRT_LOCK_TABLE_SIZE)
    {
        RtlEnterCriticalSection(_NtCRT_GetLock(locknum));
    }
}

EXTERN_C void __cdecl _NtCRT_unlock(int locknum)
{
    if ((unsigned int)locknum < NTCRT_LOCK_TABLE_SIZE)
    {
        RtlLeaveCriticalSection(&g_Locks[locknum]);
    }
}

/* Startup globals */

extern "C"
{
    int _NtCRT_app_type = 0;
    int _NtCRT_fmode = 0;
    int _NtCRT_commode = 0;
    int _NtCRT_argc = 0;
    char** _NtCRT_argv = NULL;
    wchar_t** _NtCRT_wargv = NULL;
    char** _NtCRT_environ = NULL;
    wchar_t** _NtCRT_wenviron = NULL;
    char** _NtCRT_initenv = NULL;
    wchar_t** _NtCRT_winitenv = NULL;
    char* _NtCRT_acmdln = NULL;
    wchar_t* _NtCRT_wcmdln = NULL;
    char* _NtCRT_pgmptr = NULL;
    wchar_t* _NtCRT_wpgmptr = NULL;
    unsigned int _NtCRT_osver = 0;
    unsigned int _NtCRT_winver = 0;
    unsigned int _NtCRT_winmajor = 0;
    unsigned int _NtCRT_winminor = 0;
    int _NtCRT_mb_cur_max = 1;
    unsigned int _NtCRT_lc_codepage = 0;
}

EXTERN_C UINT _NtCRT_CodePage(void)
{
    return NtCurrentPeb()->ActiveCodePage;
}

EXTERN_C void __cdecl _NtCRT___set_app_type(int type)
{
    _NtCRT_app_type = type;
}

EXTERN_C int* __cdecl _NtCRT___p__fmode(void) { return &_NtCRT_fmode; }
EXTERN_C int* __cdecl _NtCRT___p__commode(void) { return &_NtCRT_commode; }
EXTERN_C int* __cdecl _NtCRT___p___argc(void) { return &_NtCRT_argc; }
EXTERN_C char*** __cdecl _NtCRT___p___argv(void) { return &_NtCRT_argv; }
EXTERN_C wchar_t*** __cdecl _NtCRT___p___wargv(void) { return &_NtCRT_wargv; }
EXTERN_C char*** __cdecl _NtCRT___p__environ(void) { return &_NtCRT_environ; }
EXTERN_C wchar_t*** __cdecl _NtCRT___p__wenviron(void) { return &_NtCRT_wenviron; }
EXTERN_C char** __cdecl _NtCRT___p__acmdln(void) { return &_NtCRT_acmdln; }
EXTERN_C wchar_t** __cdecl _NtCRT___p__wcmdln(void) { return &_NtCRT_wcmdln; }
EXTERN_C char** __cdecl _NtCRT___p__pgmptr(void) { return &_NtCRT_pgmptr; }
EXTERN_C wchar_t** __cdecl _NtCRT___p__wpgmptr(void) { return &_NtCRT_wpgmptr; }
EXTERN_C int __cdecl _NtCRT____mb_cur_max_func(void) { return _NtCRT_mb_cur_max; }
EXTERN_C unsigned int __cdecl _NtCRT____lc_codepage_func(void) { return _NtCRT_lc_codepage; }
EXTERN_C int* __cdecl _NtCRT___p___mb_cur_max(void) { return &_NtCRT_mb_cur_max; }

/* Command line, follows the parsing rules of msvcrt */

template <typename T>
static
void
_NtCRT_ParseCommandLine(
    _In_ const T* CommandLine,
    _Out_opt_ T** Argv,
    _Out_opt_ T* Args,
    _Out_ size_t* ArgCount,
    _Out_ size_t* CharCount)
{
    const T* p = CommandLine;
    bool InQuote;

    *ArgCount = 0;
    *CharCount = 0;

#define _NTCRT_EMIT(c) do { if (Args != NULL) { *Args++ = (c); } ++*CharCount; } while (0)

    /* Program name, ends at the closing quote or whitespace, backslashes are not special */
    if (Argv != NULL)
    {
        *Argv++ = Args;
    }
    ++*ArgCount;
    if (*p == '"')
    {
        p++;
        while (*p != '\0' && *p != '"')
        {
            _NTCRT_EMIT(*p);
            p++;
        }
        if (*p == '"')
        {
            p++;
        }
    } else
    {
        while (*p != '\0' && *p != ' ' && *p != '\t')
        {
            _NTCRT_EMIT(*p);
            p++;
        }
    }
    _NTCRT_EMIT('\0');

    for (;;)
    {
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }
        if (Argv != NULL)
        {
            *Argv++ = Args;
        }
        ++*ArgCount;
        InQuote = false;
        for (;;)
        {
            bool CopyChar = true;
            size_t Backslashes = 0;

            while (*p == '\\')
            {
                p++;
                Backslashes++;
            }
            if (*p == '"')
            {
                /* 2N backslashes + quote -> N backslashes + toggle, 2N+1 -> N backslashes + literal quote */
                if (Backslashes % 2 == 0)
                {
                    if (InQuote && p[1] == '"')
                    {
                        /* "" inside quotes is a literal quote */
                        p++;
                    } else
                    {
                        CopyChar = false;
                        InQuote = !InQuote;
                    }
                }
                Backslashes /= 2;
            }
            while (Backslashes-- != 0)
            {
                _NTCRT_EMIT('\\');
            }
            if (*p == '\0' || (!InQuote && (*p == ' ' || *p == '\t')))
            {
                break;
            }
            if (CopyChar)
            {
                _NTCRT_EMIT(*p);
            }
            p++;
        }
        _NTCRT_EMIT('\0');
    }

#undef _NTCRT_EMIT
}

template <typename T>
static
T**
_NtCRT_BuildArgv(
    _In_ const T* CommandLine,
    _Out_ int* Argc)
{
    size_t ArgCount, CharCount;
    T** Argv;

    _NtCRT_ParseCommandLine<T>(CommandLine, NULL, NULL, &ArgCount, &CharCount);
    Argv = (T**)RtlAllocateHeap(NTCRT_HEAP, 0, (ArgCount + 1) * sizeof(T*) + CharCount * sizeof(T));
    if (Argv == NULL)
    {
        return NULL;
    }
    _NtCRT_ParseCommandLine<T>(CommandLine, Argv, (T*)(Argv + ArgCount + 1), &ArgCount, &CharCount);
    Argv[ArgCount] = NULL;
    *Argc = (int)ArgCount;
    return Argv;
}

/* Environment */

static
wchar_t**
_NtCRT_BuildWideEnviron(void)
{
    PWSTR Block, p;
    size_t Count = 0;
    wchar_t** Env;

    Block = _Inline_GetEnvironmentStringsW();
    if (Block == NULL)
    {
        return NULL;
    }
    for (p = Block; *p != UNICODE_NULL; p += wcslen(p) + 1)
    {
        if (*p != L'=')
        {
            Count++;
        }
    }
    Env = (wchar_t**)RtlAllocateHeap(NTCRT_HEAP, 0, (Count + 1) * sizeof(wchar_t*));
    if (Env == NULL)
    {
        _Inline_FreeEnvironmentStringsW(Block);
        return NULL;
    }
    Count = 0;
    for (p = Block; *p != UNICODE_NULL; p += wcslen(p) + 1)
    {
        size_t Length;
        /* Skip "=C:=C:\..." drive current directory entries as msvcrt does */
        if (*p == L'=')
        {
            continue;
        }
        Length = (wcslen(p) + 1) * sizeof(wchar_t);
        Env[Count] = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, Length);
        if (Env[Count] != NULL)
        {
            memcpy(Env[Count++], p, Length);
        }
    }
    Env[Count] = NULL;
    _Inline_FreeEnvironmentStringsW(Block);
    return Env;
}

static
char**
_NtCRT_BuildEnvironFromWide(
    _In_ wchar_t** WideEnv)
{
    size_t Count = 0, i;
    char** Env;

    while (WideEnv[Count] != NULL)
    {
        Count++;
    }
    Env = (char**)RtlAllocateHeap(NTCRT_HEAP, 0, (Count + 1) * sizeof(char*));
    if (Env == NULL)
    {
        return NULL;
    }
    for (i = 0; i < Count; i++)
    {
        Env[i] = _NtCRT_WideToAnsi(WideEnv[i]);
    }
    Env[Count] = NULL;
    return Env;
}

static
void
_NtCRT_InitEnviron(void)
{
    if (_NtCRT_wenviron == NULL)
    {
        _NtCRT_wenviron = _NtCRT_BuildWideEnviron();
    }
    if (_NtCRT_environ == NULL && _NtCRT_wenviron != NULL)
    {
        _NtCRT_environ = _NtCRT_BuildEnvironFromWide(_NtCRT_wenviron);
    }
}

static
void
_NtCRT_InitVersion(void)
{
    PPEB Peb = NtCurrentPeb();

    _NtCRT_osver = Peb->OSBuildNumber;
    _NtCRT_winmajor = Peb->OSMajorVersion;
    _NtCRT_winminor = Peb->OSMinorVersion;
    _NtCRT_winver = (_NtCRT_winmajor << 8) | _NtCRT_winminor;
}

static
void
_NtCRT_InitProgramName(void)
{
    PUNICODE_STRING Image = &NtCurrentPeb()->ProcessParameters->ImagePathName;

    if (_NtCRT_wpgmptr == NULL)
    {
        _NtCRT_wpgmptr = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, Image->Length + sizeof(WCHAR));
        if (_NtCRT_wpgmptr != NULL)
        {
            memcpy(_NtCRT_wpgmptr, Image->Buffer, Image->Length);
            _NtCRT_wpgmptr[Image->Length / sizeof(WCHAR)] = UNICODE_NULL;
            _NtCRT_pgmptr = _NtCRT_WideToAnsi(_NtCRT_wpgmptr);
        }
    }
}

typedef struct
{
    int newmode;
} _NtCRT_startupinfo;

EXTERN_C int __cdecl _NtCRT_InitRuntime(void);

EXTERN_C
int
__cdecl
_NtCRT___getmainargs(
    _Out_ int* argc,
    _Out_ char*** argv,
    _Out_ char*** envp,
    _In_ int dowildcard,
    _In_opt_ _NtCRT_startupinfo* startinfo)
{
    UNREFERENCED_PARAMETER(dowildcard);
    UNREFERENCED_PARAMETER(startinfo);

    _NtCRT_InitRuntime();
    if (_NtCRT_argv == NULL)
    {
        _NtCRT_acmdln = _NtCRT_WideToAnsi(NtCurrentPeb()->ProcessParameters->CommandLine.Buffer);
        if (_NtCRT_acmdln == NULL || (_NtCRT_argv = _NtCRT_BuildArgv<char>(_NtCRT_acmdln, &_NtCRT_argc)) == NULL)
        {
            _NtCRT_amsg_exit(8); /* _RT_SPACEARG */
            return -1;
        }
    }
    _NtCRT_InitEnviron();
    _NtCRT_initenv = _NtCRT_environ;
    *argc = _NtCRT_argc;
    *argv = _NtCRT_argv;
    *envp = _NtCRT_environ;
    return 0;
}

EXTERN_C
int
__cdecl
_NtCRT___wgetmainargs(
    _Out_ int* argc,
    _Out_ wchar_t*** argv,
    _Out_ wchar_t*** envp,
    _In_ int dowildcard,
    _In_opt_ _NtCRT_startupinfo* startinfo)
{
    UNREFERENCED_PARAMETER(dowildcard);
    UNREFERENCED_PARAMETER(startinfo);

    _NtCRT_InitRuntime();
    if (_NtCRT_wargv == NULL)
    {
        _NtCRT_wcmdln = NtCurrentPeb()->ProcessParameters->CommandLine.Buffer;
        _NtCRT_wargv = _NtCRT_BuildArgv<wchar_t>(_NtCRT_wcmdln, &_NtCRT_argc);
        if (_NtCRT_wargv == NULL)
        {
            _NtCRT_amsg_exit(8);
            return -1;
        }
    }
    _NtCRT_InitEnviron();
    _NtCRT_winitenv = _NtCRT_wenviron;
    *argc = _NtCRT_argc;
    *argv = _NtCRT_wargv;
    *envp = _NtCRT_wenviron;
    return 0;
}

/* getenv / _putenv operate on _environ and the process environment */

EXTERN_C char* __cdecl _NtCRT_getenv(const char* name)
{
    size_t Length;
    char* Value = NULL;

    if (name == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    _NtCRT_lock(_ENV_LOCK);
    _NtCRT_InitEnviron();
    Length = strlen(name);
    if (_NtCRT_environ != NULL && Length != 0)
    {
        for (char** p = _NtCRT_environ; *p != NULL; p++)
        {
            if (_strnicmp(*p, name, Length) == 0 && (*p)[Length] == '=')
            {
                Value = *p + Length + 1;
                break;
            }
        }
    }
    _NtCRT_unlock(_ENV_LOCK);
    return Value;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wgetenv(const wchar_t* name)
{
    size_t Length;
    wchar_t* Value = NULL;

    if (name == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    _NtCRT_lock(_ENV_LOCK);
    _NtCRT_InitEnviron();
    Length = wcslen(name);
    if (_NtCRT_wenviron != NULL && Length != 0)
    {
        for (wchar_t** p = _NtCRT_wenviron; *p != NULL; p++)
        {
            if (_wcsnicmp(*p, name, Length) == 0 && (*p)[Length] == L'=')
            {
                Value = *p + Length + 1;
                break;
            }
        }
    }
    _NtCRT_unlock(_ENV_LOCK);
    return Value;
}

/* Replace or remove "Name=" in an environment table, takes the ownership of Entry */
template <typename T>
static
int
_NtCRT_UpdateEnvTable(
    _Inout_ T*** Table,
    _In_ T* Entry,
    _In_ size_t NameLength,
    _In_ bool Remove)
{
    size_t Count = 0, Index = (size_t)-1;
    T** Env = *Table;

    if (Env != NULL)
    {
        for (; Env[Count] != NULL; Count++)
        {
            const T* e = Env[Count];
            size_t i = 0;
            for (; i < NameLength; i++)
            {
                T a = e[i], b = Entry[i];
                if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
                if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
                if (a != b) break;
            }
            if (i == NameLength && e[NameLength] == '=' && Index == (size_t)-1)
            {
                Index = Count;
            }
        }
    }
    if (Index != (size_t)-1)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Env[Index]);
        if (Remove)
        {
            memmove(&Env[Index], &Env[Index + 1], (Count - Index) * sizeof(T*));
            RtlFreeHeap(NTCRT_HEAP, 0, Entry);
        } else
        {
            Env[Index] = Entry;
        }
        return 0;
    }
    if (Remove)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Entry);
        return 0;
    }
    Env = (T**)(Env != NULL ?
                RtlReAllocateHeap(NTCRT_HEAP, 0, Env, (Count + 2) * sizeof(T*)) :
                RtlAllocateHeap(NTCRT_HEAP, 0, 2 * sizeof(T*)));
    if (Env == NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Entry);
        return -1;
    }
    Env[Count] = Entry;
    Env[Count + 1] = NULL;
    *Table = Env;
    return 0;
}

EXTERN_C int __cdecl _NtCRT__wputenv(const wchar_t* envstring)
{
    const wchar_t* Equal;
    size_t NameLength;
    bool Remove;
    wchar_t* Name;
    wchar_t* WideEntry;
    char* AnsiEntry;
    int Result = -1;

    if (envstring == NULL || (Equal = wcschr(envstring, L'=')) == NULL || Equal == envstring)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    NameLength = Equal - envstring;
    Remove = Equal[1] == L'\0';

    _NtCRT_lock(_ENV_LOCK);
    _NtCRT_InitEnviron();
    WideEntry = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, (wcslen(envstring) + 1) * sizeof(wchar_t));
    if (WideEntry == NULL)
    {
        goto _Exit;
    }
    wcscpy(WideEntry, envstring);
    AnsiEntry = _NtCRT_WideToAnsi(envstring);
    if (AnsiEntry == NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, WideEntry);
        goto _Exit;
    }

    /* Update the process environment */
    Name = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, (NameLength + 1) * sizeof(wchar_t));
    if (Name != NULL)
    {
        memcpy(Name, envstring, NameLength * sizeof(wchar_t));
        Name[NameLength] = L'\0';
        _Inline_SetEnvironmentVariableW(Name, Remove ? NULL : Equal + 1);
        RtlFreeHeap(NTCRT_HEAP, 0, Name);
    }

    Result = _NtCRT_UpdateEnvTable<wchar_t>(&_NtCRT_wenviron, WideEntry, NameLength, Remove);
    if (Result == 0)
    {
        const char* AnsiEqual = strchr(AnsiEntry, '=');
        Result = _NtCRT_UpdateEnvTable<char>(&_NtCRT_environ, AnsiEntry, AnsiEqual - AnsiEntry, Remove);
    } else
    {
        RtlFreeHeap(NTCRT_HEAP, 0, AnsiEntry);
    }

_Exit:
    _NtCRT_unlock(_ENV_LOCK);
    if (Result != 0)
    {
        NTCRT_ERRNO = ENOMEM;
    }
    return Result;
}

EXTERN_C int __cdecl _NtCRT__putenv(const char* envstring)
{
    int Result;
    PWSTR Wide;

    if (envstring == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Wide = _NtCRT_AnsiToWide(envstring);
    if (Wide == NULL)
    {
        return -1;
    }
    Result = _NtCRT__wputenv(Wide);
    RtlFreeHeap(NTCRT_HEAP, 0, Wide);
    return Result;
}

/* Initializers and termination */

typedef void(__cdecl* _PVFV)(void);
typedef int(__cdecl* _PIFV)(void);
typedef int(__cdecl* _onexit_t)(void);

EXTERN_C void __cdecl _NtCRT__initterm(_PVFV* pfbegin, _PVFV* pfend)
{
    for (; pfbegin < pfend; pfbegin++)
    {
        if (*pfbegin != NULL)
        {
            (**pfbegin)();
        }
    }
}

EXTERN_C int __cdecl _NtCRT__initterm_e(_PIFV* pfbegin, _PIFV* pfend)
{
    int ret = 0;

    for (; pfbegin < pfend && ret == 0; pfbegin++)
    {
        if (*pfbegin != NULL)
        {
            ret = (**pfbegin)();
        }
    }
    return ret;
}

static _onexit_t* g_OnExitTable = NULL;
static size_t g_OnExitCount = 0;
static size_t g_OnExitCapacity = 0;
static volatile LONG g_Exiting = 0;

EXTERN_C _onexit_t __cdecl _NtCRT__onexit(_onexit_t func)
{
    _onexit_t Result = NULL;

    _NtCRT_lock(_EXIT_LOCK1);
    if (g_OnExitCount == g_OnExitCapacity)
    {
        size_t NewCapacity = g_OnExitCapacity != 0 ? g_OnExitCapacity * 2 : 32;
        _onexit_t* NewTable =
            (_onexit_t*)(g_OnExitTable != NULL
                             ? RtlReAllocateHeap(NTCRT_HEAP, 0, g_OnExitTable, NewCapacity * sizeof(_onexit_t))
                             : RtlAllocateHeap(NTCRT_HEAP, 0, NewCapacity * sizeof(_onexit_t)));
        if (NewTable == NULL)
        {
            goto _Exit;
        }
        g_OnExitTable = NewTable;
        g_OnExitCapacity = NewCapacity;
    }
    g_OnExitTable[g_OnExitCount++] = (_onexit_t)RtlEncodePointer((PVOID)func);
    Result = func;

_Exit:
    _NtCRT_unlock(_EXIT_LOCK1);
    return Result;
}

EXTERN_C int __cdecl _NtCRT_atexit(_PVFV func)
{
    return _NtCRT__onexit((_onexit_t)func) == NULL ? -1 : 0;
}

static
void
_NtCRT_RunOnExit(void)
{
    _NtCRT_lock(_EXIT_LOCK1);
    while (g_OnExitCount != 0)
    {
        _onexit_t Func = (_onexit_t)RtlDecodePointer((PVOID)g_OnExitTable[--g_OnExitCount]);
        if (Func != NULL)
        {
            _NtCRT_unlock(_EXIT_LOCK1);
            Func();
            _NtCRT_lock(_EXIT_LOCK1);
        }
    }
    _NtCRT_unlock(_EXIT_LOCK1);
}

static
void
_NtCRT_DoExit(
    _In_ int code,
    _In_ bool quick,
    _In_ bool retcaller)
{
    UNREFERENCED_PARAMETER(code);
    if (!quick)
    {
        _NtCRT_RunOnExit();
    }
    _NtCRT_FlushAll();
    if (!retcaller)
    {
        RtlExitUserProcess(code);
    }
}

EXTERN_C __attribute__((noreturn)) void __cdecl _NtCRT_exit(int code)
{
    _NtCRT_DoExit(code, false, false);
    __builtin_unreachable();
}

EXTERN_C void __cdecl _NtCRT__cexit(void)
{
    _NtCRT_DoExit(0, false, true);
}

EXTERN_C void __cdecl _NtCRT__c_exit(void)
{
    _NtCRT_DoExit(0, true, true);
}

EXTERN_C __attribute__((noreturn)) void __cdecl _NtCRT__exit(int code)
{
    RtlExitUserProcess(code);
}

static
void
_NtCRT_WriteStderr(
    _In_z_ const char* Message)
{
    _NtCRT_write(2, Message, (unsigned int)strlen(Message));
}

EXTERN_C void __cdecl _NtCRT_amsg_exit(int rterrnum)
{
    char Buffer[64];

    _snprintf(Buffer, sizeof(Buffer), "\r\nruntime error R60%02d\r\n", rterrnum);
    Buffer[sizeof(Buffer) - 1] = '\0';
    _NtCRT_WriteStderr(Buffer);
    _NtCRT__exit(255);
}

/* signal */

typedef void(__cdecl* _NtCRT_sighandler_t)(int);

static _NtCRT_sighandler_t g_SignalHandlers[NSIG];
static bool g_CtrlHandlerInstalled = false;

/* As msvcrt, SIGILL of a privileged instruction has its own action, set by signal and reset by _XcptFilter */
static _NtCRT_sighandler_t g_PrivilegedInstructionHandler;

/* As msvcrt, any console control event other than Ctrl+C raises SIGBREAK */
static
BOOL
WINAPI
_NtCRT_CtrlHandler(
    _In_ DWORD CtrlType)
{
    int Signal = CtrlType == CTRL_C_EVENT ? SIGINT : SIGBREAK;
    _NtCRT_sighandler_t Handler;

    _NtCRT_lock(_SIGNAL_LOCK);
    Handler = g_SignalHandlers[Signal];
    if (Handler != SIG_IGN)
    {
        g_SignalHandlers[Signal] = SIG_DFL;
    }
    _NtCRT_unlock(_SIGNAL_LOCK);
    if (Handler == SIG_DFL)
    {
        return FALSE;
    }
    if (Handler != SIG_IGN)
    {
        Handler(Signal);
    }
    return TRUE;
}

EXTERN_C _NtCRT_sighandler_t __cdecl _NtCRT_signal(int sig, _NtCRT_sighandler_t func)
{
    _NtCRT_sighandler_t Old;

    if (sig <= 0 || sig >= NSIG || func == SIG_ERR ||
        (sig != SIGINT && sig != SIGILL && sig != SIGFPE && sig != SIGSEGV && sig != SIGTERM && sig != SIGBREAK &&
         sig != SIGABRT && sig != SIGABRT_COMPAT))
    {
        NTCRT_ERRNO = EINVAL;
        return SIG_ERR;
    }
    if (sig == SIGABRT_COMPAT)
    {
        sig = SIGABRT;
    }
    _NtCRT_lock(_SIGNAL_LOCK);
    if ((sig == SIGINT || sig == SIGBREAK) && !g_CtrlHandlerInstalled)
    {
        typedef BOOL(WINAPI* PNTCRT_SET_CONSOLE_CTRL_HANDLER)(PHANDLER_ROUTINE, BOOL);
        PNTCRT_SET_CONSOLE_CTRL_HANDLER SetConsoleCtrlHandler =
            (PNTCRT_SET_CONSOLE_CTRL_HANDLER)_NtCRT_GetKernel32Function("SetConsoleCtrlHandler");

        if (SetConsoleCtrlHandler == NULL || !SetConsoleCtrlHandler(_NtCRT_CtrlHandler, TRUE))
        {
            _NtCRT_GetPtd()->Doserrno = SetConsoleCtrlHandler == NULL ? ERROR_PROC_NOT_FOUND : _Inline_GetLastError();
            _NtCRT_unlock(_SIGNAL_LOCK);
            NTCRT_ERRNO = EINVAL;
            return SIG_ERR;
        }
        g_CtrlHandlerInstalled = true;
    }
    Old = g_SignalHandlers[sig];
    g_SignalHandlers[sig] = func;
    if (sig == SIGILL)
    {
        g_PrivilegedInstructionHandler = func;
    }
    _NtCRT_unlock(_SIGNAL_LOCK);
    return Old;
}

/* Call the handler of SIGFPE, SIGILL or SIGSEGV with the exception information of the thread */
static
void
_NtCRT_CallXcptHandler(
    _In_ _NtCRT_sighandler_t Handler,
    _In_ int Signal,
    _In_opt_ void* XcptInfoPtrs,
    _In_ int Fpecode)
{
    PNTCRT_PTD Ptd = _NtCRT_GetPtd();
    void* OldXcptInfoPtrs = Ptd->PxcptInfoPtrs;
    int OldFpecode = Ptd->Fpecode;

    Ptd->PxcptInfoPtrs = XcptInfoPtrs;
    if (Signal == SIGFPE)
    {
        Ptd->Fpecode = Fpecode;
        ((void(__cdecl*)(int, int))(void*)Handler)(SIGFPE, Fpecode);
        Ptd->Fpecode = OldFpecode;
    } else
    {
        Handler(Signal);
    }
    Ptd->PxcptInfoPtrs = OldXcptInfoPtrs;
}

EXTERN_C int __cdecl _NtCRT_raise(int sig)
{
    _NtCRT_sighandler_t Handler;

    if (sig <= 0 || sig >= NSIG)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (sig == SIGABRT_COMPAT)
    {
        sig = SIGABRT;
    }
    _NtCRT_lock(_SIGNAL_LOCK);
    Handler = g_SignalHandlers[sig];
    if (Handler != SIG_IGN && Handler != SIG_DFL)
    {
        g_SignalHandlers[sig] = SIG_DFL;
    }
    _NtCRT_unlock(_SIGNAL_LOCK);
    if (Handler == SIG_IGN)
    {
        return 0;
    }
    if (Handler == SIG_DFL)
    {
        _NtCRT__exit(3);
    }
    if (sig == SIGFPE || sig == SIGILL || sig == SIGSEGV)
    {
        _NtCRT_CallXcptHandler(Handler, sig, NULL, _FPE_EXPLICITGEN);
    } else
    {
        Handler(sig);
    }
    return 0;
}

/* The exceptions that _XcptFilter passes to the handlers of signals */
static const struct
{
    NTSTATUS Status;
    int Signal;
    int Fpecode;
} g_XcptSignals[] = {
    { STATUS_ACCESS_VIOLATION, SIGSEGV, 0 },
    { STATUS_ILLEGAL_INSTRUCTION, SIGILL, 0 },
    { STATUS_PRIVILEGED_INSTRUCTION, SIGILL, 0 },
    { STATUS_FLOAT_DENORMAL_OPERAND, SIGFPE, _FPE_DENORMAL },
    { STATUS_FLOAT_DIVIDE_BY_ZERO, SIGFPE, _FPE_ZERODIVIDE },
    { STATUS_FLOAT_INEXACT_RESULT, SIGFPE, _FPE_INEXACT },
    { STATUS_FLOAT_INVALID_OPERATION, SIGFPE, _FPE_INVALID },
    { STATUS_FLOAT_OVERFLOW, SIGFPE, _FPE_OVERFLOW },
    { STATUS_FLOAT_STACK_CHECK, SIGFPE, _FPE_STACKOVERFLOW },
    { STATUS_FLOAT_UNDERFLOW, SIGFPE, _FPE_UNDERFLOW },
};

/* Other exceptions and SIG_DFL are left to the other exception handlers */
EXTERN_C int __cdecl _NtCRT__XcptFilter(unsigned long xcptnum, PEXCEPTION_POINTERS pxcptinfoptrs)
{
    _NtCRT_sighandler_t Handler = SIG_DFL;
    int Signal = 0, Fpecode = 0;

    for (size_t i = 0; i < ARRAYSIZE(g_XcptSignals); i++)
    {
        if ((unsigned long)g_XcptSignals[i].Status == xcptnum)
        {
            Signal = g_XcptSignals[i].Signal;
            Fpecode = g_XcptSignals[i].Fpecode;
            break;
        }
    }
    if (Signal != 0)
    {
        _NtCRT_sighandler_t* Action = xcptnum == (unsigned long)STATUS_PRIVILEGED_INSTRUCTION ?
                                          &g_PrivilegedInstructionHandler :
                                          &g_SignalHandlers[Signal];

        _NtCRT_lock(_SIGNAL_LOCK);
        Handler = *Action;
        if (Handler != SIG_IGN && Handler != SIG_DFL)
        {
            *Action = SIG_DFL;
        }
        _NtCRT_unlock(_SIGNAL_LOCK);
    }
    if (Handler == SIG_DFL)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (Handler != SIG_IGN)
    {
        _NtCRT_CallXcptHandler(Handler, Signal, pxcptinfoptrs, Fpecode);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

EXTERN_C __attribute__((noreturn)) void __cdecl _NtCRT_abort(void)
{
    if (g_SignalHandlers[SIGABRT] == SIG_DFL || g_SignalHandlers[SIGABRT] == SIG_IGN)
    {
        _NtCRT_WriteStderr("\r\nThis application has requested the Runtime to terminate it in an unusual way.\r\n");
    }
    _NtCRT_raise(SIGABRT);
    _NtCRT__exit(3);
}

typedef void(__cdecl* _NtCRT_invalid_parameter_handler)(
    const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t);
static _NtCRT_invalid_parameter_handler g_InvalidParameterHandler = NULL;

EXTERN_C
_NtCRT_invalid_parameter_handler
__cdecl
_NtCRT__set_invalid_parameter_handler(
    _In_opt_ _NtCRT_invalid_parameter_handler handler)
{
    return (_NtCRT_invalid_parameter_handler)_InterlockedExchangePointer((PVOID*)&g_InvalidParameterHandler,
                                                                         (PVOID)handler);
}

EXTERN_C _NtCRT_invalid_parameter_handler __cdecl _NtCRT__get_invalid_parameter_handler(void)
{
    return g_InvalidParameterHandler;
}

/* The "_s" functions of msvcrt.dll do not call the handler of _set_invalid_parameter_handler */
EXTERN_C int _NtCRT_InvalidParameter(int Errno)
{
    NTCRT_ERRNO = Errno;
    return Errno;
}

EXTERN_C void __cdecl _NtCRT__assert(const char* message, const char* file, unsigned line)
{
    char Buffer[1024];

    _snprintf(Buffer, sizeof(Buffer), "Assertion failed: %s, file %s, line %u\n", message, file, line);
    Buffer[sizeof(Buffer) - 1] = '\0';
    _NtCRT_WriteStderr(Buffer);
    _NtCRT_abort();
}

EXTERN_C void __cdecl _NtCRT__wassert(const wchar_t* message, const wchar_t* file, unsigned line)
{
    char Buffer[1024];

    _snprintf(Buffer, sizeof(Buffer), "Assertion failed: %ls, file %ls, line %u\n", message, file, line);
    Buffer[sizeof(Buffer) - 1] = '\0';
    _NtCRT_WriteStderr(Buffer);
    _NtCRT_abort();
}

typedef int(__cdecl* _NtCRT_matherr_t)(struct _exception*);
static _NtCRT_matherr_t g_UserMathErr = NULL;

EXTERN_C void __cdecl _NtCRT___setusermatherr(_NtCRT_matherr_t func)
{
    g_UserMathErr = func;
}

EXTERN_C void __cdecl _NtCRT__fpreset(void)
{
    __asm__ __volatile__("fninit");
    unsigned int Mxcsr = 0x1F80;
    __asm__ __volatile__("ldmxcsr %0" : : "m"(Mxcsr));
}

/* Threads */

typedef unsigned(__stdcall* _NtCRT_beginthreadex_proc)(void*);
typedef void(__cdecl* _NtCRT_beginthread_proc)(void*);

typedef struct _NTCRT_THREAD_PARAM
{
    _NtCRT_beginthreadex_proc StartEx;
    _NtCRT_beginthread_proc Start;
    void* ArgList;
    HANDLE Handle;
} NTCRT_THREAD_PARAM, *PNTCRT_THREAD_PARAM;

static
NTSTATUS
NTAPI
_NtCRT_ThreadStartEx(
    _In_ PVOID Parameter)
{
    NTCRT_THREAD_PARAM Param = *(PNTCRT_THREAD_PARAM)Parameter;
    unsigned Result = 0;

    RtlFreeHeap(NTCRT_HEAP, 0, Parameter);
    if (Param.StartEx != NULL)
    {
        Result = Param.StartEx(Param.ArgList);
    } else
    {
        Param.Start(Param.ArgList);
    }
    _NtCRT_ThreadDetach();
    RtlExitUserThread((NTSTATUS)Result);
}

static
HANDLE
_NtCRT_CreateThread(
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ unsigned StackSize,
    _In_ PNTCRT_THREAD_PARAM Template,
    _In_ bool Suspended,
    _Out_opt_ unsigned* ThreadId)
{
    NTSTATUS Status;
    HANDLE Handle;
    CLIENT_ID ClientId;
    PNTCRT_THREAD_PARAM Param;

    Param = (PNTCRT_THREAD_PARAM)RtlAllocateHeap(NTCRT_HEAP, 0, sizeof(*Param));
    if (Param == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    *Param = *Template;
    Status = RtlCreateUserThread(NtCurrentProcess(),
                                 SecurityDescriptor,
                                 Suspended,
                                 0,
                                 0,
                                 StackSize,
                                 (PTHREAD_START_ROUTINE)_NtCRT_ThreadStartEx,
                                 Param,
                                 &Handle,
                                 &ClientId);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Param);
        _NtCRT_SetErrnoFromNtStatus(Status);
        return NULL;
    }
    if (ThreadId != NULL)
    {
        *ThreadId = (unsigned)(ULONG_PTR)ClientId.UniqueThread;
    }
    return Handle;
}

EXTERN_C
uintptr_t
__cdecl
_NtCRT__beginthreadex(
    _In_opt_ void* security,
    _In_ unsigned stack_size,
    _In_ _NtCRT_beginthreadex_proc start_address,
    _In_opt_ void* arglist,
    _In_ unsigned initflag,
    _Out_opt_ unsigned* thrdaddr)
{
    NTCRT_THREAD_PARAM Param = { start_address, NULL, arglist, NULL };

    if (start_address == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return 0;
    }
    return (uintptr_t)_NtCRT_CreateThread(security != NULL ? ((LPSECURITY_ATTRIBUTES)security)->lpSecurityDescriptor
                                                           : NULL,
                                          stack_size,
                                          &Param,
                                          (initflag & CREATE_SUSPENDED) != 0,
                                          thrdaddr);
}

EXTERN_C
uintptr_t
__cdecl
_NtCRT__beginthread(
    _In_ _NtCRT_beginthread_proc start_address,
    _In_ unsigned stack_size,
    _In_opt_ void* arglist)
{
    NTCRT_THREAD_PARAM Param = { NULL, start_address, arglist, NULL };
    HANDLE Handle;

    if (start_address == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return (uintptr_t)-1;
    }
    /* _beginthread closes the handle on thread exit in msvcrt, here the caller never owns it */
    Handle = _NtCRT_CreateThread(NULL, stack_size, &Param, false, NULL);
    if (Handle == NULL)
    {
        return (uintptr_t)-1;
    }
    NtClose(Handle);
    return (uintptr_t)Handle;
}

EXTERN_C __attribute__((noreturn)) void __cdecl _NtCRT__endthreadex(unsigned retval)
{
    _NtCRT_ThreadDetach();
    RtlExitUserThread((NTSTATUS)retval);
}

EXTERN_C __attribute__((noreturn)) void __cdecl _NtCRT__endthread(void)
{
    _NtCRT__endthreadex(0);
}

/* The locale information of the "C" locale, see Locale.cpp for setlocale */

static char g_EmptyString[] = "";
static char g_DecimalPoint[] = ".";
static struct lconv g_Lconv;

EXTERN_C struct lconv* __cdecl _NtCRT_localeconv(void)
{
    if (g_Lconv.decimal_point == NULL)
    {
        g_Lconv.thousands_sep = g_Lconv.grouping = g_Lconv.int_curr_symbol = g_Lconv.currency_symbol = g_EmptyString;
        g_Lconv.mon_decimal_point = g_Lconv.mon_thousands_sep = g_Lconv.mon_grouping = g_EmptyString;
        g_Lconv.positive_sign = g_Lconv.negative_sign = g_EmptyString;
        g_Lconv.int_frac_digits = g_Lconv.frac_digits = g_Lconv.p_cs_precedes = g_Lconv.p_sep_by_space = CHAR_MAX;
        g_Lconv.n_cs_precedes = g_Lconv.n_sep_by_space = g_Lconv.p_sign_posn = g_Lconv.n_sign_posn = CHAR_MAX;
        g_Lconv.decimal_point = g_DecimalPoint;
    }
    return &g_Lconv;
}

EXTERN_C int __cdecl _NtCRT___lconv_init(void)
{
    return 0;
}

EXTERN_C int __cdecl _NtCRT__configthreadlocale(int type)
{
    UNREFERENCED_PARAMETER(type);
    return 2; /* _DISABLE_PER_THREAD_LOCALE */
}

/* One-time runtime initialization, also called by DLL entry points through __getmainargs-less paths */

EXTERN_C int __cdecl _NtCRT_InitRuntime(void)
{
    static volatile LONG Initialized = 0;

    if (_InterlockedCompareExchange(&Initialized, 1, 0) == 0)
    {
        _NtCRT_InitVersion();
        _NtCRT_InitProgramName();
        _NtCRT_InitLowIO();
    }
    return 0;
}

/* Run the initialization early, before C++ constructors of user code can use stdio */
static void __cdecl _NtCRT_InitRuntimeEntry(void)
{
    _NtCRT_InitRuntime();
}
extern "C" __attribute__((section(".CRT$XIB"), used)) void(__cdecl* const _NtCRT_InitRuntimeEntryPtr)(void) =
    _NtCRT_InitRuntimeEntry;

NTCRT_IMPORT(_errno, _NtCRT_errno);
NTCRT_IMPORT(__doserrno, _NtCRT___doserrno);
NTCRT_IMPORT(_get_errno, _NtCRT__get_errno);
NTCRT_IMPORT(_set_errno, _NtCRT__set_errno);
NTCRT_IMPORT(_get_doserrno, _NtCRT__get_doserrno);
NTCRT_IMPORT(_lock, _NtCRT_lock);
NTCRT_IMPORT(_unlock, _NtCRT_unlock);
NTCRT_IMPORT(__set_app_type, _NtCRT___set_app_type);
NTCRT_IMPORT_ONLY(_fmode, _NtCRT_fmode);
NTCRT_IMPORT_ONLY(_commode, _NtCRT_commode);
NTCRT_IMPORT_DATA(__argc, _NtCRT_argc);
NTCRT_IMPORT_DATA(__argv, _NtCRT_argv);
NTCRT_IMPORT_DATA(__wargv, _NtCRT_wargv);
NTCRT_IMPORT_DATA(_environ, _NtCRT_environ);
NTCRT_IMPORT_DATA(_wenviron, _NtCRT_wenviron);
NTCRT_IMPORT_DATA(__initenv, _NtCRT_initenv);
NTCRT_IMPORT_DATA(__winitenv, _NtCRT_winitenv);
NTCRT_IMPORT_DATA(_acmdln, _NtCRT_acmdln);
NTCRT_IMPORT_DATA(_wcmdln, _NtCRT_wcmdln);
NTCRT_IMPORT_DATA(_pgmptr, _NtCRT_pgmptr);
NTCRT_IMPORT_DATA(_wpgmptr, _NtCRT_wpgmptr);
NTCRT_IMPORT_DATA(_osver, _NtCRT_osver);
NTCRT_IMPORT_DATA(_winver, _NtCRT_winver);
NTCRT_IMPORT_DATA(_winmajor, _NtCRT_winmajor);
NTCRT_IMPORT_DATA(_winminor, _NtCRT_winminor);
NTCRT_IMPORT_DATA(__mb_cur_max, _NtCRT_mb_cur_max);
NTCRT_IMPORT_DATA(__lc_codepage, _NtCRT_lc_codepage);
NTCRT_IMPORT(__p__fmode, _NtCRT___p__fmode);
NTCRT_IMPORT(__p__commode, _NtCRT___p__commode);
NTCRT_IMPORT(__p___argc, _NtCRT___p___argc);
NTCRT_IMPORT(__p___argv, _NtCRT___p___argv);
NTCRT_IMPORT(__p___wargv, _NtCRT___p___wargv);
NTCRT_IMPORT(__p__environ, _NtCRT___p__environ);
NTCRT_IMPORT(__p__wenviron, _NtCRT___p__wenviron);
NTCRT_IMPORT(__p__acmdln, _NtCRT___p__acmdln);
NTCRT_IMPORT(__p__wcmdln, _NtCRT___p__wcmdln);
NTCRT_IMPORT(__p__pgmptr, _NtCRT___p__pgmptr);
NTCRT_IMPORT(__p__wpgmptr, _NtCRT___p__wpgmptr);
NTCRT_IMPORT(__p___mb_cur_max, _NtCRT___p___mb_cur_max);
NTCRT_IMPORT(___mb_cur_max_func, _NtCRT____mb_cur_max_func);
NTCRT_IMPORT(___lc_codepage_func, _NtCRT____lc_codepage_func);
NTCRT_IMPORT(__getmainargs, _NtCRT___getmainargs);
NTCRT_IMPORT(__wgetmainargs, _NtCRT___wgetmainargs);
NTCRT_IMPORT(getenv, _NtCRT_getenv);
NTCRT_IMPORT(_wgetenv, _NtCRT__wgetenv);
NTCRT_IMPORT(_putenv, _NtCRT__putenv);
NTCRT_IMPORT(putenv, _NtCRT__putenv);
NTCRT_IMPORT(_wputenv, _NtCRT__wputenv);
NTCRT_IMPORT(_initterm, _NtCRT__initterm);
NTCRT_IMPORT(_initterm_e, _NtCRT__initterm_e);
NTCRT_IMPORT(_onexit, _NtCRT__onexit);
NTCRT_IMPORT(__dllonexit, _NtCRT__onexit);
NTCRT_IMPORT_ONLY(atexit, _NtCRT_atexit);
NTCRT_IMPORT(exit, _NtCRT_exit);
NTCRT_IMPORT(_exit, _NtCRT__exit);
NTCRT_IMPORT_ONLY(_Exit, _NtCRT__exit);
NTCRT_IMPORT(_cexit, _NtCRT__cexit);
NTCRT_IMPORT(_c_exit, _NtCRT__c_exit);
NTCRT_IMPORT(_amsg_exit, _NtCRT_amsg_exit);
NTCRT_IMPORT(signal, _NtCRT_signal);
NTCRT_IMPORT(_XcptFilter, _NtCRT__XcptFilter);
NTCRT_IMPORT(raise, _NtCRT_raise);
NTCRT_IMPORT(abort, _NtCRT_abort);
NTCRT_IMPORT(_set_invalid_parameter_handler, _NtCRT__set_invalid_parameter_handler);
NTCRT_IMPORT(_get_invalid_parameter_handler, _NtCRT__get_invalid_parameter_handler);
NTCRT_IMPORT(_assert, _NtCRT__assert);
NTCRT_IMPORT(_wassert, _NtCRT__wassert);
NTCRT_IMPORT(__setusermatherr, _NtCRT___setusermatherr);
NTCRT_IMPORT_ONLY(_fpreset, _NtCRT__fpreset);
NTCRT_IMPORT(_beginthreadex, _NtCRT__beginthreadex);
NTCRT_IMPORT(_beginthread, _NtCRT__beginthread);
NTCRT_IMPORT(_endthreadex, _NtCRT__endthreadex);
NTCRT_IMPORT(_endthread, _NtCRT__endthread);
NTCRT_IMPORT(localeconv, _NtCRT_localeconv);
NTCRT_IMPORT(__lconv_init, _NtCRT___lconv_init);
NTCRT_IMPORT(_configthreadlocale, _NtCRT__configthreadlocale);
