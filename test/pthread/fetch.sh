#!/bin/sh
# usage: fetch.sh dir
#
# The thread tests of pthreads4w 3.0.0 (Apache-2.0) into dir/pthreads4w and of musl libc-test (MIT) into
# dir/libc-test, not part of this repository, checked by SHA-256. pthreads4w comes as the release archive of
# SourceForge (its tests/ and license files are extracted), libc-test by git at a pinned commit: its web view is
# behind a bot check, the files are read from the objects (no line ending conversion). What is there with the right
# checksums is kept.
set -e
P4W_URL=https://downloads.sourceforge.net/project/pthreads4w/pthreads4w-code-v3.0.0.zip
P4W_SUM=b81136effb7185c77601fe2e0e6ac19bd996912e4814cebdd3010b0fac9e259b
LIBC_TEST_URL=https://repo.or.cz/libc-test.git
LIBC_TEST_COMMIT=7b95dfa5f5d5ca4d949221e0228ccc290bacc14e
dir=$1
[ -n "$dir" ] || { echo "usage: $0 dir" >&2; exit 2; }
mkdir -p "$dir"

# pthreads4w, the archive of tag Version-3-0-0-release (commit 07053a521b0a9deb6db2a649cde1f828f2eb1f4f)
zip=$dir/pthreads4w-code-v3.0.0.zip
if ! [ -f "$zip" ] || ! echo "$P4W_SUM  $zip" | sha256sum -c --status; then
    echo "fetch $P4W_URL"
    curl -sSfL --retry 3 -m 300 -A curl -o "$zip.part" "$P4W_URL"
    if ! echo "$P4W_SUM  $zip.part" | sha256sum -c --status; then
        echo "$zip: checksum mismatch" >&2
        rm -f "$zip.part"
        exit 1
    fi
    mv "$zip.part" "$zip"
    rm -rf "$dir/pthreads4w"
fi
if ! [ -f "$dir/pthreads4w/tests/test.h" ]; then
    rm -rf "$dir/pthreads4w" "$dir/pthreads4w.tmp"
    unzip -q "$zip" '*/tests/*' '*/LICENSE' '*/NOTICE' -d "$dir/pthreads4w.tmp"
    mv "$dir"/pthreads4w.tmp/pthreads4w-code-* "$dir/pthreads4w"
    rmdir "$dir/pthreads4w.tmp"
fi

