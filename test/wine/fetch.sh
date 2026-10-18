#!/bin/sh
# usage: fetch.sh dir
#
# The CRT tests of Wine (LGPL, not part of this repository) at a release tag, checked by SHA-256, into dir. A file
# already there with the right checksum is kept.
set -e
TAG=wine-11.0
URL=https://gitlab.winehq.org/api/v4/projects/wine%2Fwine/repository/files
dir=$1
[ -n "$dir" ] || { echo "usage: $0 dir" >&2; exit 2; }

while read -r sum path; do
    [ -n "$sum" ] || continue
    mkdir -p "$dir/$(dirname "$path")"
    if [ -f "$dir/$path" ] && echo "$sum  $dir/$path" | sha256sum -c --status; then
        continue
    fi
    echo "fetch $TAG $path"
    curl -sSf --retry 3 -m 120 -A curl -o "$dir/$path.part" \
        "$URL/$(printf '%s' "$path" | sed 's,/,%2F,g')/raw?ref=$TAG"
    if ! echo "$sum  $dir/$path.part" | sha256sum -c --status; then
        echo "$path: checksum mismatch" >&2
        rm -f "$dir/$path.part"
        exit 1
    fi
    mv "$dir/$path.part" "$dir/$path"
done <<EOF
2a8b0cb6dcf26a4affc21ffa6423e2ae79f5c2b2fa15a0e016d5b73d663bb81d include/wine/test.h
c79da204684432ed1a438bd9899dc8c4c353648e85482e9bce75bff443578dfe dlls/msvcrt/tests/data.c
d85e486576efd9376f15b985d30f4db47355fd0ae1e1e8e70113fa922f164667 dlls/msvcrt/tests/dir.c
5a5fbbd9f64902893e86209fb9cf9f3da0d655c9256b88e9f5bd6332ed0434bc dlls/msvcrt/tests/environ.c
7b5c6421c844dd6366adc1221c389455783ce465b70d2f9e13f7698612ef3492 dlls/msvcrt/tests/file.c
c6050489ceb9c21d139bd9f40f8fd3026801258c52482435950d100d44fc1499 dlls/msvcrt/tests/heap.c
15a8e39b1ccf20d823fb407ede482ce825856af56c262d0016246bac231d375e dlls/msvcrt/tests/locale.c
f5d0637dfa90035f49d7b96a74cb98742b539192d218994183f152ac3e495465 dlls/msvcrt/tests/misc.c
266af869ae5306aedc51c31ce6908bf10268c1e9125338b4642090cda5b53dbd dlls/msvcrt/tests/printf.c
5d6cb3e35ed5b6130e580a612ea227f5bd3ccf9e27ab05fa6c6dd00345b255ad dlls/msvcrt/tests/scanf.c
4c2791d51d769258bcbe646155cab3a533bb9b9f04f15373844acdca27dfd0c9 dlls/msvcrt/tests/signal.c
864b5698e821cc1e85d657638e3a85ef962d84268d952e1d9ad2ef5b0c90bae6 dlls/msvcrt/tests/string.c
701e6c5d67bc3a7bd1c4c92ca8ba9616142d658b93bd230107bdcb47f31ce924 dlls/msvcrt/tests/time.c
544681866487c570f44e9ecf9e5cea1f60aeeb2c47b5c53aa206123f7f862949 dlls/ucrtbase/tests/environ.c
1306c4d0b76f5bcbfa8e0a1008510e18938a5d8b0454734b5b9a589ab4eeede5 dlls/ucrtbase/tests/file.c
5656b7559a0af3c02e8081ac95bab6f5367ad414964a66316fc6838dc41ac704 dlls/ucrtbase/tests/misc.c
cd6625aa647e4348ca50b84e7c08364affb3f92d1bdac7f989f924576a63af1e dlls/ucrtbase/tests/string.c
EOF
