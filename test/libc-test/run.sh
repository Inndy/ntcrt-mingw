#!/bin/sh
# usage: run.sh dir [suite/test...]
#
# Builds and runs the tests of libc-test that fetch.sh put in dir/src, those named (shell patterns such as
# "functional/strto*") or all, the programs in dir/<suite>, the output in dir/log/<suite>/<test>.txt. NTCRT_CC is
# the compiler with the driver of NtCRT. A test passes when it builds, imports ntdll.dll only and exits with 0 within
# TIMEOUT seconds (default 20). skip.txt leaves out tests ("suite test reason"). The sources are edited by
# libc-test.sed first, compat.h is included before them and include/ has <langinfo.h>. Prints each test that does not
# pass and a summary per suite, fails when a test fails. JOBS=n builds n at a time (default 8).

# Paths of Windows for the compilers under msys (pwd -W)
here=$(cd "$(dirname "$0")" && { pwd -W 2>/dev/null || pwd; })
# The flags of libc-test (its Makefile), warnings off but for a call of an undeclared function. off_t and time_t of
# 64 bits as in musl, by its config.mak for glibc (_FILE_OFFSET_BITS) and for time_t on x86 (_TIME_BITS)
FLAGS="-std=c99 -D_POSIX_C_SOURCE=200809L -fno-builtin -frounding-math -w -Werror=implicit-function-declaration
       -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64"

# run.sh --build dir suite test: one program, built again when older than its inputs or the libntcrt.a of the build
if [ "$1" = --build ]; then
    dir=$2 suite=$3 t=$4
    exe=$dir/$suite/$t.exe
    src=$dir/src/libc-test/src/$suite/$t.c
    new=1
    if [ -f "$exe" ]; then
        new=
        for f in "$src" "$(dirname "$dir")/libntcrt.a" "$dir/libtest.a" "$here/libc-test.sed" "$here/compat.h" \
                 "$here/include/langinfo.h" "$here/run.sh"; do
            [ "$f" -nt "$exe" ] && new=1
        done
    fi
    [ -n "$new" ] || exit 0
    rm -f "$exe"
    sed -f "$here/libc-test.sed" "$src" > "$dir/$suite/src/$t.c"
    # shellcheck disable=SC2086
    $NTCRT_CC $FLAGS -I"$here/include" -I"$dir/src/libc-test/src/common" -include "$here/compat.h" -static \
        -o "$exe" "$dir/$suite/src/$t.c" "$dir/libtest.a" > "$dir/log/$suite/$t.build.txt" 2>&1
    exit 0
fi

[ -n "$1" ] && [ -n "$NTCRT_CC" ] || { echo "usage: NTCRT_CC=... $0 dir [suite/test...]" >&2; exit 2; }
mkdir -p "$1"
dir=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
shift
TIMEOUT=${TIMEOUT:-20}
JOBS=${JOBS:-8}
common=$dir/src/libc-test/src/common

# selected suite test pattern...
selected() {
    name=$1/$2
    shift 2
    [ $# -eq 0 ] && return 0
    for p in "$@"; do
        # shellcheck disable=SC2254
        case $name in $p) return 0;; esac
    done
    return 1
}

# The helpers of src/common the tests use, built again when one is newer
for s in functional regression; do
    mkdir -p "$dir/$s/src" "$dir/log/$s" "$dir/run/$s"
done
mkdir -p "$dir/common"
for c in print rand path utf8; do
    if ! [ -f "$dir/common/$c.o" ] || [ "$common/$c.c" -nt "$dir/common/$c.o" ] || [ "$here/run.sh" -nt "$dir/common/$c.o" ]; then
        # shellcheck disable=SC2086
        $NTCRT_CC $FLAGS -I"$here/include" -I"$common" -include "$here/compat.h" -c -o "$dir/common/$c.o" \
            "$common/$c.c" || exit 1
        rm -f "$dir/libtest.a"
    fi
done
[ -f "$dir/libtest.a" ] || ar rcs "$dir/libtest.a" "$dir"/common/*.o || exit 1

# "suite test reason" for each test, the reason of skip.txt or none
for f in "$dir"/src/libc-test/src/functional/*.c "$dir"/src/libc-test/src/regression/*.c; do
    echo "$(basename "$(dirname "$f")") $(basename "$f" .c)"
done > "$dir/tests.txt"
todo=$dir/todo.txt
awk 'NR == FNR { if ($1 !~ /^#/ && NF > 2) { r = $0; sub(/^[^ ]+ +[^ ]+ +/, "", r); Skip[$1 " " $2] = r }; next }
     { print (($1 " " $2) in Skip) ? $1 " " $2 " " Skip[$1 " " $2] : $0 }' "$here/skip.txt" "$dir/tests.txt" > "$todo"

# Build what is selected and not skipped
awk 'NF == 2' "$todo" | while read -r suite t; do
    if selected "$suite" "$t" "$@"; then echo "$suite $t"; fi
done > "$dir/build.txt"
xargs -P "$JOBS" -n 2 sh "$here/run.sh" --build "$dir" < "$dir/build.txt"

status=0
for s in functional regression; do
    pass=0 fail=0 skip=0
    while read -r suite t reason; do
        [ "$suite" = "$s" ] || continue
        selected "$suite" "$t" "$@" || continue
        if [ -n "$reason" ]; then
            skip=$((skip + 1))
            continue
        fi
        exe=$dir/$s/$t.exe
        log=$dir/log/$s/$t.txt
        result=
        if ! [ -f "$exe" ]; then
            result="does not build, see $dir/log/$s/$t.build.txt"
        elif objdump -p "$exe" | grep 'DLL Name' | grep -v -i -q ntdll.dll; then
            result="imports more than ntdll.dll: $(objdump -p "$exe" | sed -n 's/.*DLL Name: //p' | tr '\n' ' ')"
        else
            mkdir -p "$dir/run/$s/$t"
            # stdin of its own: a test that takes fd 0 (fgetwc-buffering dup2s a pipe onto it) must not move the
            # offset of the todo list that this loop reads
            (cd "$dir/run/$s/$t" && timeout -k 5 "$TIMEOUT" "$(cd "$dir/$s" && pwd)/$t.exe" < /dev/null > "$log" 2>&1)
            code=$?
            case $code in
            0) ;;
            124|137) result="timed out, see $log" ;;
            *) result="exit code $code, see $log" ;;
            esac
        fi
        if [ -n "$result" ]; then
            fail=$((fail + 1))
            printf '%-11s %-28s FAIL %s\n' "$s" "$t" "$result"
        else
            pass=$((pass + 1))
        fi
    done < "$todo"
    [ $fail -eq 0 ] || status=1
    printf '%-11s pass %d, fail %d, skip %d\n' "$s" $pass $fail $skip
done
exit $status
