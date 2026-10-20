/*
 * The PEB and TEB of NDK.h against what kernel32 tells: the offsets that NDK.h checks at compile time are the ones
 * the system uses, on x64 and on x86 (WOW64). The kernel32 functions are not the inline ones of NtCRT.
 */

#define NTCRT_NO_INLINE_WIN32
#include <KNSoft/NDK/NDK.h>
#include <stdio.h>

static int Failed;

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
    BOOL Wow64 = FALSE;
    HANDLE Thread;
    int i;

    CHECK(Teb->NtTib.Self == &Teb->NtTib);
    CHECK(Teb->ClientId.UniqueProcess == (HANDLE)(ULONG_PTR)GetCurrentProcessId());
    CHECK(Teb->ClientId.UniqueThread == (HANDLE)(ULONG_PTR)GetCurrentThreadId());
    SetLastError(0x1234);
    CHECK(Teb->LastErrorValue == 0x1234);
    CHECK(Teb->CurrentLocale == GetThreadLocale());
    CHECK(RtlNtStatusToDosError(STATUS_ACCESS_DENIED) == ERROR_ACCESS_DENIED &&
          Teb->LastStatusValue == STATUS_ACCESS_DENIED);
    CHECK(Teb->StaticUnicodeString.Buffer == Teb->StaticUnicodeBuffer);
    CHECK(VirtualQuery(&Mbi, &Mbi, sizeof(Mbi)) && Teb->DeallocationStack == Mbi.AllocationBase);
    CHECK(SetThreadStackGuarantee(&Guarantee) && Teb->GuaranteedStackBytes == 0x4000);

    /* The last one is in TlsExpansionSlots */
    for (i = 0; i <= TLS_MINIMUM_AVAILABLE; i++)
    {
        Tls[i] = TlsAlloc();
        TlsSetValue(Tls[i], (LPVOID)(ULONG_PTR)(0x100 + i));
    }
    CHECK(Tls[0] < TLS_MINIMUM_AVAILABLE && Teb->TlsSlots[Tls[0]] == (PVOID)0x100);
    i = TLS_MINIMUM_AVAILABLE;
    CHECK(Tls[i] >= TLS_MINIMUM_AVAILABLE && Teb->TlsExpansionSlots &&
          Teb->TlsExpansionSlots[Tls[i] - TLS_MINIMUM_AVAILABLE] == (PVOID)(ULONG_PTR)(0x100 + i));
    CHECK(Peb->TlsBitmap && Peb->TlsBitmap->Buffer == Peb->TlsBitmapBits);
    CHECK(Peb->TlsExpansionBitmap && Peb->TlsExpansionBitmap->Buffer == Peb->TlsExpansionBitmapBits);
    Fls = FlsAlloc(NULL);
    CHECK(FlsSetValue(Fls, (PVOID)1) && Teb->FlsData != NULL);
    CHECK(Teb->InitialThread);
    Thread = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    WaitForSingleObject(Thread, INFINITE);
    CHECK(GetExitCodeThread(Thread, &Code) && Code == 0);

    /* The 32-bit TEB of a WOW64 process precedes the 64-bit one, which keeps the error mode of the thread */
    IsWow64Process(GetCurrentProcess(), &Wow64);
    CHECK((Teb->WowTebOffset != 0) == !!Wow64);
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, NULL);
    if (Wow64)
        CHECK(Teb->WowTebOffset < 0 && *(PULONG)((PUCHAR)Teb + Teb->WowTebOffset + 0x16B0) != 0);
    else
        CHECK(Teb->HardErrorMode != 0);

    CHECK(Peb->ImageBaseAddress == GetModuleHandleW(NULL));
    CHECK(Peb->ProcessHeap == GetProcessHeap());
    CHECK(Peb->ProcessParameters->ImagePathName.Length != 0);
    CHECK(Peb->Ldr->InLoadOrderModuleList.Flink != NULL);
    CHECK(Peb->FastPebLock && Peb->LoaderLock && Peb->FastPebLock != Peb->LoaderLock);
    CHECK(Peb->BeingDebugged == IsDebuggerPresent());
    GetSystemInfo(&Info);
    CHECK(Peb->NumberOfProcessors == Info.dwNumberOfProcessors);
    CHECK(Peb->OSMajorVersion >= 6 && Peb->OSBuildNumber >= 7600);
    CHECK(Peb->ImageSubsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI);
    CHECK(ProcessIdToSessionId(GetCurrentProcessId(), &Session) && Peb->SessionId == Session);
    CHECK(Peb->ActiveCodePage == GetACP());
    CHECK(Peb->OemCodePage == GetOEMCP());

    if (Failed)
        return 1;
    printf("pebteb: ok\n");
    return 0;
}
