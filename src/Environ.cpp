/* getenv / _putenv operate on _environ and the process environment, see Startup.cpp for their initialization */

#include "NtCRT.h"

#include <errno.h>

extern "C" char** _NtCRT_environ;
extern "C" wchar_t** _NtCRT_wenviron;

/* Whether an environment entry is "Name=...", names compare case-insensitively as by the system */
static
BOOL
_NtCRT_IsEnvName(
    _In_z_ const char* Entry,
    _In_reads_(Length) const char* Name,
    _In_ size_t Length)
{
    for (size_t i = 0; i < Length; i++)
    {
        unsigned char a = (unsigned char)Entry[i], b = (unsigned char)Name[i];

        a = a >= 'A' && a <= 'Z' ? a + ('a' - 'A') : a;
        b = b >= 'A' && b <= 'Z' ? b + ('a' - 'A') : b;
        if (a == 0 || a != b)
        {
            return FALSE;
        }
    }
    return Entry[Length] == '=';
}

static
BOOL
_NtCRT_IsEnvName(
    _In_z_ const wchar_t* Entry,
    _In_reads_(Length) const wchar_t* Name,
    _In_ size_t Length)
{
    for (size_t i = 0; i < Length; i++)
    {
        if (Entry[i] == 0 || RtlUpcaseUnicodeChar(Entry[i]) != RtlUpcaseUnicodeChar(Name[i]))
        {
            return FALSE;
        }
    }
    return Entry[Length] == L'=';
}

EXTERN_C char* __cdecl _NtCRT_getenv(const char* name)
{
    size_t Length;
    char* Value = NULL;

    if (name == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    _NtCRT_lock(_ENV_LOCK);
    _NtCRT_InitEnviron();
    Length = strlen(name);
    if (_NtCRT_environ != NULL && Length != 0)
    {
        for (char** p = _NtCRT_environ; *p != NULL; p++)
        {
            if (_NtCRT_IsEnvName(*p, name, Length))
            {
                Value = *p + Length + 1;
                break;
            }
        }
    }
    _NtCRT_unlock(_ENV_LOCK);
    return Value;
}

EXTERN_C wchar_t* __cdecl _NtCRT__wgetenv(const wchar_t* name)
{
    size_t Length;
    wchar_t* Value = NULL;

    if (name == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return NULL;
    }
    _NtCRT_lock(_ENV_LOCK);
    _NtCRT_InitEnviron();
    Length = wcslen(name);
    if (_NtCRT_wenviron != NULL && Length != 0)
    {
        for (wchar_t** p = _NtCRT_wenviron; *p != NULL; p++)
        {
            if (_NtCRT_IsEnvName(*p, name, Length))
            {
                Value = *p + Length + 1;
                break;
            }
        }
    }
    _NtCRT_unlock(_ENV_LOCK);
    return Value;
}

/* Replace or remove "Name=" in an environment table, takes the ownership of Entry; names compare as in getenv */
template <typename T>
static
int
_NtCRT_UpdateEnvTable(
    _Inout_ T*** Table,
    _In_ T* Entry,
    _In_ size_t NameLength,
    _In_ bool Remove)
{
    size_t Count = 0, Index = (size_t)-1;
    T** Env = *Table;

    if (Env != NULL)
    {
        for (; Env[Count] != NULL; Count++)
        {
            if (Index == (size_t)-1 && _NtCRT_IsEnvName(Env[Count], Entry, NameLength))
            {
                Index = Count;
            }
        }
    }
    if (Index != (size_t)-1)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Env[Index]);
        if (Remove)
        {
            memmove(&Env[Index], &Env[Index + 1], (Count - Index) * sizeof(T*));
            RtlFreeHeap(NTCRT_HEAP, 0, Entry);
        } else
        {
            Env[Index] = Entry;
        }
        return 0;
    }
    if (Remove)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Entry);
        return 0;
    }
    Env = (T**)(Env != NULL ?
                RtlReAllocateHeap(NTCRT_HEAP, 0, Env, (Count + 2) * sizeof(T*)) :
                RtlAllocateHeap(NTCRT_HEAP, 0, 2 * sizeof(T*)));
    if (Env == NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, Entry);
        NTCRT_ERRNO = ENOMEM;
        return -1;
    }
    Env[Count] = Entry;
    Env[Count + 1] = NULL;
    *Table = Env;
    return 0;
}

EXTERN_C int __cdecl _NtCRT__wputenv(const wchar_t* envstring)
{
    const wchar_t* Equal;
    size_t NameLength;
    bool Remove;
    wchar_t* WideEntry;
    char* AnsiEntry;
    NTSTATUS Status;
    int Result = -1;

    if (envstring == NULL || (Equal = wcschr(envstring, L'=')) == NULL || Equal == envstring)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    NameLength = Equal - envstring;
    Remove = Equal[1] == L'\0';

    _NtCRT_lock(_ENV_LOCK);
    _NtCRT_InitEnviron();
    WideEntry = (wchar_t*)RtlAllocateHeap(NTCRT_HEAP, 0, (wcslen(envstring) + 1) * sizeof(wchar_t));
    if (WideEntry == NULL)
    {
        NTCRT_ERRNO = ENOMEM;
        goto _Exit;
    }
    wcscpy(WideEntry, envstring);
    AnsiEntry = _NtCRT_WideToAnsi(envstring);
    if (AnsiEntry == NULL)
    {
        RtlFreeHeap(NTCRT_HEAP, 0, WideEntry);
        goto _Exit;
    }

    /* The process environment first, which children inherit: the tables do not change when it fails */
    Status =
        RtlSetEnvironmentVar(NULL, envstring, NameLength, Remove ? NULL : Equal + 1, Remove ? 0 : wcslen(Equal + 1));
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(NTCRT_HEAP, 0, AnsiEntry);
        RtlFreeHeap(NTCRT_HEAP, 0, WideEntry);
        _NtCRT_SetErrnoFromNtStatus(Status);
        goto _Exit;
    }

    Result = _NtCRT_UpdateEnvTable<wchar_t>(&_NtCRT_wenviron, WideEntry, NameLength, Remove);
    if (Result == 0)
    {
        const char* AnsiEqual = strchr(AnsiEntry, '=');
        Result = _NtCRT_UpdateEnvTable<char>(&_NtCRT_environ, AnsiEntry, AnsiEqual - AnsiEntry, Remove);
    } else
    {
        RtlFreeHeap(NTCRT_HEAP, 0, AnsiEntry);
    }

_Exit:
    _NtCRT_unlock(_ENV_LOCK);
    return Result;
}

EXTERN_C int __cdecl _NtCRT__putenv(const char* envstring)
{
    int Result;
    PWSTR Wide;

    if (envstring == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    Wide = _NtCRT_AnsiToWide(envstring);
    if (Wide == NULL)
    {
        return -1;
    }
    Result = _NtCRT__wputenv(Wide);
    RtlFreeHeap(NTCRT_HEAP, 0, Wide);
    return Result;
}

NTCRT_IMPORT(getenv, _NtCRT_getenv);
NTCRT_IMPORT(_wgetenv, _NtCRT__wgetenv);
NTCRT_IMPORT(_putenv, _NtCRT__putenv);
NTCRT_IMPORT(putenv, _NtCRT__putenv);
NTCRT_IMPORT(_wputenv, _NtCRT__wputenv);
