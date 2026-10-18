/* Built with acp.manifest, the ANSI code page is UTF-8. run.sh passes U+4E2D U+1F600 as the argument. */
#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static void hx(const char* s)
{
    printf("[");
    for (; *s; s++)
        printf("%02x", (unsigned char)*s);
    printf("]");
}

int main(int argc, char** argv)
{
    const char* name = "acp_\xe4\xb8\xad\xf0\x9f\x98\x80.txt";
    char* l;
    FILE* f;

    printf("acp=%u argv1=", GetACP());
    hx(argc > 1 ? argv[1] : "");
    printf(" C: mbcur=%d", (int)MB_CUR_MAX);
    f = fopen(name, "w");
    printf(" fopen=%s", f ? "ok" : "fail");
    if (f)
        fclose(f);
    printf(" wexists=%d", GetFileAttributesW(L"acp_\x4e2d\xd83d\xde00.txt") != INVALID_FILE_ATTRIBUTES);
    printf(" remove=%d\n", remove(name));
    l = setlocale(LC_ALL, "");
    printf("setlocale(\"\")=%s", l ? l : "(null)");
    printf(" mbcur=%d", (int)MB_CUR_MAX);
    printf(" mblen=%d\n", mblen("\xe4\xb8\xad", 3));
    return 0;
}
