/* kernel32 functions of NtCRT (src/Kernel32.cpp, src/Kernel32Lock.cpp) against kernel32 itself */

#include <KNSoft/NDK/NDK.inl>

#include <stdio.h>
#include <string.h>

typedef DWORD(WINAPI* PFN_GET_MODULE_FILE_NAME_A)(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
typedef DWORD(WINAPI* PFN_GET_MODULE_FILE_NAME_W)(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);

static BOOL g_Ok;

static void Check(bool Ok, const char* What)
{
    if (!Ok)
    {
        printf("FAIL kernel32: %s\n", What);
        g_Ok = FALSE;
    }
}

#define CHECK(Expr) Check(Expr, #Expr)

static PVOID GetKernel32Procedure(_In_z_ PCSTR Name)
{
    ANSI_STRING ProcedureName;
    HMODULE Kernel32 = _Inline_GetModuleHandleW(L"kernel32.dll");
    PVOID Address;

    _Inline_RtlInitAnsiString(&ProcedureName, Name);
    return Kernel32 != NULL && NT_SUCCESS(LdrGetProcedureAddress(Kernel32, &ProcedureName, 0, &Address)) ? Address
                                                                                                           : NULL;
}

/* Truncated to nSize characters with the terminator, nSize 1 too */
static void TestGetModuleFileNameA(void)
{
    PFN_GET_MODULE_FILE_NAME_A System = (PFN_GET_MODULE_FILE_NAME_A)GetKernel32Procedure("GetModuleFileNameA");
    char Expected[MAX_PATH * 4], Actual[MAX_PATH * 4];
    DWORD Length, Sizes[] = { 1, 2, 5, 0, 0 };

    if (System == NULL)
    {
        return;
    }
    Length = GetModuleFileNameA(NULL, Actual, sizeof(Actual));
    CHECK(Length != 0 && Length < sizeof(Actual));
    Sizes[3] = Length;
    Sizes[4] = Length + 1;
    for (DWORD Size : Sizes)
    {
        DWORD ExpectedRet, ActualRet, ExpectedError, ActualError;

        memset(Expected, 'x', sizeof(Expected));
        memset(Actual, 'x', sizeof(Actual));
        SetLastError(0);
        ExpectedRet = System(NULL, Expected, Size);
        ExpectedError = GetLastError();
        SetLastError(0);
        ActualRet = GetModuleFileNameA(NULL, Actual, Size);
        ActualError = GetLastError();
        if (ActualRet != ExpectedRet || ActualError != ExpectedError || memcmp(Actual, Expected, Size + 1) != 0)
        {
            printf("FAIL kernel32: GetModuleFileNameA of %lu: %lu %lu, kernel32 %lu %lu\n", Size, ActualRet,
                   ActualError, ExpectedRet, ExpectedError);
            g_Ok = FALSE;
        }
    }
}

static void TestGetModuleFileNameW(void)
{
    PFN_GET_MODULE_FILE_NAME_W System = (PFN_GET_MODULE_FILE_NAME_W)GetKernel32Procedure("GetModuleFileNameW");
    WCHAR Expected[MAX_PATH], Actual[MAX_PATH];
    DWORD Length, Sizes[] = { 1, 2, 5, 0, 0 };

    if (System == NULL)
    {
        return;
    }
    Length = GetModuleFileNameW(NULL, Actual, ARRAYSIZE(Actual));
    CHECK(Length != 0 && Length + 1 < ARRAYSIZE(Actual));
    Sizes[3] = Length;
    Sizes[4] = Length + 1;
    for (DWORD Size : Sizes)
    {
        DWORD ExpectedRet, ActualRet, ExpectedError, ActualError;

        memset(Expected, 'x', sizeof(Expected));
        memset(Actual, 'x', sizeof(Actual));
        SetLastError(0);
        ExpectedRet = System(NULL, Expected, Size);
        ExpectedError = GetLastError();
        SetLastError(0);
        ActualRet = GetModuleFileNameW(NULL, Actual, Size);
        ActualError = GetLastError();
        if (ActualRet != ExpectedRet || ActualError != ExpectedError ||
            memcmp(Actual, Expected, (Size + 1) * sizeof(WCHAR)) != 0)
        {
            printf("FAIL kernel32: GetModuleFileNameW of %lu: %lu %lu, kernel32 %lu %lu\n", Size, ActualRet,
                   ActualError, ExpectedRet, ExpectedError);
            g_Ok = FALSE;
        }
    }
}

/* An allocated message has at least nSize characters */
static void TestFormatMessage(void)
{
    const DWORD Flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER;
    PWSTR Wide = NULL;
    PSTR Narrow = NULL;

    CHECK(FormatMessageW(Flags, NULL, ERROR_SUCCESS, 0, (LPWSTR)&Wide, 4096, NULL) != 0 &&
          RtlSizeHeap(RtlProcessHeap(), 0, Wide) >= 4096 * sizeof(WCHAR));
    CHECK(FormatMessageA(Flags, NULL, ERROR_SUCCESS, 0, (LPSTR)&Narrow, 4096, NULL) != 0 &&
          RtlSizeHeap(RtlProcessHeap(), 0, Narrow) >= 4096);
    LocalFree(Wide);
    LocalFree(Narrow);
}

static DWORD WINAPI SleepThread(LPVOID Milliseconds)
{
    Sleep((DWORD)(ULONG_PTR)Milliseconds);
    return 0;
}

/* Milliseconds beyond LONG_MAX are a relative time too, the threads still sleep at the exit of the process */
static void TestSleep(void)
{
    HANDLE Threads[] = {
        CreateThread(NULL, 0, SleepThread, (LPVOID)(ULONG_PTR)0x80000000u, 0, NULL),
        CreateThread(NULL, 0, SleepThread, (LPVOID)(ULONG_PTR)0xFFFFFFFEu, 0, NULL),
    };

    CHECK(Threads[0] != NULL && Threads[1] != NULL);
    CHECK(WaitForMultipleObjects(2, Threads, FALSE, 200) == WAIT_TIMEOUT);
    CloseHandle(Threads[0]);
    CloseHandle(Threads[1]);
}

typedef DWORD(WINAPI* PFN_GET_DWORD)(VOID);
typedef HANDLE(WINAPI* PFN_GET_HANDLE)(VOID);

/* The functions inlined by include/ (errhandlingapi.h, processthreadsapi.h), by a pointer of NtCRT and of kernel32 */
static void TestInline(void)
{
    PFN_GET_DWORD Error = (PFN_GET_DWORD)GetKernel32Procedure("GetLastError");
    PFN_GET_DWORD ProcessId = (PFN_GET_DWORD)GetKernel32Procedure("GetCurrentProcessId");
    PFN_GET_DWORD ThreadId = (PFN_GET_DWORD)GetKernel32Procedure("GetCurrentThreadId");
    PFN_GET_HANDLE Process = (PFN_GET_HANDLE)GetKernel32Procedure("GetCurrentProcess");
    PFN_GET_HANDLE Thread = (PFN_GET_HANDLE)GetKernel32Procedure("GetCurrentThread");
    volatile PFN_GET_DWORD OwnError = GetLastError;
    volatile PFN_GET_DWORD OwnThreadId = GetCurrentThreadId;

    CHECK(Error != NULL && ProcessId != NULL && ThreadId != NULL && Process != NULL && Thread != NULL);
    SetLastError(1234);
    CHECK(GetLastError() == 1234 && Error() == 1234 && OwnError() == 1234);
    SetLastError(ERROR_SUCCESS);
    CHECK(GetLastError() == ERROR_SUCCESS && Error() == ERROR_SUCCESS);
    CHECK(GetCurrentProcessId() == ProcessId() && GetCurrentThreadId() == ThreadId() && OwnThreadId() == ThreadId());
    CHECK(GetCurrentProcess() == Process() && GetCurrentThread() == Thread());
}

extern "C"
BOOL
TestKernel32(VOID)
{
    g_Ok = TRUE;
    TestInline();
    TestGetModuleFileNameA();
    TestGetModuleFileNameW();
    TestFormatMessage();
    TestSleep();
    return g_Ok;
}
