/*
 * KNSoft.NtCRT for MinGW-w64, internal header
 *
 * MinGW-w64 runtime (crt2.o, libmingw32, libmingwex, libgcc, libstdc++, libwinpthread) and user objects import
 * functions and variables from kernel32.dll and msvcrt.dll. This library provides them on top of ntdll.dll,
 * see README.md for how it is linked.
 */

#pragma once

#define _CRT_NTDDI_MIN NTDDI_VISTA
#define NT_VERSION_MIN NT_VERSION_VISTA

#include <KNSoft/NDK/NDK.inl>

#include <stdarg.h>

/*
 * Define an import that MinGW-w64 objects can link against, both "Name" (direct call) and "__imp_Name"
 * (pointer used by __declspec(dllimport) references) resolve to the extern "C" function "Impl".
 * The Makefile collects these lines and emits an object for each name, so a name is only linked while it is
 * still undefined, and kernel32 or msvcrt linked explicitly by the user takes precedence instead of clashing.
 */
#ifndef NTCRT_TCC
#define NTCRT_IMPORT(Name, Impl) static_assert(sizeof(&Impl) != 0, #Name)

/* Only "__imp_Name", for a name that the MinGW-w64 runtime defines itself, collected as above */
#define NTCRT_IMPORT_ONLY(Name, Impl) static_assert(sizeof(&Impl) != 0, #Name)
#else
/*
 * tcc links whole objects and reads no "__imp_" of a function: "Name" is a weak alias of "Impl" in its object (which
 * must define it), a program may define its own. Exports.awk keeps "_imp__Name" of the variables.
 */
