/*
 * The runtime pseudo relocator of mingw-w64 (pseudo-reloc.c, public domain) on ntdll. It applies the
 * relocations of data auto-imported from DLLs before the C runtime is initialized. libmingw32 reports an error
 * by fprintf, which links the printf family into every program, this one writes to the standard error handle.
 *
 * libntcrt_start.a precedes libmingw32.a in the link, see the specs in the Makefile.
 */

#include "../NtCRT.h"

#include <string.h>

typedef struct
{
    DWORD Addend;
    DWORD Target;
} NTCRT_PSEUDO_RELOC_V1;

typedef struct
{
    DWORD Magic1;
    DWORD Magic2;
    DWORD Version;
} NTCRT_PSEUDO_RELOC_HEADER;

typedef struct
{
    DWORD Sym;
    DWORD Target;
    DWORD Flags;
} NTCRT_PSEUDO_RELOC_V2;

#define NTCRT_PSEUDO_RELOC_VERSION_V1 0
#define NTCRT_PSEUDO_RELOC_VERSION_V2 1

extern "C" char __RUNTIME_PSEUDO_RELOC_LIST__[];
extern "C" char __RUNTIME_PSEUDO_RELOC_LIST_END__[];
extern "C" IMAGE_DOS_HEADER __ImageBase;

static
void
_NtCRT_RelocWrite(
    _In_z_ const char* Text)
{
    HANDLE Handle = NtCurrentPeb()->ProcessParameters->StandardError;
    IO_STATUS_BLOCK IoStatusBlock;

    if (Handle != NULL)
    {
        NtWriteFile(Handle, NULL, NULL, NULL, &IoStatusBlock, (PVOID)Text, (ULONG)strlen(Text), NULL, NULL);
    }
}

static
void
_NtCRT_RelocWriteHex(
    _In_ ULONG64 Value)
{
    char Text[19] = "0x";
    int n = 16;

    while (n > 1 && (Value >> ((n - 1) * 4)) == 0)
    {
        n--;
    }
    for (int i = 0; i < n; i++)
    {
        Text[2 + i] = "0123456789abcdef"[(Value >> ((n - 1 - i) * 4)) & 0xF];
    }
    Text[2 + n] = '\0';
    _NtCRT_RelocWrite(Text);
}

/* Report a failure as libmingw32 does and exit with the code of abort */
static
__attribute__((noreturn))
void
_NtCRT_RelocFail(
    _In_z_ const char* Message,
    _In_ ULONG64 Value)
{
    _NtCRT_RelocWrite("Mingw-w64 runtime failure:\n");
    _NtCRT_RelocWrite(Message);
    _NtCRT_RelocWriteHex(Value);
    _NtCRT_RelocWrite("\n");
    RtlExitUserProcess(3);
}

/* Write to the image, making the page writable for the time of the write */
static
void
_NtCRT_RelocWriteMemory(
    _In_ PVOID Address,
    _In_reads_bytes_(Length) const void* Data,
    _In_ SIZE_T Length)
{
    MEMORY_BASIC_INFORMATION Info;
    PVOID Base = Address;
    SIZE_T Size = Length;
    ULONG Old = 0;
    BOOL Protect;
    NTSTATUS Status;

    Status = NtQueryVirtualMemory(NtCurrentProcess(), Address, MemoryBasicInformation, &Info, sizeof(Info), NULL);
    if (!NT_SUCCESS(Status))
    {
        _NtCRT_RelocFail("  NtQueryVirtualMemory failed at address ", (ULONG_PTR)Address);
    }
    Protect = !(Info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
    if (Protect)
    {
        Status = NtProtectVirtualMemory(NtCurrentProcess(),
                                        &Base,
                                        &Size,
                                        (Info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ)) ? PAGE_EXECUTE_READWRITE
                                                                                         : PAGE_READWRITE,
                                        &Old);
        if (!NT_SUCCESS(Status))
        {
            _NtCRT_RelocFail("  NtProtectVirtualMemory failed with status ", (ULONG)Status);
        }
    }
    memcpy(Address, Data, Length);
    if (Protect)
    {
        Base = Address;
        Size = Length;
        NtProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, Old, &Old);
    }
}

static
void
_NtCRT_DoPseudoReloc(
    _In_ char* Start,
    _In_ char* End,
    _In_ char* ImageBase)
{
    NTCRT_PSEUDO_RELOC_HEADER* Header = (NTCRT_PSEUDO_RELOC_HEADER*)Start;

    /* One v1 entry has 8 bytes */
    if (End - Start < 8)
    {
        return;
    }
    /* A v1 list may start with the header { 0, 0, version 1 } */
    if (End - Start >= 12 && Header->Magic1 == 0 && Header->Magic2 == 0 &&
        Header->Version == NTCRT_PSEUDO_RELOC_VERSION_V1)
    {
        Header++;
    }
    if (Header->Magic1 != 0 || Header->Magic2 != 0)
    {
        for (NTCRT_PSEUDO_RELOC_V1* r = (NTCRT_PSEUDO_RELOC_V1*)Header; (char*)r < End; r++)
        {
            char* Target = ImageBase + r->Target;
            DWORD Value;

            memcpy(&Value, Target, sizeof(Value));
            Value += r->Addend;
            _NtCRT_RelocWriteMemory(Target, &Value, sizeof(Value));
        }
        return;
    }
    if (Header->Version != NTCRT_PSEUDO_RELOC_VERSION_V2)
    {
        _NtCRT_RelocFail("  Unknown pseudo relocation protocol version ", Header->Version);
    }
    for (NTCRT_PSEUDO_RELOC_V2* r = (NTCRT_PSEUDO_RELOC_V2*)(Header + 1); (char*)r < End; r++)
    {
        /* The target holds the offset to the import address entry, it receives the offset to the import */
        char* Target = ImageBase + r->Target;
        ptrdiff_t Import = *(ptrdiff_t*)(ImageBase + r->Sym);
        unsigned int Bits = r->Flags & 0xFF;
        ptrdiff_t Data;

        switch (Bits)
        {
        case 8:
            Data = *(signed char*)Target;
            break;
        case 16:
            Data = *(short*)Target;
            break;
        case 32:
            Data = *(int*)Target;
            break;
#ifdef _WIN64
        case 64:
            Data = *(ptrdiff_t*)Target;
            break;
#endif
        default:
            _NtCRT_RelocFail("  Unknown pseudo relocation bit size ", Bits);
        }
        Data -= (ptrdiff_t)(ImageBase + r->Sym);
        Data += Import;
        if (Bits < 8 * sizeof(ptrdiff_t))
        {
            ptrdiff_t MaxUnsigned = ((ptrdiff_t)1 << Bits) - 1;
            ptrdiff_t MinSigned = (ptrdiff_t)(~(size_t)0 << (Bits - 1));

            if (Data > MaxUnsigned || Data < MinSigned)
            {
                _NtCRT_RelocFail("  Pseudo relocation out of range at ", (ULONG_PTR)Target);
            }
        }
        _NtCRT_RelocWriteMemory(Target, &Data, Bits / 8);
    }
}

EXTERN_C void _pei386_runtime_relocator(void)
{
    static BOOL Done;

    _NtCRT_ProcessAttach();
    if (Done)
    {
        return;
    }
    Done = TRUE;
    _NtCRT_DoPseudoReloc(__RUNTIME_PSEUDO_RELOC_LIST__, __RUNTIME_PSEUDO_RELOC_LIST_END__, (char*)&__ImageBase);
}
