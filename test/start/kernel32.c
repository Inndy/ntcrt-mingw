/*
 * kernel32 gives a program the functions it calls that NtCRT does not have, after NtCRT: GetSystemPowerStatus is
 * imported from kernel32.dll, GetCurrentProcessId and the C library stay those of NtCRT. make checks the imports.
 */

#include <stdio.h>
#include <windows.h>

int main(void)
{
    SYSTEM_POWER_STATUS Status;

    if (!GetSystemPowerStatus(&Status) || GetCurrentProcessId() == 0)
        return 1;
    printf("kernel32: ok\n");
    return 0;
}
