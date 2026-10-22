#!/bin/sh
# usage: run.sh dir [suite/test...]
#
# Builds and runs the tests that fetch.sh put in dir/src, those named (shell patterns) or all, the programs in
# dir/<suite>, the output in dir/log/<suite>/<test>.txt. NTCRT_CC is the compiler with the driver of NtCRT. A test
# passes when it builds, imports ntdll.dll only and exits with 0 within TIMEOUT seconds (default 120), each of RUNS
# times (default 1). skip.txt leaves out tests ("suite test reason"). Prints each test that does not pass and a
# summary per suite, fails when a test fails or hangs. JOBS=n builds n at a time (default 8).

# Paths of Windows for the compilers under msys (pwd -W)
here=$(cd "$(dirname "$0")" && { pwd -W 2>/dev/null || pwd; })

# run.sh --build dir suite test: one program, built again when older than its inputs. The sources are edited by
# suite.sed into dir/suite/src first, pthreads4w.h is included before those of pthreads4w.
if [ "$1" = --build ]; then
    dir=$2 suite=$3 t=$4
    exe=$dir/$suite/$t.exe
    log=$dir/log/$suite/$t.build.txt
    lib=$(dirname "$dir")/libntcrt.a
    case $suite in
    pthreads4w)
        src=$dir/src/pthreads4w/tests
        srcs=$src/$t.c
        flags="-I$src -include $here/pthreads4w.h"
        ;;
    libc-test)
        src=$dir/src/libc-test/src
        srcs="$(ls "$src"/functional/"$t".c "$src"/regression/"$t".c 2>/dev/null) $src/common/print.c"
        # tls_align and the like with their "DSO" linked in
        for f in "$src"/functional/"$t"_dso.c "$src"/regression/"$t"_dso.c; do
            [ -f "$f" ] && srcs="$srcs $f"
        done
        flags="-D_GNU_SOURCE -I$src/common"
        ;;
    esac
    new=1
    if [ -f "$exe" ]; then
        new=
        for f in $srcs "$lib" "$here/$suite.h" "$here/$suite.sed" "$here/run.sh"; do
            [ "$f" -nt "$exe" ] && new=1
        done
    fi
    [ -n "$new" ] || exit 0
    rm -f "$exe"
    mkdir -p "$dir/$suite/src/$t"
    edited=
    for f in $srcs; do
        sed -f "$here/$suite.sed" "$f" > "$dir/$suite/src/$t/$(basename "$f")"
        edited="$edited $dir/$suite/src/$t/$(basename "$f")"
    done
    # GCC 14 and clang make errors of the int-pointer conversions (pthreads4w initializes its handles by NULL) and of
    # calls by other function pointer types (pthread_cleanup_push of pthread_mutex_unlock)
    # shellcheck disable=SC2086
    $NTCRT_CC -std=gnu17 $flags -O2 -w -Wno-implicit-function-declaration \
        -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-incompatible-function-pointer-types \
        -static -pthread -o "$exe" $edited -lpthread > "$log" 2>&1
    exit 0
fi

[ -n "$1" ] && [ -n "$NTCRT_CC" ] || { echo "usage: NTCRT_CC=... $0 dir [suite/test...]" >&2; exit 2; }
mkdir -p "$1"
dir=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
shift
TIMEOUT=${TIMEOUT:-120}
RUNS=${RUNS:-1}
JOBS=${JOBS:-8}

# The tests of each suite, "suite test"; the DSO parts of libc-test are no tests
list() {
    for f in "$dir"/src/pthreads4w/tests/*.c; do
        t=$(basename "$f" .c)
        [ "$t" = benchlib ] || echo "pthreads4w $t"
    done
    for f in "$dir"/src/libc-test/src/functional/*.c "$dir"/src/libc-test/src/regression/*.c; do
        t=$(basename "$f" .c)
        case $t in *_dso) ;; *) echo "libc-test $t";; esac
    done
}
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

# "suite test reason" for each test, the reason of skip.txt or none
list > "$dir/tests.txt"
todo=$dir/todo.txt
awk 'NR == FNR { if ($1 !~ /^#/ && NF > 2) { r = $0; sub(/^[^ ]+ +[^ ]+ +/, "", r); Skip[$1 " " $2] = r }; next }
     { print (($1 " " $2) in Skip) ? $1 " " $2 " " Skip[$1 " " $2] : $0 }' "$here/skip.txt" "$dir/tests.txt" > "$todo"

# Build what is selected and not skipped
for s in pthreads4w libc-test; do
    mkdir -p "$dir/$s" "$dir/log/$s" "$dir/run/$s"
done
awk 'NF == 2' "$todo" | while read -r suite t; do
    if selected "$suite" "$t" "$@"; then echo "$suite $t"; fi
done > "$dir/build.txt"
xargs -P "$JOBS" -n 2 sh "$here/run.sh" --build "$dir" < "$dir/build.txt"

status=0
for s in pthreads4w libc-test; do
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
        elif objdump -p "$exe" | grep 'DLL Name' | grep -v -q -i ntdll.dll; then
            result="imports more than ntdll.dll: $(objdump -p "$exe" | sed -n 's/.*DLL Name: //p' | tr '\n' ' ')"
        else
            n=0 bad=0 how=
            run=$(cd "$dir/run/$s" && pwd)
            while [ $n -lt "$RUNS" ]; do
                n=$((n + 1))
                (cd "$run" && timeout -k 5 "$TIMEOUT" "$(cd "$dir/$s" && pwd)/$t.exe" > "$log.tmp" 2>&1)
                code=$?
                if [ $code -ne 0 ]; then
                    bad=$((bad + 1))
                    case $code in 124|137) how="timed out";; *) how="exit code $code";; esac
                    cp "$log.tmp" "$log"
                elif [ $bad -eq 0 ]; then
                    cp "$log.tmp" "$log"
                fi
            done
            rm -f "$log.tmp"
            [ $bad -eq 0 ] || result="$how ($bad of $RUNS runs failed), see $log"
        fi
        if [ -n "$result" ]; then
            fail=$((fail + 1))
            printf '%-12s %-28s FAIL %s\n' "$s" "$t" "$result"
        else
            pass=$((pass + 1))
        fi
    done < "$todo"
    [ $fail -eq 0 ] || status=1
    printf '%-12s pass %d, fail %d, skip %d\n' "$s" $pass $fail $skip
done
exit $status
