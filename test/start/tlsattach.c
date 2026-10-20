/*
 * The DLL_PROCESS_ATTACH of the TLS callbacks, once before main. Windows 7 sends none to a program that imports ntdll
 * and kernel32 only, NtCRT sends it then: a copy of this program without its TLS directory, to which no loader sends
 * any, gets it from NtCRT alone.
 */

#include <process.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern IMAGE_DOS_HEADER __ImageBase;
static volatile LONG g_Attach;

static void NTAPI Callback(PVOID DllHandle, DWORD Reason, PVOID Reserved)
{
    (void)Reserved;
    if (Reason == DLL_PROCESS_ATTACH && DllHandle == &__ImageBase)
    {
        InterlockedIncrement(&g_Attach);
    }
}

__attribute__((section(".CRT$XLY"), used)) const PIMAGE_TLS_CALLBACK TlsAttachEntry = Callback;

/* This program, its TLS directory zeroed, as Copy */
static int WriteCopy(const char* Self, const char* Copy)
{
    static char Image[4 << 20];
    IMAGE_NT_HEADERS Headers;
    LONG Offset;
    size_t Size, Directory;
    FILE* File;

    if ((File = fopen(Self, "rb")) == NULL)
    {
        return 0;
    }
    Size = fread(Image, 1, sizeof(Image), File);
    fclose(File);
    memcpy(&Offset, Image + offsetof(IMAGE_DOS_HEADER, e_lfanew), sizeof(Offset));
    if (Size == sizeof(Image) || Offset <= 0 || (size_t)Offset + sizeof(Headers) > Size)
    {
        return 0;
    }
    Directory = (size_t)Offset + offsetof(IMAGE_NT_HEADERS, OptionalHeader.DataDirectory) +
                IMAGE_DIRECTORY_ENTRY_TLS * sizeof(IMAGE_DATA_DIRECTORY);
    memset(Image + Directory, 0, sizeof(IMAGE_DATA_DIRECTORY));
    if ((File = fopen(Copy, "wb")) == NULL)
    {
        return 0;
    }
    Size = fwrite(Image, 1, Size, File) == Size;
    return fclose(File) == 0 && Size;
}

int main(int argc, char** argv)
{
    static const char Copy[] = "tlsattach_notls.exe";
    intptr_t Status;

    (void)argv;
    if (argc > 1)
    {
        return (int)g_Attach;
    }
    if (g_Attach != 1)
    {
        printf("tlsattach: %ld process attach from the loader\n", g_Attach);
        return 1;
    }
    if (!WriteCopy(_pgmptr, Copy))
    {
        printf("tlsattach: no copy\n");
        return 1;
    }
    Status = _spawnl(_P_WAIT, Copy, Copy, "child", NULL);
    remove(Copy);
    if (Status != 1)
    {
        printf("tlsattach: %ld process attach without the TLS directory\n", (long)Status);
        return 1;
    }
    printf("tlsattach: ok\n");
    return 0;
}
