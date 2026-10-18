/* rename and _wrename of msvcrt by MoveFileExW of File.cpp */

#include "NtCRT.h"

#include <errno.h>

EXTERN_C int __cdecl _NtCRT__wrename(const wchar_t* oldname, const wchar_t* newname)
{
    if (oldname == NULL || newname == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    if (!_K32_MoveFileExW(oldname, newname, MOVEFILE_COPY_ALLOWED))
    {
        _NtCRT_SetErrnoFromWin32(_Inline_GetLastError());
        return -1;
    }
    return 0;
}

EXTERN_C int __cdecl _NtCRT_rename(const char* oldname, const char* newname)
{
    PWSTR WideOld, WideNew;
    int Ret = -1;

    if (oldname == NULL || newname == NULL)
    {
        NTCRT_ERRNO = EINVAL;
        return -1;
    }
    WideOld = _NtCRT_FileToWide(oldname);
    if (WideOld == NULL)
    {
        return -1;
    }
    WideNew = _NtCRT_FileToWide(newname);
    if (WideNew != NULL)
    {
        Ret = _NtCRT__wrename(WideOld, WideNew);
        RtlFreeHeap(NTCRT_HEAP, 0, WideNew);
    }
    RtlFreeHeap(NTCRT_HEAP, 0, WideOld);
    return Ret;
}

NTCRT_IMPORT(rename, _NtCRT_rename);
NTCRT_IMPORT(_wrename, _NtCRT__wrename);
