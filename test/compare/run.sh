#!/bin/sh
# Build the tests of test/compare with NtCRT and with UCRT (cl of Visual Studio), run them and show the differences.
# Run from the repository root after make, a test name selects the tests to run. make compare sets BUILD and
# NTCRT_CC for the LLVM build.
set -e

BUILD=${BUILD:-build}
NTCRT_CC=${NTCRT_CC:-gcc -specs=$BUILD/ntcrt.specs}
OUT=$BUILD/compare
VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"

if [ -z "$VCVARS" ] && [ -x "$VSWHERE" ]; then
    VCVARS=$("$VSWHERE" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
                        -find 'VC\Auxiliary\Build\vcvars64.bat' | head -n 1 | tr -d '\r')
fi
if [ -z "$VCVARS" ]; then
    echo "vcvars64.bat is not found, set VCVARS to it" >&2
    exit 1
fi

mkdir -p "$OUT/nt" "$OUT/ucrt"
printf '@echo off\r\ncall "%s" >nul 2>&1\r\ncl /nologo /utf-8 /O1 /MD /W0 %%*\r\n' "$VCVARS" > "$OUT/cl.bat"

TESTS=${*:-$(cd test/compare && ls *.c | sed 's/\.c$//')}
for t in $TESTS; do
    # A test with a manifest embeds it, acp.manifest makes UTF-8 the ANSI code page
    NTRES= CLRES=
    if [ -f "test/compare/$t.manifest" ]; then
        printf '1 24 "test/compare/%s.manifest"\n' "$t" > "$OUT/nt/$t.rc"
        windres "$OUT/nt/$t.rc" -O coff -o "$OUT/nt/$t.res.o"
        NTRES="$OUT/nt/$t.res.o"
        CLRES="/link /MANIFEST:EMBED /MANIFESTINPUT:..\\..\\..\\test\\compare\\$t.manifest"
    fi
    $NTCRT_CC -std=c17 -O1 -w -static -o "$OUT/nt/$t.exe" "test/compare/$t.c" $NTRES
    (cd "$OUT/ucrt" && cmd //c "..\\cl.bat ..\\..\\..\\test\\compare\\$t.c /Fe:$t.exe $CLRES" > /dev/null)
    for crt in nt ucrt; do
        (cd "$OUT/$crt" && "./$t.exe" "$(printf '\344\270\255\360\237\230\200')" > "$t.txt" 2>&1 || echo "exit=$?" >> "$t.txt")
    done
    if cmp -s "$OUT/ucrt/$t.txt" "$OUT/nt/$t.txt"; then
        echo "== $t: same"
    else
        echo "== $t: different (- UCRT, + NtCRT)"
        diff -u "$OUT/ucrt/$t.txt" "$OUT/nt/$t.txt" | tail -n +3 || true
    fi
done
