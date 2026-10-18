#!/bin/sh
# usage: table.sh msvcrt.dll ucrtbase.dll libntcrt.a library... > table.c
#
# crt_exports of crt.c: the names that msvcrt.dll or ucrtbase.dll export, resolved as a program on NtCRT links them:
# the name, __imp_name or __mingw_name of libntcrt.a (__imp_ for the names whose function the MinGW-w64 runtime
# defines, __mingw_ for strtof and the like, which libmsvcrt_static would take from gdtoa), else the name or
# __imp_name of the other libraries (libntdll_crt, libmsvcrt_static, libmingwex). The name, not __imp_name, of a
# function of libntdll_crt.a: its __imp_ would clash with the function of libmingwex (pow) that a program calls. The
# names of msvcrt.dll that no library defines are listed at the end, GetProcAddress returns NULL for them.
msvcrt=$1
ucrtbase=$2
ntcrt=$3
shift 3
exports() {
    objdump -p "$1" | awk -v dll=$2 '/Ordinal\/Name Pointer/ { f = 1; next } f && /^\t\[/ { print "E", $NF, dll } f && /^$/ { exit }'
}
{
    nm -g --defined-only "$ntcrt" | awk 'NF == 3 { print "N", $3 }'
    nm -g --defined-only "$@" 2>/dev/null | awk 'NF == 3 { print "D", $3 }'
    exports "$msvcrt" msvcrt
    exports "$ucrtbase" ucrtbase
} | awk '
$1 == "N" { NtCRT[$2] = 1; next }
$1 == "D" { Other[$2] = 1; next }
$2 !~ /^[A-Za-z_][A-Za-z0-9_]*$/ || $2 in Seen { next }
{
    Seen[$2] = 1
    if ($2 in NtCRT) s = $2
    else if (("__imp_" $2) in NtCRT) s = "__imp_" $2
    else if (("__mingw_" $2) in NtCRT) s = "__mingw_" $2
    else if ($2 in Other) s = $2
    else if (("__imp_" $2) in Other) s = "__imp_" $2
    else { if ($3 == "msvcrt") Missing[M++] = $2; next }
    Name[N] = $2
    Sym[N++] = s
}
END {
    print "/* Written by test/wine/table.sh */"
    print "struct crt_export { const char *name; const void *addr; int imp; };"
    for (i = 0; i < N; i++)
        printf "extern const char e%d __asm__(\"%s\");\n", i, Sym[i]
    print "const struct crt_export crt_exports[] = {"
    for (i = 0; i < N; i++)
        printf "    { \"%s\", &e%d, %d },\n", Name[i], i, Sym[i] ~ /^__imp_/
    print "    { 0, 0, 0 }"
    print "};"
    printf "/* Exports of msvcrt.dll not defined (%d):", M
    for (i = 0; i < M; i++)
        printf "%s%s", i % 8 ? " " : "\n * ", Missing[i]
    print "\n */"
}'
