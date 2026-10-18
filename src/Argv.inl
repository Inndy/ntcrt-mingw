/* The command line of __getmainargs (Startup.cpp) and __wgetmainargs (WideMain.cpp), parsed by the rules of msvcrt */

#pragma once

template <typename T>
static
void
_NtCRT_ParseCommandLine(
    _In_ const T* CommandLine,
    _Out_opt_ T** Argv,
    _Out_opt_ T* Args,
    _Out_ size_t* ArgCount,
    _Out_ size_t* CharCount)
{
    const T* p = CommandLine;
    bool InQuote;

    *ArgCount = 0;
    *CharCount = 0;

#define _NTCRT_EMIT(c) do { if (Args != NULL) { *Args++ = (c); } ++*CharCount; } while (0)

    /* Program name, ends at the closing quote or whitespace, backslashes are not special */
    if (Argv != NULL)
    {
        *Argv++ = Args;
    }
    ++*ArgCount;
    if (*p == '"')
    {
        p++;
        while (*p != '\0' && *p != '"')
        {
            _NTCRT_EMIT(*p);
            p++;
        }
        if (*p == '"')
        {
            p++;
        }
    } else
    {
        while (*p != '\0' && *p != ' ' && *p != '\t')
        {
            _NTCRT_EMIT(*p);
            p++;
        }
    }
    _NTCRT_EMIT('\0');

    for (;;)
    {
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }
        if (Argv != NULL)
        {
            *Argv++ = Args;
        }
        ++*ArgCount;
        InQuote = false;
        for (;;)
        {
            bool CopyChar = true;
            size_t Backslashes = 0;

            while (*p == '\\')
            {
                p++;
                Backslashes++;
            }
            if (*p == '"')
            {
                /* 2N backslashes + quote -> N backslashes + toggle, 2N+1 -> N backslashes + literal quote */
                if (Backslashes % 2 == 0)
                {
                    if (InQuote && p[1] == '"')
                    {
                        /* "" inside quotes is a literal quote */
                        p++;
                    } else
                    {
                        CopyChar = false;
                        InQuote = !InQuote;
                    }
                }
                Backslashes /= 2;
            }
            while (Backslashes-- != 0)
            {
                _NTCRT_EMIT('\\');
            }
            if (*p == '\0' || (!InQuote && (*p == ' ' || *p == '\t')))
            {
                break;
            }
            if (CopyChar)
            {
                _NTCRT_EMIT(*p);
            }
            p++;
        }
        _NTCRT_EMIT('\0');
    }

#undef _NTCRT_EMIT
}

template <typename T>
static
T**
_NtCRT_BuildArgv(
    _In_ const T* CommandLine,
    _Out_ int* Argc)
{
    size_t ArgCount, CharCount;
    T** Argv;

    _NtCRT_ParseCommandLine<T>(CommandLine, NULL, NULL, &ArgCount, &CharCount);
    Argv = (T**)RtlAllocateHeap(NTCRT_HEAP, 0, (ArgCount + 1) * sizeof(T*) + CharCount * sizeof(T));
    if (Argv == NULL)
    {
        return NULL;
    }
    _NtCRT_ParseCommandLine<T>(CommandLine, Argv, (T*)(Argv + ArgCount + 1), &ArgCount, &CharCount);
    Argv[ArgCount] = NULL;
    *Argc = (int)ArgCount;
    return Argv;
}

/* The last argument of __getmainargs and __wgetmainargs */
typedef struct
{
    int newmode;
} _NtCRT_startupinfo;