# libc-test: repo.or.cz drops connections now and then, the clone is tried again
git=$dir/libc-test.git
check() {
    while read -r sum path; do
        [ -n "$sum" ] || continue
        if ! [ -f "$dir/libc-test/$path" ] || ! echo "$sum  $dir/libc-test/$path" | sha256sum -c --status; then
            [ -n "$1" ] || return 1
            mkdir -p "$dir/libc-test/$(dirname "$path")"
            git --git-dir="$git" cat-file blob "$LIBC_TEST_COMMIT:$path" > "$dir/libc-test/$path"
            if ! echo "$sum  $dir/libc-test/$path" | sha256sum -c --status; then
                echo "libc-test/$path: checksum mismatch" >&2
                exit 1
            fi
        fi
    done <<EOF
41b8f2e2706012eb534f89e72ff547308cb81dcb9acf077c40a9058a7c06088d COPYRIGHT
ed618763ddd503dc076f103c4d03c6f03229900f78b7282cf03feb104b58f85d src/common/test.h
efb1ccda4d9de7c83528838eb60357b6e4cf82da14dfaf79c3b68ea866da55d7 src/common/print.c
c3dab750cfd2f64d60bcc86b3edb14b47a552f89f50fba5b829632bf1c2413c0 src/functional/pthread_cancel-points.c
1cd812e5a9d99fc8cbaee1d9eaa03d205273dd10addf99c49f42b39f5b4aa08e src/functional/pthread_cancel.c
ff927892b30330e90560d5625b46b475bde13c49709a9a4a153bb4e53f49c0b9 src/functional/pthread_cond.c
f3c55b9b50098243d82e80a92a38d97d346c4cf71af1ff8d52c95e1892d3e1ac src/functional/pthread_mutex.c
7bdbd63abce3377067d6cff6cbf1a0da44ddc1b9bb71e931b1e42897f277b5a0 src/functional/pthread_mutex_pi.c
a01f5e85f830fc493e231bef6246a295731cdf197873ac430d2c0c81f3fbbcc1 src/functional/pthread_robust.c
d200d49851c7de60049af62ea6c4ef11d430a594920706618c6376aa5a5c47f6 src/functional/pthread_tsd.c
04e86db0d63d62fc394cf61d683b9d05a9052273913d5ea0c5f083c3cdfdcd26 src/functional/sem_init.c
2d0d398e4125de62121ed2174f8dba93d94c780e3dc44920789c338a2eb037d3 src/functional/sem_open.c
533c12959f53f741eaf07d0e22476f19b772f4a906817da0724e6737f5c57cc3 src/functional/tls_align.c
4957ecb257e8c4582ce425d0ae1f21677bd8f37447b4951a8d1159f20a5f5b68 src/functional/tls_align_dlopen.c
9ec8d7183da68df535f93cd4080b431a041629a02557e85bd0fd5e18d2e909c7 src/functional/tls_align_dso.c
13e3a2b4181277554082e48f5504d6107b6b173cba6752d874d9c5f5e9ffa887 src/functional/tls_init.c
2fe80011405bbe4ab58a300dca5012dc5c481a2570ddd942715a6b1d5cea9ab8 src/functional/tls_init_dlopen.c
fa06bd727e163535542cf0887d27d5e09bd2937ac23590118a0d807b892517a4 src/functional/tls_init_dso.c
93c13c54d8b26ccd580240d7a11b47a701f22ac227a731b9392d842a6e3fb81f src/functional/tls_local_exec.c
6b19a8d6e669d4d6906b547f40f19f3af60659ad0235ccef249b40fdbbbc14fc src/regression/pthread-robust-detach.c
7f9b91d115013fa3f4d08bc2d785dc42f3183b2b15c7d3163da38bb2fadebb11 src/regression/pthread_atfork-errno-clobber.c
fcb44acdb0b80fcf99aa0c68880b094eab57d7846d48e4907c26113a93bcc2b1 src/regression/pthread_cancel-sem_wait.c
ba0ee4385a7d4eac9c1b7b7160ded9f253fac04b6852d875acdb118da22709e0 src/regression/pthread_cond-smasher.c
c6e8b67c9869ea53b3ba0d1a0cc2b124ff2b2c8ec148288c0d520ac2a6eaa58a src/regression/pthread_cond_wait-cancel_ignored.c
440484d752bd8ebf0410365a9bf51fa13a68b70f7fc1f63fe7d90b9dc9162782 src/regression/pthread_condattr_setclock.c
3023db056b70e0d2f138b9cb3abe4d6595a57f7b4869e3e4bdefadcf415ca83f src/regression/pthread_create-oom.c
f2c2c6a2931ebb4ac7608dbdfe3cdca803c6a7cae3790c4e87d5ab01c8f229e8 src/regression/pthread_exit-cancel.c
079de9e8009ef1bfdfaae065ca8e1fdfbdb92ef3ccb72d3a3ccc5230496fe35b src/regression/pthread_exit-dtor.c
fe0240f3f6d1059ad6c9c99ab67c692c4688e2634e1dffe0bb742106e900a335 src/regression/pthread_once-deadlock.c
7de5dd564594166338d1fe2c51ab65629ec440e5b3fa349a978759101633aa5c src/regression/pthread_rwlock-ebusy.c
447a714e14b4929ffd65c157bb07b5181c7066b82e6aa19ccc5fd5864802be73 src/regression/raise-race.c
e170e7fd920d852b63bcef744fe8c71c9bdc59879fe6bc4744faf1936c73cf6c src/regression/sem_close-unmap.c
080fcc0d93ee7e6d3fb8904128f33ffa1ca09e6ee42a8e8d3f33259126b039c0 src/regression/tls_get_new-dtv.c
870fca1d4fb1ceabd1b15fdb86ee94e09b4045cd30df3aba9f676d479609983e src/regression/tls_get_new-dtv_dso.c
EOF
}
if ! check; then
    if ! git --git-dir="$git" cat-file -e "$LIBC_TEST_COMMIT^{commit}" 2>/dev/null; then
        rm -rf "$git"
        for i in 1 2 3 4 5; do
            echo "fetch $LIBC_TEST_URL"
            git clone -q --bare "$LIBC_TEST_URL" "$git" && break
            rm -rf "$git"
        done
    fi
    check copy
fi
