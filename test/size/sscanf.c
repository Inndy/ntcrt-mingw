#include <stdio.h>

int main(int argc, char** argv)
{
    int i;
    double d;
    char s[16];

    return sscanf(argc > 1 ? argv[1] : "1 2.5 x", "%d %lf %15s", &i, &d, s) == 3 ? 0 : 1;
}
