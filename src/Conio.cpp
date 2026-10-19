/* msvcrt console I/O, console input needs kernel32 */

#include "NtCRT.h"

#include <conio.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

EXTERN_C HANDLE WINAPI _K32_CreateFileW(LPCWSTR lpFileName,
                                        DWORD dwDesiredAccess,
                                        DWORD dwShareMode,
                                        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                                        DWORD dwCreationDisposition,
                                        DWORD dwFlagsAndAttributes,
                                        HANDLE hTemplateFile);

typedef BOOL(WINAPI* PNTCRT_CONSOLE_INPUT)(HANDLE, PINPUT_RECORD, DWORD, LPDWORD);
typedef BOOL(WINAPI* PNTCRT_GET_NUMBER_OF_CONSOLE_INPUT_EVENTS)(HANDLE, LPDWORD);
typedef BOOL(WINAPI* PNTCRT_SET_CONSOLE_MODE)(HANDLE, DWORD);
typedef BOOL(WINAPI* PNTCRT_READ_CONSOLE_W)(HANDLE, LPVOID, DWORD, LPDWORD, PCONSOLE_READCONSOLE_CONTROL);
typedef UINT(WINAPI* PNTCRT_GET_CONSOLE_CP)(VOID);

static struct
{
    volatile LONG Resolved;
    PNTCRT_CONSOLE_INPUT ReadConsoleInputW;
    PNTCRT_CONSOLE_INPUT PeekConsoleInputW;
    PNTCRT_GET_NUMBER_OF_CONSOLE_INPUT_EVENTS GetNumberOfConsoleInputEvents;
    PNTCRT_SET_CONSOLE_MODE SetConsoleMode;
    PNTCRT_READ_CONSOLE_W ReadConsoleW;
    PNTCRT_GET_CONSOLE_CP GetConsoleCP;
} g_ConsoleApi;

static
BOOL
_NtCRT_ResolveConsoleApi(void)
{
    if (!g_ConsoleApi.Resolved)
    {
        g_ConsoleApi.ReadConsoleInputW = (PNTCRT_CONSOLE_INPUT)_NtCRT_GetKernel32Function("ReadConsoleInputW");
        g_ConsoleApi.PeekConsoleInputW = (PNTCRT_CONSOLE_INPUT)_NtCRT_GetKernel32Function("PeekConsoleInputW");
        g_ConsoleApi.GetNumberOfConsoleInputEvents =
            (PNTCRT_GET_NUMBER_OF_CONSOLE_INPUT_EVENTS)_NtCRT_GetKernel32Function("GetNumberOfConsoleInputEvents");
        g_ConsoleApi.SetConsoleMode = (PNTCRT_SET_CONSOLE_MODE)_NtCRT_GetKernel32Function("SetConsoleMode");
        g_ConsoleApi.ReadConsoleW = (PNTCRT_READ_CONSOLE_W)_NtCRT_GetKernel32Function("ReadConsoleW");
        g_ConsoleApi.GetConsoleCP = (PNTCRT_GET_CONSOLE_CP)_NtCRT_GetKernel32Function("GetConsoleCP");
        _InterlockedExchange(&g_ConsoleApi.Resolved, TRUE);
    }
    return g_ConsoleApi.ReadConsoleInputW != NULL && g_ConsoleApi.PeekConsoleInputW != NULL &&
           g_ConsoleApi.GetNumberOfConsoleInputEvents != NULL && g_ConsoleApi.SetConsoleMode != NULL &&
           g_ConsoleApi.ReadConsoleW != NULL && g_ConsoleApi.GetConsoleCP != NULL;
}

static
HANDLE
_NtCRT_OpenConsole(
    _Inout_ HANDLE volatile* Cache,
    _In_z_ PCWSTR Name)
{
    if (*Cache == INVALID_HANDLE_VALUE)
    {
        HANDLE Handle = _K32_CreateFileW(Name,
                                         GENERIC_READ | GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         NULL,
                                         OPEN_EXISTING,
                                         0,
                                         NULL);
        if (Handle == INVALID_HANDLE_VALUE)
        {
            return INVALID_HANDLE_VALUE;
        }
        if (_InterlockedCompareExchangePointer(Cache, Handle, INVALID_HANDLE_VALUE) != INVALID_HANDLE_VALUE)
        {
            NtClose(Handle);
        }
    }
    return *Cache;
}

