/*
 * KNSoft.NDK subset for MinGW-w64
 *
 * KNSoft.NDK only supports MSVC, this header provides the part of it used by KNSoft.NtCRT and its tests,
 * built on top of MinGW-w64 headers. Definitions follow KNSoft.NDK (MIT, https://github.com/KNSoft/KNSoft.NDK).
 */

#pragma once

#if !defined(__MINGW32__)
#error This header is for MinGW-w64 only, use KNSoft.NDK with MSVC.
#endif

#if !defined(__x86_64__) && !defined(__i386__)
#error Only x64 and x86 are supported now.
#endif

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <ntdef.h>
#include <winioctl.h>
#include <intrin.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>

#pragma region Compiler and SAL

#ifdef __cplusplus
#define EXTERN_C_START extern "C" {
#define EXTERN_C_END }
#else
#define EXTERN_C_START
#define EXTERN_C_END
#endif

#ifndef _Interlocked_operand_
#define _Interlocked_operand_
#endif
#ifndef _Pre_
#define _Pre_
#endif
#ifndef _NullNull_terminated_
#define _NullNull_terminated_
#endif
#ifndef _In_NLS_string_
#define _In_NLS_string_(size)
#endif
#ifndef _Kernel_entry_
#define _Kernel_entry_
#endif
#ifndef _Frees_ptr_opt_
#define _Frees_ptr_opt_
#endif

#ifndef ASSERT
#define ASSERT(Expression) ((void)0)
#endif

#define Add2Ptr(Pointer, Offset) ((PVOID)((PUCHAR)(Pointer) + (ULONG_PTR)(Offset)))
#define TEST_FLAGS(Value, Flags) (((Value) & (Flags)) == (Flags))

#pragma endregion

#pragma region Basic Types

