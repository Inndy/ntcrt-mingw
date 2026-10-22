/* Included before each test of libc-test (run.sh): the POSIX calls that the tests use only to set up their input,
   by the names of the CRT of Windows. A pipe is opened in binary mode, as on POSIX. */
#include <io.h>
#include <fcntl.h>
#define pipe(p) _pipe(p, 4096, _O_BINARY)
