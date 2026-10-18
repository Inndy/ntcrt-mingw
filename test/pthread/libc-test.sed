# Edits run.sh makes to the tests of libc-test, parts left out of tests that check more:
# the shm_open scenario of pthread_cancel-points (no POSIX shared memory on Windows)
/^#include <sys\/mman\.h>/d
/^static void execute_shm_open(/,/^}/d
/^static void cleanup_shm(/,/^}/d
/execute_shm_open, cleanup_shm/d
# the asynchronous cancellation of pthread_cancel, not supported (a thread that never reaches a cancellation point)
/\/\* Asynchronous cancellation \*\//,/^$/d