typedef struct _CLIENT_ID
{
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _IO_STATUS_BLOCK
{
    union
    {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef
VOID
NTAPI
IO_APC_ROUTINE(
    _In_ PVOID ApcContext,
    _In_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG Reserved);
typedef IO_APC_ROUTINE* PIO_APC_ROUTINE;

typedef struct _RTL_BITMAP
{
    ULONG SizeOfBitMap;
    PULONG Buffer;
} RTL_BITMAP, *PRTL_BITMAP;

#define NtCurrentProcess() ((HANDLE)(LONG_PTR)-1)
#define NtCurrentThread() ((HANDLE)(LONG_PTR)-2)

#define TLS_EXPANSION_SLOTS 1024
#define RTL_MAX_DRIVE_LETTERS 32

#pragma endregion

#pragma region NLS

#define MAXIMUM_LEADBYTES 12

typedef struct _CPTABLEINFO
{
    USHORT CodePage;
    USHORT MaximumCharacterSize;
    USHORT DefaultChar;
    USHORT UniDefaultChar;
    USHORT TransDefaultChar;
    USHORT TransUniDefaultChar;
    USHORT DBCSCodePage;
    UCHAR LeadByte[MAXIMUM_LEADBYTES];
    PUSHORT MultiByteTable;
    PVOID WideCharTable;
    PUSHORT DBCSRanges;
    PUSHORT DBCSOffsets;
} CPTABLEINFO, *PCPTABLEINFO;

typedef struct _NLSTABLEINFO
{
    CPTABLEINFO OemTableInfo;
    CPTABLEINFO AnsiTableInfo;
    PUSHORT UpperCaseTable;
    PUSHORT LowerCaseTable;
} NLSTABLEINFO, *PNLSTABLEINFO;

/* Private locale.nls layouts observed from Windows. */

typedef struct _NLS_LOCALE_FILE_HEADER
{
    ULONG CTypeTableOffset;
    ULONG Reserved0[3];
    ULONG LocaleTableOffset;
    ULONG CharMapTableOffset;
    ULONG GeoIdTableOffset;
    ULONG ScriptTableOffset;
} NLS_LOCALE_FILE_HEADER, *PNLS_LOCALE_FILE_HEADER;
typedef const NLS_LOCALE_FILE_HEADER* PCNLS_LOCALE_FILE_HEADER;

typedef struct _NLS_CTYPE_HEADER
{
    USHORT DataSize;
    USHORT IndexTableOffset;
    WORD TypeTable[ANYSIZE_ARRAY];
} NLS_CTYPE_HEADER, *PNLS_CTYPE_HEADER;
typedef const NLS_CTYPE_HEADER* PCNLS_CTYPE_HEADER;

typedef struct _NLS_LOCALE_DATA
{
    ULONG SNameOffset;
    ULONG SOpenTypeLanguageTagOffset;
    USHORT ILanguage;
    USHORT Reserved0;
    USHORT IDigits;
    USHORT INegNumber;
    USHORT ICurrDigits;
    USHORT ICurrency;
    USHORT INegCurr;
    USHORT ILZero;
    USHORT INeutral;
    USHORT IFirstDayOfWeek;
    USHORT IFirstWeekOfYear;
    USHORT ICountry;
    USHORT IMeasure;
    USHORT IDigitSubstitution;
    ULONG SGroupingOffset;
    ULONG SMonGroupingOffset;
    ULONG SListOffset;
    ULONG SDecimalOffset;
    ULONG SThousandOffset;
    ULONG SCurrencyOffset;
    ULONG SMonDecimalSepOffset;
    ULONG SMonThousandSepOffset;
    ULONG SPositiveSignOffset;
    ULONG SNegativeSignOffset;
    ULONG S1159Offset;
    ULONG S2359Offset;
    ULONG SNativeDigitsOffset;
    ULONG STimeFormatOffset;
    ULONG SShortDateOffset;
    ULONG SLongDateOffset;
    ULONG SYearMonthOffset;
    ULONG SDurationOffset;
    USHORT IDefaultLanguage;
    USHORT IDefaultAnsiCodePage;
    USHORT IDefaultCodePage;
    USHORT IDefaultMacCodePage;
    USHORT IDefaultEbcdicCodePage;
    /* Truncated, only leading fields are used */
} NLS_LOCALE_DATA, *PNLS_LOCALE_DATA;
typedef const NLS_LOCALE_DATA* PCNLS_LOCALE_DATA;

typedef struct _NLS_LOCALE_LCID_INDEX
{
    ULONG LocaleId;
    USHORT LocaleIndex;
    USHORT LocaleNameOffset;
} NLS_LOCALE_LCID_INDEX, *PNLS_LOCALE_LCID_INDEX;
typedef const NLS_LOCALE_LCID_INDEX* PCNLS_LOCALE_LCID_INDEX;

typedef struct _NLS_LOCALE_HEADER
{
    ULONG VersionOffset;
    ULONG Reserved0;
    ULONG Version;
    ULONG Magic;
    ULONG Reserved1[3];
    USHORT HeaderSize;
    USHORT LocaleIdCount;
    USHORT LocaleCount;
    USHORT LocaleDataSize;
    ULONG LocaleDataOffset;
    USHORT LocaleNameCount;
    USHORT Reserved2;
    ULONG LocaleIdIndexOffset;
    ULONG LocaleNameIndexOffset;
    ULONG Reserved3;
    USHORT CalendarCount;
    USHORT CalendarDataSize;
    ULONG CalendarDataOffset;
    ULONG StringTableOffset;
    USHORT Reserved4[4];
} NLS_LOCALE_HEADER, *PNLS_LOCALE_HEADER;
typedef const NLS_LOCALE_HEADER* PCNLS_LOCALE_HEADER;

#pragma endregion

#pragma region Process Environment

typedef struct _CURDIR
{
    UNICODE_STRING DosPath;
    HANDLE Handle;
} CURDIR, *PCURDIR;

typedef struct _RTL_DRIVE_LETTER_CURDIR
{
    USHORT Flags;
    USHORT Length;
    ULONG TimeStamp;
    STRING DosPath;
} RTL_DRIVE_LETTER_CURDIR, *PRTL_DRIVE_LETTER_CURDIR;

typedef struct _RTL_USER_PROCESS_PARAMETERS
{
    ULONG MaximumLength;
    ULONG Length;
    ULONG Flags;
    ULONG DebugFlags;
    HANDLE ConsoleHandle;
    ULONG ConsoleFlags;
    HANDLE StandardInput;
    HANDLE StandardOutput;
    HANDLE StandardError;
    CURDIR CurrentDirectory;
    UNICODE_STRING DllPath;
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
    PVOID Environment;
    ULONG StartingX;
    ULONG StartingY;
    ULONG CountX;
    ULONG CountY;
    ULONG CountCharsX;
    ULONG CountCharsY;
    ULONG FillAttribute;
    ULONG WindowFlags;
    ULONG ShowWindowFlags;
    UNICODE_STRING WindowTitle;
    UNICODE_STRING DesktopInfo;
    UNICODE_STRING ShellInfo;
    UNICODE_STRING RuntimeData;
    RTL_DRIVE_LETTER_CURDIR CurrentDirectories[RTL_MAX_DRIVE_LETTERS];
    ULONG_PTR EnvironmentSize;
    ULONG_PTR EnvironmentVersion;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

typedef struct _PEB_LDR_DATA
{
    ULONG Length;
    BOOLEAN Initialized;
    HANDLE SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    PVOID EntryInProgress;
    BOOLEAN ShutdownInProgress;
    HANDLE ShutdownThreadId;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _LDR_DATA_TABLE_ENTRY
{
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    /* Truncated, only leading fields are used */
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef struct _RTL_PERTHREAD_CURDIR
{
    PRTL_DRIVE_LETTER_CURDIR CurrentDirectories;
    PUNICODE_STRING ImageName;
    PVOID Environment;
} RTL_PERTHREAD_CURDIR, *PRTL_PERTHREAD_CURDIR;

/* A size or an offset that differs between x86 and x64 */
#ifdef _WIN64
#define NTCRT_X86_X64(X86, X64) X64
#else
#define NTCRT_X86_X64(X86, X64) X86
#endif

#define GDI_HANDLE_BUFFER_SIZE32 34
#define GDI_HANDLE_BUFFER_SIZE64 60
#define GDI_HANDLE_BUFFER_SIZE NTCRT_X86_X64(GDI_HANDLE_BUFFER_SIZE32, GDI_HANDLE_BUFFER_SIZE64)

/* Truncated after the fields we use, offsets are verified below */
typedef struct _PEB
{
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    BOOLEAN BitField;
    HANDLE Mutant;
    PVOID ImageBaseAddress;
    PPEB_LDR_DATA Ldr;
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
    PVOID SubSystemData;
    PVOID ProcessHeap;
    PRTL_CRITICAL_SECTION FastPebLock;
    PSLIST_HEADER AtlThunkSListPtr;
    PVOID IFEOKey;
    ULONG CrossProcessFlags;
    PVOID KernelCallbackTable;
    ULONG SystemReserved;
    ULONG AtlThunkSListPtr32;
    PVOID ApiSetMap;
    ULONG TlsExpansionCounter;
    PRTL_BITMAP TlsBitmap;
    ULONG TlsBitmapBits[2];
    PVOID ReadOnlySharedMemoryBase;
    PVOID SharedData;
    PVOID* ReadOnlyStaticServerData;
    /*
     * Not CPTABLEINFO: the data of the .nls file of the code page (its USHORT[1] is the code page), which
     * RtlInitCodePageTable takes. NULL on Windows 11 24H2.
     */
    PCPTABLEINFO AnsiCodePageData;
    PCPTABLEINFO OemCodePageData;
    PNLSTABLEINFO UnicodeCaseTableData;
    ULONG NumberOfProcessors;
    ULONG NtGlobalFlag;
    LARGE_INTEGER CriticalSectionTimeout;
    SIZE_T HeapSegmentReserve;
    SIZE_T HeapSegmentCommit;
    SIZE_T HeapDeCommitTotalFreeThreshold;
    SIZE_T HeapDeCommitFreeBlockThreshold;
    ULONG NumberOfHeaps;
    ULONG MaximumNumberOfHeaps;
    PVOID* ProcessHeaps;
    PVOID GdiSharedHandleTable;
    PVOID ProcessStarterHelper;
    ULONG GdiDCAttributeList;
    PRTL_CRITICAL_SECTION LoaderLock;
    ULONG OSMajorVersion;
    ULONG OSMinorVersion;
    USHORT OSBuildNumber;
    USHORT OSCSDVersion;
    ULONG OSPlatformId;
    ULONG ImageSubsystem;
    ULONG ImageSubsystemMajorVersion;
    ULONG ImageSubsystemMinorVersion;
    KAFFINITY ActiveProcessAffinityMask;
    ULONG GdiHandleBuffer[GDI_HANDLE_BUFFER_SIZE];
    PVOID PostProcessInitRoutine;
    PRTL_BITMAP TlsExpansionBitmap;
    ULONG TlsExpansionBitmapBits[32];
    ULONG SessionId;
    ULARGE_INTEGER AppCompatFlags;
    ULARGE_INTEGER AppCompatFlagsUser;
    PVOID pShimData;
    PVOID AppCompatInfo;
    UNICODE_STRING CSDVersion;
    PVOID ActivationContextData;
    PVOID ProcessAssemblyStorageMap;
    PVOID SystemDefaultActivationContextData;
    PVOID SystemAssemblyStorageMap;
    SIZE_T MinimumStackCommit;
    PVOID SparePointers[2];
    PVOID PatchLoaderData;
    PVOID ChpeV2ProcessInfo;
    ULONG AppModelFeatureState;
    ULONG SpareUlongs[2];
    USHORT ActiveCodePage; /* Windows 10 and later, another member before (Windows 7 reads 0) */
    USHORT OemCodePage;    /* Windows 10 and later */
    USHORT UseCaseMapping;
    USHORT UnusedNlsField;
} PEB, *PPEB;

/* Truncated after the fields we use, offsets are verified below */
typedef struct _TEB
{
    NT_TIB NtTib;
    PVOID EnvironmentPointer;
    CLIENT_ID ClientId;
    PVOID ActiveRpcHandle;
    PVOID* ThreadLocalStoragePointer;
    PPEB ProcessEnvironmentBlock;
    ULONG LastErrorValue;
    ULONG CountOfOwnedCriticalSections;
    PVOID CsrClientThread;
    PVOID Win32ThreadInfo;
    ULONG User32Reserved[26];
    ULONG UserReserved[5];
    PVOID WOW32Reserved;
    LCID CurrentLocale;
    ULONG FpSoftwareStatusRegister;
    UCHAR Reserved0[NTCRT_X86_X64(0xB28, 0x1140)];
    NTSTATUS LastStatusValue;
    UNICODE_STRING StaticUnicodeString;
    WCHAR StaticUnicodeBuffer[261];
    PVOID DeallocationStack;
    PVOID TlsSlots[TLS_MINIMUM_AVAILABLE];
    LIST_ENTRY TlsLinks;
    PVOID Vdm;
    PVOID ReservedForNtRpc;
    PVOID DbgSsReserved[2];
    ULONG HardErrorMode;
    UCHAR Reserved1[NTCRT_X86_X64(0x4C, 0x94)];
    ULONG GuaranteedStackBytes;
    UCHAR Reserved2[NTCRT_X86_X64(0x18, 0x34)];
    PVOID* TlsExpansionSlots;
    UCHAR Reserved3[NTCRT_X86_X64(0x1C, 0x40)];
    PVOID FlsData;
    UCHAR Reserved4[NTCRT_X86_X64(0x12, 0x1E)];
    union
    {
        USHORT SameTebFlags;
        struct
        {
            USHORT SafeThunkCall : 1;
            USHORT InDebugPrint : 1;
            USHORT HasFiberData : 1;
            USHORT SkipThreadAttach : 1;
            USHORT WerInShipAssertCode : 1;
            USHORT RanProcessInit : 1;
            USHORT ClonedThread : 1;
            USHORT SuppressDebugMsg : 1;
            USHORT DisableUserStackWalk : 1;
            USHORT RtlExceptionAttached : 1;
            USHORT InitialThread : 1;
            USHORT SessionAware : 1;
            USHORT LoadOwner : 1;
            USHORT LoaderWorker : 1;
            USHORT SkipLoaderInit : 1;
            USHORT SkipFileAPIBrokering : 1;
        };
    };
    UCHAR Reserved5[NTCRT_X86_X64(0x10, 0x1C)];
    LONG WowTebOffset;
} TEB, *PTEB;

/* The offset on both, or that on x86 then that on x64 */
#ifdef __cplusplus
#define NTCRT_CHECK_OFFSET_(Type, Field, Offset) \
    static_assert(offsetof(Type, Field) == (Offset), #Type "." #Field " offset mismatch")
#else
#define NTCRT_CHECK_OFFSET_(Type, Field, Offset) \
    _Static_assert(offsetof(Type, Field) == (Offset), #Type "." #Field " offset mismatch")
#endif
#define NTCRT_CHECK_OFFSET_ARCH(Type, Field, X86, X64) NTCRT_CHECK_OFFSET_(Type, Field, NTCRT_X86_X64(X86, X64))
#define NTCRT_CHECK_OFFSET_PICK(_1, _2, Macro, ...) Macro
#define NTCRT_CHECK_OFFSET(Type, Field, ...) \
    NTCRT_CHECK_OFFSET_PICK(__VA_ARGS__, NTCRT_CHECK_OFFSET_ARCH, NTCRT_CHECK_OFFSET_, )(Type, Field, __VA_ARGS__)

NTCRT_CHECK_OFFSET(PEB, BeingDebugged, 0x002);
NTCRT_CHECK_OFFSET(PEB, ImageBaseAddress, 0x008, 0x010);
NTCRT_CHECK_OFFSET(PEB, Ldr, 0x00C, 0x018);
NTCRT_CHECK_OFFSET(PEB, ProcessParameters, 0x010, 0x020);
NTCRT_CHECK_OFFSET(PEB, ProcessHeap, 0x018, 0x030);
NTCRT_CHECK_OFFSET(PEB, FastPebLock, 0x01C, 0x038);
NTCRT_CHECK_OFFSET(PEB, CrossProcessFlags, 0x028, 0x050);
NTCRT_CHECK_OFFSET(PEB, TlsExpansionCounter, 0x03C, 0x070);
NTCRT_CHECK_OFFSET(PEB, TlsBitmap, 0x040, 0x078);
NTCRT_CHECK_OFFSET(PEB, TlsBitmapBits, 0x044, 0x080);
NTCRT_CHECK_OFFSET(PEB, AnsiCodePageData, 0x058, 0x0A0);
NTCRT_CHECK_OFFSET(PEB, OemCodePageData, 0x05C, 0x0A8);
NTCRT_CHECK_OFFSET(PEB, NumberOfProcessors, 0x064, 0x0B8);
NTCRT_CHECK_OFFSET(PEB, LoaderLock, 0x0A0, 0x110);
NTCRT_CHECK_OFFSET(PEB, OSMajorVersion, 0x0A4, 0x118);
NTCRT_CHECK_OFFSET(PEB, OSMinorVersion, 0x0A8, 0x11C);
NTCRT_CHECK_OFFSET(PEB, OSBuildNumber, 0x0AC, 0x120);
NTCRT_CHECK_OFFSET(PEB, ImageSubsystem, 0x0B4, 0x128);
NTCRT_CHECK_OFFSET(PEB, TlsExpansionBitmap, 0x150, 0x238);
NTCRT_CHECK_OFFSET(PEB, TlsExpansionBitmapBits, 0x154, 0x240);
NTCRT_CHECK_OFFSET(PEB, SessionId, 0x1D4, 0x2C0);
NTCRT_CHECK_OFFSET(PEB, ActiveCodePage, 0x228, 0x34C);
NTCRT_CHECK_OFFSET(PEB, OemCodePage, 0x22A, 0x34E);

NTCRT_CHECK_OFFSET(TEB, ThreadLocalStoragePointer, 0x002C, 0x0058);
NTCRT_CHECK_OFFSET(TEB, LastErrorValue, 0x0034, 0x0068);
NTCRT_CHECK_OFFSET(TEB, CurrentLocale, 0x00C4, 0x0108);
NTCRT_CHECK_OFFSET(TEB, LastStatusValue, 0x0BF4, 0x1250);
NTCRT_CHECK_OFFSET(TEB, DeallocationStack, 0x0E0C, 0x1478);
NTCRT_CHECK_OFFSET(TEB, TlsSlots, 0x0E10, 0x1480);
NTCRT_CHECK_OFFSET(TEB, TlsLinks, 0x0F10, 0x1680);
NTCRT_CHECK_OFFSET(TEB, HardErrorMode, 0x0F28, 0x16B0);
NTCRT_CHECK_OFFSET(TEB, GuaranteedStackBytes, 0x0F78, 0x1748);
NTCRT_CHECK_OFFSET(TEB, TlsExpansionSlots, 0x0F94, 0x1780);
NTCRT_CHECK_OFFSET(TEB, FlsData, 0x0FB4, 0x17C8);
NTCRT_CHECK_OFFSET(TEB, SameTebFlags, 0x0FCA, 0x17EE);
NTCRT_CHECK_OFFSET(TEB, WowTebOffset, 0x0FDC, 0x180C);

/*
 * MinGW-w64's NtCurrentTeb reads TIB.Self through __readgsqword, whose "m" operand trips -Warray-bounds,
 * read it from the segment register directly instead: gs on x64, fs on x86.
 */
static __inline__ __attribute__((__always_inline__)) PTEB _NtCRT_CurrentTeb(void)
{
    PTEB Teb;
#ifdef _WIN64
    __asm__("mov{q %%gs:%c1, %0| %0, gs:%c1}" : "=r"(Teb) : "i"(offsetof(NT_TIB, Self)));
#else
    __asm__("mov{l %%fs:%c1, %0| %0, fs:%c1}" : "=r"(Teb) : "i"(offsetof(NT_TIB, Self)));
#endif
    return Teb;
}
#define NtCurrentTeb _NtCRT_CurrentTeb

#define NtReadTeb(m) (NtCurrentTeb()->m)
#define NtWriteTeb(m, val) ((void)(NtCurrentTeb()->m = (val)))
#define NtCurrentPeb() (NtCurrentTeb()->ProcessEnvironmentBlock)
#define NtCurrentProcessId() (NtCurrentTeb()->ClientId.UniqueProcess)
#define NtCurrentThreadId() (NtCurrentTeb()->ClientId.UniqueThread)
#define RtlProcessHeap() (NtCurrentPeb()->ProcessHeap)

#pragma endregion

#pragma region KUSER_SHARED_DATA

typedef struct _KSYSTEM_TIME
{
    ULONG LowPart;
    LONG High1Time;
    LONG High2Time;
} KSYSTEM_TIME, *PKSYSTEM_TIME;

/* Truncated, offsets are verified below */
typedef struct _KUSER_SHARED_DATA
{
    ULONG TickCountLowDeprecated;
    ULONG TickCountMultiplier;
    KSYSTEM_TIME InterruptTime;
    KSYSTEM_TIME SystemTime;
    KSYSTEM_TIME TimeZoneBias;
    USHORT ImageNumberLow;
    USHORT ImageNumberHigh;
    WCHAR NtSystemRoot[260];
    ULONG MaxStackTraceDepth;
    ULONG CryptoExponent;
    ULONG TimeZoneId;
    UCHAR Reserved0[0x260 - 0x244];
    ULONG NtBuildNumber;
    ULONG NtProductType;
    BOOLEAN ProductTypeIsValid;
    BOOLEAN Reserved1;
    USHORT NativeProcessorArchitecture;
    ULONG NtMajorVersion;
    ULONG NtMinorVersion;
    BOOLEAN ProcessorFeatures[64];
    UCHAR Reserved2[0x320 - 0x2B4];
    union
    {
        KSYSTEM_TIME TickCount;
        ULONG64 TickCountQuad;
    };
    UCHAR Reserved3[0x3D8 - 0x330];
    struct
    {
        ULONG64 EnabledFeatures;
        ULONG64 EnabledVolatileFeatures;
    } XState;
} KUSER_SHARED_DATA, *PKUSER_SHARED_DATA;

NTCRT_CHECK_OFFSET(KUSER_SHARED_DATA, SystemTime, 0x014);
NTCRT_CHECK_OFFSET(KUSER_SHARED_DATA, NtSystemRoot, 0x030);
NTCRT_CHECK_OFFSET(KUSER_SHARED_DATA, TimeZoneId, 0x240);
NTCRT_CHECK_OFFSET(KUSER_SHARED_DATA, NtBuildNumber, 0x260);
NTCRT_CHECK_OFFSET(KUSER_SHARED_DATA, NtMajorVersion, 0x26C);
NTCRT_CHECK_OFFSET(KUSER_SHARED_DATA, ProcessorFeatures, 0x274);
NTCRT_CHECK_OFFSET(KUSER_SHARED_DATA, TickCountQuad, 0x320);
NTCRT_CHECK_OFFSET(KUSER_SHARED_DATA, XState, 0x3D8);

#define SharedUserData ((KUSER_SHARED_DATA*)0x7FFE0000)

#ifndef PROCESSOR_FEATURE_MAX
#define PROCESSOR_FEATURE_MAX 64
#endif

#pragma endregion

#pragma region NT Version

#define NT_VERSION(Major, Minor, Build) (((((Major) & 0xFF) << 24) | (((Minor) & 0xFF) << 16)) | ((Build) & 0xFFFF))

#define NT_VERSION_WINXP NT_VERSION(5, 1, 0)
#define NT_VERSION_VISTA NT_VERSION(6, 0, 0)
#define NT_VERSION_WIN7 NT_VERSION(6, 1, 0)
#define NT_VERSION_WIN8 NT_VERSION(6, 2, 0)
#define NT_VERSION_WINBLUE NT_VERSION(6, 3, 0)
#define NT_VERSION_WIN10 NT_VERSION(10, 0, 0)

#ifndef NT_VERSION_MIN
#define NT_VERSION_MIN NT_VERSION_WINXP
#endif

#define GET_NT_VERSION() NT_VERSION(NtCurrentPeb()->OSMajorVersion, \
                                    NtCurrentPeb()->OSMinorVersion, \
                                    NtCurrentPeb()->OSBuildNumber)
#define IS_NT_VERSION_GE(Version) (NT_VERSION_MIN >= (Version) || GET_NT_VERSION() >= (Version))

#pragma endregion

#pragma region Loader

#ifndef STARTF_USEMONITOR
#define STARTF_USEMONITOR 0x00000400
#endif

#define LDR_IS_DATAFILE(DllHandle) (((ULONG_PTR)(DllHandle)) & (ULONG_PTR)1)
#define LDR_IS_IMAGEMAPPING(DllHandle) (((ULONG_PTR)(DllHandle)) & (ULONG_PTR)2)
#define LDR_IS_RESOURCE(DllHandle) (LDR_IS_IMAGEMAPPING(DllHandle) || LDR_IS_DATAFILE(DllHandle))

#define LDR_PATH_IS_FLAGS                     0x00000001
#define LDR_PATH_WITH_ALTERED_SEARCH_PATH     0x00000008
#define LDR_PATH_SEARCH_DLL_LOAD_DIR          0x00000100
#define LDR_PATH_SEARCH_APPLICATION_DIR       0x00000200
#define LDR_PATH_SEARCH_USER_DIRS             0x00000400
#define LDR_PATH_SEARCH_SYSTEM32              0x00000800
#define LDR_PATH_SEARCH_DEFAULT_DIRS          0x00001000

#define LDR_ADDREF_DLL_PIN 0x00000001
#define LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS 0x00000001
#define LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS 0x00000001
#define LDR_GET_DLL_HANDLE_EX_UNCHANGED_REFCOUNT 0x00000001

#pragma endregion

#pragma region I/O

#define FILE_SUPERSEDE                  0x00000000
#define FILE_OPEN                       0x00000001
#define FILE_CREATE                     0x00000002
#define FILE_OPEN_IF                    0x00000003
#define FILE_OVERWRITE                  0x00000004
#define FILE_OVERWRITE_IF               0x00000005

#define FILE_DIRECTORY_FILE             0x00000001
#define FILE_WRITE_THROUGH              0x00000002
#define FILE_SEQUENTIAL_ONLY            0x00000004
#define FILE_SYNCHRONOUS_IO_ALERT       0x00000010
#define FILE_SYNCHRONOUS_IO_NONALERT    0x00000020
#define FILE_NON_DIRECTORY_FILE         0x00000040
#define FILE_RANDOM_ACCESS              0x00000800
#define FILE_DELETE_ON_CLOSE            0x00001000
#define FILE_NO_INTERMEDIATE_BUFFERING  0x00000008
#define FILE_OPEN_FOR_BACKUP_INTENT     0x00004000
#define FILE_OPEN_REPARSE_POINT         0x00200000
#define FILE_OPEN_FOR_FREE_SPACE_QUERY  0x00800000

#define FILE_SUPERSEDED                 0x00000000
#define FILE_OPENED                     0x00000001
#define FILE_CREATED                    0x00000002
#define FILE_OVERWRITTEN                0x00000003
#define FILE_EXISTS                     0x00000004
#define FILE_DOES_NOT_EXIST             0x00000005

typedef enum _FILE_INFORMATION_CLASS
{
    FileDirectoryInformation = 1,
    FileBasicInformation = 4,
    FileStandardInformation = 5,
    FileInternalInformation = 6,
    FileAccessInformation = 8,
    FileNameInformation = 9,
    FileRenameInformation = 10,
    FileLinkInformation = 11,
    FileDispositionInformation = 13,
    FilePositionInformation = 14,
    FileModeInformation = 16,
    FileAllInformation = 18,
    FileAllocationInformation = 19,
    FileEndOfFileInformation = 20,
    FilePipeLocalInformation = 24,
    FileAttributeTagInformation = 35,
    FileDispositionInformationEx = 64,
    FileRenameInformationEx = 65,
} FILE_INFORMATION_CLASS, *PFILE_INFORMATION_CLASS;

typedef enum _FSINFOCLASS
{
    FileFsVolumeInformation = 1,
    FileFsSizeInformation = 3,
    FileFsDeviceInformation = 4,
    FileFsAttributeInformation = 5,
    FileFsFullSizeInformation = 7,
} FS_INFORMATION_CLASS, *PFS_INFORMATION_CLASS;

typedef struct _FILE_FS_DEVICE_INFORMATION
{
    DEVICE_TYPE DeviceType;
    ULONG Characteristics;
} FILE_FS_DEVICE_INFORMATION, *PFILE_FS_DEVICE_INFORMATION;

typedef struct _FILE_BASIC_INFORMATION
{
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG FileAttributes;
} FILE_BASIC_INFORMATION, *PFILE_BASIC_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION
{
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
} FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

typedef struct _FILE_POSITION_INFORMATION
{
    LARGE_INTEGER CurrentByteOffset;
} FILE_POSITION_INFORMATION, *PFILE_POSITION_INFORMATION;

typedef struct _FILE_END_OF_FILE_INFORMATION
{
    LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFORMATION, *PFILE_END_OF_FILE_INFORMATION;

typedef struct _FILE_DISPOSITION_INFORMATION
{
    BOOLEAN DeleteFile;
} FILE_DISPOSITION_INFORMATION, *PFILE_DISPOSITION_INFORMATION;

typedef struct _FILE_RENAME_INFORMATION
{
    BOOLEAN ReplaceIfExists;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_RENAME_INFORMATION, *PFILE_RENAME_INFORMATION;

typedef struct _FILE_LINK_INFORMATION
{
    BOOLEAN ReplaceIfExists;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_LINK_INFORMATION, *PFILE_LINK_INFORMATION;

typedef struct _FILE_ALLOCATION_INFORMATION
{
    LARGE_INTEGER AllocationSize;
} FILE_ALLOCATION_INFORMATION, *PFILE_ALLOCATION_INFORMATION;

typedef struct _FILE_FS_VOLUME_INFORMATION
{
    LARGE_INTEGER VolumeCreationTime;
    ULONG VolumeSerialNumber;
    ULONG VolumeLabelLength;
    BOOLEAN SupportsObjects;
    WCHAR VolumeLabel[1];
} FILE_FS_VOLUME_INFORMATION, *PFILE_FS_VOLUME_INFORMATION;

typedef struct _FILE_FS_ATTRIBUTE_INFORMATION
{
    ULONG FileSystemAttributes;
    LONG MaximumComponentNameLength;
    ULONG FileSystemNameLength;
    WCHAR FileSystemName[1];
} FILE_FS_ATTRIBUTE_INFORMATION, *PFILE_FS_ATTRIBUTE_INFORMATION;

typedef struct _FILE_FS_FULL_SIZE_INFORMATION
{
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER CallerAvailableAllocationUnits;
    LARGE_INTEGER ActualAvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
} FILE_FS_FULL_SIZE_INFORMATION, *PFILE_FS_FULL_SIZE_INFORMATION;

typedef struct _FILE_ATTRIBUTE_TAG_INFORMATION
{
    ULONG FileAttributes;
    ULONG ReparseTag;
} FILE_ATTRIBUTE_TAG_INFORMATION, *PFILE_ATTRIBUTE_TAG_INFORMATION;

typedef struct _FILE_INTERNAL_INFORMATION
{
    LARGE_INTEGER IndexNumber;
} FILE_INTERNAL_INFORMATION, *PFILE_INTERNAL_INFORMATION;

typedef struct _FILE_NETWORK_OPEN_INFORMATION
{
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG FileAttributes;
} FILE_NETWORK_OPEN_INFORMATION, *PFILE_NETWORK_OPEN_INFORMATION;

typedef struct _FILE_DIRECTORY_INFORMATION
{
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_DIRECTORY_INFORMATION, *PFILE_DIRECTORY_INFORMATION;

#pragma endregion

#pragma region Object / Process / Thread / Memory

typedef enum _OBJECT_INFORMATION_CLASS
{
    ObjectBasicInformation = 0,
    ObjectTypeInformation = 2,
    ObjectHandleFlagInformation = 4,
} OBJECT_INFORMATION_CLASS;

typedef struct _OBJECT_HANDLE_FLAG_INFORMATION
{
    BOOLEAN Inherit;
    BOOLEAN ProtectFromClose;
} OBJECT_HANDLE_FLAG_INFORMATION, *POBJECT_HANDLE_FLAG_INFORMATION;

typedef enum _PROCESSINFOCLASS
{
    ProcessBasicInformation = 0,
    ProcessTimes = 4,
    ProcessDefaultHardErrorMode = 12,
    ProcessAffinityMask = 21,
    ProcessDeviceMap = 23,
    ProcessImageFileName = 27,
} PROCESSINFOCLASS;

typedef struct _PROCESS_BASIC_INFORMATION
{
    NTSTATUS ExitStatus;
    PPEB PebBaseAddress;
    KAFFINITY AffinityMask;
    LONG BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION, *PPROCESS_BASIC_INFORMATION;

typedef enum _THREADINFOCLASS
{
    ThreadBasicInformation = 0,
    ThreadTimes = 1,
    ThreadPriority = 2,
    ThreadBasePriority = 3,
    ThreadAffinityMask = 4,
    ThreadQuerySetWin32StartAddress = 9,
    ThreadZeroTlsCell = 10,
    ThreadHideFromDebugger = 17,
    ThreadNameInformation = 38,
} THREADINFOCLASS;

typedef struct _THREAD_BASIC_INFORMATION
{
    NTSTATUS ExitStatus;
    PTEB TebBaseAddress;
    CLIENT_ID ClientId;
    KAFFINITY AffinityMask;
    LONG Priority;
    LONG BasePriority;
} THREAD_BASIC_INFORMATION, *PTHREAD_BASIC_INFORMATION;

typedef struct _KERNEL_USER_TIMES
{
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER ExitTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
} KERNEL_USER_TIMES, *PKERNEL_USER_TIMES;

typedef enum _SYSTEM_INFORMATION_CLASS
{
    SystemBasicInformation = 0,
    SystemTimeOfDayInformation = 3,
    SystemTimeAdjustmentInformation = 28,
    SystemCurrentTimeZoneInformation = 44,
} SYSTEM_INFORMATION_CLASS;

typedef struct _SYSTEM_QUERY_TIME_ADJUST_INFORMATION
{
    ULONG TimeAdjustment;
    ULONG TimeIncrement;
    BOOLEAN Enable;
} SYSTEM_QUERY_TIME_ADJUST_INFORMATION, *PSYSTEM_QUERY_TIME_ADJUST_INFORMATION;

typedef struct _TIME_FIELDS
{
    CSHORT Year;
    CSHORT Month;
    CSHORT Day;
    CSHORT Hour;
    CSHORT Minute;
    CSHORT Second;
    CSHORT Milliseconds;
    CSHORT Weekday;
} TIME_FIELDS, *PTIME_FIELDS;

typedef struct _RTL_TIME_ZONE_INFORMATION
{
    LONG Bias;
    WCHAR StandardName[32];
    TIME_FIELDS StandardStart;
    LONG StandardBias;
    WCHAR DaylightName[32];
    TIME_FIELDS DaylightStart;
    LONG DaylightBias;
} RTL_TIME_ZONE_INFORMATION, *PRTL_TIME_ZONE_INFORMATION;

typedef struct _SYSTEM_BASIC_INFORMATION
{
    ULONG Reserved;
    ULONG TimerResolution;
    ULONG PageSize;
    ULONG NumberOfPhysicalPages;
    ULONG LowestPhysicalPageNumber;
    ULONG HighestPhysicalPageNumber;
    ULONG AllocationGranularity;
    ULONG_PTR MinimumUserModeAddress;
    ULONG_PTR MaximumUserModeAddress;
    KAFFINITY ActiveProcessorsAffinityMask;
    CCHAR NumberOfProcessors;
} SYSTEM_BASIC_INFORMATION, *PSYSTEM_BASIC_INFORMATION;

typedef struct _SYSTEM_TIMEOFDAY_INFORMATION
{
    LARGE_INTEGER BootTime;
    LARGE_INTEGER CurrentTime;
    LARGE_INTEGER TimeZoneBias;
    ULONG TimeZoneId;
    ULONG Reserved;
    ULONGLONG BootTimeBias;
    ULONGLONG SleepTimeBias;
} SYSTEM_TIMEOFDAY_INFORMATION, *PSYSTEM_TIMEOFDAY_INFORMATION;

typedef enum _MEMORY_INFORMATION_CLASS
{
    MemoryBasicInformation = 0,
} MEMORY_INFORMATION_CLASS;

typedef struct _RTL_HEAP_WALK_ENTRY
{
    PVOID DataAddress;
    SIZE_T DataSize;
    UCHAR OverheadBytes;
    UCHAR SegmentIndex;
    USHORT Flags;
    union
    {
        struct
        {
            SIZE_T Settable;
            USHORT TagIndex;
            USHORT AllocatorBackTraceIndex;
            ULONG Reserved[2];
        } Block;
        struct
        {
            ULONG CommittedSize;
            ULONG UnCommittedSize;
            PVOID FirstEntry;
            PVOID LastEntry;
        } Segment;
    };
} RTL_HEAP_WALK_ENTRY, *PRTL_HEAP_WALK_ENTRY;

#define RTL_HEAP_BUSY (USHORT)0x0001
#define RTL_HEAP_SEGMENT (USHORT)0x0002
#define RTL_HEAP_UNCOMMITTED_RANGE (USHORT)0x0100

typedef struct _PROCESS_DEVICEMAP_INFORMATION
{
    union
    {
        struct
        {
            HANDLE DirectoryHandle;
        } Set;
        struct
        {
            ULONG DriveMap;
            UCHAR DriveType[32];
        } Query;
    };
} PROCESS_DEVICEMAP_INFORMATION, *PPROCESS_DEVICEMAP_INFORMATION;

#define MESSAGE_RESOURCE_UNICODE 0x0001
#define MESSAGE_RESOURCE_UTF8    0x0002

typedef LONG KPRIORITY;

#ifndef HIGH_PRIORITY
#define LOW_PRIORITY 0
#define HIGH_PRIORITY 31
#endif

#ifndef FILE_ATTRIBUTE_VALID_SET_FLAGS
#define FILE_ATTRIBUTE_VALID_SET_FLAGS 0x001031A7
#endif

#pragma endregion

#pragma region Console Driver

#define CONSOLE_FIRST_API_NUMBER(Layer) ((Layer) << 24)

typedef struct _CD_IO_BUFFER
{
    ULONG Size;
    PVOID Buffer;
} CD_IO_BUFFER, *PCD_IO_BUFFER;

typedef struct _CD_IO_BUFFER64
{
    ULONG Size;
    PVOID64 Buffer;
} CD_IO_BUFFER64, *PCD_IO_BUFFER64;

typedef struct _CD_USER_DEFINED_IO
{
    HANDLE Client;
    ULONG InputCount;
    ULONG OutputCount;
    CD_IO_BUFFER Buffers[ANYSIZE_ARRAY];
} CD_USER_DEFINED_IO, *PCD_USER_DEFINED_IO;

typedef struct _CD_USER_DEFINED_IO64
{
    PVOID64 Client;
    ULONG InputCount;
    ULONG OutputCount;
    CD_IO_BUFFER64 Buffers[ANYSIZE_ARRAY];
} CD_USER_DEFINED_IO64, *PCD_USER_DEFINED_IO64;

#define IOCTL_CONDRV_ISSUE_USER_IO CTL_CODE(FILE_DEVICE_CONSOLE, 5, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)

typedef struct _CONSOLE_GETCP_MSG
{
    ULONG CodePage;
    BOOLEAN Output;
} CONSOLE_GETCP_MSG, *PCONSOLE_GETCP_MSG;

typedef struct _CONSOLE_MODE_MSG
{
    ULONG Mode;
} CONSOLE_MODE_MSG, *PCONSOLE_MODE_MSG;

typedef struct _CONSOLE_READCONSOLE_MSG
{
    BOOLEAN Unicode;
    BOOLEAN ProcessControlZ;
    USHORT ExeNameLength;
    ULONG InitialNumBytes;
    ULONG CtrlWakeupMask;
    ULONG ControlKeyState;
    ULONG NumBytes;
} CONSOLE_READCONSOLE_MSG, *PCONSOLE_READCONSOLE_MSG;

typedef struct _CONSOLE_WRITECONSOLE_MSG
{
    ULONG NumBytes;
    BOOLEAN Unicode;
} CONSOLE_WRITECONSOLE_MSG, *PCONSOLE_WRITECONSOLE_MSG;

typedef enum _CONSOLE_API_NUMBER_L1
{
    ConsolepGetCP = CONSOLE_FIRST_API_NUMBER(1),
    ConsolepGetMode,
    ConsolepSetMode,
    ConsolepGetNumberOfInputEvents,
    ConsolepGetConsoleInput,
    ConsolepReadConsole,
    ConsolepWriteConsole,
    ConsolepNotifyLastClose,
    ConsolepGetLangId,
    ConsolepMapBitmap,
} CONSOLE_API_NUMBER_L1, *PCONSOLE_API_NUMBER_L1;

typedef struct _CONSOLE_MSG_HEADER
{
    ULONG ApiNumber;
    ULONG ApiDescriptorSize;
} CONSOLE_MSG_HEADER, *PCONSOLE_MSG_HEADER;

typedef union _CONSOLE_MSG_BODY_L1
{
    CONSOLE_GETCP_MSG GetConsoleCP;
    CONSOLE_MODE_MSG GetConsoleMode;
    CONSOLE_MODE_MSG SetConsoleMode;
    CONSOLE_READCONSOLE_MSG ReadConsole;
    CONSOLE_WRITECONSOLE_MSG WriteConsole;
} CONSOLE_MSG_BODY_L1, *PCONSOLE_MSG_BODY_L1;

#ifdef __cplusplus
typedef struct _CONSOLE_MSG_L1 : public CONSOLE_MSG_HEADER
{
    CONSOLE_MSG_BODY_L1 u;
} CONSOLE_MSG_L1, *PCONSOLE_MSG_L1;
#else
typedef struct _CONSOLE_MSG_L1
{
    CONSOLE_MSG_HEADER Header;
    CONSOLE_MSG_BODY_L1 u;
} CONSOLE_MSG_L1, *PCONSOLE_MSG_L1;
#endif

#pragma endregion

#pragma region Native API

EXTERN_C_START

/* Nt* system services */

NTSYSCALLAPI NTSTATUS NTAPI NtClose(_In_ HANDLE Handle);

NTSYSCALLAPI NTSTATUS NTAPI NtCreateFile(
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER AllocationSize,
    _In_ ULONG FileAttributes,
    _In_ ULONG ShareAccess,
    _In_ ULONG CreateDisposition,
    _In_ ULONG CreateOptions,
    _In_reads_bytes_opt_(EaLength) PVOID EaBuffer,
    _In_ ULONG EaLength);

NTSYSCALLAPI NTSTATUS NTAPI NtOpenFile(
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG ShareAccess,
    _In_ ULONG OpenOptions);

NTSYSCALLAPI NTSTATUS NTAPI NtReadFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_opt_ PULONG Key);

NTSYSCALLAPI NTSTATUS NTAPI NtWriteFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_opt_ PULONG Key);

NTSYSCALLAPI NTSTATUS NTAPI NtFlushBuffersFile(_In_ HANDLE FileHandle, _Out_ PIO_STATUS_BLOCK IoStatusBlock);

NTSYSCALLAPI NTSTATUS NTAPI NtLockFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_ PLARGE_INTEGER Length,
    _In_ ULONG Key,
    _In_ BOOLEAN FailImmediately,
    _In_ BOOLEAN ExclusiveLock);

NTSYSCALLAPI NTSTATUS NTAPI NtUnlockFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_ PLARGE_INTEGER Length,
    _In_ ULONG Key);

NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass);

NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_reads_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass);

NTSYSCALLAPI NTSTATUS NTAPI NtQueryDirectoryFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ BOOLEAN ReturnSingleEntry,
    _In_opt_ PUNICODE_STRING FileName,
    _In_ BOOLEAN RestartScan);

/* DOS wildcards of FsRtlIsNameInExpression */
#define DOS_STAR (L'<')
#define DOS_QM (L'>')
#define DOS_DOT (L'"')

NTSYSCALLAPI NTSTATUS NTAPI NtQueryVolumeInformationFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID FsInformation,
    _In_ ULONG Length,
    _In_ FS_INFORMATION_CLASS FsInformationClass);

NTSYSCALLAPI NTSTATUS NTAPI NtQueryAttributesFile(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PFILE_BASIC_INFORMATION FileInformation);

NTSYSCALLAPI NTSTATUS NTAPI NtQueryFullAttributesFile(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PFILE_NETWORK_OPEN_INFORMATION FileInformation);

NTSYSCALLAPI NTSTATUS NTAPI NtDeleteFile(_In_ POBJECT_ATTRIBUTES ObjectAttributes);

NTSYSCALLAPI NTSTATUS NTAPI NtDeviceIoControlFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength);

NTSYSCALLAPI NTSTATUS NTAPI NtWaitForSingleObject(
    _In_ HANDLE Handle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout);

NTSYSCALLAPI NTSTATUS NTAPI NtWaitForMultipleObjects(
    _In_ ULONG Count,
    _In_reads_(Count) HANDLE Handles[],
    _In_ WAIT_TYPE WaitType,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout);

NTSYSCALLAPI NTSTATUS NTAPI NtDelayExecution(_In_ BOOLEAN Alertable, _In_ PLARGE_INTEGER DelayInterval);
NTSYSCALLAPI NTSTATUS NTAPI NtYieldExecution(VOID);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryTimerResolution(
    _Out_ PULONG MaximumTime,
    _Out_ PULONG MinimumTime,
    _Out_ PULONG CurrentTime);

NTSYSCALLAPI NTSTATUS NTAPI NtCreateEvent(
    _Out_ PHANDLE EventHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ EVENT_TYPE EventType,
    _In_ BOOLEAN InitialState);
NTSYSCALLAPI NTSTATUS NTAPI NtSetEvent(_In_ HANDLE EventHandle, _Out_opt_ PLONG PreviousState);
NTSYSCALLAPI NTSTATUS NTAPI NtResetEvent(_In_ HANDLE EventHandle, _Out_opt_ PLONG PreviousState);

NTSYSCALLAPI NTSTATUS NTAPI NtCreateSemaphore(
    _Out_ PHANDLE SemaphoreHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ LONG InitialCount,
    _In_ LONG MaximumCount);
NTSYSCALLAPI NTSTATUS NTAPI NtReleaseSemaphore(
    _In_ HANDLE SemaphoreHandle,
    _In_ LONG ReleaseCount,
    _Out_opt_ PLONG PreviousCount);

NTSYSCALLAPI NTSTATUS NTAPI NtCreateMutant(
    _Out_ PHANDLE MutantHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ BOOLEAN InitialOwner);
NTSYSCALLAPI NTSTATUS NTAPI NtReleaseMutant(_In_ HANDLE MutantHandle, _Out_opt_ PLONG PreviousCount);

NTSYSCALLAPI NTSTATUS NTAPI NtDuplicateObject(
    _In_ HANDLE SourceProcessHandle,
    _In_ HANDLE SourceHandle,
    _In_opt_ HANDLE TargetProcessHandle,
    _Out_opt_ PHANDLE TargetHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG HandleAttributes,
    _In_ ULONG Options);

NTSYSCALLAPI NTSTATUS NTAPI NtQueryObject(
    _In_opt_ HANDLE Handle,
    _In_ OBJECT_INFORMATION_CLASS ObjectInformationClass,
    _Out_writes_bytes_opt_(ObjectInformationLength) PVOID ObjectInformation,
    _In_ ULONG ObjectInformationLength,
    _Out_opt_ PULONG ReturnLength);

NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationObject(
    _In_ HANDLE Handle,
    _In_ OBJECT_INFORMATION_CLASS ObjectInformationClass,
    _In_reads_bytes_(ObjectInformationLength) PVOID ObjectInformation,
    _In_ ULONG ObjectInformationLength);

NTSYSCALLAPI NTSTATUS NTAPI NtOpenProcess(
    _Out_ PHANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PCLIENT_ID ClientId);

NTSYSCALLAPI NTSTATUS NTAPI NtOpenThread(
    _Out_ PHANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PCLIENT_ID ClientId);

NTSYSCALLAPI NTSTATUS NTAPI NtTerminateProcess(_In_opt_ HANDLE ProcessHandle, _In_ NTSTATUS ExitStatus);
NTSYSCALLAPI NTSTATUS NTAPI NtTerminateThread(_In_opt_ HANDLE ThreadHandle, _In_ NTSTATUS ExitStatus);

NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationProcess(
    _In_ HANDLE ProcessHandle,
    _In_ PROCESSINFOCLASS ProcessInformationClass,
    _Out_writes_bytes_(ProcessInformationLength) PVOID ProcessInformation,
    _In_ ULONG ProcessInformationLength,
    _Out_opt_ PULONG ReturnLength);

NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationProcess(
    _In_ HANDLE ProcessHandle,
    _In_ PROCESSINFOCLASS ProcessInformationClass,
    _In_reads_bytes_(ProcessInformationLength) PVOID ProcessInformation,
    _In_ ULONG ProcessInformationLength);

NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationThread(
    _In_ HANDLE ThreadHandle,
    _In_ THREADINFOCLASS ThreadInformationClass,
    _Out_writes_bytes_(ThreadInformationLength) PVOID ThreadInformation,
    _In_ ULONG ThreadInformationLength,
    _Out_opt_ PULONG ReturnLength);

NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationThread(
    _In_ HANDLE ThreadHandle,
    _In_ THREADINFOCLASS ThreadInformationClass,
    _In_reads_bytes_(ThreadInformationLength) PVOID ThreadInformation,
    _In_ ULONG ThreadInformationLength);

NTSYSCALLAPI NTSTATUS NTAPI NtSuspendThread(_In_ HANDLE ThreadHandle, _Out_opt_ PULONG PreviousSuspendCount);
NTSYSCALLAPI NTSTATUS NTAPI NtResumeThread(_In_ HANDLE ThreadHandle, _Out_opt_ PULONG PreviousSuspendCount);
NTSYSCALLAPI NTSTATUS NTAPI NtGetContextThread(_In_ HANDLE ThreadHandle, _Inout_ PCONTEXT ThreadContext);
NTSYSCALLAPI NTSTATUS NTAPI NtSetContextThread(_In_ HANDLE ThreadHandle, _In_ PCONTEXT ThreadContext);

NTSYSCALLAPI NTSTATUS NTAPI NtQuerySystemInformation(
    _In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
    _Out_writes_bytes_opt_(SystemInformationLength) PVOID SystemInformation,
    _In_ ULONG SystemInformationLength,
    _Out_opt_ PULONG ReturnLength);

NTSYSCALLAPI NTSTATUS NTAPI NtQuerySystemTime(_Out_ PLARGE_INTEGER SystemTime);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryPerformanceCounter(
    _Out_ PLARGE_INTEGER PerformanceCounter,
    _Out_opt_ PLARGE_INTEGER PerformanceFrequency);

NTSYSCALLAPI NTSTATUS NTAPI NtAllocateVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID* BaseAddress,
    _In_ ULONG_PTR ZeroBits,
    _Inout_ PSIZE_T RegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG Protect);
NTSYSCALLAPI NTSTATUS NTAPI NtFreeVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID* BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _In_ ULONG FreeType);
NTSYSCALLAPI NTSTATUS NTAPI NtProtectVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID* BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _In_ ULONG NewProtect,
    _Out_ PULONG OldProtect);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _In_opt_ PVOID BaseAddress,
    _In_ MEMORY_INFORMATION_CLASS MemoryInformationClass,
    _Out_writes_bytes_(MemoryInformationLength) PVOID MemoryInformation,
    _In_ SIZE_T MemoryInformationLength,
    _Out_opt_ PSIZE_T ReturnLength);