static HANDLE volatile g_ConsoleOutput = INVALID_HANDLE_VALUE;
static HANDLE volatile g_ConsoleInput = INVALID_HANDLE_VALUE;

static
HANDLE
_NtCRT_ConsoleInput(void)
{
    return _NtCRT_ResolveConsoleApi() ? _NtCRT_OpenConsole(&g_ConsoleInput, L"CONIN$") : INVALID_HANDLE_VALUE;
}

/* Narrow characters of the console are UTF-8 in "C.UTF-8", else of the code page of the console */
static
UINT
_NtCRT_ConsoleInputCP(void)
{
    UINT CodePage = g_ConsoleApi.GetConsoleCP != NULL ? g_ConsoleApi.GetConsoleCP() : 0;

    if (_NtCRT_CurrentCodePage() == CP_UTF8)
    {
        return CP_UTF8;
    }
    return CodePage != 0 ? CodePage : _NtCRT_CodePage();
}

/* Console output */

static
BOOL
_NtCRT_ConsoleWrite(
    _In_reads_(Count) const wchar_t* Buffer,
    _In_ DWORD Count)
{
    HANDLE Console = _NtCRT_OpenConsole(&g_ConsoleOutput, L"CONOUT$");
    DWORD Written;

    return Console != INVALID_HANDLE_VALUE && _NtCRT_WriteConsoleW(Console, Buffer, Count, &Written) &&
           Written == Count;
}

