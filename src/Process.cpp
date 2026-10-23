/* msvcrt process control: spawn, exec, system, pipes. ntdll cannot create Win32 processes, kernel32 does it */

#include "NtCRT.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

EXTERN_C PUCHAR _NtCRT_BuildInheritBlock(_Out_ PUSHORT Size);
EXTERN_C BOOL WINAPI _K32_GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode);
EXTERN_C BOOL WINAPI _K32_DuplicateHandle(HANDLE hSourceProcessHandle,
                                          HANDLE hSourceHandle,
                                          HANDLE hTargetProcessHandle,
                                          LPHANDLE lpTargetHandle,
                                          DWORD dwDesiredAccess,
                                          BOOL bInheritHandle,
                                          DWORD dwOptions);

typedef BOOL(WINAPI* PNTCRT_CREATE_PROCESS_W)(LPCWSTR,
                                              LPWSTR,
                                              LPSECURITY_ATTRIBUTES,
                                              LPSECURITY_ATTRIBUTES,
                                              BOOL,
                                              DWORD,
                                              LPVOID,
                                              LPCWSTR,
                                              LPSTARTUPINFOW,
                                              LPPROCESS_INFORMATION);
typedef BOOL(WINAPI* PNTCRT_CREATE_PIPE)(PHANDLE, PHANDLE, LPSECURITY_ATTRIBUTES, DWORD);

#define NTCRT_MAX_COMMAND_LINE 32767

static
void
_NtCRT_Free(
    _In_opt_ PVOID Buffer)
{
    if (Buffer != NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Buffer);
    }
}

/* msvcrt joins the arguments with spaces and does not quote them */
static
PWSTR
_NtCRT_JoinArgs(
    _In_ const wchar_t* const* argv)
{
    size_t Length = 0;
    PWSTR CommandLine, p;

    for (size_t i = 0; argv[i] != NULL; i++)
    {
        Length += wcslen(argv[i]) + 1;
    }
    if (Length > NTCRT_MAX_COMMAND_LINE)
    {
        NTCRT_ERRNO = E2BIG;
        return NULL;
    }
    CommandLine = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, (Length + 1) * sizeof(WCHAR));
    if (CommandLine == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    p = CommandLine;
    for (size_t i = 0; argv[i] != NULL; i++)
    {
        size_t n = wcslen(argv[i]);
        if (i > 0)
        {
            *p++ = L' ';
        }
        memcpy(p, argv[i], n * sizeof(WCHAR));
        p += n;
    }
    *p = UNICODE_NULL;
    return CommandLine;
}

static
PWSTR
_NtCRT_BuildEnvironment(
    _In_ const wchar_t* const* envp)
{
    size_t Length = 1;
    PWSTR Block, p;

    for (size_t i = 0; envp[i] != NULL; i++)
    {
        Length += wcslen(envp[i]) + 1;
    }
    if (Length == 1)
    {
        Length = 2;
    }
    Block = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, Length * sizeof(WCHAR));
    if (Block == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    p = Block;
    for (size_t i = 0; envp[i] != NULL; i++)
    {
        size_t n = wcslen(envp[i]) + 1;
        memcpy(p, envp[i], n * sizeof(WCHAR));
        p += n;
    }
    *p++ = UNICODE_NULL;
    if (p == Block + 1)
    {
        *p = UNICODE_NULL;
    }
    return Block;
}