NTSYSCALLAPI NTSTATUS NTAPI NtFlushInstructionCache(
    _In_ HANDLE ProcessHandle,
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T Length);

NTSYSCALLAPI NTSTATUS NTAPI NtGetNlsSectionPtr(
    _In_ ULONG SectionType,
    _In_ ULONG SectionData,
    _Out_opt_ PVOID* ContextData,
    _Out_ PVOID* SectionPointer,
    _Out_opt_ PSIZE_T SectionSize);

/* Ldr* */

NTSYSAPI NTSTATUS NTAPI LdrLoadDll(
    _In_opt_ PCWSTR DllPath,
    _In_opt_ PULONG DllCharacteristics,
    _In_ PUNICODE_STRING DllName,
    _Out_ PVOID* DllHandle);
NTSYSAPI NTSTATUS NTAPI LdrUnloadDll(_In_ PVOID DllHandle);
NTSYSAPI NTSTATUS NTAPI LdrGetDllHandle(
    _In_opt_ PCWSTR DllPath,
    _In_opt_ PULONG DllCharacteristics,
    _In_ PUNICODE_STRING DllName,
    _Out_ PVOID* DllHandle);
NTSYSAPI NTSTATUS NTAPI LdrGetDllHandleEx(
    _In_ ULONG Flags,
    _In_opt_ PCWSTR DllPath,
    _In_opt_ PULONG DllCharacteristics,
    _In_ PUNICODE_STRING DllName,
    _Out_ PVOID* DllHandle);
