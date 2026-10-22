/* The patterns of _findfirst (Dir.cpp) and of the wildcards of __getmainargs and __wgetmainargs (Argv.inl) */

#pragma once

/* The file name part of a pattern: after the last slash or backslash, or after the colon of a drive */
static
PCWSTR
_NtCRT_FindFileName(
    _In_ PCWSTR Pattern)
{
    PCWSTR FileName = Pattern;

    for (PCWSTR p = Pattern; *p != L'\0'; p++)
    {
        if (*p == L'\\' || *p == L'/' || (*p == L':' && p == Pattern + 1))
        {
            FileName = p + 1;
        }
    }
    return FileName;
}

/*
 * The file name part converted to the DOS wildcards of NtQueryDirectoryFile as FindFirstFileW does, into Mask (as long
 * as FileName), returns its length. The name is first that of a Win32 path, without its trailing spaces and dots, and a
 * dot again if it ended with one ("*.c " is "*.c", "*. " is "*", "*.." is "*."). Then a '?' is DOS_QM, a '.' before a
 * '?' or a '*' or at the end DOS_DOT ("*." names those without an extension), a '*' before a '.' DOS_STAR.
 */
static
size_t
_NtCRT_FindMask(
    _In_ PCWSTR FileName,
    _Out_ PWSTR Mask)
{
    size_t Length = wcslen(FileName);
    bool EndsInDot = Length != 0 && FileName[Length - 1] == L'.';

    while (Length != 0 && (FileName[Length - 1] == L' ' || FileName[Length - 1] == L'.'))
    {
        Length--;
    }
    memcpy(Mask, FileName, Length * sizeof(WCHAR));
    if (EndsInDot)
    {
        Mask[Length++] = L'.';
    }
    if (Length == 3 && Mask[0] == L'*' && Mask[1] == L'.' && Mask[2] == L'*')
    {
        return 1;
    }
    for (size_t i = 0; i < Length; i++)
    {
        WCHAR Next = i + 1 < Length ? Mask[i + 1] : L'\0';

        if (Mask[i] == L'?')
        {
            Mask[i] = DOS_QM;
        } else if (Mask[i] == L'.' && (Next == L'?' || Next == L'*' || Next == L'\0'))
        {
            Mask[i] = DOS_DOT;
        } else if (Mask[i] == L'*' && Next == L'.')
        {
            Mask[i] = DOS_STAR;
        }
    }
    return Length;
}