static
BOOL
_NtCRT_IsProgramFile(
    _In_z_ PCWSTR Path)
{
    DWORD Attributes = _K32_GetFileAttributesW(Path);
    return Attributes != INVALID_FILE_ATTRIBUTES && !(Attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static
BOOL
_NtCRT_HasPathSeparator(
    _In_z_ PCWSTR Name)
{
    return wcspbrk(Name, L"\\/") != NULL || (Name[0] != UNICODE_NULL && Name[1] == L':');
}

/*
 * Find the program as msvcrt does: use the name as is if it has an extension,
 * otherwise try ".com", ".exe", ".bat" and ".cmd" appended.
 */
static
PWSTR
_NtCRT_ProbeProgram(
    _In_reads_opt_(DirLength) PCWSTR Dir,
    _In_ size_t DirLength,
    _In_z_ PCWSTR Name)
{
    static const WCHAR Extensions[][5] = { L".com", L".exe", L".bat", L".cmd" };
    size_t NameLength = wcslen(Name), Length;
    PCWSTR Base, Dot;
    PWSTR Path;

    Path = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, (DirLength + 1 + NameLength + 5) * sizeof(WCHAR));
    if (Path == NULL)
    {
        return NULL;
    }
    Length = 0;
    if (DirLength > 0)
    {
        memcpy(Path, Dir, DirLength * sizeof(WCHAR));
        Length = DirLength;
        if (Path[Length - 1] != L'\\' && Path[Length - 1] != L'/')
        {
            Path[Length++] = L'\\';
        }
    }
    memcpy(Path + Length, Name, (NameLength + 1) * sizeof(WCHAR));
    Length += NameLength;

    Base = Name;
    for (PCWSTR p = Name; *p != UNICODE_NULL; p++)
    {
        if (*p == L'\\' || *p == L'/' || *p == L':')
        {
            Base = p + 1;
        }
    }
    Dot = wcsrchr(Base, L'.');
    if (Dot != NULL)
    {
        if (_NtCRT_IsProgramFile(Path))
        {
            return Path;
        }
    } else
    {
        for (const auto& Extension : Extensions)
        {
            memcpy(Path + Length, Extension, sizeof(Extension));
            if (_NtCRT_IsProgramFile(Path))
            {
                return Path;
            }
        }
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Path);
    return NULL;
}

/* Find the program, search directories in PATH if requested and the name has no path */
static
PWSTR
_NtCRT_FindProgram(
    _In_z_ PCWSTR Name,
    _In_ BOOL SearchPath)
{
    PWSTR Path = _NtCRT_ProbeProgram(NULL, 0, Name);
    PCWSTR Env, p;

    if (Path != NULL || !SearchPath || _NtCRT_HasPathSeparator(Name))
    {
        return Path;
    }
    Env = _wgetenv(L"PATH");
    if (Env == NULL)
    {
        return NULL;
    }
    for (p = Env; *p != UNICODE_NULL;)
    {
        PCWSTR Begin = p, End;

        while (*p != UNICODE_NULL && *p != L';')
        {
            p++;
        }
        End = p;
        if (*p == L';')
        {
            p++;
        }
        if (End - Begin >= 2 && *Begin == L'"' && End[-1] == L'"')
        {
            Begin++;
            End--;
        }
        if (End > Begin)
        {
            Path = _NtCRT_ProbeProgram(Begin, End - Begin, Name);
            if (Path != NULL)
            {
                return Path;
            }
        }
    }
    return NULL;
}

static
int
_NtCRT_QueryExitCode(
    _In_ HANDLE Process)
{
    DWORD ExitCode = 0;

    _K32_GetExitCodeProcess(Process, &ExitCode);
    return (int)ExitCode;
}

/* Create the process, StdHandles redirects standard handles instead of passing the fd table */
static
intptr_t
_NtCRT_CreateProcess(
    _In_ int Mode,
    _In_z_ PCWSTR Program,
    _In_z_ PWSTR CommandLine,
    _In_opt_ PWSTR Environment,
    _In_reads_opt_(3) const HANDLE* StdHandles)
{
    auto const CreateProcessW = (PNTCRT_CREATE_PROCESS_W)_NtCRT_GetKernel32Function(NTCRT_K32_CreateProcessW);
    STARTUPINFOW StartupInfo = {};
    PROCESS_INFORMATION ProcessInfo;
    PUCHAR InheritBlock = NULL;
    USHORT InheritSize;
    DWORD Flags = CREATE_UNICODE_ENVIRONMENT;
    BOOL Success;

    StartupInfo.cb = sizeof(StartupInfo);

    if (CreateProcessW == NULL)
    {
        NTCRT_ERRNO = ENOEXEC;
        return -1;
    }
    if (StdHandles != NULL)
    {
        StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        StartupInfo.hStdInput = StdHandles[0];
        StartupInfo.hStdOutput = StdHandles[1];
        StartupInfo.hStdError = StdHandles[2];
    } else
    {
        InheritBlock = _NtCRT_BuildInheritBlock(&InheritSize);
        if (InheritBlock == NULL)
        {
            NTCRT_ERRNO = ENOMEM;
            return -1;
        }
        StartupInfo.cbReserved2 = InheritSize;
        StartupInfo.lpReserved2 = InheritBlock;
    }
    if (Mode == _P_DETACH)
    {
        Flags |= DETACHED_PROCESS;
    }
    Success =
        CreateProcessW(Program, CommandLine, NULL, NULL, TRUE, Flags, Environment, NULL, &StartupInfo, &ProcessInfo);
    _NtCRT_Free(InheritBlock);
    if (!Success)
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return -1;
    }
    NtClose(ProcessInfo.hThread);

    switch (Mode)
    {
        case _P_OVERLAY:
            _exit(0);
        case _P_WAIT:
        {
            int ExitCode;
            NtWaitForSingleObject(ProcessInfo.hProcess, FALSE, NULL);
            ExitCode = _NtCRT_QueryExitCode(ProcessInfo.hProcess);
            NtClose(ProcessInfo.hProcess);
            return ExitCode;
        }
        case _P_DETACH:
            NtClose(ProcessInfo.hProcess);
            return 0;
        default:
            return (intptr_t)ProcessInfo.hProcess;
    }
}