NTSYSAPI NTSTATUS NTAPI LdrGetProcedureAddress(
    _In_ PVOID DllHandle,
    _In_opt_ PANSI_STRING ProcedureName,
    _In_opt_ ULONG ProcedureNumber,
    _Out_ PVOID* ProcedureAddress);
NTSYSAPI NTSTATUS NTAPI LdrAddRefDll(_In_ ULONG Flags, _In_ PVOID DllHandle);
NTSYSAPI NTSTATUS NTAPI LdrLockLoaderLock(_In_ ULONG Flags, _Out_opt_ ULONG* Disposition, _Out_ PVOID* Cookie);
NTSYSAPI NTSTATUS NTAPI LdrUnlockLoaderLock(_In_ ULONG Flags, _Inout_ PVOID Cookie);
NTSYSAPI NTSTATUS NTAPI LdrFindResource_U(
    _In_ PVOID DllHandle,
    _In_ PULONG_PTR ResourceIdPath,
    _In_ ULONG ResourceIdPathLength,
    _Out_ PIMAGE_RESOURCE_DATA_ENTRY* ResourceDataEntry);

/* Rtl* */

NTSYSAPI ULONG NTAPI RtlNtStatusToDosError(_In_ NTSTATUS Status);
NTSYSAPI ULONG NTAPI RtlNtStatusToDosErrorNoTeb(_In_ NTSTATUS Status);
NTSYSAPI ULONG NTAPI RtlGetLastWin32Error(VOID);
NTSYSAPI NTSTATUS NTAPI RtlAdjustPrivilege(
    _In_ ULONG Privilege,
    _In_ BOOLEAN Enable,
    _In_ BOOLEAN Client,
    _Out_ PBOOLEAN WasEnabled);
