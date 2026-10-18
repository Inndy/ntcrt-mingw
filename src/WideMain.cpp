/* __wgetmainargs, the startup of wmain, see Startup.cpp */

#include "NtCRT.h"
#include "Argv.inl"

extern "C" int _NtCRT_argc;
extern "C" wchar_t** _NtCRT_wargv;
extern "C" wchar_t** _NtCRT_wenviron;
extern "C" wchar_t** _NtCRT_winitenv;
extern "C" wchar_t* _NtCRT_wcmdln;

EXTERN_C
int
__cdecl
_NtCRT___wgetmainargs(
    _Out_ int* argc,
    _Out_ wchar_t*** argv,
    _Out_ wchar_t*** envp,
    _In_ int dowildcard,
    _In_opt_ _NtCRT_startupinfo* startinfo)
{
    UNREFERENCED_PARAMETER(dowildcard);
    UNREFERENCED_PARAMETER(startinfo);

    _NtCRT_InitRuntime();
    if (_NtCRT_wargv == NULL)
    {
        _NtCRT_wcmdln = NtCurrentPeb()->ProcessParameters->CommandLine.Buffer;
        _NtCRT_wargv = _NtCRT_BuildArgv<wchar_t>(_NtCRT_wcmdln, &_NtCRT_argc);
        if (_NtCRT_wargv == NULL)
        {
            _NtCRT_amsg_exit(8);
            return -1;
        }
    }
    _NtCRT_InitEnviron();
    _NtCRT_winitenv = _NtCRT_wenviron;
    *argc = _NtCRT_argc;
    *argv = _NtCRT_wargv;
    *envp = _NtCRT_wenviron;
    return 0;
}

NTCRT_IMPORT(__wgetmainargs, _NtCRT___wgetmainargs);
