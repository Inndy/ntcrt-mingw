/*
 * Run the scenarios of child.exe in a pseudo console: type the keyboard input into it, read what the console
 * shows and look for the expected lines. Built with the default runtime of MinGW-w64, not NtCRT.
 *
 *   conpty.exe path\to\child.exe
 */
/* Pseudo consoles are of Windows 10 1809 */
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000006

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct
{
    const char* Name;
    const char* Input;      /* UTF-8 typed into the console */
    const char* Expect[8];  /* lines the console must show */
} SCENARIO;

static const SCENARIO g_Scenarios[] = {
    { "write", "",
      { "W1[a\xe4\xb8\xad\xc3\xa9]", "W2[\xf0\x9f\x98\x80]", "W3[\xe4\xb8\xad\xf0\x9f\x98\x80]",
        "W4[\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80]", "W5[\xf0\x9f\x98\x80]", "end" } },
    { "u8write", "", { "W6[\xe4\xb8\xad\xc3\xa9]", "W7[\xf0\x9f\x98\x80]", "end" } },
    { "read", "\xe4\xb8\xad\xf0\x9f\x98\x80\xc3\xa9\r" "ab\xe4\xb8\xad\r" "\x1a\r",
      { "R1:e4b8adf09f9880c3a90a", "R2:6162e4b8ad0a", "R3:eof", "end" } },
    { "u8read", "\xe4\xb8\xad\xf0\x9f\x98\x80z\r" "a\xf0\x9f\x98\x80\xe4\xb8\xad\r",
      { "R4:4e2dd83dde00007a000a", "R5:0061d83dde004e2d000a", "end" } },
    { "u16read", "\xe4\xb8\xad\xf0\x9f\x98\x80z\r" "a\xf0\x9f\x98\x80\xe4\xb8\xad\r",
      { "R4:4e2dd83dde00007a000a", "R5:0061d83dde004e2d000a", "end" } },
    { "getch", "\xe4\xb8\xad\xf0\x9f\x98\x80\xc3\xa9\xe4\xb8\xad",
      { "R6:e4b8adf09f9880", "R7:00e94e2d", "end" } },
    { "scanf", "12 \xe4\xb8\xad\r" "34 ab\r", { "S1:2 12 4e2d", "S2:2 34 ab", "end" } },
};

typedef struct
{
    HANDLE Pipe;
    char* Data;
    size_t Length;
} READER;

static DWORD WINAPI read_output(LPVOID Param)
{
    READER* r = (READER*)Param;
    char Buffer[4096];
    DWORD Read;

    while (ReadFile(r->Pipe, Buffer, sizeof(Buffer), &Read, NULL) && Read > 0)
    {
        r->Data = (char*)realloc(r->Data, r->Length + Read + 1);
        memcpy(r->Data + r->Length, Buffer, Read);
        r->Length += Read;
        r->Data[r->Length] = '\0';
    }
    return 0;
}

/* The text of the VT output: escape sequences removed, a cursor movement or a line feed starts a line */
static char* strip_vt(const char* s, size_t n)
{
    char* Out = (char*)malloc(n + 1);
    size_t o = 0;

    for (size_t i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)s[i];

        if (c == 0x1B && i + 1 < n && s[i + 1] == '[')
        {
            /* CSI: parameters up to a final byte, a cursor movement ends the line */
            for (i += 2; i < n && !((unsigned char)s[i] >= 0x40 && (unsigned char)s[i] <= 0x7E); i++)
                ;
            if (i < n && strchr("HfABCDEFGJKd", s[i]) != NULL)
                Out[o++] = '\n';
        } else if (c == 0x1B && i + 1 < n && s[i + 1] == ']')
        {
            /* OSC up to BEL or ST */
            for (i += 2; i < n && s[i] != 0x07 && !(s[i] == 0x1B && i + 1 < n && s[i + 1] == '\\'); i++)
                ;
            if (i < n && s[i] == 0x1B)
                i++;
        } else if (c == 0x1B)
        {
            i++;
        } else if (c == '\n' || c == '\r')
        {
            Out[o++] = '\n';
        } else if (c >= 0x20)
        {
            Out[o++] = (char)c;
        }
    }
    Out[o] = '\0';
    return Out;
}

static int has_line(const char* Text, const char* Line)
{
    size_t n = strlen(Line);

    for (const char* p = Text; (p = strstr(p, Line)) != NULL; p++)
    {
        /* The whole line, trailing spaces of the screen allowed */
        const char* e = p + n;

        if ((p == Text || p[-1] == '\n'))
        {
            while (*e == ' ')
                e++;
            if (*e == '\n' || *e == '\0')
                return 1;
        }
    }
    return 0;
}