NTSYSAPI VOID NTAPI RtlSetLastWin32Error(_In_ ULONG Win32Error);

NTSYSAPI VOID NTAPI RtlAcquirePebLock(VOID);
NTSYSAPI VOID NTAPI RtlReleasePebLock(VOID);

NTSYSAPI PVOID NTAPI RtlAllocateHeap(_In_ PVOID HeapHandle, _In_opt_ ULONG Flags, _In_ SIZE_T Size);
NTSYSAPI BOOLEAN NTAPI RtlFreeHeap(_In_ PVOID HeapHandle, _In_opt_ ULONG Flags, _Frees_ptr_opt_ PVOID BaseAddress);
NTSYSAPI PVOID NTAPI RtlReAllocateHeap(
    _In_ PVOID HeapHandle,
    _In_ ULONG Flags,
    _Frees_ptr_opt_ PVOID BaseAddress,
    _In_ SIZE_T Size);
NTSYSAPI SIZE_T NTAPI RtlSizeHeap(_In_ PVOID HeapHandle, _In_ ULONG Flags, _In_ PVOID BaseAddress);
NTSYSAPI BOOLEAN NTAPI RtlValidateHeap(_In_opt_ PVOID HeapHandle, _In_ ULONG Flags, _In_opt_ PVOID BaseAddress);
NTSYSAPI SIZE_T NTAPI RtlCompactHeap(_In_ PVOID HeapHandle, _In_ ULONG Flags);
NTSYSAPI NTSTATUS NTAPI RtlWalkHeap(_In_ PVOID HeapHandle, _Inout_ PRTL_HEAP_WALK_ENTRY Entry);

