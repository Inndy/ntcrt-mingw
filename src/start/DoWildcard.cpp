/*
 * The _dowildcard of a program that defines none: the arguments are expanded when the program links libntcrt_glob.a
 * (-lntcrt_glob, src/glob/Glob.cpp), not otherwise. The libmingw32 of a MinGW-w64 configured with --enable-wildcard
 * defines it as -1, this one precedes it. A program that defines its own (int _dowildcard = -1, CRT_glob.o of
 * MinGW-w64) needs the library too, 0 leaves the arguments as they are.
 */

#include "../NtCRT.h"

extern "C" {
int _dowildcard = NTCRT_DOWILDCARD_DEFAULT;
}
