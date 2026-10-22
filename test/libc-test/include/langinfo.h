/* <langinfo.h> for the tests of libc-test that check for a UTF-8 locale: nl_langinfo(CODESET) by the name of the
   LC_CTYPE locale, "C" or "C.UTF-8" in NtCRT */
#include <locale.h>
#include <string.h>
#define CODESET 14
static inline char *nl_langinfo(int item)
{
    const char *l = setlocale(LC_CTYPE, NULL);
    (void)item;
    return (char *)(l && strstr(l, "UTF-8") ? "UTF-8" : "ANSI_X3.4-1968");
}
