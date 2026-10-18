#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#include <locale.h>
#include <wchar.h>
#include <errno.h>

static void hx(const char* s)
{
    printf("[");
    for (; *s; s++)
        printf("%02x", (unsigned char)*s);
    printf("]");
}

static void run(void)
{
    const char* d = "u5_\xe4\xb8\xad\xf0\x9f\x98\x80";
    const char* f = "u5_\xe4\xb8\xad\xf0\x9f\x98\x80/\xc3\xa9.txt";
    errno = 0;
    int r = _mkdir(d);
    printf(" mkdir=%d/%d", r, errno);
    FILE* fp = fopen(f, "w");
    printf(" fopen=%s/%d", fp ? "ok" : "null", errno);
    if (fp)
    {
        fputs("x", fp);
        fclose(fp);
    }
    struct _stat st;
    errno = 0;
    printf(" stat=%d/%d", _stat(f, &st), errno);
    wchar_t wf[] = L"u5_\x4e2d\xd83d\xde00/\xe9.txt";
    printf(" wstat=%d", _wstat(wf, &st));
    errno = 0;
    printf(" access=%d", _access(f, 0));
    char cwd[260], buf[260];
    _getcwd(cwd, sizeof(cwd));
    errno = 0;
    printf(" chdir=%d/%d", _chdir(d), errno);
    char* g = _getcwd(buf, sizeof(buf));
    printf(" getcwd=");
    if (g && strcmp(buf, cwd) == 0)
        printf("unchanged");
    else if (g)
        hx(strrchr(buf, '\\') + 1);
    else
        printf("null/%d", errno);
    _chdir(cwd);
    struct _finddata_t fd;
    char pat[64];
    sprintf(pat, "%s/*.txt", d);
    intptr_t h = _findfirst(pat, &fd);
    printf(" find=");
    if (h != -1)
    {
        hx(fd.name);
        _findclose(h);
    } else
        printf("-1/%d", errno);
    char* fp2 = _fullpath(NULL, d, 0);
    printf(" fullpath=");
    if (fp2)
    {
        hx(strrchr(fp2, '\\') + 1);
        free(fp2);
    }
    char nf[64];
    sprintf(nf, "%s/\xf0\x9f\x98\x81.txt", d);
    errno = 0;
    printf(" rename=%d/%d", rename(f, nf), errno);
    errno = 0;
    printf(" remove=%d/%d", remove(nf), errno);
    errno = 0;
    printf(" rmdir=%d/%d", _rmdir(d), errno);
    char* t = _tempnam(NULL, "\xc3\xa9");
    printf(" tempnam=");
    if (t)
    {
        /* The prefix, the rest is unique */
        char* n = strrchr(t, '\\') + 1;
        n[2] = '\0';
        hx(n);
        free(t);
    }
    printf("\n");
}

int main()
{
    printf("C:");
    run();
    setlocale(LC_ALL, ".UTF-8");
    printf("UTF-8:");
    run();
    return 0;
}
