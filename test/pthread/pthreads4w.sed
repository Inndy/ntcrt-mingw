# Edits run.sh makes to the tests of pthreads4w: a pthread_t of pthreads4w is a struct, whose pointer member p
# the tests compare with NULL, for winpthreads and NtCRT it is an integer. The tests that include the private
# headers of pthreads4w (config.h, implement.h) and use nothing of them build without.
s/\.p \([!=]=\) NULL/ \1 0/g
s/= *__PTW32_THREAD_NULL_ID/= 0/
/^#include "\.\.\/config\.h"/d
/^#include "\.\.\/implement\.h"/d
# The counters of the condition variables of pthreads4w, printed when pthread_cond_destroy fails
/cv->nWaiters/d
# The objects of pthreads4w are pointers, allocated by init or the first use of a static initializer and NULL after
# destroy, which the tests check: the asserts on the contents of an object go, those of NtCRT are words
/^[ \t]*assert *( *[A-Za-z_][A-Za-z0-9_.]* *[!=]= *\(NULL\|PTHREAD_[A-Z_]*INITIALIZER\) *);/d
# pthreads4w finds the handle of an ended thread invalid (ESRCH), using it is undefined in POSIX
/assert(pthread_kill(\(id\[i\]\|threadbag\[i\]\.self\), 0) == ESRCH);/d
# timeouts checks errno after pthread_cond_timedwait, which returns the error (POSIX does not set errno)
/result = pthread_cond_timedwait/,/assert(errno == ETIMEDOUT);/{/assert(errno == ETIMEDOUT);/d
}
