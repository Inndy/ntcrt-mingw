/*
 * The data of data.dll is declared without dllimport, so ld auto-imports it with runtime pseudo relocations
 * that the relocator of libntcrt_start applies at startup.
 */
#include <stdio.h>
#include <string.h>

extern int dll_values[4];
extern const char dll_text[];
int dll_get(int i);

/* Relocations in initialized data, with an addend */
static int* g_Third = &dll_values[2];
static const char* g_Text = dll_text + 1;

int main(void)
{
    int Failures = 0;

    if (dll_values[0] != 11 || dll_values[3] != 44)
    {
        printf("FAIL read %d %d\n", dll_values[0], dll_values[3]);
        Failures++;
    }
    if (*g_Third != 33 || strcmp(g_Text, "seudo") != 0)
    {
        printf("FAIL initialized data\n");
        Failures++;
    }
    dll_values[1] = 99;
    if (dll_get(1) != 99)
    {
        printf("FAIL write seen by the DLL: %d\n", dll_get(1));
        Failures++;
    }
    printf("pseudoreloc: %d failures\n", Failures);
    return Failures != 0;
}
