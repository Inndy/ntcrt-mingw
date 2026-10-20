/*
 * The code pages of a process before Windows 11: PEB::AnsiCodePageData and OemCodePageData point to the data of the
 * .nls files (not a CPTABLEINFO), and before Windows 10 the PEB has no ActiveCodePage and OemCodePage (Windows 7 reads
 * 0 there). The PEB of Windows 11 (no .nls data, the code pages set) is made such, the conversions of code page
 * CP_ACP and CP_OEMCP agree with those of the code pages themselves.
 */

#include <KNSoft/NDK/NDK.h>
#include <stdio.h>
#include <string.h>

static int Failed;

#define CHECK(Cond) \
    do \
    { \
        if (!(Cond)) \
        { \
            printf("FAIL nlsdata line %d: %s\n", __LINE__, #Cond); \
            Failed = 1; \
        } \
    } while (0)

static char Bytes[224];
static const WCHAR Wide[] = L"A\x00E9\x4E00\x00A2\x0416\x3042 z";

/* The conversions of CodePage, and of Alias (CP_ACP or CP_OEMCP) with the PEB of Windows 7 */
static void Compare(UINT CodePage, UINT Alias)
{
    static WCHAR WideOut[2][sizeof(Bytes)];
    static char BytesOut[2][64];
    int WideLength[2], BytesLength[2];
    PPEB Peb = NtCurrentPeb();
    PCPTABLEINFO Ansi = Peb->AnsiCodePageData, Oem = Peb->OemCodePageData;
    USHORT ActiveCodePage = Peb->ActiveCodePage, OemCodePage = Peb->OemCodePage;
    PVOID Data[2];
    SIZE_T Size;
    CPINFO Info;

    WideLength[0] = MultiByteToWideChar(CodePage, 0, Bytes, sizeof(Bytes), WideOut[0], sizeof(Bytes));
    BytesLength[0] = WideCharToMultiByte(CodePage, 0, Wide, -1, BytesOut[0], sizeof(BytesOut[0]), NULL, NULL);
    CHECK(WideLength[0] > 0 && BytesLength[0] > 0);

    if (Ansi == NULL)
    {
        CHECK(NT_SUCCESS(NtGetNlsSectionPtr(11, GetACP(), NULL, &Data[0], &Size)) &&
              NT_SUCCESS(NtGetNlsSectionPtr(11, GetOEMCP(), NULL, &Data[1], &Size)));
        Peb->AnsiCodePageData = (PCPTABLEINFO)Data[0];
        Peb->OemCodePageData = (PCPTABLEINFO)Data[1];
    }
    Peb->ActiveCodePage = 0;
    Peb->OemCodePage = 0;

    CHECK((Alias == CP_ACP ? GetACP() : GetOEMCP()) == CodePage);
    WideLength[1] = MultiByteToWideChar(Alias, 0, Bytes, sizeof(Bytes), WideOut[1], sizeof(Bytes));
    BytesLength[1] = WideCharToMultiByte(Alias, 0, Wide, -1, BytesOut[1], sizeof(BytesOut[1]), NULL, NULL);
    CHECK(WideLength[1] == WideLength[0] && memcmp(WideOut[1], WideOut[0], WideLength[0] * sizeof(WCHAR)) == 0);
    CHECK(BytesLength[1] == BytesLength[0] && memcmp(BytesOut[1], BytesOut[0], BytesLength[0]) == 0);
    CHECK(GetCPInfo(Alias, &Info) && IsValidCodePage(CodePage));

    Peb->AnsiCodePageData = Ansi;
    Peb->OemCodePageData = Oem;
    Peb->ActiveCodePage = ActiveCodePage;
    Peb->OemCodePage = OemCodePage;
}

int main(void)
{
    UINT Acp = GetACP(), Oemcp = GetOEMCP();

    for (int i = 0; i < (int)sizeof(Bytes); i++)
    {
        Bytes[i] = (char)(0x20 + i);
    }
    Compare(Acp, CP_ACP);
    Compare(Oemcp, CP_OEMCP);
    if (Failed)
        return 1;
    printf("nlsdata: ok\n");
    return 0;
}
