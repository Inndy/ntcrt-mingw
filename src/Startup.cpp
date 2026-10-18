/* msvcrt startup, termination, errno, locks and signal */

#include "NtCRT.h"
#include "Argv.inl"

#include <errno.h>
#include <signal.h>
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

extern "C"
{
    void (*_NtCRT_ThreadExit)(void) = NULL;
    void (*_NtCRT_TestCancel)(void (*Pass)(void* Object), void* Object) = NULL;
    struct _pthread_cleanup** (*_NtCRT_CleanupList)(void) = NULL;
}

EXTERN_C void _NtCRT_ThreadDetach(void)
{
    PNTCRT_PTD Ptd;

    /* The key destructors may use the per-thread data */
    if (_NtCRT_ThreadExit != NULL)
    {
        _NtCRT_ThreadExit();
    }
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
        /* The slot holds the data, an expansion slot has its array */
        if (g_PtdIndex < TLS_MINIMUM_AVAILABLE)
        {
            NtWriteTeb(TlsSlots[g_PtdIndex], NULL);
        } else
        {
            NtReadTeb(TlsExpansionSlots)[g_PtdIndex - TLS_MINIMUM_AVAILABLE] = NULL;
        }
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

EXTERN_C
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

extern "C"
{
    void (*_NtCRT_FlushAllAtExit)(void) = NULL;
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
    if (_NtCRT_FlushAllAtExit != NULL)
    {
        _NtCRT_FlushAllAtExit();
    }
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

/* The messages of the runtime are built without the printf family, which a program may not link otherwise */

EXTERN_C
void
_NtCRT_AppendText(
    _Inout_updates_z_(Size) char* Buffer,
    _In_ size_t Size,
    _In_z_ const char* Text)
{
    size_t Length = strlen(Buffer);

    while (*Text != '\0' && Length + 1 < Size)
    {
        Buffer[Length++] = *Text++;
    }
    Buffer[Length] = '\0';
}

EXTERN_C
void
_NtCRT_AppendNumber(
    _Inout_updates_z_(Size) char* Buffer,
    _In_ size_t Size,
    _In_ unsigned int Value,
    _In_ int MinDigits)
{
    char Digits[16];
    char* p = Digits + sizeof(Digits) - 1;

    *p = '\0';
    do
    {
        *--p = (char)('0' + Value % 10);
        Value /= 10;
    } while (Value != 0 || Digits + sizeof(Digits) - 1 - p < MinDigits);
    _NtCRT_AppendText(Buffer, Size, p);
}

EXTERN_C void __cdecl _NtCRT_amsg_exit(int rterrnum)
{
    char Buffer[64] = "\r\nruntime error R60";

    _NtCRT_AppendNumber(Buffer, sizeof(Buffer), (unsigned int)rterrnum, 2);
    _NtCRT_AppendText(Buffer, sizeof(Buffer), "\r\n");
    _NtCRT_WriteStderrRaw(Buffer);
    _NtCRT__exit(255);
}

/* signal */

_NtCRT_sighandler_t _NtCRT_SignalHandlers[NSIG];
static bool g_CtrlHandlerInstalled = false;

/* As msvcrt, SIGILL of a privileged instruction has its own action, set by signal and reset by _XcptFilter */
_NtCRT_sighandler_t _NtCRT_PrivilegedInstructionHandler;

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
    Handler = _NtCRT_SignalHandlers[Signal];
    if (Handler != SIG_IGN)
    {
        _NtCRT_SignalHandlers[Signal] = SIG_DFL;
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
    Old = _NtCRT_SignalHandlers[sig];
    _NtCRT_SignalHandlers[sig] = func;
    if (sig == SIGILL)
    {
        _NtCRT_PrivilegedInstructionHandler = func;
    }
    _NtCRT_unlock(_SIGNAL_LOCK);
    return Old;
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
NTCRT_IMPORT(_set_invalid_parameter_handler, _NtCRT__set_invalid_parameter_handler);
NTCRT_IMPORT(_get_invalid_parameter_handler, _NtCRT__get_invalid_parameter_handler);
NTCRT_IMPORT(__setusermatherr, _NtCRT___setusermatherr);
NTCRT_IMPORT_ONLY(_fpreset, _NtCRT__fpreset);
