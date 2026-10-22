/*
 * Built by the drivers of an installation (make test-install): prints TEXT, which the command line defines with
 * quotes, spaces and a backslash. As C++ it links libstdc++ without -static. -DGLOB (with -ntcrt-glob) prints the
 * arguments, which the wildcards expand.
 */

#ifdef __cplusplus
#include <iostream>
#else
#include <stdio.h>
#endif

#ifdef GLOB
int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
    {
        puts(argv[i]);
    }
    return 0;
}
#else
int main(void)
{
#ifdef __cplusplus
    std::cout << TEXT << std::endl;
#else
    puts(TEXT);
#endif
    return 0;
}
#endif