static int run(const wchar_t* Child, const SCENARIO* s)
{
    HANDLE InRead, InWrite, OutRead, OutWrite;
    HPCON Console;
    COORD Size = { 100, 40 };
    STARTUPINFOEXW Si;
    PROCESS_INFORMATION Pi;
    SIZE_T AttrSize = 0;
    wchar_t CommandLine[MAX_PATH * 2];
    READER Reader = { 0 };
    HANDLE Thread;
    DWORD Written, Wait, Exit = 0;
    char* Text;
    int Failed = 0;

    CreatePipe(&InRead, &InWrite, NULL, 0);
    CreatePipe(&OutRead, &OutWrite, NULL, 0);
    if (FAILED(CreatePseudoConsole(Size, InRead, OutWrite, 0, &Console)))
    {
        printf("CreatePseudoConsole failed\n");
        return 1;
    }
    CloseHandle(InRead);
    CloseHandle(OutWrite);
    Reader.Pipe = OutRead;
    Thread = CreateThread(NULL, 0, read_output, &Reader, 0, NULL);

    ZeroMemory(&Si, sizeof(Si));
    Si.StartupInfo.cb = sizeof(Si);
    /* Without this the child inherits the standard handles of this process when they are redirected */
    Si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    InitializeProcThreadAttributeList(NULL, 1, 0, &AttrSize);
    Si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(AttrSize);
    InitializeProcThreadAttributeList(Si.lpAttributeList, 1, 0, &AttrSize);
    UpdateProcThreadAttribute(Si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, Console, sizeof(Console),
                              NULL, NULL);
    swprintf(CommandLine, MAX_PATH * 2, L"\"%ls\" %hs", Child, s->Name);
    if (!CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                        &Si.StartupInfo, &Pi))
    {
        printf("CreateProcess failed: %lu\n", GetLastError());
        return 1;
    }
    /* The console queues the keys until the child reads them */
    WriteFile(InWrite, s->Input, (DWORD)strlen(s->Input), &Written, NULL);
    Wait = WaitForSingleObject(Pi.hProcess, 10000);
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(Pi.hProcess, 1);
    GetExitCodeProcess(Pi.hProcess, &Exit);
    ClosePseudoConsole(Console);
    WaitForSingleObject(Thread, 5000);
    CloseHandle(Thread);
    CloseHandle(InWrite);
    CloseHandle(OutRead);
    CloseHandle(Pi.hProcess);
    CloseHandle(Pi.hThread);
    DeleteProcThreadAttributeList(Si.lpAttributeList);
    free(Si.lpAttributeList);

    Text = strip_vt(Reader.Data ? Reader.Data : "", Reader.Length);
    if (Wait != WAIT_OBJECT_0)
    {
        printf("  %s: timed out\n", s->Name);
        Failed = 1;
    } else if (Exit != 0)
    {
        printf("  %s: exit code %lu\n", s->Name, Exit);
        Failed = 1;
    }
    for (int i = 0; i < 8 && s->Expect[i] != NULL; i++)
    {
        if (!has_line(Text, s->Expect[i]))
        {
            printf("  %s: missing line \"%s\"\n", s->Name, s->Expect[i]);
            Failed = 1;
        }
    }
    if (Failed && getenv("CONPTY_DUMP") != NULL)
        printf("--- screen of %s ---\n%s\n---\n", s->Name, Text);
    printf("%s %s\n", Failed ? "FAIL" : "ok  ", s->Name);
    free(Text);
    free(Reader.Data);
    return Failed;
}

int main(int argc, char** argv)
{
    wchar_t Arg[MAX_PATH], Child[MAX_PATH];
    int Failures = 0;

    if (argc < 2)
    {
        printf("usage: conpty.exe child.exe [scenario]\n");
        return 2;
    }
    /* CreateProcess does not search a relative path with slashes */
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, Arg, MAX_PATH);
    GetFullPathNameW(Arg, MAX_PATH, Child, NULL);
    for (size_t i = 0; i < sizeof(g_Scenarios) / sizeof(g_Scenarios[0]); i++)
        if (argc < 3 || strcmp(argv[2], g_Scenarios[i].Name) == 0)
            Failures += run(Child, &g_Scenarios[i]);
    printf("console: %d failures\n", Failures);
    return Failures != 0;
}
