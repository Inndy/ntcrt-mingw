/* _splitpath of Mbcs.cpp (narrow, by the multibyte code page) and _wsplitpath of Misc.cpp */

#pragma once

#include <errno.h>
#include <mbctype.h>
#include <string.h>

/*
 * Split Path into its components. Truncate copies what fits like _splitpath, otherwise a component that does not
 * fit empties all of them and fails like _splitpath_s. MbType is the _mbctype table (Type[c + 1]) of a multibyte
 * code page or NULL: the trail byte of a lead byte is never a separator or a dot.
 */
template<typename TChar>
static
errno_t
_NtCRT_SplitPath(
    _In_z_ const TChar* Path,
    _Out_writes_opt_z_(DriveSize) TChar* Drive,
    _In_ size_t DriveSize,
    _Out_writes_opt_z_(DirSize) TChar* Dir,
    _In_ size_t DirSize,
    _Out_writes_opt_z_(FnameSize) TChar* Fname,
    _In_ size_t FnameSize,
    _Out_writes_opt_z_(ExtSize) TChar* Ext,
    _In_ size_t ExtSize,
    _In_ bool Truncate,
    _In_opt_ const unsigned char* MbType)
{
    TChar* Buffers[4] = { Drive, Dir, Fname, Ext };
    size_t Sizes[4] = { DriveSize, DirSize, FnameSize, ExtSize };
    const TChar* Starts[4];
    size_t Lengths[4];
    const TChar* p = Path;
    const TChar* LastSlash = NULL;
    const TChar* LastDot = NULL;
    const TChar* End;
    int i;

    Starts[0] = p;
    Lengths[0] = 0;
    if (p[0] != 0 && p[1] == ':')
    {
        Lengths[0] = 2;
        p += 2;
    }
    for (End = p; *End != 0; End++)
    {
        if (MbType != NULL && (MbType[(unsigned char)*End + 1] & _M1) && End[1] != 0)
        {
            End++;
        } else if (*End == '/' || *End == '\\')
        {
            LastSlash = End;
            LastDot = NULL;
        } else if (*End == '.')
        {
            LastDot = End;
        }
    }
    Starts[1] = p;
    Lengths[1] = LastSlash == NULL ? 0 : LastSlash + 1 - p;
    Starts[2] = p + Lengths[1];
    Starts[3] = LastDot == NULL ? End : LastDot;
    Lengths[2] = Starts[3] - Starts[2];
    Lengths[3] = End - Starts[3];

    for (i = 0; i < 4; i++)
    {
        if (Buffers[i] != NULL && Lengths[i] >= Sizes[i])
        {
            if (Truncate)
            {
                Lengths[i] = Sizes[i] - 1;
            } else
            {
                for (i = 0; i < 4; i++)
                {
                    if (Buffers[i] != NULL)
                    {
                        Buffers[i][0] = 0;
                    }
                }
                return _NtCRT_InvalidParameter(ERANGE);
            }
        }
    }
    for (i = 0; i < 4; i++)
    {
        if (Buffers[i] != NULL)
        {
            memcpy(Buffers[i], Starts[i], Lengths[i] * sizeof(TChar));
            Buffers[i][Lengths[i]] = 0;
        }
    }
    return 0;
}

template<typename TChar>
static
errno_t
_NtCRT_SplitPathS(
    _In_z_ const TChar* Path,
    _Out_writes_opt_z_(DriveSize) TChar* Drive,
    _In_ size_t DriveSize,
    _Out_writes_opt_z_(DirSize) TChar* Dir,
    _In_ size_t DirSize,
    _Out_writes_opt_z_(FnameSize) TChar* Fname,
    _In_ size_t FnameSize,
    _Out_writes_opt_z_(ExtSize) TChar* Ext,
    _In_ size_t ExtSize,
    _In_opt_ const unsigned char* MbType)
{
    if (Path == NULL ||
        (Drive == NULL) != (DriveSize == 0) ||
        (Dir == NULL) != (DirSize == 0) ||
        (Fname == NULL) != (FnameSize == 0) ||
        (Ext == NULL) != (ExtSize == 0))
    {
        TChar* Buffers[4] = { Drive, Dir, Fname, Ext };

        for (TChar* Buffer : Buffers)
        {
            if (Buffer != NULL && Path != NULL)
            {
                Buffer[0] = 0;
            }
        }
        return _NtCRT_InvalidParameter(EINVAL);
    }
    return _NtCRT_SplitPath(Path, Drive, DriveSize, Dir, DirSize, Fname, FnameSize, Ext, ExtSize, false, MbType);
}