#define NTCRT_IMPORT(Name, Impl)                \
    static_assert(sizeof(&Impl) != 0, #Name);   \
    __asm__(".ifnc " #Name "," #Impl "\n\t"     \
            ".weak " #Name "\n\t"               \
            ".set " #Name ", " #Impl "\n\t"     \
            ".endif")
#define NTCRT_IMPORT_ONLY NTCRT_IMPORT
#endif

/* The prefix of the assembler names of C names ("_" on x86), the alignment and the directive of a pointer */
#define NTCRT_STRINGIZE_(x) #x
#define NTCRT_STRINGIZE(x) NTCRT_STRINGIZE_(x)
#define NTCRT_ASM_PREFIX NTCRT_STRINGIZE(__USER_LABEL_PREFIX__)
#ifdef _WIN64
#define NTCRT_ASM_PTR_ALIGN ".p2align 3\n"
#define NTCRT_ASM_PTR ".quad "
#else
#define NTCRT_ASM_PTR_ALIGN ".p2align 2\n"
#define NTCRT_ASM_PTR ".long "
#endif

/*
 * Name, an alias of X86 or X64 (defined in the same file) by the architecture: the function of a name of msvcrt whose
 * time_t or file size depends on it, time is _time32 on x86 and _time64 on x64. NTCRT_IMPORT takes Name.
 */
#define NTCRT_ARCH_ALIAS(Name, X86, X64)              \
    EXTERN_C __typeof__(NTCRT_X86_X64(X86, X64)) Name \
        __attribute__((nothrow, alias(NTCRT_STRINGIZE(NTCRT_X86_X64(X86, X64)))))

/*
 * NTCRT_TCC: the ELF objects of the library for tcc (make TCC=1), see README.md. tcc reads the variables of msvcrt by
 * "_imp__Name", or by "Name" as an import slot where its headers declare them __declspec(dllimport)
 * (NTCRT_IMPORT_DATA_SLOT). It sorts no section by "$": the C initializers are in .init_array, which the startup of tcc
 * runs after __getmainargs, and the program has no TLS callback.
 */
#ifdef NTCRT_TCC
#define NTCRT_IMPORT_DATA(Name, Impl) \
    __asm__(".globl " #Name "\n\t" \
            ".set " #Name ", " #Impl "\n\t" \
            ".pushsection .data\n\t" \
            ".globl _imp__" #Name "\n\t" \
            ".p2align 3\n" \
            "_imp__" #Name ":\n\t" \
            ".quad " #Impl "\n\t" \
            ".popsection")
#define NTCRT_IMPORT_DATA_SLOT(Name, Impl) \
    __asm__(".pushsection .data\n\t" \
            ".globl " #Name "\n\t" \
            ".p2align 3\n" \
            #Name ":\n\t" \
            ".quad " #Impl "\n\t" \
            ".popsection")
#define NTCRT_INITIALIZER(Section, Name, Function) \
    static __attribute__((section(".init_array"), used)) int(__cdecl* Name)(void) = Function
#else
/* A variable cannot be forwarded by a jump, "Name" and "__imp_Name" are defined in the object of "Impl" */
#define NTCRT_IMPORT_DATA(Name, Impl) \
    __asm__(".globl " NTCRT_ASM_PREFIX #Name "\n\t" \
            ".set " NTCRT_ASM_PREFIX #Name ", " NTCRT_ASM_PREFIX #Impl "\n\t" \
            ".section .rdata$__imp_" #Name ",\"dr\"\n\t" \
            ".linkonce discard\n\t" \
            ".globl __imp_" NTCRT_ASM_PREFIX #Name "\n\t" \
            NTCRT_ASM_PTR_ALIGN \
            "__imp_" NTCRT_ASM_PREFIX #Name ":\n\t" \
            NTCRT_ASM_PTR NTCRT_ASM_PREFIX #Impl "\n\t" \
            ".text")
#define NTCRT_IMPORT_DATA_SLOT NTCRT_IMPORT_DATA
/* A C initializer, run by the startup of MinGW-w64 in the order of Section (".CRT$XIB" to ".CRT$XIZ") */
#define NTCRT_INITIALIZER(Section, Name, Function) \
    extern "C" __attribute__((section(Section), used)) int(__cdecl* const Name)(void) = Function
#endif

#define NTCRT_API extern "C" __attribute__((used))

/* Thread exit notification, called by the TLS callback with DLL_THREAD_DETACH / DLL_PROCESS_DETACH */
extern "C" void _NtCRT_ThreadDetach(void);

/* The end of a thread NtCRT started, under tcc (no TLS callback) after the end of its per-thread data */
static inline __attribute__((noreturn)) void _NtCRT_ExitUserThread(NTSTATUS Status)
{
#ifdef NTCRT_TCC
    _NtCRT_ThreadDetach();
#endif
    RtlExitUserThread(Status);
}

/* The DLL_PROCESS_ATTACH of the TLS callbacks that Windows 7 may not send, see Startup.cpp */
extern "C" void _NtCRT_ProcessAttach(void);

/* Per-thread CRT data */
typedef struct _NTCRT_PTD
{
    int Errno;
    unsigned long Doserrno;
    unsigned int RandSeed; /* seed ^ 1, so zeroed memory means srand(1) */
    char* StrtokContext;
    wchar_t* WcstokContext;
    unsigned char* MbstokContext;
    struct tm* TmBuffer;
    char* AsctimeBuffer;
    wchar_t* WcserrorBuffer;
    char* CvtBuffer;
    int Fpecode;
    void* PxcptInfoPtrs;
    RTL_HEAP_WALK_ENTRY HeapWalkEntry; /* Last entry returned by _heapwalk */
    unsigned int LocaleCodePage;       /* LC_CTYPE code page + 1 of the "_l" function running, 0 if none */
    char StrerrorBuffer[128];
    struct _NTCRT_PARKER* Parker; /* The event of the waits (PthreadWait.cpp), a reference */
    HANDLE ThreadHandle;          /* Of a thread of _beginthread, which _endthread closes */
#ifndef _WIN64
    struct
    {
        void* Mutex;
        ULONG Levels;
    } MutexLevels[4]; /* Of recursive mutexes the thread holds beyond the count in their word (PthreadMutex.cpp) */
#endif
} NTCRT_PTD, *PNTCRT_PTD;

/* _NtCRT_FallbackPtd is shared by the threads that could not get their own, _NtCRT_PtdSlot is its TLS slot */
extern "C" PNTCRT_PTD _NtCRT_GetPtd(void);
extern "C" NTCRT_PTD _NtCRT_FallbackPtd;
extern "C" DWORD _NtCRT_PtdSlot(void);

/*
 * The thread functions (Pthread*.cpp), set when a program uses them so that the others do not link them:
 * _NtCRT_ThreadExit is _NtCRT_KeyExit, which runs the destructors of pthread keys (_NtCRT_KeyDestructors, which
 * pthread_exit runs first) and then _NtCRT_PthreadExit, the end of the pthread_t of the thread, at the exit of a
 * thread. Once a thread was cancelled, _NtCRT_TestCancel ends a cancelled thread at a cancellation point (Pass passes
 * on a wake that the thread may have taken from others) and _NtCRT_CancelRequested tells whether the thread has a
 * cancellation pending that acts. _NtCRT_CleanupList is the cleanup handlers of the thread, which it gives a
 * pthread_t, NULL if out of memory. _NtCRT_DelayHook is _NtCRT_Delay, the sleep that pthread_cancel can end, set at
 * startup. _NtCRT_WaitSlot, set at startup too, is where a cancellable wait of the thread publishes its address for
 * pthread_cancel (NULL for a thread without a pthread_t). _NtCRT_ParkerExit is _NtCRT_ParkerRelease, of the parker in
 * the per-thread data at the exit of a thread.
 */
extern "C" void (*_NtCRT_ThreadExit)(void);
extern "C" void (*_NtCRT_TestCancel)(void (*Pass)(void* Object), void* Object);
extern "C" bool (*_NtCRT_CancelRequested)(void);
extern "C" struct _pthread_cleanup** (*_NtCRT_CleanupList)(void);
extern "C" NTSTATUS (*_NtCRT_DelayHook)(PLARGE_INTEGER Timeout);
extern "C" const volatile void* volatile* (*_NtCRT_WaitSlot)(void);
extern "C" void (*_NtCRT_ParkerExit)(struct _NTCRT_PARKER* Parker);
extern "C" void (*_NtCRT_PthreadExit)(void);
extern "C" void _NtCRT_KeyExit(void);
extern "C" void _NtCRT_KeyDestructors(void);

/* A cancellation point: _NtCRT_TestCancel once a thread was cancelled, which pthread_cancel sets in any thread */
static inline void _NtCRT_CancelPoint(void (*Pass)(void* Object), void* Object)
{
    void (*Test)(void (*)(void*), void*) = __atomic_load_n(&_NtCRT_TestCancel, __ATOMIC_ACQUIRE);

    if (Test != NULL)
    {
        Test(Pass, Object);
    }
}

/*
 * Wait while the Size bytes (4 or 8) at Address are Value, until _NtCRT_Wake of the address or the timeout. Returns
 * STATUS_SUCCESS (woken, or the value differs), STATUS_TIMEOUT, or with NTCRT_WAIT_CANCEL (a cancellation point)
 * STATUS_CANCELLED when pthread_cancel requested the thread to end. The waker changes the value before the wake. No
 * wait is alertable.
 */
#define NTCRT_WAIT_CANCEL 1
extern "C" NTSTATUS _NtCRT_Wait(const volatile void* Address, ULONG64 Value, ULONG Size, PLARGE_INTEGER Timeout,
                                ULONG Flags);
extern "C" void _NtCRT_Wake(const volatile void* Address, BOOL All);

/*
 * _NtCRT_Waiting: whether a thread is queued on Address (parked or about to). _NtCRT_Backoff: a round of a wait for
 * another thread that runs, pause, then yield, then sleep 1 ms (Rounds from 0).
 */
extern "C" bool _NtCRT_Waiting(const volatile void* Address);
extern "C" void _NtCRT_Backoff(ULONG* Rounds);

/*
 * The rounds of YieldProcessor (pause) that a thread spends on a lock, a post or a signal before it parks, while no
 * thread is parked on it: a park and an unpark cost about 4 us, 100 rounds 1.4 us on Zen 5 (3.5 us where a pause
 * takes 140 cycles, as on Intel since Skylake). None when the affinity of the process has one processor (at the
 * first spin), where the thread waited for cannot run meanwhile. _NtCRT_Parked tells whether a thread may be parked
 * on Address (or another address of its bucket).
 */
extern "C" int _NtCRT_Spins(void);
extern "C" bool _NtCRT_Parked(const volatile void* Address);

/*
 * The parker of a thread (PthreadWait.cpp): NtWaitForAlertByThreadId (NTCRT_PARKER_ALERT, Windows 8 and later) or an
 * event (NTCRT_PARKER_EVENT), _NtCRT_ParkerMode once chosen. An event has references: _NtCRT_ParkerCreate makes one
 * with Refs (true and NULL without events, false if out of resources), _NtCRT_ParkerSelf adds one to that of the
 * thread (NULL without), _NtCRT_ParkerRelease drops one. The per-thread data holds the event of a thread.
 * _NtCRT_CancelWait ends the cancellable wait of a thread (its ID) on an address. _NtCRT_Delay parks the thread
 * until Timeout: STATUS_SUCCESS when it passed, STATUS_ALERTED if pthread_cancel ended it (or spuriously).
 */
#define NTCRT_PARKER_ALERT 1
#define NTCRT_PARKER_EVENT 2
typedef struct _NTCRT_PARKER
{
    volatile LONG Refs;
    HANDLE Event;
} NTCRT_PARKER, *PNTCRT_PARKER;
extern "C" LONG _NtCRT_ParkerMode;
extern "C" bool _NtCRT_ParkerCreate(PNTCRT_PARKER* Parker, LONG Refs);
extern "C" PNTCRT_PARKER _NtCRT_ParkerSelf(void);
extern "C" void _NtCRT_ParkerRelease(PNTCRT_PARKER Parker);
extern "C" void _NtCRT_CancelWait(DWORD Thread, const volatile void* Address);
extern "C" NTSTATUS _NtCRT_Delay(PLARGE_INTEGER Timeout);

/*
 * Times of the thread functions in 100 ns units (PthreadTime.cpp). _NtCRT_Timeout makes the NT timeout of Units, a
 * time of CLOCK_REALTIME or CLOCK_MONOTONIC since its epoch or a relative time (Clock -1), NULL for none
 * (NTCRT_TIME_NEVER). Units of a timespec are rounded up.
 */
#define NTCRT_TIME_NEVER MAXLONGLONG
#define NTCRT_TIMESPEC_VALID(Time) ((unsigned long)(Time)->tv_nsec < 1000000000UL)
extern "C" LONGLONG _NtCRT_MonotonicTime(void);
extern "C" LONGLONG _NtCRT_TimespecUnits(const struct timespec* Time);
extern "C" int __cdecl _NtCRT_nanosleep(const struct timespec* request, struct timespec* remain);
extern "C" PLARGE_INTEGER _NtCRT_Timeout(LONGLONG Units, int Clock, PLARGE_INTEGER Timeout);

/* pthread_create with a start function of C11 (thrd_create) if StartC11 */
extern "C" int _NtCRT_ThreadCreate(uintptr_t* Thread, const void* Attr, void* (*Start)(void*), int (*StartC11)(void*),
                                   void* Arg);

/* The mutex of a condition wait, unlocked whatever its recursion count, which Relock restores */
extern "C" int _NtCRT_MutexUnlockAll(intptr_t* Mutex, LONG64* Count);
extern "C" void _NtCRT_MutexRelock(intptr_t* Mutex, LONG64 Count);

/* errno helpers */
extern "C" int* _NtCRT_errno(void);
extern "C" void _NtCRT_SetErrnoFromNtStatus(NTSTATUS Status);
extern "C" void _NtCRT_SetErrnoFromWin32(ULONG Win32Error);
extern "C" int _NtCRT_Win32ToErrno(ULONG Win32Error);
#define NTCRT_ERRNO (*_NtCRT_errno())

/* Report an invalid parameter of a "_s" function like msvcrt.dll: set errno and return it */
extern "C" int _NtCRT_InvalidParameter(int Errno);

/* Heap */
#define NTCRT_HEAP RtlProcessHeap()
/* malloc, calloc, free and realloc of msvcrt, see Heap.cpp */
extern "C" void* __cdecl _NtCRT_malloc(size_t size);
extern "C" void* __cdecl _NtCRT_calloc(size_t count, size_t size);
extern "C" void __cdecl _NtCRT_free(void* block);
extern "C" void* __cdecl _NtCRT_realloc(void* block, size_t size);

/* The ANSI code page of the process */
extern "C" UINT _NtCRT_CodePage(void);

/*
 * Locale, see Locale.cpp and CodePage.cpp. A category is either the "C" locale or "C.UTF-8", the "C" locale with
 * UTF-8 multibyte characters. The LC_CTYPE code page is 0 for "C", where a byte is the character of the same code
 * point, and CP_UTF8 for "C.UTF-8".
 */
extern "C" UINT _NtCRT_CurrentCodePage(void);

/* The locale information of msvcrt that _locale_t points to, which the headers of mingw-w64 13 no longer define */
#ifndef _THREADLOCALEINFO
#define _THREADLOCALEINFO
typedef struct threadlocaleinfostruct
{
    int refcount;
    unsigned int lc_codepage;
    unsigned int lc_collate_cp;
    unsigned long lc_handle[6];
    LC_ID lc_id[6];
    struct
    {
        char* locale;
        wchar_t* wlocale;
        int* refcount;
        int* wrefcount;
    } lc_category[6];
    int lc_clike;
    int mb_cur_max;
    int* lconv_intl_refcount;
    int* lconv_num_refcount;
    int* lconv_mon_refcount;
    struct lconv* lconv;
    int* ctype1_refcount;
    unsigned short* ctype1;
    const unsigned short* pctype;
    const unsigned char* pclmap;
    const unsigned char* pcumap;
    struct __lc_time_data* lc_time_curr;
} threadlocinfo;
#endif
extern "C" UINT _NtCRT_LocaleCodePage(_locale_t Locale);
/* Code page of narrow file names and other strings exchanged with the system: CP_UTF8 or the ANSI code page */
extern "C" UINT _NtCRT_FileCodePage(void);
/* Count the multibyte characters of the locale in at most MaxBytes bytes, (size_t)-1 with EILSEQ if invalid */
extern "C" size_t _NtCRT_MbsLength(const char* Str, size_t MaxBytes);
/* For LocaleObject.cpp: parse a locale name into the "C.UTF-8" categories, the name of a category or of all, the
   ctype table of "C.UTF-8" and the categories of the current locale */
extern "C" BOOL _NtCRT_ParseLocale(int Category, const char* Name, BOOL* Utf8);
extern "C" char* _NtCRT_LocaleName(int Category, const BOOL* Utf8, char* Buffer);
extern "C" unsigned short* _NtCRT_CtypeUtf8(void);
extern "C" void _NtCRT_GetLocaleUtf8(BOOL* Utf8);

/* Float.cpp: the decimal digits of Mant * 2**Exp for %e (Mode 'e') or %f ('f') of printf */
#define NTCRT_FLOAT_DIGITS 800
#define NTCRT_FLOAT_DIGITS_LONG 12000
extern "C" int _NtCRT_FloatDigits(ULONG64 Mant, int Exp, char Mode, int Precision, char* Digits, int Size, int* Point);

/* _ctype, the character classes of "C" (Ctype.cpp) */
struct NTCRT_CTYPE_TABLE
{
    unsigned short Table[257];
};
extern "C" NTCRT_CTYPE_TABLE _NtCRT_ctype;

/* Run the body of a "_l" function by the LC_CTYPE code page of the locale, the current locale if NULL */
class NTCRT_LOCALE_SCOPE
{
public:
    NTCRT_LOCALE_SCOPE(_locale_t Locale);
    ~NTCRT_LOCALE_SCOPE();

private:
    PNTCRT_PTD m_Ptd;
    unsigned int m_Saved;
};

/* Kernel32 replacements used internally */
extern "C" int WINAPI _NtCRT_MultiByteToWideChar(
    UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar);
extern "C" int WINAPI _NtCRT_WideCharToMultiByte(UINT CodePage,
                                                 DWORD dwFlags,
                                                 LPCWCH lpWideCharStr,
                                                 int cchWideChar,
                                                 LPSTR lpMultiByteStr,
                                                 int cbMultiByte,
                                                 LPCCH lpDefaultChar,
                                                 LPBOOL lpUsedDefaultChar);
extern "C" DWORD WINAPI _CRT_GetFileType(HANDLE hFile);
extern "C" BOOL WINAPI _CRT_GetConsoleMode(HANDLE hConsoleHandle, LPDWORD lpMode);
extern "C" UINT WINAPI _CRT_GetConsoleOutputCP(VOID);
extern "C" NTSTATUS WINAPI _CRT_ConsoleApiCall(HANDLE ConsoleHandle,
                                               PCONSOLE_MSG_L1 Message,
                                               ULONG MessageSize,
                                               ULONG InputCount,
                                               const VOID* Data,
                                               ULONG DataSize,
                                               ULONG ApiDescriptorSize);
extern "C" BOOL WINAPI _NtCRT_WriteConsoleW(HANDLE hConsoleOutput,
                                            const VOID* lpBuffer,
                                            DWORD nNumberOfCharsToWrite,
                                            LPDWORD lpNumberOfCharsWritten);

/* Allocate a NUL-terminated wide string converted from a narrow string in ANSI code page, free by RtlFreeHeap */
extern "C" PWSTR _NtCRT_AnsiToWide(const char* Str);
/* Allocate a NUL-terminated narrow string converted from a wide string in ANSI code page, free by RtlFreeHeap */
extern "C" char* _NtCRT_WideToAnsi(const wchar_t* Str);
/* As _NtCRT_AnsiToWide and _NtCRT_WideToAnsi in the file code page, for file names and commands of the CRT */
extern "C" PWSTR _NtCRT_FileToWide(const char* Str);
extern "C" char* _NtCRT_WideToFile(const wchar_t* Str);

/* NtOpenFile of a DOS path, see FilePath.cpp */
extern "C" NTSTATUS _NtCRT_OpenPath(
    _Out_ PHANDLE FileHandle,
    _In_ PCWSTR FileName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG ShareAccess,
    _In_ ULONG OpenOptions);
/* Delete a file (Options FILE_NON_DIRECTORY_FILE) or a directory (FILE_DIRECTORY_FILE), FALSE with the last error */
extern "C" BOOL _NtCRT_DeletePath(_In_ PCWSTR Path, _In_ ULONG Options);

/* Open a file by DOS path with NtCreateFile, returns Win32 error */
extern "C" ULONG _NtCRT_CreateFile(
    _Out_ PHANDLE FileHandle,
    _In_ PCWSTR FileName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG ShareAccess,
    _In_ ULONG CreateDisposition,
    _In_ ULONG CreateOptions,
    _In_ ULONG FileAttributes,
    _Out_opt_ PULONG_PTR Information);

/* Lock helpers shared by stdio and low-level I/O */
#define NTCRT_LOCK_TABLE_SIZE 36
extern "C" void __cdecl _NtCRT_lock(int locknum);
extern "C" void __cdecl _NtCRT_unlock(int locknum);

/* Low-level I/O, see LowIO.cpp */
extern "C" int __cdecl _NtCRT_read(int fd, void* buffer, unsigned int count);
extern "C" int __cdecl _NtCRT_write(int fd, const void* buffer, unsigned int count);
extern "C" int __cdecl _NtCRT_close(int fd);
extern "C" long long __cdecl _NtCRT_lseeki64(int fd, long long offset, int origin);
extern "C" int __cdecl _NtCRT_chsize_s(int fd, long long size);
extern "C" int __cdecl _NtCRT_open_handle(HANDLE Handle, int flags);
extern "C" int __cdecl _NtCRT_wsopen(const wchar_t* path, int oflag, int shflag, int pmode);
/* The permissions _umask takes from new files, see LowIOOpen.cpp */
extern "C" int _NtCRT_Umask;
extern "C" intptr_t __cdecl _NtCRT_get_osfhandle(int fd);
extern "C" void __cdecl _NtCRT_WriteStderrRaw(const char* Text);
extern "C" int __cdecl _NtCRT_isatty(int fd);
/* Results of _NtCRT_fd_textmode */
#define NTCRT_FD_BINARY 0
#define NTCRT_FD_ANSI   1
#define NTCRT_FD_UTF8   2
#define NTCRT_FD_UTF16  3
extern "C" int __cdecl _NtCRT_fd_textmode(int fd);
extern "C" int __cdecl _NtCRT_fd_narrow_invalid(int fd);
extern "C" int __cdecl _NtCRT_fd_crlf(int fd);
extern "C" void _NtCRT_InitLowIO(void);
extern "C" BOOL _NtCRT_ConsoleWriteW(_In_reads_(Count) const wchar_t* Buffer, _In_ DWORD Count);

/* stdio, see Stdio.cpp */

/*
 * printf (Printf.inl, Printf.cpp and PrintfWide.cpp). _NtCRT_FormatString is snprintf of C (the terminator within
 * Count, returns the whole length), _NtCRT_FormatLegacy _vsnprintf of msvcrt (no terminator if the output fills
 * Count, -1 if it does not fit). They return -1 with errno for an encoding error (EILSEQ) or a length beyond
 * INT_MAX (EOVERFLOW).
 */
#define NTCRT_PRINTF_LEGACY_WIDE 0x1 /* %s and %c of a wide format are wide, %S and %C narrow (msvcrt) */
#define NTCRT_PRINTF_LONG_DOUBLE 0x2 /* %Lf is the 80-bit long double of GCC (__mingw_*), else a double (MSVC) */
extern "C" int _NtCRT_FormatString(char* Buffer, size_t Count, const char* Format, va_list ap, ULONG Options);
extern "C" int _NtCRT_FormatStringW(wchar_t* Buffer, size_t Count, const wchar_t* Format, va_list ap, ULONG Options);
extern "C" int _NtCRT_FormatLegacy(char* Buffer, size_t Count, const char* Format, va_list ap, ULONG Options);
extern "C" int _NtCRT_FormatLegacyW(wchar_t* Buffer, size_t Count, const wchar_t* Format, va_list ap, ULONG Options);
extern "C" int _NtCRT_FormatAlloc(char** Result, const char* Format, va_list ap, ULONG Options);
extern "C" int _NtCRT_FormatStream(FILE* Stream, const char* Format, va_list ap, ULONG Options);
extern "C" int _NtCRT_FormatStreamW(FILE* Stream, const wchar_t* Format, va_list ap, ULONG Options);

/*
 * swprintf and vswprintf of C, which mingw-w64 13 and later declare (the name of msvcrt and those of __ms_ and
 * __mingw_): the size of the buffer, -1 if the output does not fit. Before 13 they take no size, as msvcrt.
 */
#define NTCRT_SWPRINTF_ISO (__MINGW64_VERSION_MAJOR >= 13)
extern "C" int _NtCRT_FormatSwprintf(wchar_t* Buffer, size_t Count, const wchar_t* Format, va_list ap, ULONG Options);
extern "C" int _NtCRT_FormatGrowW(wchar_t** Buffer, size_t Count, const wchar_t* Format, va_list ap, ULONG Options);

/* Streams of Stdio.cpp used by StdioPrintf.cpp, StdioScanf.cpp and StdioWscanf.cpp */
extern "C" FILE _NtCRT_iob[];
extern "C" void __cdecl _NtCRT_lock_file(FILE* stream);
extern "C" void __cdecl _NtCRT_unlock_file(FILE* stream);
extern "C" int __cdecl _NtCRT__fflush_nolock(FILE* stream);
extern "C" int _NtCRT_FlushPut(const char* Bytes, int Length, FILE* stream);
extern "C" wint_t _NtCRT_putwc_nolock(wchar_t Ch, FILE* Stream);
extern "C" int _NtCRT_StreamWrite(FILE* stream, const char* Bytes, size_t Length);
extern "C" wint_t __cdecl _NtCRT__fgetwc_nolock(FILE* stream);
extern "C" wint_t __cdecl _NtCRT__ungetwc_nolock(wint_t ch, FILE* stream);

/*
 * scanf (Scanf.inl, Scanf.cpp and ScanfWide.cpp). The input is a string of at most Left characters up to a null
 * character, or a stream locked by the caller, read by Get (EOF at its end) and pushed back by Unget, one character
 * at most. Count is the characters read. The functions return the count of assignments, EOF for an input failure
 * before the first conversion.
 */
#define NTCRT_SCANF_LEGACY_WIDE 0x1 /* %s, %c and %[ of a wide format are wide, %S and %C narrow (msvcrt) */
#define NTCRT_SCANF_SECURE      0x2 /* The size of the array follows the argument of %c, %s and %[ (_s) */
#define NTCRT_SCANF_LONG_DOUBLE 0x4 /* %Lf, %llf store the 80-bit long double of GCC (__mingw_*), else a double */
template<typename TChar>
struct NTCRT_SCANF_INPUT
{
    const TChar* Next;
    size_t Left;
    FILE* Stream;
    int (*Get)(FILE* Stream);
    void (*Unget)(int Ch, FILE* Stream);
    size_t Count;
    BOOL Eof;
};
extern "C" int _NtCRT_Scan(NTCRT_SCANF_INPUT<char>* In, const char* Format, va_list ap, ULONG Options);
extern "C" int _NtCRT_ScanW(NTCRT_SCANF_INPUT<wchar_t>* In, const wchar_t* Format, va_list ap, ULONG Options);
extern "C" int _NtCRT_ScanString(const char* Input, size_t Count, const char* Format, va_list ap, ULONG Options);
extern "C" int _NtCRT_ScanStringW(const wchar_t* Input, size_t Count, const wchar_t* Format, va_list ap,
                                  ULONG Options);
extern "C" int _NtCRT_ScanStream(FILE* Stream, const char* Format, va_list ap, ULONG Options);
extern "C" int _NtCRT_ScanStreamW(FILE* Stream, const wchar_t* Format, va_list ap, ULONG Options);
/* FloatParse.cpp: Text, a matching sequence of strtod, to a float (Kind 0), double (1) or long double (2), TRUE if
   it is out of range as for ERANGE */
extern "C" BOOL _NtCRT_ScanFloatText(const char* Text, int Kind, void* Value);

/* A variadic function calling its va_list form */
#define NTCRT_WRAP_VARIADIC(Name, VName, Params, Last, Args) \
    NTCRT_API int __cdecl Name Params \
    { \
        va_list ap; \
        int Ret; \
\
        va_start(ap, Last); \
        Ret = VName Args; \
        va_end(ap); \
        return Ret; \
    }

/* Terminating: the process ends without exit, the other threads are gone */
extern "C" void _NtCRT_FlushAll(bool Terminating);
extern "C" void _NtCRT_CloseAll(void);
/* _NtCRT_FlushAll, set by Stdio.cpp at startup so that exit does not link the streams of a program without them */
extern "C" void (*_NtCRT_FlushAllAtExit)(bool Terminating);

/* exit, see Startup.cpp */
extern "C" __attribute__((noreturn)) void __cdecl _NtCRT_exit(int code);
extern "C" __attribute__((noreturn)) void __cdecl _NtCRT__exit(int code);
extern "C" void __cdecl _NtCRT_amsg_exit(int rterrnum);
extern "C" __attribute__((noreturn)) void __cdecl _NtCRT_abort(void);

/* Messages of the runtime without the printf family: append Text or Value of at least MinDigits to Buffer */
extern "C" void _NtCRT_AppendText(_Inout_updates_z_(Size) char* Buffer, _In_ size_t Size, _In_z_ const char* Text);
extern "C" void _NtCRT_AppendNumber(_Inout_updates_z_(Size) char* Buffer, _In_ size_t Size, _In_ unsigned int Value,
                                    _In_ int MinDigits);

/* One-time runtime initialization, see Startup.cpp */
extern "C" int __cdecl _NtCRT_InitRuntime(void);
/* _environ and _wenviron from the process environment if not built yet, see Startup.cpp */
extern "C" void _NtCRT_InitEnviron(void);

/* The actions of the signals and of SIGILL of a privileged instruction, set by signal (Startup.cpp) */
typedef void(__cdecl* _NtCRT_sighandler_t)(int);
extern "C" _NtCRT_sighandler_t _NtCRT_SignalHandlers[];
extern "C" _NtCRT_sighandler_t _NtCRT_PrivilegedInstructionHandler;

/* Globals, see Startup.cpp */
extern "C" int _NtCRT_fmode;
extern "C" int _NtCRT_commode;

/* Lock numbers of msvcrt */
#define _SIGNAL_LOCK    0
#define _IOB_SCAN_LOCK  1
#define _TMPNAM_LOCK    2
#define _INPUT_LOCK     3
#define _OUTPUT_LOCK    4
#define _CSCANF_LOCK    5
#define _CPRINTF_LOCK   6
#define _CONIO_LOCK     7
#define _HEAP_LOCK      8
#define _TIME_LOCK      11
#define _ENV_LOCK       12
#define _EXIT_LOCK1     13
#define _LOCKTAB_LOCK   15
#define _SETLOCALE_LOCK 19
#define _MB_CP_LOCK     25

/* threadmbcinfostruct of msvcrt, incomplete in the headers of MinGW-w64, see Mbcs.cpp */
typedef struct _NTCRT_MBCINFO
{
    int refcount;
    int mbcodepage;
    int ismbcodepage;
    int mblcid;
    unsigned short mbulinfo[6];
    unsigned char mbctype[257];
    unsigned char mbcasemap[256];
} NTCRT_MBCINFO;

/* The multibyte code page set by _setmbcp if Current, else the single-byte one of the "C" locale */
extern "C" void _NtCRT_GetMbcInfo(NTCRT_MBCINFO* Info, BOOL Current);

/* kernel32 replacements used internally */
extern "C" BOOL WINAPI _K32_IsDBCSLeadByteEx(UINT CodePage, BYTE TestChar);
extern "C" BOOL WINAPI _K32_DeleteFileW(LPCWSTR lpFileName);
extern "C" BOOL WINAPI _K32_MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags);
extern "C" DWORD WINAPI _K32_GetTempPathW(DWORD nBufferLength, LPWSTR lpBuffer);
extern "C" DWORD WINAPI _K32_GetFileAttributesW(LPCWSTR lpFileName);
extern "C" BOOL WINAPI _K32_SetHandleInformation(HANDLE hObject, DWORD dwMask, DWORD dwFlags);
extern "C" HANDLE WINAPI _K32_CreateFileW(LPCWSTR lpFileName,
                                          DWORD dwDesiredAccess,
                                          DWORD dwShareMode,
                                          LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                                          DWORD dwCreationDisposition,
                                          DWORD dwFlagsAndAttributes,
                                          HANDLE hTemplateFile);