NTSYSAPI VOID NTAPI RtlInitializeCriticalSection(_Out_ PRTL_CRITICAL_SECTION CriticalSection);
NTSYSAPI NTSTATUS NTAPI RtlInitializeCriticalSectionAndSpinCount(
    _Out_ PRTL_CRITICAL_SECTION CriticalSection,
    _In_ ULONG SpinCount);
NTSYSAPI NTSTATUS NTAPI RtlInitializeCriticalSectionEx(
    _Out_ PRTL_CRITICAL_SECTION CriticalSection,
    _In_ ULONG SpinCount,
    _In_ ULONG Flags);
NTSYSAPI NTSTATUS NTAPI RtlDeleteCriticalSection(_Inout_ PRTL_CRITICAL_SECTION CriticalSection);
NTSYSAPI NTSTATUS NTAPI RtlEnterCriticalSection(_Inout_ PRTL_CRITICAL_SECTION CriticalSection);
NTSYSAPI NTSTATUS NTAPI RtlLeaveCriticalSection(_Inout_ PRTL_CRITICAL_SECTION CriticalSection);
NTSYSAPI LOGICAL NTAPI RtlTryEnterCriticalSection(_Inout_ PRTL_CRITICAL_SECTION CriticalSection);
NTSYSAPI ULONG NTAPI RtlSetCriticalSectionSpinCount(
    _Inout_ PRTL_CRITICAL_SECTION CriticalSection,
    _In_ ULONG SpinCount);

