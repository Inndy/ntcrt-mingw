/*
 * Built by the drivers of an installation (make test-install): prints TEXT, which the command line defines with
 * quotes, spaces and a backslash. As C++ it links libstdc++ without -static.
 */

#ifdef __cplusplus
#include <iostream>
#else
#include <stdio.h>
#endif

int main(void)
{
#ifdef __cplusplus
    std::cout << TEXT << std::endl;
#else
    puts(TEXT);
#endif
    return 0;
}
