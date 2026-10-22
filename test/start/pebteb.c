/*
 * The PEB and TEB of NDK.h against what kernel32 tells: the offsets that NDK.h checks at compile time are the ones
 * the system uses, on x64 and on x86 (WOW64). NtCRT replaces the imports of the kernel32 functions it has, which read
 * the same fields: the ones of kernel32 come from its exports.
 */

#define NTCRT_NO_INLINE_WIN32
#include <KNSoft/NDK/NDK.inl>
#include <stdio.h>
#include <stdlib.h>

static int Failed;

static PVOID Kernel32(PCSTR Name)
{
    ANSI_STRING ProcedureName;
    PVOID Address;

    _Inline_RtlInitAnsiString(&ProcedureName, Name);
    if (!NT_SUCCESS(LdrGetProcedureAddress(_Inline_GetModuleHandleW(L"kernel32.dll"), &ProcedureName, 0, &Address)))
    {
        printf("FAIL kernel32 exports no %s\n", Name);
        exit(1);
    }
    return Address;
}

#define K32(Name) ((__typeof__(&Name))Kernel32(#Name))

#define CHECK(Cond) \
    do \
    { \
        if (!(Cond)) \
        { \
            printf("FAIL %s\n", #Cond); \
            Failed = 1; \
        } \
    } while (0)

/* InitialThread of SameTebFlags */
static DWORD WINAPI Worker(LPVOID Arg)
{
    (void)Arg;
    return NtCurrentTeb()->InitialThread;
}

int main(void)
{
    PTEB Teb = NtCurrentTeb();
    PPEB Peb = NtCurrentPeb();
    MEMORY_BASIC_INFORMATION Mbi;
    SYSTEM_INFO Info;
    DWORD Session = 0, Tls[TLS_MINIMUM_AVAILABLE + 1], Fls, Code = 1;
    ULONG Guarantee = 0x4000;
    BOOL Wow64 = FALSE, Win7;
    HANDLE Thread;
    int i;

    CHECK(Teb->NtTib.Self == &Teb->NtTib);
    CHECK(Teb->ClientId.UniqueProcess == (HANDLE)(ULONG_PTR)K32(GetCurrentProcessId)());
    CHECK(Teb->ClientId.UniqueThread == (HANDLE)(ULONG_PTR)K32(GetCurrentThreadId)());
    K32(SetLastError)(0x1234);
    CHECK(Teb->LastErrorValue == 0x1234);
    CHECK(Teb->CurrentLocale == K32(GetThreadLocale)());
    CHECK(RtlNtStatusToDosError(STATUS_ACCESS_DENIED) == ERROR_ACCESS_DENIED &&
          Teb->LastStatusValue == STATUS_ACCESS_DENIED);
    CHECK(Teb->StaticUnicodeString.Buffer == Teb->StaticUnicodeBuffer);
    CHECK(K32(VirtualQuery)(&Mbi, &Mbi, sizeof(Mbi)) && Teb->DeallocationStack == Mbi.AllocationBase);
    CHECK(K32(SetThreadStackGuarantee)(&Guarantee) && Teb->GuaranteedStackBytes == 0x4000);

    /* The last one is in TlsExpansionSlots */
    for (i = 0; i <= TLS_MINIMUM_AVAILABLE; i++)
    {
        Tls[i] = K32(TlsAlloc)();
        K32(TlsSetValue)(Tls[i], (LPVOID)(ULONG_PTR)(0x100 + i));
    }
    CHECK(Tls[0] < TLS_MINIMUM_AVAILABLE && Teb->TlsSlots[Tls[0]] == (PVOID)0x100);
    i = TLS_MINIMUM_AVAILABLE;
    CHECK(Tls[i] >= TLS_MINIMUM_AVAILABLE && Teb->TlsExpansionSlots &&
          Teb->TlsExpansionSlots[Tls[i] - TLS_MINIMUM_AVAILABLE] == (PVOID)(ULONG_PTR)(0x100 + i));
    CHECK(Peb->TlsBitmap && Peb->TlsBitmap->Buffer == Peb->TlsBitmapBits);
    CHECK(Peb->TlsExpansionBitmap && Peb->TlsExpansionBitmap->Buffer == Peb->TlsExpansionBitmapBits);
    Fls = K32(FlsAlloc)(NULL);
    CHECK(K32(FlsSetValue)(Fls, (PVOID)1) && Teb->FlsData != NULL);
    CHECK(Teb->InitialThread);
    Thread = K32(CreateThread)(NULL, 0, Worker, NULL, 0, NULL);
    K32(WaitForSingleObject)(Thread, INFINITE);
    CHECK(K32(GetExitCodeThread)(Thread, &Code) && Code == 0);

    /*
     * The 32-bit TEB of a WOW64 process precedes the 64-bit one by 0x2000, which keeps the error mode of the thread.
     * Windows 7 has the same layout but tells no WowTebOffset (0), Windows 8 and later do.
     */
    K32(IsWow64Process)(K32(GetCurrentProcess)(), &Wow64);
    Win7 = Peb->OSMajorVersion == 6 && Peb->OSMinorVersion < 2;
    CHECK(Teb->WowTebOffset == (Wow64 && !Win7 ? -0x2000 : 0));
    K32(SetThreadErrorMode)(SEM_FAILCRITICALERRORS, NULL);
    if (Wow64)
        CHECK(*(PULONG)((PUCHAR)Teb - 0x2000 + 0x16B0) != 0);
    else
        CHECK(Teb->HardErrorMode != 0);

    CHECK(Peb->ImageBaseAddress == K32(GetModuleHandleW)(NULL));
    CHECK(Peb->ProcessHeap == K32(GetProcessHeap)());
    CHECK(Peb->ProcessParameters->ImagePathName.Length != 0);
    CHECK(Peb->Ldr->InLoadOrderModuleList.Flink != NULL);
    CHECK(Peb->FastPebLock && Peb->LoaderLock && Peb->FastPebLock != Peb->LoaderLock);
    CHECK(Peb->BeingDebugged == K32(IsDebuggerPresent)());
    K32(GetSystemInfo)(&Info);
    CHECK(Peb->NumberOfProcessors == Info.dwNumberOfProcessors);
    CHECK(Peb->OSMajorVersion >= 6 && Peb->OSBuildNumber >= 7600);
    CHECK(Peb->ImageSubsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI);
    CHECK(K32(ProcessIdToSessionId)(K32(GetCurrentProcessId)(), &Session) && Peb->SessionId == Session);
    /* The code pages: in the header of the .nls data when there is one, in the PEB since Windows 10 */
    if (Peb->OSMajorVersion >= 10)
    {
        CHECK(Peb->ActiveCodePage == K32(GetACP)());
        CHECK(Peb->OemCodePage == K32(GetOEMCP)());
    }
    CHECK(Peb->AnsiCodePageData == NULL || ((PUSHORT)Peb->AnsiCodePageData)[1] == K32(GetACP)());
    CHECK(Peb->OemCodePageData == NULL || ((PUSHORT)Peb->OemCodePageData)[1] == K32(GetOEMCP)());

    if (Failed)
        return 1;
    printf("pebteb: ok\n");
    return 0;
}
