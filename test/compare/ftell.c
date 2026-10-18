#include <stdio.h>
#include <string.h>
#include <wchar.h>

static void run(const char* name, const char* bytes, size_t len, const char* mode)
{
    FILE* f = fopen("ft.txt", "wb");
    fwrite(bytes, 1, len, f);
    fclose(f);
    f = fopen("ft.txt", mode);
    long pos[64];
    wint_t ch[64];
    int n = 0;
    printf("%s:", name);
    for (; n < 64; n++)
    {
        pos[n] = ftell(f);
        ch[n] = fgetwc(f);
        printf(" %ld:%x", pos[n], ch[n]);
        if (ch[n] == WEOF)
            break;
    }
    int bad = 0;
    for (int i = 0; i < n; i++)
    {
        fseek(f, pos[i], SEEK_SET);
        if (fgetwc(f) != ch[i])
            bad++;
    }
    printf(" mismatch=%d\n", bad);
    fclose(f);
}

int main()
{
    const char u8[] = "\xef\xbb\xbf" "a\r\n\xe4\xb8\xad\xf0\x9f\x98\x80" "b\r\n\xc3\xa9";
    run("utf8", u8, sizeof(u8) - 1, "r, ccs=UTF-8");
    const char u8n[] = "a\r\n\xe4\xb8\xad\xf0\x9f\x98\x80" "b\r\n\xc3\xa9";
    run("utf8-nobom", u8n, sizeof(u8n) - 1, "r, ccs=UTF-8");
    const char u16[] = "\xff\xfe" "a\0\r\0\n\0\x2d\x4e\x3d\xd8\x00\xde" "b\0\r\0\n\0";
    run("utf16", u16, sizeof(u16) - 1, "r, ccs=UTF-16LE");
    return 0;
}