static
intptr_t
_NtCRT_SpawnW(
    _In_ int Mode,
    _In_z_ const wchar_t* Name,
    _In_ const wchar_t* const* argv,
    _In_opt_ const wchar_t* const* envp,
    _In_ BOOL SearchPath,
    _In_reads_opt_(3) const HANDLE* StdHandles)
{
    PWSTR Program, CommandLine, Environment = NULL;
    intptr_t Result = -1;

    if (Name == NULL || *Name == UNICODE_NULL || argv == NULL || argv[0] == NULL || *argv[0] == UNICODE_NULL ||
        Mode < _P_WAIT || Mode > _P_DETACH)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    CommandLine = _NtCRT_JoinArgs(argv);
    if (CommandLine == NULL)
    {
        return -1;
    }
    if (envp != NULL)
    {
        Environment = _NtCRT_BuildEnvironment(envp);
        if (Environment == NULL)
        {
            goto _Exit;
        }
    }
    Program = _NtCRT_FindProgram(Name, SearchPath);
    if (Program == NULL)
    {
        NTCRT_ERRNO = ENOENT;
        goto _Exit;
    }
    Result = _NtCRT_CreateProcess(Mode, Program, CommandLine, Environment, StdHandles);
    RtlFreeHeap(NTCRT_HEAP, 0, Program);
_Exit:
    _NtCRT_Free(Environment);
    RtlFreeHeap(NTCRT_HEAP, 0, CommandLine);
    return Result;
}

/* Narrow strings to wide */

static
void
_NtCRT_FreeWideArray(
    _In_opt_ wchar_t** Array)
{
    if (Array != NULL)
    {
        for (size_t i = 0; Array[i] != NULL; i++)
        {
            RtlFreeHeap(NTCRT_HEAP, 0, Array[i]);
        }
        RtlFreeHeap(NTCRT_HEAP, 0, Array);
    }
}