/* Write to the console as _putwch, for a Unicode text mode fd of a console */
EXTERN_C BOOL _NtCRT_ConsoleWriteW(_In_reads_(Count) const wchar_t* Buffer, _In_ DWORD Count)
{
    BOOL Ret;

    _NtCRT_lock(_CONIO_LOCK);
    Ret = _NtCRT_ConsoleWrite(Buffer, Count);
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

EXTERN_C wint_t __cdecl _NtCRT__putwch(wchar_t ch)
{
    wint_t Ret;

    _NtCRT_lock(_CONIO_LOCK);
    Ret = _NtCRT_ConsoleWrite(&ch, 1) ? ch : WEOF;
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__cputws(const wchar_t* str)
{
    int Ret;

    if (str == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_lock(_CONIO_LOCK);
    Ret = _NtCRT_ConsoleWrite(str, (DWORD)wcslen(str)) ? 0 : -1;
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

/* The start of a UTF-8 character written by _putch in "C.UTF-8" */
static char g_PutCarry[4];
static int g_PutCarryLength;

static
int
_NtCRT_ConsoleWriteA(
    _In_reads_(Length) const char* Str,
    _In_ int Length)
{
    WCHAR Stack[256];
    PWSTR Wide = Stack;
    UINT CodePage = _CRT_GetConsoleOutputCP();
    char Joined[8];
    int Count, Ret;

    if (CodePage == 0)
    {
        CodePage = _NtCRT_CodePage();
    }
    if (_NtCRT_CurrentCodePage() == CP_UTF8)
    {
        int Tail = 0;

        CodePage = CP_UTF8;
        if (g_PutCarryLength != 0 && Length <= 4)
        {
            /* Continue a character written a byte at a time */
            memcpy(Joined, g_PutCarry, g_PutCarryLength);
            memcpy(Joined + g_PutCarryLength, Str, Length);
            Length += g_PutCarryLength;
            Str = Joined;
            g_PutCarryLength = 0;
        }
        while (Tail < 3 && Tail < Length && ((unsigned char)Str[Length - 1 - Tail] & 0xC0) == 0x80)
        {
            Tail++;
        }
        if (Tail < Length)
        {
            unsigned char Lead = (unsigned char)Str[Length - 1 - Tail];
            int Need = Lead >= 0xF0 && Lead <= 0xF4 ? 4 : Lead >= 0xE0 ? 3 : Lead >= 0xC2 ? 2 : 1;

            if (Lead >= 0xC2 && Lead <= 0xF4 && Need > Tail + 1)
            {
                g_PutCarryLength = Tail + 1;
                memcpy(g_PutCarry, Str + Length - g_PutCarryLength, g_PutCarryLength);
                Length -= g_PutCarryLength;
                if (Length == 0)
                {
                    return 0;
                }
            }
        }
    }
    Count = _NtCRT_MultiByteToWideChar(CodePage, 0, Str, Length, NULL, 0);
    if (Count <= 0)
    {
        return Length == 0 ? 0 : -1;
    }
    if ((size_t)Count > ARRAYSIZE(Stack))
    {
        Wide = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, Count * sizeof(WCHAR));
        if (Wide == NULL)
        {
            return -1;
        }
    }
    _NtCRT_MultiByteToWideChar(CodePage, 0, Str, Length, Wide, Count);
    Ret = _NtCRT_ConsoleWrite(Wide, Count) ? 0 : -1;
    if (Wide != Stack)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Wide);
    }
    return Ret;
}

static
int
_NtCRT_putch_nolock(
    _In_ int ch)
{
    char c = (char)ch;
    return _NtCRT_ConsoleWriteA(&c, 1) == 0 ? (unsigned char)c : EOF;
}

EXTERN_C int __cdecl _NtCRT__putch(int ch)
{
    int Ret;

    _NtCRT_lock(_CONIO_LOCK);
    Ret = _NtCRT_putch_nolock(ch);
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__cputs(const char* str)
{
    int Ret;

    if (str == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    _NtCRT_lock(_CONIO_LOCK);
    Ret = _NtCRT_ConsoleWriteA(str, (int)strlen(str));
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

/* Formatted once, a %n of the format may change the arguments */
EXTERN_C int __cdecl _NtCRT__vcprintf(const char* format, va_list ap)
{
    char* Buffer;
    int Length = _NtCRT_FormatAlloc(&Buffer, format, ap, 0);

    if (Length < 0)
    {
        return -1;
    }
    _NtCRT_lock(_CONIO_LOCK);
    if (_NtCRT_ConsoleWriteA(Buffer, Length) != 0)
    {
        Length = -1;
    }
    _NtCRT_unlock(_CONIO_LOCK);
    free(Buffer);
    return Length;
}

EXTERN_C int __cdecl _NtCRT__cprintf(const char* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vcprintf(format, ap);
    va_end(ap);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__vcwprintf(const wchar_t* format, va_list ap)
{
    wchar_t Stack[256], *Buffer = Stack;
    int Length;

    if (format == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Length = _NtCRT_FormatGrowW(&Buffer, ARRAYSIZE(Stack), format, ap, NTCRT_PRINTF_LEGACY_WIDE);
    if (Length >= 0)
    {
        _NtCRT_lock(_CONIO_LOCK);
        if (!_NtCRT_ConsoleWrite(Buffer, (DWORD)Length))
        {
            Length = -1;
        }
        _NtCRT_unlock(_CONIO_LOCK);
    }
    if (Buffer != Stack)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Buffer);
    }
    return Length;
}

EXTERN_C int __cdecl _NtCRT__cwprintf(const wchar_t* format, ...)
{
    va_list ap;
    int Ret;

    va_start(ap, format);
    Ret = _NtCRT__vcwprintf(format, ap);
    va_end(ap);
    return Ret;
}

/* Console input */

/* Codes of keys without a character, the lead byte is 0xE0 for enhanced keys or 0 */
typedef struct _NTCRT_EXTENDED_KEY
{
    UCHAR ScanCode;
    UCHAR Normal;
    UCHAR Shift;
    UCHAR Ctrl;
    UCHAR Alt;
} NTCRT_EXTENDED_KEY;

static const NTCRT_EXTENDED_KEY g_ExtendedKeys[] = {
    { 0x3B, 0x3B, 0x54, 0x5E, 0x68 }, /* F1 */
    { 0x3C, 0x3C, 0x55, 0x5F, 0x69 },
    { 0x3D, 0x3D, 0x56, 0x60, 0x6A },
    { 0x3E, 0x3E, 0x57, 0x61, 0x6B },
    { 0x3F, 0x3F, 0x58, 0x62, 0x6C },
    { 0x40, 0x40, 0x59, 0x63, 0x6D },
    { 0x41, 0x41, 0x5A, 0x64, 0x6E },
    { 0x42, 0x42, 0x5B, 0x65, 0x6F },
    { 0x43, 0x43, 0x5C, 0x66, 0x70 },
    { 0x44, 0x44, 0x5D, 0x67, 0x71 }, /* F10 */
    { 0x47, 0x47, 0x47, 0x77, 0x97 }, /* Home */
    { 0x48, 0x48, 0x48, 0x8D, 0x98 }, /* Up */
    { 0x49, 0x49, 0x49, 0x84, 0x99 }, /* Page Up */
    { 0x4B, 0x4B, 0x4B, 0x73, 0x9B }, /* Left */
    { 0x4D, 0x4D, 0x4D, 0x74, 0x9D }, /* Right */
    { 0x4F, 0x4F, 0x4F, 0x75, 0x9F }, /* End */
    { 0x50, 0x50, 0x50, 0x91, 0xA0 }, /* Down */
    { 0x51, 0x51, 0x51, 0x76, 0xA1 }, /* Page Down */
    { 0x52, 0x52, 0x52, 0x92, 0xA2 }, /* Insert */
    { 0x53, 0x53, 0x53, 0x93, 0xA3 }, /* Delete */
    { 0x57, 0x85, 0x87, 0x89, 0x8B }, /* F11 */
    { 0x58, 0x86, 0x88, 0x8A, 0x8C }, /* F12 */
};

/* Get the extended key code pair of a key event, or FALSE */
static
BOOL
_NtCRT_ExtendedKey(
    _In_ const KEY_EVENT_RECORD* Key,
    _Out_ PUCHAR Lead,
    _Out_ PUCHAR Code)
{
    DWORD State = Key->dwControlKeyState;

    for (const auto& Entry : g_ExtendedKeys)
    {
        if (Entry.ScanCode != Key->wVirtualScanCode)
        {
            continue;
        }
        if (State & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
        {
            *Code = Entry.Alt;
            *Lead = 0;
            return TRUE;
        }
        if (State & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
        {
            *Code = Entry.Ctrl;
        } else if (State & SHIFT_PRESSED)
        {
            *Code = Entry.Shift;
        } else
        {
            *Code = Entry.Normal;
        }
        *Lead = (State & ENHANCED_KEY) ? 0xE0 : 0;
        return TRUE;
    }
    return FALSE;
}

/*
 * The character or the extended key of a key event, 0 for an event that is not a key. Alt and the keypad digits
 * compose a character that the console reports with the release of Alt, as it does for characters it cannot map
 * to a key (a character beyond U+FFFF typed into a pseudo console arrives as two of them, one per surrogate).
 * msvcrt and UCRT report the keypad digits as Alt+arrow keys and lose the character.
 */
static
int
_NtCRT_KeyEventInput(
    _In_ const INPUT_RECORD* Record,
    _Out_ wint_t* Char,
    _Out_ PUCHAR Lead,
    _Out_ PUCHAR Code)
{
    const KEY_EVENT_RECORD* Key = &Record->Event.KeyEvent;
    DWORD State = Key->dwControlKeyState;
    WORD Scan = Key->wVirtualScanCode;

    if (Record->EventType != KEY_EVENT)
    {
        return 0;
    }
    if (!Key->bKeyDown)
    {
        if (Key->wVirtualKeyCode != VK_MENU || Key->uChar.UnicodeChar == UNICODE_NULL)
        {
            return 0;
        }
        *Char = Key->uChar.UnicodeChar;
        return 1;
    }
    if (Key->uChar.UnicodeChar != UNICODE_NULL)
    {
        *Char = Key->uChar.UnicodeChar;
        return 1;
    }
    /* Keypad 7 8 9, 4 5 6, 1 2 3 and 0 */
    if ((State & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) && !(State & ENHANCED_KEY) &&
        ((Scan >= 0x47 && Scan <= 0x49) || (Scan >= 0x4B && Scan <= 0x4D) || (Scan >= 0x4F && Scan <= 0x52)))
    {
        return 0;
    }
    return _NtCRT_ExtendedKey(Key, Lead, Code) ? 2 : 0;
}

/* Character pending for the next _getch (extended key code or _ungetch), or EOF */
static int g_PendingChar = EOF;
/* Bytes of a multibyte character pending for the next _getch after g_PendingChar */
static unsigned char g_PendingBytes[3];
static int g_PendingCount;
static wint_t g_PendingWideChar = WEOF;

/* Wait for a key, returns the character or the lead byte of an extended key with Code set */
static
wint_t
_NtCRT_ReadKey(
    _Out_ PINT Code)
{
    HANDLE Console = _NtCRT_ConsoleInput();
    DWORD OldMode;
    wint_t Result = WEOF;

    *Code = EOF;
    if (Console == INVALID_HANDLE_VALUE || !_CRT_GetConsoleMode(Console, &OldMode))
    {
        return WEOF;
    }
    g_ConsoleApi.SetConsoleMode(Console, 0);
    for (;;)
    {
        INPUT_RECORD Record;
        DWORD Read;
        UCHAR Lead, Extended;
        int Kind;

        if (!g_ConsoleApi.ReadConsoleInputW(Console, &Record, 1, &Read) || Read == 0)
        {
            break;
        }
        Kind = _NtCRT_KeyEventInput(&Record, &Result, &Lead, &Extended);
        if (Kind == 1)
        {
            break;
        } else if (Kind == 2)
        {
            Result = Lead;
            *Code = Extended;
            break;
        }
    }
    g_ConsoleApi.SetConsoleMode(Console, OldMode);
    return Result;
}

static
wint_t
_NtCRT_getwch_nolock(void)
{
    wint_t Result;
    int Code;

    if (g_PendingWideChar != WEOF)
    {
        Result = g_PendingWideChar;
        g_PendingWideChar = WEOF;
        return Result;
    }
    Result = _NtCRT_ReadKey(&Code);
    if (Code != EOF)
    {
        g_PendingWideChar = (wint_t)Code;
    }
    return Result;
}

static
int
_NtCRT_getch_nolock(void)
{
    wint_t Char;
    int Code, Length, Units = 1;
    WCHAR Pair[2];
    char Bytes[8];

    if (g_PendingChar != EOF)
    {
        Code = g_PendingChar;
        g_PendingChar = EOF;
        return Code;
    }
    if (g_PendingCount != 0)
    {
        Code = g_PendingBytes[0];
        memmove(g_PendingBytes, g_PendingBytes + 1, --g_PendingCount);
        return Code;
    }
    Char = _NtCRT_ReadKey(&Code);
    if (Char == WEOF)
    {
        return EOF;
    }
    if (Code != EOF)
    {
        g_PendingChar = Code;
        return (int)Char;
    }
    Pair[0] = (WCHAR)Char;
    if (IS_HIGH_SURROGATE(Pair[0]))
    {
        /* The low surrogate is the next key */
        wint_t Low = _NtCRT_ReadKey(&Code);

        if (Low != WEOF && Code == EOF && IS_LOW_SURROGATE(Low))
        {
            Pair[Units++] = (WCHAR)Low;
        }
    }
    Length = _NtCRT_WideCharToMultiByte(_NtCRT_ConsoleInputCP(), 0, Pair, Units, Bytes, sizeof(Bytes), NULL, NULL);
    if (Length <= 0)
    {
        return '?';
    }
    g_PendingCount = Length - 1;
    memcpy(g_PendingBytes, Bytes + 1, g_PendingCount);
    return (unsigned char)Bytes[0];
}

EXTERN_C int __cdecl _NtCRT__getch(void)
{
    int Ret;

    _NtCRT_lock(_CONIO_LOCK);
    Ret = _NtCRT_getch_nolock();
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

EXTERN_C wint_t __cdecl _NtCRT__getwch(void)
{
    wint_t Ret;

    _NtCRT_lock(_CONIO_LOCK);
    Ret = _NtCRT_getwch_nolock();
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

/* Pending characters are returned without echo, as msvcrt does */
EXTERN_C int __cdecl _NtCRT__getche(void)
{
    int Ret;

    _NtCRT_lock(_CONIO_LOCK);
    if (g_PendingChar != EOF)
    {
        Ret = g_PendingChar;
        g_PendingChar = EOF;
    } else
    {
        Ret = _NtCRT_getch_nolock();
        if (Ret != EOF && _NtCRT_putch_nolock(Ret) == EOF)
        {
            Ret = EOF;
        }
    }
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

EXTERN_C wint_t __cdecl _NtCRT__getwche(void)
{
    wint_t Ret;

    _NtCRT_lock(_CONIO_LOCK);
    if (g_PendingWideChar != WEOF)
    {
        Ret = g_PendingWideChar;
        g_PendingWideChar = WEOF;
    } else
    {
        Ret = _NtCRT_getwch_nolock();
        if (Ret != WEOF && !_NtCRT_ConsoleWrite((const wchar_t*)&Ret, 1))
        {
            Ret = WEOF;
        }
    }
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__ungetch(int ch)
{
    int Ret = EOF;

    _NtCRT_lock(_CONIO_LOCK);
    if (ch != EOF && g_PendingChar == EOF)
    {
        g_PendingChar = ch & 0xFF;
        Ret = ch;
    }
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

EXTERN_C wint_t __cdecl _NtCRT__ungetwch(wint_t ch)
{
    wint_t Ret = WEOF;

    _NtCRT_lock(_CONIO_LOCK);
    if (ch != WEOF && g_PendingWideChar == WEOF)
    {
        g_PendingWideChar = ch;
        Ret = ch;
    }
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

EXTERN_C int __cdecl _NtCRT__kbhit(void)
{
    HANDLE Console;
    DWORD Count, Read;
    PINPUT_RECORD Records;
    int Ret = 0;

    _NtCRT_lock(_CONIO_LOCK);
    if (g_PendingChar != EOF || g_PendingCount != 0)
    {
        Ret = 1;
        goto _Exit;
    }
    Console = _NtCRT_ConsoleInput();
    if (Console == INVALID_HANDLE_VALUE || !g_ConsoleApi.GetNumberOfConsoleInputEvents(Console, &Count) || Count == 0)
    {
        goto _Exit;
    }
    Records = (PINPUT_RECORD)RtlAllocateHeap(NTCRT_HEAP, 0, Count * sizeof(INPUT_RECORD));
    if (Records == NULL)
    {
        goto _Exit;
    }
    if (g_ConsoleApi.PeekConsoleInputW(Console, Records, Count, &Read))
    {
        for (DWORD i = 0; i < Read; i++)
        {
            UCHAR Lead, Code;
            wint_t Char;

            if (_NtCRT_KeyEventInput(&Records[i], &Char, &Lead, &Code) != 0)
            {
                Ret = 1;
                break;
            }
        }
    }
    RtlFreeHeap(NTCRT_HEAP, 0, Records);
_Exit:
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

/* Read a line in cooked mode with echo, without the CR LF */
static
BOOL
_NtCRT_ReadConsoleLine(
    _Out_writes_(Capacity) PWSTR Buffer,
    _In_ DWORD Capacity,
    _Out_ PDWORD Length)
{
    HANDLE Console = _NtCRT_ConsoleInput();
    DWORD OldMode;
    BOOL Success;

    *Length = 0;
    if (Console == INVALID_HANDLE_VALUE || !_CRT_GetConsoleMode(Console, &OldMode))
    {
        return FALSE;
    }
    if (Capacity == 0)
    {
        return TRUE;
    }
    g_ConsoleApi.SetConsoleMode(Console, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    Success = g_ConsoleApi.ReadConsoleW(Console, Buffer, Capacity, Length, NULL);
    g_ConsoleApi.SetConsoleMode(Console, OldMode);
    if (!Success)
    {
        *Length = 0;
        return FALSE;
    }
    if (*Length > 0 && Buffer[*Length - 1] == L'\n')
    {
        (*Length)--;
    }
    if (*Length > 0 && Buffer[*Length - 1] == L'\r')
    {
        (*Length)--;
    }
    return TRUE;
}

/*
 * buffer[0] is the buffer size, buffer[1] receives the length and the string starts at buffer[2].
 * At most buffer[0] - 1 characters are read, the rest of the line is left for the next read like msvcrt.
 */
EXTERN_C wchar_t* __cdecl _NtCRT__cgetws(wchar_t* buffer)
{
    DWORD Length;
    wchar_t* Ret = NULL;

    if (buffer == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    _NtCRT_lock(_CONIO_LOCK);
    if (_NtCRT_ReadConsoleLine(buffer + 2, buffer[0] > 0 ? buffer[0] - 1 : 0, &Length))
    {
        buffer[2 + Length] = UNICODE_NULL;
        buffer[1] = (wchar_t)Length;
        Ret = buffer + 2;
    }
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

EXTERN_C char* __cdecl _NtCRT__cgets(char* buffer)
{
    WCHAR Wide[256];
    DWORD Length;
    unsigned char Max;
    int Bytes;
    char* Ret = NULL;

    if (buffer == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    Max = (unsigned char)buffer[0];
    _NtCRT_lock(_CONIO_LOCK);
    if (_NtCRT_ReadConsoleLine(Wide, Max > 0 ? Max - 1 : 0, &Length))
    {
        Bytes = _NtCRT_WideCharToMultiByte(_NtCRT_ConsoleInputCP(), 0, Wide, Length, buffer + 2, Max, NULL, NULL);
        if (Bytes < 0 || (Length > 0 && Bytes == 0))
        {
            Bytes = 0;
        }
        if (Bytes >= Max)
        {
            Bytes = Max > 0 ? Max - 1 : 0;
        }
        buffer[2 + Bytes] = '\0';
        buffer[1] = (char)Bytes;
        Ret = buffer + 2;
    }
    _NtCRT_unlock(_CONIO_LOCK);
    return Ret;
}

/* Secure line input, the buffer is emptied before the size read is validated */
EXTERN_C errno_t __cdecl _NtCRT__cgetws_s(wchar_t* buffer, size_t numberOfElements, size_t* pSizeRead)
{
    DWORD Length;
    errno_t Ret = 0;

    if (buffer == NULL || numberOfElements == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    buffer[0] = UNICODE_NULL;
    if (pSizeRead == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (numberOfElements > MAXDWORD)
    {
        numberOfElements = MAXDWORD;
    }
    _NtCRT_lock(_CONIO_LOCK);
    if (_NtCRT_ReadConsoleLine(buffer, (DWORD)numberOfElements - 1, &Length))
    {
        buffer[Length] = UNICODE_NULL;
    } else
    {
        Ret = NTCRT_ERRNO = EBADF;
    }
    _NtCRT_unlock(_CONIO_LOCK);
    *pSizeRead = Length;
    return Ret;
}

EXTERN_C errno_t __cdecl _NtCRT__cgets_s(char* buffer, size_t numberOfElements, size_t* pSizeRead)
{
    PWSTR Wide;
    DWORD Length;
    int Bytes = 0;
    errno_t Ret = 0;

    if (buffer == NULL || numberOfElements == 0)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    buffer[0] = '\0';
    if (pSizeRead == NULL)
    {
        return _NtCRT_InvalidParameter(EINVAL);
    }
    if (numberOfElements > INT_MAX)
    {
        numberOfElements = INT_MAX;
    }
    Wide = (PWSTR)RtlAllocateHeap(NTCRT_HEAP, 0, numberOfElements * sizeof(WCHAR));
    if (Wide == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        return ENOMEM;
    }
    _NtCRT_lock(_CONIO_LOCK);
    if (_NtCRT_ReadConsoleLine(Wide, (DWORD)numberOfElements - 1, &Length))
    {
        Bytes = _NtCRT_WideCharToMultiByte(_NtCRT_ConsoleInputCP(),
                                           0,
                                           Wide,
                                           Length,
                                           buffer,
                                           (int)numberOfElements - 1,
                                           NULL,
                                           NULL);
        Bytes = Bytes > 0 ? Bytes : 0;
        buffer[Bytes] = '\0';
    } else
    {
        Ret = NTCRT_ERRNO = EBADF;
    }
    _NtCRT_unlock(_CONIO_LOCK);
    RtlFreeHeap(NTCRT_HEAP, 0, Wide);
    *pSizeRead = Bytes;
    return Ret;
}

/* A console line for _cwscanf, FALSE at the end of input */
EXTERN_C BOOL _NtCRT_ConsoleScanLineW(PWSTR Line, DWORD Count)
{
    DWORD Length;
    BOOL Ret;

    _NtCRT_lock(_CONIO_LOCK);
    Ret = _NtCRT_ReadConsoleLine(Line, Count - 1, &Length);
    _NtCRT_unlock(_CONIO_LOCK);
    if (Ret)
    {
        Line[Length] = UNICODE_NULL;
    }
    return Ret;
}

/* A console line in the console input code page for _cscanf, FALSE at the end of input */
EXTERN_C BOOL _NtCRT_ConsoleScanLineA(char* Line, DWORD Size)
{
    WCHAR Wide[512];
    DWORD Length;
    int Bytes;

    _NtCRT_lock(_CONIO_LOCK);
    if (!_NtCRT_ReadConsoleLine(Wide, ARRAYSIZE(Wide), &Length))
    {
        _NtCRT_unlock(_CONIO_LOCK);
        return FALSE;
    }
    _NtCRT_unlock(_CONIO_LOCK);
    Bytes = _NtCRT_WideCharToMultiByte(_NtCRT_ConsoleInputCP(), 0, Wide, Length, Line, Size - 1, NULL, NULL);
    Line[Bytes > 0 ? Bytes : 0] = '\0';
    return TRUE;
}

NTCRT_IMPORT(_putwch, _NtCRT__putwch);
NTCRT_IMPORT(_cputws, _NtCRT__cputws);
NTCRT_IMPORT(_putch, _NtCRT__putch);
NTCRT_IMPORT(_cputs, _NtCRT__cputs);
NTCRT_IMPORT(_vcprintf, _NtCRT__vcprintf);
NTCRT_IMPORT(_cprintf, _NtCRT__cprintf);
NTCRT_IMPORT(_vcwprintf, _NtCRT__vcwprintf);
NTCRT_IMPORT(_cwprintf, _NtCRT__cwprintf);
NTCRT_IMPORT(_getch, _NtCRT__getch);
NTCRT_IMPORT(_getwch, _NtCRT__getwch);
NTCRT_IMPORT(_getche, _NtCRT__getche);
NTCRT_IMPORT(_getwche, _NtCRT__getwche);
NTCRT_IMPORT(_ungetch, _NtCRT__ungetch);
NTCRT_IMPORT(_ungetwch, _NtCRT__ungetwch);
NTCRT_IMPORT(_kbhit, _NtCRT__kbhit);
NTCRT_IMPORT(_cgets, _NtCRT__cgets);
NTCRT_IMPORT(_cgetws, _NtCRT__cgetws);
NTCRT_IMPORT(_cgets_s, _NtCRT__cgets_s);
NTCRT_IMPORT(_cgetws_s, _NtCRT__cgetws_s);

/* POSIX names */
NTCRT_IMPORT(putch, _NtCRT__putch);
NTCRT_IMPORT(getch, _NtCRT__getch);
NTCRT_IMPORT(getche, _NtCRT__getche);
NTCRT_IMPORT(ungetch, _NtCRT__ungetch);
NTCRT_IMPORT(kbhit, _NtCRT__kbhit);
