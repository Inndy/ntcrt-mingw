/*
 * _matherr of libntcrt_start: the startup code installs it when the program sets _MINGW_INSTALL_DEBUG_MATHERR,
 * and a _matherr of the program replaces it (built with -DCUSTOM).
 */
#include <math.h>
#include <stdio.h>

int _MINGW_INSTALL_DEBUG_MATHERR = 1;

#ifdef CUSTOM
int __cdecl _matherr(struct _exception* e)
{
    printf("custom _matherr type=%d name=%s\n", e->type, e->name);
    return 1;
}
#endif

int main(void)
{
    volatile double x = -1.0;

    fflush(stdout);
    printf("log=%g\n", log(x));
    return 0;
}