static
wchar_t**
_NtCRT_WideArray(
    _In_ const char* const* Array)
{
    size_t Count = 0;
    wchar_t** Result;

    while (Array[Count] != NULL)
    {
        Count++;
    }
    Result = (wchar_t**)RtlAllocateHeap(NTCRT_HEAP, HEAP_ZERO_MEMORY, (Count + 1) * sizeof(wchar_t*));
    if (Result == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    for (size_t i = 0; i < Count; i++)
    {
        Result[i] = _NtCRT_FileToWide(Array[i]);
        if (Result[i] == NULL)
        {
            _NtCRT_FreeWideArray(Result);
            return NULL;
        }
    }
    return Result;
}

static
intptr_t
_NtCRT_SpawnA(
    _In_ int Mode,
    _In_z_ const char* Name,
    _In_ const char* const* argv,
    _In_opt_ const char* const* envp,
    _In_ BOOL SearchPath)
{
    PWSTR WideName;
    wchar_t **WideArgv, **WideEnvp = NULL;
    intptr_t Result = -1;

    if (Name == NULL || argv == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    /* A conversion sets errno (EILSEQ, ENOMEM), the first that fails ends the call */
    WideName = _NtCRT_FileToWide(Name);
    WideArgv = WideName != NULL ? _NtCRT_WideArray(argv) : NULL;
    if (envp != NULL && WideArgv != NULL)
    {
        WideEnvp = _NtCRT_WideArray(envp);
    }
    if (WideArgv != NULL && (envp == NULL || WideEnvp != NULL))
    {
        Result = _NtCRT_SpawnW(Mode, WideName, WideArgv, WideEnvp, SearchPath, NULL);
    }
    _NtCRT_FreeWideArray(WideEnvp);
    _NtCRT_FreeWideArray(WideArgv);
    _NtCRT_Free(WideName);
    return Result;
}

/* Variadic argument lists end with NULL, followed by envp for the "e" variants */
template <typename T>
static
const T**
_NtCRT_CollectArgs(
    _In_opt_ const T* arg0,
    _In_ va_list ap,
    _Out_opt_ const T* const** envp)
{
    va_list Copy;
    size_t Count = 0;
    const T** Argv;

    if (arg0 != NULL)
    {
        va_copy(Copy, ap);
        Count = 1;
        while (va_arg(Copy, const T*) != NULL)
        {
            Count++;
        }
        va_end(Copy);
    }
    Argv = (const T**)RtlAllocateHeap(NTCRT_HEAP, 0, (Count + 1) * sizeof(T*));
    if (Argv == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    if (Count > 0)
    {
        Argv[0] = arg0;
        for (size_t i = 1; i < Count; i++)
        {
            Argv[i] = va_arg(ap, const T*);
        }
        (void)va_arg(ap, const T*);
    }
    Argv[Count] = NULL;
    if (envp != NULL)
    {
        *envp = va_arg(ap, const T* const*);
    }
    return Argv;
}

#define NTCRT_SPAWN_LIST(Mode, Name, arg0, Search, HasEnv, T, Spawn) \
    do \
    { \
        va_list ap; \
        const T* const* envp = NULL; \
        const T** argv; \
        intptr_t Result = -1; \
        va_start(ap, arg0); \
        argv = _NtCRT_CollectArgs<T>(arg0, ap, HasEnv ? &envp : NULL); \
        va_end(ap); \
        if (argv != NULL) \
        { \
            Result = Spawn; \
            RtlFreeHeap(NTCRT_HEAP, 0, argv); \
        } \
        return Result; \
    } while (0)

#define NTCRT_SPAWN_A(Mode, Name, Search) _NtCRT_SpawnA(Mode, Name, argv, envp, Search)
#define NTCRT_SPAWN_W(Mode, Name, Search) _NtCRT_SpawnW(Mode, Name, argv, envp, Search, NULL)

/* _spawn* */

EXTERN_C intptr_t __cdecl _NtCRT__spawnv(int mode, const char* filename, const char* const* argv)
{
    return _NtCRT_SpawnA(mode, filename, argv, NULL, FALSE);
}

EXTERN_C intptr_t __cdecl _NtCRT__spawnve(int mode,
                                          const char* filename,
                                          const char* const* argv,
                                          const char* const* envp)
{
    return _NtCRT_SpawnA(mode, filename, argv, envp, FALSE);
}

EXTERN_C intptr_t __cdecl _NtCRT__spawnvp(int mode, const char* filename, const char* const* argv)
{
    return _NtCRT_SpawnA(mode, filename, argv, NULL, TRUE);
}

EXTERN_C intptr_t __cdecl _NtCRT__spawnvpe(int mode,
                                           const char* filename,
                                           const char* const* argv,
                                           const char* const* envp)
{
    return _NtCRT_SpawnA(mode, filename, argv, envp, TRUE);
}

EXTERN_C intptr_t __cdecl _NtCRT__spawnl(int mode, const char* filename, const char* arg0, ...)
{
    NTCRT_SPAWN_LIST(mode, filename, arg0, FALSE, FALSE, char, NTCRT_SPAWN_A(mode, filename, FALSE));
}

EXTERN_C intptr_t __cdecl _NtCRT__spawnle(int mode, const char* filename, const char* arg0, ...)
{
    NTCRT_SPAWN_LIST(mode, filename, arg0, FALSE, TRUE, char, NTCRT_SPAWN_A(mode, filename, FALSE));
}

EXTERN_C intptr_t __cdecl _NtCRT__spawnlp(int mode, const char* filename, const char* arg0, ...)
{
    NTCRT_SPAWN_LIST(mode, filename, arg0, TRUE, FALSE, char, NTCRT_SPAWN_A(mode, filename, TRUE));
}

EXTERN_C intptr_t __cdecl _NtCRT__spawnlpe(int mode, const char* filename, const char* arg0, ...)
{
    NTCRT_SPAWN_LIST(mode, filename, arg0, TRUE, TRUE, char, NTCRT_SPAWN_A(mode, filename, TRUE));
}

EXTERN_C intptr_t __cdecl _NtCRT__wspawnv(int mode, const wchar_t* filename, const wchar_t* const* argv)
{
    return _NtCRT_SpawnW(mode, filename, argv, NULL, FALSE, NULL);
}

EXTERN_C intptr_t __cdecl _NtCRT__wspawnve(int mode,
                                           const wchar_t* filename,
                                           const wchar_t* const* argv,
                                           const wchar_t* const* envp)
{
    return _NtCRT_SpawnW(mode, filename, argv, envp, FALSE, NULL);
}

EXTERN_C intptr_t __cdecl _NtCRT__wspawnvp(int mode, const wchar_t* filename, const wchar_t* const* argv)
{
    return _NtCRT_SpawnW(mode, filename, argv, NULL, TRUE, NULL);
}

EXTERN_C intptr_t __cdecl _NtCRT__wspawnvpe(int mode,
                                            const wchar_t* filename,
                                            const wchar_t* const* argv,
                                            const wchar_t* const* envp)
{
    return _NtCRT_SpawnW(mode, filename, argv, envp, TRUE, NULL);
}

EXTERN_C intptr_t __cdecl _NtCRT__wspawnl(int mode, const wchar_t* filename, const wchar_t* arg0, ...)
{
    NTCRT_SPAWN_LIST(mode, filename, arg0, FALSE, FALSE, wchar_t, NTCRT_SPAWN_W(mode, filename, FALSE));
}

EXTERN_C intptr_t __cdecl _NtCRT__wspawnle(int mode, const wchar_t* filename, const wchar_t* arg0, ...)
{
    NTCRT_SPAWN_LIST(mode, filename, arg0, FALSE, TRUE, wchar_t, NTCRT_SPAWN_W(mode, filename, FALSE));
}

EXTERN_C intptr_t __cdecl _NtCRT__wspawnlp(int mode, const wchar_t* filename, const wchar_t* arg0, ...)
{
    NTCRT_SPAWN_LIST(mode, filename, arg0, TRUE, FALSE, wchar_t, NTCRT_SPAWN_W(mode, filename, TRUE));
}

EXTERN_C intptr_t __cdecl _NtCRT__wspawnlpe(int mode, const wchar_t* filename, const wchar_t* arg0, ...)
{
    NTCRT_SPAWN_LIST(mode, filename, arg0, TRUE, TRUE, wchar_t, NTCRT_SPAWN_W(mode, filename, TRUE));
}

/* _exec*, return only on failure */

EXTERN_C intptr_t __cdecl _NtCRT__execv(const char* filename, const char* const* argv)
{
    return _NtCRT_SpawnA(_P_OVERLAY, filename, argv, NULL, FALSE);
}

EXTERN_C intptr_t __cdecl _NtCRT__execve(const char* filename, const char* const* argv, const char* const* envp)
{
    return _NtCRT_SpawnA(_P_OVERLAY, filename, argv, envp, FALSE);
}

EXTERN_C intptr_t __cdecl _NtCRT__execvp(const char* filename, const char* const* argv)
{
    return _NtCRT_SpawnA(_P_OVERLAY, filename, argv, NULL, TRUE);
}

EXTERN_C intptr_t __cdecl _NtCRT__execvpe(const char* filename, const char* const* argv, const char* const* envp)
{
    return _NtCRT_SpawnA(_P_OVERLAY, filename, argv, envp, TRUE);
}

EXTERN_C intptr_t __cdecl _NtCRT__execl(const char* filename, const char* arg0, ...)
{
    NTCRT_SPAWN_LIST(_P_OVERLAY, filename, arg0, FALSE, FALSE, char, NTCRT_SPAWN_A(_P_OVERLAY, filename, FALSE));
}

EXTERN_C intptr_t __cdecl _NtCRT__execle(const char* filename, const char* arg0, ...)
{
    NTCRT_SPAWN_LIST(_P_OVERLAY, filename, arg0, FALSE, TRUE, char, NTCRT_SPAWN_A(_P_OVERLAY, filename, FALSE));
}

EXTERN_C intptr_t __cdecl _NtCRT__execlp(const char* filename, const char* arg0, ...)
{
    NTCRT_SPAWN_LIST(_P_OVERLAY, filename, arg0, TRUE, FALSE, char, NTCRT_SPAWN_A(_P_OVERLAY, filename, TRUE));
}

EXTERN_C intptr_t __cdecl _NtCRT__execlpe(const char* filename, const char* arg0, ...)
{
    NTCRT_SPAWN_LIST(_P_OVERLAY, filename, arg0, TRUE, TRUE, char, NTCRT_SPAWN_A(_P_OVERLAY, filename, TRUE));
}

EXTERN_C intptr_t __cdecl _NtCRT__wexecv(const wchar_t* filename, const wchar_t* const* argv)
{
    return _NtCRT_SpawnW(_P_OVERLAY, filename, argv, NULL, FALSE, NULL);
}

EXTERN_C intptr_t __cdecl _NtCRT__wexecve(const wchar_t* filename,
                                          const wchar_t* const* argv,
                                          const wchar_t* const* envp)
{
    return _NtCRT_SpawnW(_P_OVERLAY, filename, argv, envp, FALSE, NULL);
}

EXTERN_C intptr_t __cdecl _NtCRT__wexecvp(const wchar_t* filename, const wchar_t* const* argv)
{
    return _NtCRT_SpawnW(_P_OVERLAY, filename, argv, NULL, TRUE, NULL);
}

EXTERN_C intptr_t __cdecl _NtCRT__wexecvpe(const wchar_t* filename,
                                           const wchar_t* const* argv,
                                           const wchar_t* const* envp)
{
    return _NtCRT_SpawnW(_P_OVERLAY, filename, argv, envp, TRUE, NULL);
}

EXTERN_C intptr_t __cdecl _NtCRT__wexecl(const wchar_t* filename, const wchar_t* arg0, ...)
{
    NTCRT_SPAWN_LIST(_P_OVERLAY, filename, arg0, FALSE, FALSE, wchar_t, NTCRT_SPAWN_W(_P_OVERLAY, filename, FALSE));
}

EXTERN_C intptr_t __cdecl _NtCRT__wexecle(const wchar_t* filename, const wchar_t* arg0, ...)
{
    NTCRT_SPAWN_LIST(_P_OVERLAY, filename, arg0, FALSE, TRUE, wchar_t, NTCRT_SPAWN_W(_P_OVERLAY, filename, FALSE));
}

EXTERN_C intptr_t __cdecl _NtCRT__wexeclp(const wchar_t* filename, const wchar_t* arg0, ...)
{
    NTCRT_SPAWN_LIST(_P_OVERLAY, filename, arg0, TRUE, FALSE, wchar_t, NTCRT_SPAWN_W(_P_OVERLAY, filename, TRUE));
}

EXTERN_C intptr_t __cdecl _NtCRT__wexeclpe(const wchar_t* filename, const wchar_t* arg0, ...)
{
    NTCRT_SPAWN_LIST(_P_OVERLAY, filename, arg0, TRUE, TRUE, wchar_t, NTCRT_SPAWN_W(_P_OVERLAY, filename, TRUE));
}

EXTERN_C intptr_t __cdecl _NtCRT__cwait(int* termstat, intptr_t procHandle, int action)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(action);
    Status = NtWaitForSingleObject((HANDLE)procHandle, FALSE, NULL);
    if (Status != STATUS_WAIT_0)
    {
        NTCRT_ERRNO = Status == STATUS_INVALID_HANDLE ? ECHILD : EINVAL;
        _NtCRT_GetPtd()->Doserrno = RtlNtStatusToDosError(Status);
        return -1;
    }
    if (termstat != NULL)
    {
        *termstat = _NtCRT_QueryExitCode((HANDLE)procHandle);
    }
    NtClose((HANDLE)procHandle);
    return procHandle;
}

/* Command processor: COMSPEC, or cmd.exe in PATH */

static
PWSTR
_NtCRT_FindShell(void)
{
    PCWSTR Comspec = _wgetenv(L"COMSPEC");
    PWSTR Path;

    if (Comspec != NULL)
    {
        Path = _NtCRT_ProbeProgram(NULL, 0, Comspec);
        if (Path != NULL)
        {
            return Path;
        }
    }
    return _NtCRT_FindProgram(L"cmd.exe", TRUE);
}

EXTERN_C int __cdecl _NtCRT__wsystem(const wchar_t* command)
{
    PWSTR Shell = _NtCRT_FindShell();
    const wchar_t* argv[4];
    intptr_t Result;

    if (command == NULL)
    {
        _NtCRT_Free(Shell);
        return Shell != NULL;
    }
    if (Shell == NULL)
    {
        NTCRT_ERRNO = ENOENT;
        return -1;
    }
    argv[0] = Shell;
    argv[1] = L"/c";
    argv[2] = command;
    argv[3] = NULL;
    Result = _NtCRT_SpawnW(_P_WAIT, Shell, argv, NULL, FALSE, NULL);
    RtlFreeHeap(NTCRT_HEAP, 0, Shell);
    return (int)Result;
}

EXTERN_C int __cdecl _NtCRT_system(const char* command)
{
    PWSTR WideCommand;
    int Result;

    if (command == NULL)
    {
        return _NtCRT__wsystem(NULL);
    }
    WideCommand = _NtCRT_FileToWide(command);
    if (WideCommand == NULL)
    {
        return -1;
    }
    Result = _NtCRT__wsystem(WideCommand);
    RtlFreeHeap(NTCRT_HEAP, 0, WideCommand);
    return Result;
}

/* Pipes */

static
BOOL
_NtCRT_CreatePipe(
    _Out_ PHANDLE ReadPipe,
    _Out_ PHANDLE WritePipe,
    _In_ BOOL Inherit,
    _In_ DWORD Size)
{
    auto const CreatePipe = (PNTCRT_CREATE_PIPE)_NtCRT_GetKernel32Function(NTCRT_K32_CreatePipe);
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, Inherit };

    if (CreatePipe == NULL)
    {
        NTCRT_ERRNO = EMFILE;
        return FALSE;
    }
    if (!CreatePipe(ReadPipe, WritePipe, &Attributes, Size))
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return FALSE;
    }
    return TRUE;
}

