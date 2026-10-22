# Edits run.sh makes to the tests of libc-test, parts of POSIX left out of tests that check more of C:
# sigsetjmp and the signal mask of setjmp
/^\tsigjmp_buf sjb;/,/^\tsigset_t set, set2;/d
/^\tsigemptyset(&set);/,/siglongjmp incorrectly restored mask/d
# the stack limit of sscanf_long (setrlimit), the stack of Windows is 1 MB and the input 8 MB anyway
/^#include <sys\/resource\.h>/d
/t_setrlim(RLIMIT_STACK/d
# strlcpy and strlcat of string (BSD, POSIX 2024)
/strlcpy(b, "abc", sizeof b - 1)/,/^\treturn t_status;/{/return t_status/!d}
# the TZ of strftime by _putenv of Windows (setenv is POSIX)
s/setenv("TZ", "UTC0", 1);/_putenv("TZ=UTC0");/
