/*
 * libntcrt_glob.a: the wildcards of argv, linked by -lntcrt_glob (-ntcrt-glob of the specs and of the drivers).
 * crt2.o of MinGW-w64 calls _setargv (crt2u.o of -municode _wsetargv), which links this object before the one of
 * libmingw32, whatever _dowildcard the program has. __getmainargs and __wgetmainargs call the expansion by a weak reference, which links nothing:
 * they expand the arguments of a program that defines no _dowildcard (NTCRT_DOWILDCARD_DEFAULT) or one that is not
 * 0, and without this library stop a program that sets it with an error (Startup.cpp).
 */

#include "../NtCRT.h"

#include "../FindMask.inl"

/*
 * The wildcards of the arguments, when the program sets _dowildcard (int _dowildcard = -1, CRT_glob.o of MinGW-w64), as
 * msvcrt expands them: an argument after the program name that has * or ? is replaced by the names that match it in
 * its directory, quoted or not, as FindFirstFile matches them (DOS wildcards, the short names too). The names keep the
 * directory part of the argument as it was written, . and .. are left out, hidden files are not. They are sorted
 * with the ASCII letters folded, as _stricmp and _wcsicmp of the C locale compare: the narrow names after their
 * conversion. An argument that nothing matches, or with a wildcard in its directory part, stays as it is.
 */

template <typename T>
struct _NtCRT_ArgList
{
    T** Items;
    size_t Count;
    size_t Capacity;
};

template <typename T>
static
bool
_NtCRT_ArgAppend(
    _Inout_ _NtCRT_ArgList<T>* List,
    _In_opt_ T* Item)
{
    if (List->Count == List->Capacity)
    {
        size_t Capacity = List->Capacity * 2 + 16;
        T** Items = (T**)(List->Items == NULL ? RtlAllocateHeap(NTCRT_HEAP, 0, Capacity * sizeof(T*))
                                              : RtlReAllocateHeap(NTCRT_HEAP, 0, List->Items, Capacity * sizeof(T*)));
        if (Items == NULL)
        {
            return false;
        }
        List->Items = Items;
        List->Capacity = Capacity;
    }
    List->Items[List->Count++] = Item;
    return true;
}

static inline char* _NtCRT_ArgFromWide(const wchar_t* Name, char*) { return _NtCRT_WideToAnsi(Name); }
static inline wchar_t* _NtCRT_ArgFromWide(const wchar_t* Name, wchar_t*) { return (wchar_t*)Name; }
static inline unsigned _NtCRT_ArgUnit(char c) { return (unsigned char)c; }
static inline unsigned _NtCRT_ArgUnit(wchar_t c) { return c; }

template <typename T>
static
int
_NtCRT_ArgCompare(
    _In_ const T* a,
    _In_ const T* b)
{
    for (;; a++, b++)
    {
        unsigned ca = _NtCRT_ArgUnit(*a), cb = _NtCRT_ArgUnit(*b);
        ca += ca - 'A' < 26 ? 'a' - 'A' : 0;
        cb += cb - 'A' < 26 ? 'a' - 'A' : 0;
        if (ca != cb || ca == 0)
        {
            return (int)ca - (int)cb;
        }
    }
}

