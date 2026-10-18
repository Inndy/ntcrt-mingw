/* _assert and _wassert, the message is built without the printf family */

#include "NtCRT.h"

#include <limits.h>
#include <wchar.h>

static
void
_NtCRT_WriteStderr(
    _In_z_ const char* Message)
{
    _NtCRT_write(2, Message, (unsigned int)strlen(Message));
}

/* Wide text in the multibyte characters of the locale, '?' for a character it cannot represent */
static
void
_NtCRT_AppendWideText(
    _Inout_updates_z_(Size) char* Buffer,
    _In_ size_t Size,
    _In_z_ const wchar_t* Text)
{
    mbstate_t State = 0;

    for (; *Text != L'\0'; Text++)
    {
        char Bytes[MB_LEN_MAX + 1];
        size_t Length = wcrtomb(Bytes, *Text, &State);

        if (Length == (size_t)-1)
        {
            State = 0;
            Bytes[0] = '?';
            Length = 1;
        }
        Bytes[Length] = '\0';
        _NtCRT_AppendText(Buffer, Size, Bytes);
    }
}

EXTERN_C void __cdecl _NtCRT__assert(const char* message, const char* file, unsigned line)
{
    char Buffer[1024] = "Assertion failed: ";

    _NtCRT_AppendText(Buffer, sizeof(Buffer), message);
    _NtCRT_AppendText(Buffer, sizeof(Buffer), ", file ");
    _NtCRT_AppendText(Buffer, sizeof(Buffer), file);
    _NtCRT_AppendText(Buffer, sizeof(Buffer), ", line ");
    _NtCRT_AppendNumber(Buffer, sizeof(Buffer), line, 1);
    _NtCRT_AppendText(Buffer, sizeof(Buffer), "\n");
    _NtCRT_WriteStderr(Buffer);
    _NtCRT_abort();
}

EXTERN_C void __cdecl _NtCRT__wassert(const wchar_t* message, const wchar_t* file, unsigned line)
{
    char Buffer[1024] = "Assertion failed: ";

    _NtCRT_AppendWideText(Buffer, sizeof(Buffer), message);
    _NtCRT_AppendText(Buffer, sizeof(Buffer), ", file ");
    _NtCRT_AppendWideText(Buffer, sizeof(Buffer), file);
    _NtCRT_AppendText(Buffer, sizeof(Buffer), ", line ");
    _NtCRT_AppendNumber(Buffer, sizeof(Buffer), line, 1);
    _NtCRT_AppendText(Buffer, sizeof(Buffer), "\n");
    _NtCRT_WriteStderr(Buffer);
    _NtCRT_abort();
}

NTCRT_IMPORT(_assert, _NtCRT__assert);
NTCRT_IMPORT(_wassert, _NtCRT__wassert);