static
int
_NtCRT_TextModeFlag(
    _In_ int mode)
{
    if (mode & _O_BINARY)
    {
        return _O_BINARY;
    }
    if (mode & _O_TEXT)
    {
        return _O_TEXT;
    }
    return _NtCRT_fmode == _O_BINARY ? _O_BINARY : _O_TEXT;
}

EXTERN_C int __cdecl _NtCRT__pipe(int* pfds, unsigned int psize, int textmode)
{
    HANDLE ReadPipe, WritePipe;
    int Flags;

    if (pfds == NULL || (textmode & ~(_O_BINARY | _O_TEXT | _O_NOINHERIT)) != 0 ||
        (textmode & (_O_BINARY | _O_TEXT)) == (_O_BINARY | _O_TEXT))
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (!_NtCRT_CreatePipe(&ReadPipe, &WritePipe, !(textmode & _O_NOINHERIT), psize))
    {
        return -1;
    }
    Flags = _NtCRT_TextModeFlag(textmode) | (textmode & _O_NOINHERIT);
    pfds[0] = _NtCRT_open_handle(ReadPipe, _O_RDONLY | Flags);
    if (pfds[0] == -1)
    {
        NtClose(ReadPipe);
        NtClose(WritePipe);
        return -1;
    }
    pfds[1] = _NtCRT_open_handle(WritePipe, _O_WRONLY | Flags);
    if (pfds[1] == -1)
    {
        _NtCRT_close(pfds[0]);
        NtClose(WritePipe);
        return -1;
    }
    return 0;
}

