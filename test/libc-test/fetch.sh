#!/bin/sh
# usage: fetch.sh dir [git]
#
# The tests of musl libc-test (MIT) but those of threads, which test/pthread fetches, into dir/libc-test, not part of
# this repository, checked by SHA-256: its functional/ and regression/ tests and the parts of src/common they use.
# They are read from a bare clone, git (default dir/libc-test.git), at a pinned commit, the one of test/pthread:
# the Makefile passes the clone of test/pthread, so that one download serves both. What is there with the right
# checksums is kept.
set -e
LIBC_TEST_URL=https://repo.or.cz/libc-test.git
LIBC_TEST_COMMIT=7b95dfa5f5d5ca4d949221e0228ccc290bacc14e
dir=$1
git=${2:-$dir/libc-test.git}
[ -n "$dir" ] || { echo "usage: $0 dir [git]" >&2; exit 2; }
mkdir -p "$dir"

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
    done <<EOF2
41b8f2e2706012eb534f89e72ff547308cb81dcb9acf077c40a9058a7c06088d COPYRIGHT
ed618763ddd503dc076f103c4d03c6f03229900f78b7282cf03feb104b58f85d src/common/test.h
efb1ccda4d9de7c83528838eb60357b6e4cf82da14dfaf79c3b68ea866da55d7 src/common/print.c
7c3ada9f1e3ab094d6bf71021c40f790f81b38f4e609bb69e69fcee1242142aa src/common/rand.c
abef95abe2bccd267bb3008d538f0cdc943a120c1ea215eb846cde50ab8bafcb src/common/path.c
d8f014046c23c2076929b21fd8c812f6c0efbb710e4c4dd969bf529143a98ff9 src/common/utf8.c
be05bf2430b1be467dd41ff8543723ce9470330a10560f4250672421fd3a736b src/functional/argv.c
0d2212004b7851e0eea56b2ca103369d91c3f399a7a5906f20cde90e2417791e src/functional/basename.c
726389a23bfceb466e9ea90ea125f3a652ab9eac60293db480b365bc3d64944c src/functional/clocale_mbfuncs.c
2e042cdcd00cdb9b32cf94eb6309484138c2799960617d7cdf382343f71d856d src/functional/clock_gettime.c
d25b9d533b304f9bbae0c8eae8212196e431fdbeb18e805aebf667741235aafe src/functional/crypt.c
d5068ad577633aa072d247bdaf31ab92f222e75f6c66ae2cca03db7acfd8ce05 src/functional/dirname.c
c7a0ce9f0b8544144fcc785e9cfc505eaa85506b4856bd967287025a892b8462 src/functional/dlopen.c
ce70dd5c72e9f402863d343a3177d12465ae7695bdeb6e05a51f82f88a907d64 src/functional/env.c
a0cb2d46a8377d995af334d9a21efc975d377f701d54b47fe44aa6e712287358 src/functional/fcntl.c
2fea2615fc62e3b283dac70c7b54479d41795a5f939626cfe4d109b54c84f70e src/functional/fdopen.c
c91a6b7dce4cd09f52b0fb55ccb8408ef2d8194320732a2e58725929940809e3 src/functional/fnmatch.c
c82068858288b72d7bf3b82ac20f0773658a139d1b091b08bff1767baf8512e3 src/functional/fscanf.c
bb1f982692b2bc8285058b0d20711285a824345590fdaf27a4db9f03712a9755 src/functional/fwscanf.c
9040a11c1cd037af09dbd8b9a459fe97b6b5d0b11fac9d281a8f7e59d15b3386 src/functional/iconv_open.c
59907245422d8f7d167658b03912b19eca793040ed7eac17ee0a30604ea6125b src/functional/inet_pton.c
86caed98c76a29b31193997312b54acc081426495dc66a4670bd06578242606e src/functional/ipc_msg.c
2edf8722619a41dda6702f7f137d715c80619b6218b81695bbabc573fcda385e src/functional/ipc_sem.c
cb54251d88c869287b02d03164c902c78df71dd5bc69e4bdffc7cfc142cfa1bf src/functional/ipc_shm.c
7f6ad74c723f2910491b1ff456579466254942f7726853c235e1af4b58d5caa2 src/functional/mbc.c
99276afe20af32fad98bf10b3c8720b55b54017e2c8749041fd68e5d0c048d65 src/functional/memstream.c
e571778b35935c5c28dc9dedace603050fe9b231322244bee9156bced944b5c7 src/functional/mntent.c
cd11975f2612f44f51e7c86fa0f086416e46ebddf0609d96c64fc0711fdc08a3 src/functional/popen.c
f38f28778ec58be5beaa6755fdbb0c17e8151925ceec5ab83c0080975ae61e48 src/functional/qsort.c
9f59f3b6069494d19001b75dc2989d6770c67eb3c6bed232f45c8219b8590ef8 src/functional/random.c
1135c75d7ef2fe5003dc162d587812fe9b72debc665e032db026e285b1525e32 src/functional/search_hsearch.c
955765757cae749700035e1d14158776bb60b6ac136218436c98c15a4a24411c src/functional/search_insque.c
82fd53942b1b1fb94fe3c15c5639a243a079575733c7f085f9aea8c430bb9d58 src/functional/search_lsearch.c
8dad75a9530718f9ccb11c7615692f1b2a55dca4bff56b116f600f96d9616646 src/functional/search_tsearch.c
de9320694b8a6bdb2d2145eb7650590184240578dc1a3656a923fb95c096cccf src/functional/setjmp.c
9c75c6961a1d154af62609a8a22ec98e2b8ab9b0146f8b2c61483cb4dd6a187d src/functional/snprintf.c
0d4bf1f5a6030c378dddfa5efd24bdd10a8488bdde76cdddb06532c346b6eb2e src/functional/socket.c
c03e1b90a6bf95ea126ed536afd7777efb894a85497fc184595d5ed3e959cb2c src/functional/spawn.c
89135e466cc90e521fbe947fd9d08cb3cb9daf14b6304c87edab52ea4cae63d5 src/functional/sscanf.c
80e5b2271e111211ef5133ede862549474ee045721e0ce286b5273fac2b49fcf src/functional/sscanf_long.c
1c95b10e9cc5c4f444c592f4f61c22e9da9b12c052e662b66c35e8834e63f862 src/functional/stat.c
a43f6f34638dd40a25b37d721efc914bff4d22d5f8eae4f6f7245984b48cd693 src/functional/strftime.c
c804cf25cb526bb457820471ce1df87c7971877529e0624785a7a45214df3b56 src/functional/string.c
0efeabd24ed30a6eb05df80dd85c8cfad122cbe36f2ce8181571850c9c50ed21 src/functional/string_memcpy.c
b5da056eee27482fa9371354a3fe45695970b653c795fc6bdec4d207fb972f86 src/functional/string_memmem.c
6e16f2b27f603d3978070c8d6cdb3a1c6d08b5ed4f0ae8fd2e1219af3592ce7c src/functional/string_memset.c
a443ecea4adecefa2085a98437fe908825b8d1e5f35bcdf4f2a94eba6c0ce505 src/functional/string_strchr.c
e1ecb338a60c021c0f6da29c006b1ee1b70bba9cef2c72cc20e0c2e7717bdd6a src/functional/string_strcspn.c
26bae147b913e78a531f1be98b60653f89aca3c2979209a79792691035769650 src/functional/string_strstr.c
af24cbeb224b18937c7396ce38710df72f7e35ba896dc5603de2119d16cffe8c src/functional/strptime.c
68b1d17ffa5208b6085f13df463feef4b76ba8d6a378280c4cf6b4beeaa84bc9 src/functional/strtod.c
bc456a6c096a0a719c19e8b0711ffb131f3193cd6f8b7040c5fda7b538e0f2e3 src/functional/strtod_long.c
6082f3b00ebbc33767973121c43968802e4b67658ce903a3ec790e4e5f998581 src/functional/strtod_simple.c
7892491e285ddb0857a84c73df9580b634ca2b6616531523c39ba3a83d084f32 src/functional/strtof.c
fdb61137c77a406cee78a0aaf383f5794d519e49a9b6c2bfba9a9452bdb9d1a8 src/functional/strtol.c
759de4dc8da3c415f972327acbc266b681a89a3f6f2b1e02e3f4d867d08c6ff8 src/functional/strtold.c
54f6f1daca5fd550eb005881f6aa6acfe5426b8c978fa33864482c9da59ff591 src/functional/swprintf.c
7fb2a0b49aef4f55464cf5dc8c438ff460f16f254d24bdbb3f09773265207f9f src/functional/tgmath.c
692ff88135501d14dd4aec2d8aeb18aa58a0dfbe8d13315b68d82fbe12ebb5b2 src/functional/time.c
f8c55a0096489e1f65d0323e6b82a6a57b8319c76e067c1226d0ec7bf313f119 src/functional/udiv.c
dbc960a6930ed014e2cea97c212ad7f3b24a22ea5ebdfb277d568f303ecbf006 src/functional/ungetc.c
5d61cd0fe8c71e5418f55a7f21a64d48a07ff3c8cbe90f364910f40dcd53216d src/functional/utime.c
8d93cb5d4073a099197859bcdcf30afe72f9bf2266748c844dcecc24e05208d7 src/functional/vfork.c
d4e4089e0ba33c964f7a2954ca2acf848b815a9254322002d14779d065534d94 src/functional/wcsstr.c
b57104027fae4287572e0aaad658caedfb9873be11915ff4616504693c8caa87 src/functional/wcstol.c
e3e310de1a73bc30273ac5f4711492817a2f26a5750795c9a3f0be42645871c9 src/functional/wordexp.c
ee9a7d342da754c7288581dbde0936a0e189a0df45f35ba6323edd14deeccaa0 src/regression/daemon-failure.c
e8480034a80e015212f74d2636ee5836af1bc0ecf01798c296ecc8254199e2bf src/regression/dn_expand-empty.c
654e335e4f215053ce7e23915c7b4a5fde45c08b3be7b5167675193fa571439d src/regression/dn_expand-ptr-0.c
96a498eef49184d66ddb80cae923d0ad111e0185f428cf48d2423f373a6b3b7a src/regression/execle-env.c
82bea3efb0f938bbfb08d06219adf8a588a287d8aefc920b320a56f606cf4722 src/regression/fflush-exit.c
714867abf791cd09b3931398828600249fdb4f20ef37d3f260691ec94f2b41c7 src/regression/fgets-eof.c
11c4e77e50f2def251fb5473e2c1eafe92ca9a631397f7dea6a744224c1dcd22 src/regression/fgetwc-buffering.c
b42cd72dbf2c9b47d9c352a9441804b04c243d03cc8fc1549a8dd84edd7455fb src/regression/flockfile-list.c
24891d34067311b18090cab72afcac685714b65305a9095c2416719efed6ae9d src/regression/fpclassify-invalid-ld80.c
e237f6586ba3ff401841d2ffd604f39d7f200b56efaf90336aa458daa9b3b582 src/regression/ftello-unflushed-append.c
afa7d547ac4232f4589beae4a6c1b133fa352320ff280e2d22d5d7f07b63d31f src/regression/getpwnam_r-crash.c
dd0df5077cea70d404b521b32f30d95ce825805dcc2ad4851907f61f9a595fe7 src/regression/getpwnam_r-errno.c
7e1131dec069414a66e90f3786a71dc55a1a357d90da01e3da248690eaf87c8b src/regression/iconv-roundtrips.c
f96471ef46ae6f315e28a24e5715240c6a47ec8c3d19d7341529b011d366bfbe src/regression/inet_ntop-v4mapped.c
3f2f33fb1f08def6753cf639cb40240273386ef5eea905ec6ad8c2a5db88a513 src/regression/inet_pton-empty-last-field.c
395499f85e544ab96cda42150570e29b9613742d10e7ecb8ef371180410df27c src/regression/iswspace-null.c
a1993bc320e305adacd15254877523ea5e4a6f5ca246d58882d21da4fde69672 src/regression/lrand48-signextend.c
94aa5e917c9ee908562cdf8cf104c05c5949b17099bde16730d636ec86b62ea5 src/regression/lseek-large.c
36f474d20c215dadae4a902b3408da0cd47674e4188231c6a47143908a8b2f44 src/regression/malloc-0.c
3492e04eed21b8120fcc2f11f6b71488b064d1b6148409054a0c9dd9fa5c1afc src/regression/malloc-brk-fail.c
addf8528b5342ae1786cbc6de5f5ff73547518c16058ec1cdd70ea3b325ceaf9 src/regression/malloc-oom.c
66a09745accdb8a79ca2c95802c21fff3215bf44dacdbac5c70f7942d4146313 src/regression/mbsrtowcs-overflow.c
3e85b72629df80c33b3c69af9ac2911d860fbca00dec78f4a020957790468624 src/regression/memmem-oob-read.c
a68aac59c901b691705914be72400c4d7cfc2ff00697cb91328c900dbf480a75 src/regression/memmem-oob.c
20e562982d0d4caf954210386fe96256518c25795e09e84190fd6e82e8f813cc src/regression/mkdtemp-failure.c
eb09ebd997d4c4d073a28974f4b5ad09fefc00b2852328452b8c272ea80daf31 src/regression/mkstemp-failure.c
afc44da6194c52803835ab740b83a191dc6b43f1d63e1bce2eb17a523a390125 src/regression/printf-1e9-oob.c
c22682cb0219af9bcb94060569cb8fb6fdb1b371e3a2ea710ff8f2377d8b8fe0 src/regression/printf-fmt-g-round.c
06b312bb3bf32df951ed826a6d642be9c7bd8f130d0acf9f0a928bd49201a71c src/regression/printf-fmt-g-zeros.c
dce2fc083a551c0a175b3aa22ab38ceec5e015c7b4926e09832bc68ec78d1549 src/regression/printf-fmt-n.c
4e24fea11109050a193bb7c020c2af735fc626a157053613cb703314fdbf5aa7 src/regression/putenv-doublefree.c
447a714e14b4929ffd65c157bb07b5181c7066b82e6aa19ccc5fd5864802be73 src/regression/raise-race.c
d05a792876c05872fdacc0effe590f8bc727ff9d77705c7d91ab87190c1acc93 src/regression/regex-backref-0.c
733f5a7ad7b501a1840c6e4921ce0c5f269755849fb464ec37f7712e9c9e7f02 src/regression/regex-bracket-icase.c
bfec345d22773124d1d68f7bf2ffb3f68fc2d1223944e258f1b528a7aba7b827 src/regression/regex-ere-backref.c
40147c05e016eaf8f1a6e0a5ffd795c476d171741f6bf87e7f02ce06ffc5cccc src/regression/regex-escaped-high-byte.c
5eed8d24ac32a019caba2990bd3a2411560bd5abdfc6b8494b7b92d127d92112 src/regression/regex-negated-range.c
f758134f90e448afc462a65ad372ebc7b1b25eb71028423773828e28784fd227 src/regression/regexec-nosub.c
70b89ee4378dc9cb8a8921c5d8bbc3f0976cba2ebb369792a83c9b349bbec434 src/regression/rewind-clear-error.c
1dbb2fc1cf940eefd293205abd9b88ff572fae9a3277e66a274bf2cfe9119f0a src/regression/rlimit-open-files.c
cb36c8cde00eeae5a8297878ed9a20d73db6fa6e4f4031a57e9763ca16f1392e src/regression/scanf-bytes-consumed.c
6d6767d216c1202e6df506af64bb4f83fc37e02b5223f1a02d60e4b55c249596 src/regression/scanf-match-literal-eof.c
23dfe567f42138592ce5960694411206d3f12fe2c1575cb3b39c4fb1789e516d src/regression/scanf-nullbyte-char.c
9e85cf1ad68c3f246178656b63a3458e4c188310cbf0461b87a5878e706f11dc src/regression/setenv-oom.c
5011ffa8c1dd1b75e1a35469472b0d8eb7d2f890f5405d00a21d98837d96015b src/regression/setvbuf-unget.c
18601ae861286c53018bfcc8222b901da1eea7bc0ee22db995627872aedbed9a src/regression/sigaltstack.c
7fa8ff56229bb87b9fa2c49c2eebae080e2e7f169819296763ed13ede8b572c6 src/regression/sigprocmask-internal.c
615c11891d77499465ed1917a5d800bb14f915a7d7df407a1c195c161586b2e4 src/regression/sigreturn.c
b676ebef55cebe8672fcd59cb1a2fba6c0b6e1626473537e7ea0240ae85fe15f src/regression/sscanf-eof.c
731f670d932ff0a2dbe1b777942dd08cebeb00911f2e31194ca943cde3c1d17f src/regression/statvfs.c
0f53fe7d16647f7a5c7469f766ac5405f1200b52a3f496c71c88d77b651ad435 src/regression/strverscmp.c
4513aef9bc852c7f55da3622f2d05b70cfdc61848893f38830946aa0005ce604 src/regression/syscall-sign-extend.c
c3e85759a9966376f1963692f99dbfbb236e4dc4841d4113c14cdc98f7fe65d0 src/regression/uselocale-0.c
6611f4212d9bbb44c8bc9e588611da6408e686fa32384eba45190db5d540dbe8 src/regression/wcsncpy-read-overflow.c
83eb48996028964b8d5022e41facc27878f16dee747c77b61e4a3ff0d3184090 src/regression/wcsstr-false-negative.c
EOF2
}
# repo.or.cz drops connections now and then, the clone is tried again
if ! check; then
    if ! git --git-dir="$git" cat-file -e "$LIBC_TEST_COMMIT^{commit}" 2>/dev/null; then
        rm -rf "$git"
        mkdir -p "$(dirname "$git")"
        for i in 1 2 3 4 5; do
            echo "fetch $LIBC_TEST_URL"
            git clone -q --bare "$LIBC_TEST_URL" "$git" && break
            rm -rf "$git"
        done
    fi
    check copy
fi