NTSYSAPI VOID NTAPI RtlInitializeSRWLock(_Out_ PRTL_SRWLOCK SRWLock);
NTSYSAPI VOID NTAPI RtlAcquireSRWLockExclusive(_Inout_ PRTL_SRWLOCK SRWLock);
NTSYSAPI VOID NTAPI RtlReleaseSRWLockExclusive(_Inout_ PRTL_SRWLOCK SRWLock);
NTSYSAPI VOID NTAPI RtlAcquireSRWLockShared(_Inout_ PRTL_SRWLOCK SRWLock);
NTSYSAPI VOID NTAPI RtlReleaseSRWLockShared(_Inout_ PRTL_SRWLOCK SRWLock);
NTSYSAPI BOOLEAN NTAPI RtlTryAcquireSRWLockExclusive(_Inout_ PRTL_SRWLOCK SRWLock);
NTSYSAPI BOOLEAN NTAPI RtlTryAcquireSRWLockShared(_Inout_ PRTL_SRWLOCK SRWLock);
NTSYSAPI VOID NTAPI RtlInitializeConditionVariable(_Out_ PRTL_CONDITION_VARIABLE ConditionVariable);
NTSYSAPI NTSTATUS NTAPI RtlSleepConditionVariableCS(
    _Inout_ PRTL_CONDITION_VARIABLE ConditionVariable,
    _Inout_ PRTL_CRITICAL_SECTION CriticalSection,
    _In_opt_ PLARGE_INTEGER Timeout);
NTSYSAPI NTSTATUS NTAPI RtlSleepConditionVariableSRW(
    _Inout_ PRTL_CONDITION_VARIABLE ConditionVariable,
    _Inout_ PRTL_SRWLOCK SRWLock,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ ULONG Flags);
NTSYSAPI VOID NTAPI RtlWakeConditionVariable(_Inout_ PRTL_CONDITION_VARIABLE ConditionVariable);
NTSYSAPI VOID NTAPI RtlWakeAllConditionVariable(_Inout_ PRTL_CONDITION_VARIABLE ConditionVariable);
NTSYSAPI NTSTATUS NTAPI RtlRunOnceExecuteOnce(
    _Inout_ PRTL_RUN_ONCE RunOnce,
    _In_ PRTL_RUN_ONCE_INIT_FN InitFn,
    _Inout_opt_ PVOID Parameter,
    _Outptr_opt_result_maybenull_ PVOID* Context);

NTSYSAPI ULONG NTAPI RtlFindClearBitsAndSet(
    _In_ PRTL_BITMAP BitMapHeader,
    _In_ ULONG NumberToFind,
    _In_ ULONG HintIndex);
NTSYSAPI BOOLEAN NTAPI RtlAreBitsSet(_In_ PRTL_BITMAP BitMapHeader, _In_ ULONG StartingIndex, _In_ ULONG Length);
NTSYSAPI VOID NTAPI RtlClearBits(_In_ PRTL_BITMAP BitMapHeader, _In_ ULONG StartingIndex, _In_ ULONG NumberToClear);

NTSYSAPI PVOID NTAPI RtlEncodePointer(_In_opt_ PVOID Ptr);
NTSYSAPI PVOID NTAPI RtlDecodePointer(_In_opt_ PVOID Ptr);

NTSYSAPI VOID NTAPI RtlInitializeSListHead(_Out_ PSLIST_HEADER ListHead);
NTSYSAPI PSLIST_ENTRY NTAPI RtlInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER ListHead,
    _Inout_ PSLIST_ENTRY ListEntry);
NTSYSAPI PSLIST_ENTRY NTAPI RtlInterlockedPopEntrySList(_Inout_ PSLIST_HEADER ListHead);
NTSYSAPI PSLIST_ENTRY NTAPI RtlInterlockedFlushSList(_Inout_ PSLIST_HEADER ListHead);

NTSYSAPI VOID NTAPI RtlRaiseException(_In_ PEXCEPTION_RECORD ExceptionRecord);
NTSYSAPI VOID NTAPI RtlRaiseStatus(_In_ NTSTATUS Status);
NTSYSAPI LONG NTAPI RtlUnhandledExceptionFilter(_In_ PEXCEPTION_POINTERS ExceptionPointers);
NTSYSAPI VOID NTAPI RtlSetUnhandledExceptionFilter(_In_opt_ PTOP_LEVEL_EXCEPTION_FILTER UnhandledExceptionFilter);
NTSYSAPI PVOID NTAPI RtlAddVectoredExceptionHandler(_In_ ULONG First, _In_ PVECTORED_EXCEPTION_HANDLER Handler);
NTSYSAPI ULONG NTAPI RtlRemoveVectoredExceptionHandler(_In_ PVOID Handle);
NTSYSAPI PVOID NTAPI RtlAddVectoredContinueHandler(_In_ ULONG First, _In_ PVECTORED_EXCEPTION_HANDLER Handler);
NTSYSAPI ULONG NTAPI RtlRemoveVectoredContinueHandler(_In_ PVOID Handle);
NTSYSAPI PVOID NTAPI RtlPcToFileHeader(_In_ PVOID PcValue, _Out_ PVOID* BaseOfImage);

DECLSPEC_NORETURN NTSYSAPI VOID NTAPI RtlExitUserProcess(_In_ NTSTATUS ExitStatus);
DECLSPEC_NORETURN NTSYSAPI VOID NTAPI RtlExitUserThread(_In_ NTSTATUS ExitStatus);
NTSYSAPI NTSTATUS NTAPI RtlCreateUserThread(
    _In_ HANDLE ProcessHandle,
    _In_opt_ PSECURITY_DESCRIPTOR ThreadSecurityDescriptor,
    _In_ BOOLEAN CreateSuspended,
    _In_opt_ ULONG ZeroBits,
    _In_opt_ SIZE_T MaximumStackSize,
    _In_opt_ SIZE_T CommittedStackSize,
    _In_ PTHREAD_START_ROUTINE StartAddress,
    _In_opt_ PVOID Parameter,
    _Out_opt_ PHANDLE ThreadHandle,
    _Out_opt_ PCLIENT_ID ClientId);

