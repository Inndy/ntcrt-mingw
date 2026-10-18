/* Included before each Wine test: what Wine's CRT headers declare and MinGW-w64's do not. No header is included
   here, the tests define macros such as _CRT_NON_CONFORMING_SWPRINTFS before their first include. */

typedef long __msvcrt_long;
typedef unsigned long __msvcrt_ulong;

int __cdecl __getmainargs(int *, char ***, char ***, int, int *);
int __cdecl __wgetmainargs(int *, unsigned short ***, unsigned short ***, int, int *);
void *__cdecl _Gettnames(void);
struct tm;
__SIZE_TYPE__ __cdecl _Strftime(char *, __SIZE_TYPE__, const char *, const struct tm *, void *);

#define _ACRTIMP
#define _finddata64_t __finddata64_t
/* UCRT exports the functions it shares with the system msvcrt.dll again with the prefix _o_ */
#define _o_tolower tolower
#define _o_toupper toupper