/* Append the names that match Pattern to List, false when out of memory */
template <typename T>
static
bool
_NtCRT_ArgMatch(
    _In_ PCWSTR Pattern,
    _Inout_ _NtCRT_ArgList<T>* List)
{
    PCWSTR FileName = _NtCRT_FindFileName(Pattern);
    size_t DirectoryLength = FileName - Pattern, First = List->Count;
    PWSTR Directory, Name;
    UNICODE_STRING Mask, NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Handle;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG_PTR Buffer[0x1000 / sizeof(ULONG_PTR)];
    bool Done = true, Opened = false;

    if (wcspbrk(FileName, L"*?") == NULL)
    {
        return true;
    }
    /* The directory, "." for none, then the mask */
    Directory = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, (DirectoryLength + 2 + wcslen(FileName)) * sizeof(WCHAR));
    if (Directory == NULL)
    {
        return false;
    }
    memcpy(Directory, Pattern, DirectoryLength * sizeof(WCHAR));
    wcscpy(Directory + DirectoryLength, DirectoryLength == 0 ? L"." : L"");
    Mask.Buffer = Directory + DirectoryLength + 2;
    Mask.Length = Mask.MaximumLength = (USHORT)(_NtCRT_FindMask(FileName, Mask.Buffer) * sizeof(WCHAR));
    /* Not _NtCRT_OpenPath, whose object every program would link */
    if (NT_SUCCESS(RtlDosPathNameToNtPathName_U_WithStatus(Directory, &NtPath, NULL, NULL)))
    {
        InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
        Opened = NT_SUCCESS(NtOpenFile(&Handle,
                                       FILE_LIST_DIRECTORY | SYNCHRONIZE,
                                       &ObjectAttributes,
                                       &IoStatusBlock,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT));
        RtlFreeUnicodeString(&NtPath);
    }
    if (Opened)
    {
        for (BOOLEAN Restart = TRUE; Done; Restart = FALSE)
        {
            PFILE_DIRECTORY_INFORMATION Info = (PFILE_DIRECTORY_INFORMATION)Buffer;

            if (!NT_SUCCESS(NtQueryDirectoryFile(Handle,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 &IoStatusBlock,
                                                 Buffer,
                                                 sizeof(Buffer),
                                                 FileDirectoryInformation,
                                                 FALSE,
                                                 Restart ? &Mask : NULL,
                                                 Restart)))
            {
                break;
            }
            for (;;)
            {
                size_t Length = Info->FileNameLength / sizeof(WCHAR);
                bool Dots = Info->FileName[0] == L'.' && (Length == 1 || (Length == 2 && Info->FileName[1] == L'.'));

                if (!Dots)
                {
                    T* Item;

                    Name = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, (DirectoryLength + Length + 1) * sizeof(WCHAR));
                    if (Name == NULL)
                    {
                        Done = false;
                        break;
                    }
                    memcpy(Name, Pattern, DirectoryLength * sizeof(WCHAR));
                    memcpy(Name + DirectoryLength, Info->FileName, Length * sizeof(WCHAR));
                    Name[DirectoryLength + Length] = L'\0';
                    Item = _NtCRT_ArgFromWide(Name, (T*)NULL);
                    if ((void*)Item != (void*)Name)
                    {
                        RtlFreeHeap(NTCRT_HEAP, 0, Name);
                    }
                    if (Item == NULL || !_NtCRT_ArgAppend<T>(List, Item))
                    {
                        Done = false;
                        break;
                    }
                }
                if (Info->NextEntryOffset == 0)
                {
                    break;
                }
                Info = (PFILE_DIRECTORY_INFORMATION)((PUCHAR)Info + Info->NextEntryOffset);
            }
        }
        NtClose(Handle);
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Directory);

    /* Shell sort of the names of this pattern */
    T** Items = List->Items + First;
    size_t Count = List->Count - First;
    for (size_t Gap = Count / 2; Gap != 0; Gap /= 2)
    {
        for (size_t i = Gap; i < Count; i++)
        {
            T* Item = Items[i];
            size_t j = i;
            for (; j >= Gap && _NtCRT_ArgCompare<T>(Items[j - Gap], Item) > 0; j -= Gap)
            {
                Items[j] = Items[j - Gap];
            }
            Items[j] = Item;
        }
    }
    return Done;
}

/* Argv (Argc of them) with the wildcards of Wide (the same arguments) expanded, NULL when out of memory */
template <typename T>
static
T**
_NtCRT_ExpandArgv(
    _In_ wchar_t** Wide,
    _In_ T** Argv,
    _Inout_ int* Argc)
{
    _NtCRT_ArgList<T> List = {};

    for (int i = 0; i < *Argc; i++)
    {
        size_t First = List.Count;

        if ((i != 0 && !_NtCRT_ArgMatch<T>(Wide[i], &List)) ||
            (List.Count == First && !_NtCRT_ArgAppend<T>(&List, Argv[i])))
        {
            return NULL;
        }
    }
    if (!_NtCRT_ArgAppend<T>(&List, NULL))
    {
        return NULL;
    }
    *Argc = (int)List.Count - 1;
    return List.Items;
}

extern "C" {
int __cdecl _setargv(void)
{
    return 0;
}

int __cdecl _wsetargv(void)
{
    return 0;
}

char** _NtCRT_GlobArgv(wchar_t** Wide, char** Argv, int* Argc)
{
    return _NtCRT_ExpandArgv<char>(Wide, Argv, Argc);
}

wchar_t** _NtCRT_GlobWargv(wchar_t** Argv, int* Argc)
{
    return _NtCRT_ExpandArgv<wchar_t>(Argv, Argv, Argc);
}
}