typedef struct _NTCRT_POPEN_ENTRY
{
    struct _NTCRT_POPEN_ENTRY* Next;
    FILE* Stream;
    HANDLE Process;
} NTCRT_POPEN_ENTRY, *PNTCRT_POPEN_ENTRY;

static PNTCRT_POPEN_ENTRY g_PopenList;
static RTL_SRWLOCK g_PopenLock = RTL_SRWLOCK_INIT;

/* Duplicate the handle of a standard fd for the child, NULL if there is none */
static
HANDLE
_NtCRT_InheritableStdHandle(
    _In_ int fd)
{
    HANDLE Handle = (HANDLE)_NtCRT_get_osfhandle(fd), Duplicate;

    if (Handle == INVALID_HANDLE_VALUE || Handle == NULL || Handle == (HANDLE)(LONG_PTR)-2 ||
        !_K32_DuplicateHandle(
            NtCurrentProcess(), Handle, NtCurrentProcess(), &Duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS))
    {
        return NULL;
    }
    return Duplicate;
}

static
FILE*
_NtCRT_PopenW(
    _In_z_ const wchar_t* command,
    _In_z_ const wchar_t* mode)
{
    BOOL Read;
    int TextMode = 0, ParentFd;
    HANDLE ReadPipe, WritePipe, ParentEnd, ChildEnd, StdHandles[3];
    PWSTR Shell;
    const wchar_t* argv[4];
    intptr_t Process;
    PNTCRT_POPEN_ENTRY Entry;
    FILE* Stream;

    if (command == NULL || mode == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    while (*mode == L' ')
    {
        mode++;
    }
    if (*mode != L'r' && *mode != L'w')
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    Read = *mode++ == L'r';
    while (*mode == L' ')
    {
        mode++;
    }
    if (*mode == L't')
    {
        TextMode = _O_TEXT;
        mode++;
    } else if (*mode == L'b')
    {
        TextMode = _O_BINARY;
        mode++;
    }
    while (*mode == L' ')
    {
        mode++;
    }
    if (*mode != UNICODE_NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }

    Entry = (PNTCRT_POPEN_ENTRY)RtlAllocateHeap(NTCRT_HEAP, 0, sizeof(*Entry));
    if (Entry == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return NULL;
    }
    Shell = _NtCRT_FindShell();
    if (Shell == NULL)
    {
        NTCRT_ERRNO = ENOENT;
        RtlFreeHeap(NTCRT_HEAP, 0, Entry);
        return NULL;
    }
    if (!_NtCRT_CreatePipe(&ReadPipe, &WritePipe, FALSE, 1024))
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Shell);
        RtlFreeHeap(NTCRT_HEAP, 0, Entry);
        return NULL;
    }
    ParentEnd = Read ? ReadPipe : WritePipe;
    ChildEnd = Read ? WritePipe : ReadPipe;
    _K32_SetHandleInformation(ChildEnd, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    StdHandles[0] = Read ? _NtCRT_InheritableStdHandle(0) : ChildEnd;
    StdHandles[1] = Read ? ChildEnd : _NtCRT_InheritableStdHandle(1);
    StdHandles[2] = _NtCRT_InheritableStdHandle(2);
    argv[0] = Shell;
    argv[1] = L"/c";
    argv[2] = command;
    argv[3] = NULL;
    Process = _NtCRT_SpawnW(_P_NOWAIT, Shell, argv, NULL, FALSE, StdHandles);
    for (int i = 0; i < 3; i++)
    {
        if (StdHandles[i] != NULL)
        {
            NtClose(StdHandles[i]);
        }
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Shell);
    if (Process == -1)
    {
        NtClose(ParentEnd);
        RtlFreeHeap(NTCRT_HEAP, 0, Entry);
        return NULL;
    }

    ParentFd =
        _NtCRT_open_handle(ParentEnd, (Read ? _O_RDONLY : _O_WRONLY) | _NtCRT_TextModeFlag(TextMode) | _O_NOINHERIT);
    if (ParentFd == -1)
    {
        NtClose(ParentEnd);
        NtClose((HANDLE)Process);
        RtlFreeHeap(NTCRT_HEAP, 0, Entry);
        return NULL;
    }
    Stream = _fdopen(ParentFd, Read ? (TextMode == _O_BINARY ? "rb" : "rt") : (TextMode == _O_BINARY ? "wb" : "wt"));
    if (Stream == NULL)
    {
        _NtCRT_close(ParentFd);
        NtClose((HANDLE)Process);
        RtlFreeHeap(NTCRT_HEAP, 0, Entry);
        return NULL;
    }
    Entry->Stream = Stream;
    Entry->Process = (HANDLE)Process;
    RtlAcquireSRWLockExclusive(&g_PopenLock);
    Entry->Next = g_PopenList;
    g_PopenList = Entry;
    RtlReleaseSRWLockExclusive(&g_PopenLock);
    return Stream;
}