NTSYSAPI VOID NTAPI RtlInitUnicodeString(_Out_ PUNICODE_STRING DestinationString, _In_opt_z_ PCWSTR SourceString);
NTSYSAPI NTSTATUS NTAPI RtlInitUnicodeStringEx(_Out_ PUNICODE_STRING DestinationString, _In_opt_z_ PCWSTR SourceString);
NTSYSAPI VOID NTAPI RtlInitAnsiString(_Out_ PANSI_STRING DestinationString, _In_opt_z_ PCSZ SourceString);
NTSYSAPI VOID NTAPI RtlFreeUnicodeString(_Inout_ PUNICODE_STRING UnicodeString);
NTSYSAPI VOID NTAPI RtlFreeAnsiString(_Inout_ PANSI_STRING AnsiString);
NTSYSAPI NTSTATUS NTAPI RtlUnicodeStringToAnsiString(
    _Inout_ PANSI_STRING DestinationString,
    _In_ PCUNICODE_STRING SourceString,
    _In_ BOOLEAN AllocateDestinationString);
NTSYSAPI NTSTATUS NTAPI RtlAnsiStringToUnicodeString(
    _Inout_ PUNICODE_STRING DestinationString,
    _In_ PCANSI_STRING SourceString,
    _In_ BOOLEAN AllocateDestinationString);
NTSYSAPI NTSTATUS NTAPI RtlMultiByteToUnicodeN(
    _Out_writes_bytes_to_(MaxBytesInUnicodeString, *BytesInUnicodeString) PWCH UnicodeString,
    _In_ ULONG MaxBytesInUnicodeString,
    _Out_opt_ PULONG BytesInUnicodeString,
    _In_reads_bytes_(BytesInMultiByteString) PCSTR MultiByteString,
    _In_ ULONG BytesInMultiByteString);
NTSYSAPI NTSTATUS NTAPI RtlUnicodeToMultiByteN(
    _Out_writes_bytes_to_(MaxBytesInMultiByteString, *BytesInMultiByteString) PCHAR MultiByteString,
    _In_ ULONG MaxBytesInMultiByteString,
    _Out_opt_ PULONG BytesInMultiByteString,
    _In_reads_bytes_(BytesInUnicodeString) PCWCH UnicodeString,
    _In_ ULONG BytesInUnicodeString);
NTSYSAPI NTSTATUS NTAPI RtlUTF8ToUnicodeN(
    _Out_writes_bytes_to_(UnicodeStringMaxByteCount, *UnicodeStringActualByteCount) PWSTR UnicodeStringDestination,
    _In_ ULONG UnicodeStringMaxByteCount,
    _Out_ PULONG UnicodeStringActualByteCount,
    _In_reads_bytes_(UTF8StringByteCount) PCCH UTF8StringSource,
    _In_ ULONG UTF8StringByteCount);
NTSYSAPI NTSTATUS NTAPI RtlUnicodeToUTF8N(
    _Out_writes_bytes_to_(UTF8StringMaxByteCount, *UTF8StringActualByteCount) PCHAR UTF8StringDestination,
    _In_ ULONG UTF8StringMaxByteCount,
    _Out_ PULONG UTF8StringActualByteCount,
    _In_reads_bytes_(UnicodeStringByteCount) PCWCH UnicodeStringSource,
    _In_ ULONG UnicodeStringByteCount);
NTSYSAPI NTSTATUS NTAPI RtlCustomCPToUnicodeN(
    _In_ PCPTABLEINFO CustomCP,
    _Out_writes_bytes_to_(MaxBytesInUnicodeString, *BytesInUnicodeString) PWCH UnicodeString,
    _In_ ULONG MaxBytesInUnicodeString,
    _Out_opt_ PULONG BytesInUnicodeString,
    _In_reads_bytes_(BytesInCustomCPString) PCH CustomCPString,
    _In_ ULONG BytesInCustomCPString);
NTSYSAPI NTSTATUS NTAPI RtlUnicodeToCustomCPN(
    _In_ PCPTABLEINFO CustomCP,
    _Out_writes_bytes_to_(MaxBytesInCustomCPString, *BytesInCustomCPString) PCH CustomCPString,
    _In_ ULONG MaxBytesInCustomCPString,
    _Out_opt_ PULONG BytesInCustomCPString,
    _In_reads_bytes_(BytesInUnicodeString) PWCH UnicodeString,
    _In_ ULONG BytesInUnicodeString);
NTSYSAPI VOID NTAPI RtlInitCodePageTable(_In_reads_opt_(2) PUSHORT TableBase, _Inout_ PCPTABLEINFO CodePageTable);
NTSYSAPI NTSTATUS NTAPI RtlGetLocaleFileMappingAddress(
    _Out_ PVOID* BaseAddress,
    _Out_ PLCID DefaultLocaleId,
    _Out_opt_ PLARGE_INTEGER DefaultCasingTableSize);
NTSYSAPI WCHAR NTAPI RtlUpcaseUnicodeChar(_In_ WCHAR SourceCharacter);
NTSYSAPI WCHAR NTAPI RtlDowncaseUnicodeChar(_In_ WCHAR SourceCharacter);

NTSYSAPI NTSTATUS NTAPI RtlDosPathNameToNtPathName_U_WithStatus(
    _In_ PCWSTR DosFileName,
    _Out_ PUNICODE_STRING NtFileName,
    _Out_opt_ PWSTR* FilePart,
    _Reserved_ PVOID Reserved);
NTSYSAPI ULONG NTAPI RtlGetFullPathName_UEx(
    _In_ PCWSTR FileName,
    _In_ ULONG BufferLength,
    _Out_writes_bytes_(BufferLength) PWSTR Buffer,
    _Out_opt_ PWSTR* FilePart,
    _Out_opt_ ULONG* BytesRequired);
NTSYSAPI ULONG NTAPI RtlGetCurrentDirectory_U(_In_ ULONG BufferLength, _Out_writes_bytes_(BufferLength) PWSTR Buffer);
NTSYSAPI NTSTATUS NTAPI RtlSetCurrentDirectory_U(_In_ PUNICODE_STRING PathName);
NTSYSAPI ULONG NTAPI RtlGetFullPathName_U(
    _In_ PCWSTR FileName,
    _In_ ULONG BufferLength,
    _Out_writes_bytes_(BufferLength) PWSTR Buffer,
    _Out_opt_ PWSTR* FilePart);

NTSYSAPI NTSTATUS NTAPI RtlSetEnvironmentVar(
    _Inout_opt_ PVOID* Environment,
    _In_reads_(NameLength) PCWSTR Name,
    _In_ SIZE_T NameLength,
    _In_reads_(ValueLength) PCWSTR Value,
    _In_ SIZE_T ValueLength);
NTSYSAPI NTSTATUS NTAPI RtlQueryEnvironmentVariable_U(
    _In_opt_ PVOID Environment,
    _In_ PCUNICODE_STRING Name,
    _Inout_ PUNICODE_STRING Value);

NTSYSAPI BOOLEAN NTAPI RtlQueryPerformanceCounter(_Out_ PLARGE_INTEGER PerformanceCounter);
NTSYSAPI BOOLEAN NTAPI RtlQueryPerformanceFrequency(_Out_ PLARGE_INTEGER PerformanceFrequency);
NTSYSAPI BOOLEAN NTAPI RtlTimeToSecondsSince1970(_In_ PLARGE_INTEGER Time, _Out_ PULONG ElapsedSeconds);
NTSYSAPI NTSTATUS NTAPI RtlSystemTimeToLocalTime(_In_ PLARGE_INTEGER SystemTime, _Out_ PLARGE_INTEGER LocalTime);

NTSYSAPI VOID NTAPI RtlTimeToTimeFields(_In_ PLARGE_INTEGER Time, _Out_ PTIME_FIELDS TimeFields);
NTSYSAPI BOOLEAN NTAPI RtlTimeFieldsToTime(_In_ PTIME_FIELDS TimeFields, _Out_ PLARGE_INTEGER Time);
NTSYSAPI VOID NTAPI RtlSecondsSince1970ToTime(_In_ ULONG ElapsedSeconds, _Out_ PLARGE_INTEGER Time);
NTSYSAPI NTSTATUS NTAPI RtlLocalTimeToSystemTime(_In_ PLARGE_INTEGER LocalTime, _Out_ PLARGE_INTEGER SystemTime);
NTSYSCALLAPI NTSTATUS NTAPI NtSetSystemTime(_In_opt_ PLARGE_INTEGER SystemTime, _Out_opt_ PLARGE_INTEGER PreviousTime);

NTSYSAPI NTSTATUS NTAPI RtlFindMessage(
    _In_ PVOID DllHandle,
    _In_ ULONG MessageTableId,
    _In_ ULONG MessageLanguageId,
    _In_ ULONG MessageId,
    _Out_ PMESSAGE_RESOURCE_ENTRY* MessageEntry);

NTSYSAPI ULONG NTAPI RtlRandomEx(_Inout_ PULONG Seed);
NTSYSAPI ULONG __cdecl DbgPrint(_In_z_ _Printf_format_string_ PCSTR Format, ...);
NTSYSAPI ULONG __cdecl DbgPrintEx(_In_ ULONG ComponentId, _In_ ULONG Level, _In_z_ PCSTR Format, ...);

EXTERN_C_END

#pragma endregion
