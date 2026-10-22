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

/* The file name part converted to the DOS wildcards of NtQueryDirectoryFile as FindFirstFileW does, into Mask (as long
   as FileName), returns its length */
static
size_t
_NtCRT_FindMask(
    _In_ PCWSTR FileName,
    _Out_ PWSTR Mask)
{
    size_t Length = wcslen(FileName);

    if (wcscmp(FileName, L"*.*") == 0)
    {
        Mask[0] = L'*';
        return 1;
    }
    for (size_t i = 0; i < Length; i++)
    {
        WCHAR c = FileName[i];
        if (c == L'?')
        {
            c = DOS_QM;
        } else if (c == L'.' && (FileName[i + 1] == L'?' || FileName[i + 1] == L'*' || FileName[i + 1] == L'.' ||
                                 FileName[i + 1] == L'\0'))
        {
            c = DOS_DOT;
        } else if (c == L'*' && FileName[i + 1] == L'.')
        {
            c = DOS_STAR;
        }
        Mask[i] = c;
    }
    return Length;
}
