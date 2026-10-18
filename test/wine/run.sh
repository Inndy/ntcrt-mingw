#!/bin/sh
# usage: run.sh dir dll/name...
#
# Runs the Wine tests built in dir, each in a directory of its own (they create files), the output in
# dir/log/dll_name.txt, and prints the summary line of each. Fails when a test does not finish (a crash, a timeout).
dir=$1
shift
mkdir -p "$dir/log"
status=0
for t in "$@"; do
    name=$(echo "$t" | tr / _)
    log=$(cd "$dir/log" && pwd)/$name.txt
    exe=$(cd "$dir/$(dirname "$t")" && pwd)/$(basename "$t").exe
    rm -rf "$dir/run/$name"
    mkdir -p "$dir/run/$name"
    (cd "$dir/run/$name" && timeout 600 "$exe" > "$log" 2>&1)
    code=$?
    # The summary of the test ends the output, the ones of its child processes come before
    line=$(tail -1 "$log" | grep -E '^[0-9a-f]+:[a-z_]+:[0-9.]* [0-9]+ tests executed' | sed 's/^[0-9a-f]*:[a-z_]*:[0-9.]* //')
    if [ -z "$line" ]; then
        line="did not finish (exit code $code), see $log"
        status=1
    fi
    printf '%-18s %s\n' "$t" "$line"
done
exit $status