EXTERN_C FILE* __cdecl _NtCRT__wpopen(const wchar_t* command, const wchar_t* mode)
{
    return _NtCRT_PopenW(command, mode);
}

EXTERN_C FILE* __cdecl _NtCRT__popen(const char* command, const char* mode)
{
    PWSTR WideCommand, WideMode;
    FILE* Stream = NULL;

    if (command == NULL || mode == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    /* A conversion sets errno (EILSEQ, ENOMEM) */
    WideCommand = _NtCRT_FileToWide(command);
    WideMode = WideCommand != NULL ? _NtCRT_FileToWide(mode) : NULL;
    if (WideMode != NULL)
    {
        Stream = _NtCRT_PopenW(WideCommand, WideMode);
    }
    _NtCRT_Free(WideMode);
    _NtCRT_Free(WideCommand);
    return Stream;
}

EXTERN_C int __cdecl _NtCRT__pclose(FILE* stream)
{
    PNTCRT_POPEN_ENTRY Entry, *Link;
    int Result;

    RtlAcquireSRWLockExclusive(&g_PopenLock);
    for (Link = &g_PopenList; (Entry = *Link) != NULL; Link = &Entry->Next)
    {
        if (Entry->Stream == stream)
        {
            *Link = Entry->Next;
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&g_PopenLock);
    if (Entry == NULL)
    {
        /* msvcrt: EBADF for a stream that _popen did not open */
        NTCRT_ERRNO = EBADF;
        return -1;
    }
    fclose(stream);
    if (_NtCRT__cwait(&Result, (intptr_t)Entry->Process, 0) == -1)
    {
        Result = -1;
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Entry);
    return Result;
}

NTCRT_IMPORT(_spawnv, _NtCRT__spawnv);
NTCRT_IMPORT(_spawnve, _NtCRT__spawnve);
NTCRT_IMPORT(_spawnvp, _NtCRT__spawnvp);
NTCRT_IMPORT(_spawnvpe, _NtCRT__spawnvpe);
NTCRT_IMPORT(_spawnl, _NtCRT__spawnl);
NTCRT_IMPORT(_spawnle, _NtCRT__spawnle);
NTCRT_IMPORT(_spawnlp, _NtCRT__spawnlp);
NTCRT_IMPORT(_spawnlpe, _NtCRT__spawnlpe);
NTCRT_IMPORT(_wspawnv, _NtCRT__wspawnv);
NTCRT_IMPORT(_wspawnve, _NtCRT__wspawnve);
NTCRT_IMPORT(_wspawnvp, _NtCRT__wspawnvp);
NTCRT_IMPORT(_wspawnvpe, _NtCRT__wspawnvpe);
NTCRT_IMPORT(_wspawnl, _NtCRT__wspawnl);
NTCRT_IMPORT(_wspawnle, _NtCRT__wspawnle);
NTCRT_IMPORT(_wspawnlp, _NtCRT__wspawnlp);
NTCRT_IMPORT(_wspawnlpe, _NtCRT__wspawnlpe);
NTCRT_IMPORT(_execv, _NtCRT__execv);
NTCRT_IMPORT(_execve, _NtCRT__execve);
NTCRT_IMPORT(_execvp, _NtCRT__execvp);
NTCRT_IMPORT(_execvpe, _NtCRT__execvpe);
NTCRT_IMPORT(_execl, _NtCRT__execl);
NTCRT_IMPORT(_execle, _NtCRT__execle);
NTCRT_IMPORT(_execlp, _NtCRT__execlp);
NTCRT_IMPORT(_execlpe, _NtCRT__execlpe);
NTCRT_IMPORT(_wexecv, _NtCRT__wexecv);
NTCRT_IMPORT(_wexecve, _NtCRT__wexecve);
NTCRT_IMPORT(_wexecvp, _NtCRT__wexecvp);
NTCRT_IMPORT(_wexecvpe, _NtCRT__wexecvpe);
NTCRT_IMPORT(_wexecl, _NtCRT__wexecl);
NTCRT_IMPORT(_wexecle, _NtCRT__wexecle);
NTCRT_IMPORT(_wexeclp, _NtCRT__wexeclp);
NTCRT_IMPORT(_wexeclpe, _NtCRT__wexeclpe);
NTCRT_IMPORT(_cwait, _NtCRT__cwait);
NTCRT_IMPORT(system, _NtCRT_system);
NTCRT_IMPORT(_wsystem, _NtCRT__wsystem);
NTCRT_IMPORT(_pipe, _NtCRT__pipe);
NTCRT_IMPORT(_popen, _NtCRT__popen);
NTCRT_IMPORT(_wpopen, _NtCRT__wpopen);
NTCRT_IMPORT(_pclose, _NtCRT__pclose);

/* POSIX names */
NTCRT_IMPORT(spawnv, _NtCRT__spawnv);
NTCRT_IMPORT(spawnve, _NtCRT__spawnve);
NTCRT_IMPORT(spawnvp, _NtCRT__spawnvp);
NTCRT_IMPORT(spawnvpe, _NtCRT__spawnvpe);
NTCRT_IMPORT(spawnl, _NtCRT__spawnl);
NTCRT_IMPORT(spawnle, _NtCRT__spawnle);
NTCRT_IMPORT(spawnlp, _NtCRT__spawnlp);
NTCRT_IMPORT(spawnlpe, _NtCRT__spawnlpe);
NTCRT_IMPORT(execv, _NtCRT__execv);
NTCRT_IMPORT(execve, _NtCRT__execve);
NTCRT_IMPORT(execvp, _NtCRT__execvp);
NTCRT_IMPORT(execvpe, _NtCRT__execvpe);
NTCRT_IMPORT(execl, _NtCRT__execl);
NTCRT_IMPORT(execle, _NtCRT__execle);
NTCRT_IMPORT(execlp, _NtCRT__execlp);
NTCRT_IMPORT(execlpe, _NtCRT__execlpe);
NTCRT_IMPORT(cwait, _NtCRT__cwait);
NTCRT_IMPORT(popen, _NtCRT__popen);
NTCRT_IMPORT(wpopen, _NtCRT__wpopen);
NTCRT_IMPORT(pclose, _NtCRT__pclose);
