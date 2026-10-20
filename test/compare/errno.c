#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <sys/stat.h>

/* errno of a name invalid for Win32 (EINVAL), a missing file or directory (ENOENT) and a file as a directory */
int main(void)
{
    const char* Paths[] = {"a<b.txt", "dir|x/f.txt", "a\"b", "missing.txt", "nodir/missing.txt",
                           "C:/Windows/notepad.exe/x"};
    struct _stat st;

    for (int i = 0; i < (int)(sizeof(Paths) / sizeof(Paths[0])); i++)
    {
        int fd, r;
        FILE* f;

        errno = 0;
        fd = _open(Paths[i], _O_RDONLY);
        printf("%-26s open=%d/%d", Paths[i], fd, errno);
        errno = 0;
        r = _stat(Paths[i], &st);
        printf(" stat=%d/%d", r, errno);
        errno = 0;
        f = fopen(Paths[i], "r");
        printf(" fopen=%s/%d\n", f != NULL ? "ok" : "null", errno);
    }
    return 0;
}