/* Resolve a kernel32 export at runtime, for what ntdll cannot do (console input, process creation) */
extern "C" PVOID _NtCRT_GetKernel32Function(PCSTR Name);

/*
 * A console handle of Windows 7 and earlier (3 in its low bits) is a handle of the console server, which the kernel
 * would take for the handle of the process without those bits: only kernel32 takes it
 */
static inline
BOOL
_NtCRT_IsLegacyConsoleHandle(
    _In_ HANDLE Handle)
{
    return ((ULONG_PTR)Handle & 3) == 3 && !IS_NT_VERSION_GE(NT_VERSION_WIN8);
}

/* A name that CreateFileW of kernel32 of Windows 7 and earlier opens as a console handle */
extern "C" BOOL _NtCRT_IsLegacyConsoleName(_In_ PCWSTR FileName, _In_ ACCESS_MASK DesiredAccess);

/* NtClose, CloseHandle of kernel32 for a console handle of Windows 7 */
extern "C" NTSTATUS _NtCRT_CloseHandle(_In_ HANDLE Handle);

/* Return Name of kernel32 with the arguments for a console handle of Windows 7, Failure if kernel32 has none */
#define NTCRT_LEGACY_CONSOLE_CALL(Handle, Failure, Name, ...)                                 \
    if (_NtCRT_IsLegacyConsoleHandle(Handle))                                                 \
    {                                                                                         \
        auto const Function = (decltype(&_K32_##Name))_NtCRT_GetKernel32Function(#Name);      \
        if (Function == NULL)                                                                 \
        {                                                                                     \
            _Inline_SetLastError(ERROR_INVALID_HANDLE);                                       \
            return Failure;                                                                   \
        }                                                                                     \
        return Function(__VA_ARGS__);                                                         \
    }
