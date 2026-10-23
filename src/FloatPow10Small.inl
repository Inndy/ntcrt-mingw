/*
 * The 128-bit mantissas of the powers of 10 from 1e-348 to 1e347 (high bit set, rounded up) that _NtCRT_GetPow10
 * computes: those of 1e-348, 1e-321, ... (every 27th) as hi * 2**64 + lo, and for each power the difference, plus
 * one, of its mantissa from the top 128 bits of that of the power below times 5**r, 2 bits each. Made by
 * test/float/pow10gen.py from FloatPow10.inl, the table of pow10tab.go of internal/strconv of Go 1.27, see Float.cpp
 * for its license.
 */

#define NTCRT_POW10_STEP 27

static const NTCRT_POW10 g_Pow10Base[] = {
    { 0xfa8fd5a0081c0288ull, 0x1732c869cd60e454ull }, /* 1e-348 */
    { 0xca66fa129f9b60a6ull, 0xd41a26e077774ef7ull }, /* 1e-321 */
    { 0xa37fce126597973cull, 0xe50ff107bab528a1ull }, /* 1e-294 */
    { 0x8412d9991ed58091ull, 0xe858790afe9486c3ull }, /* 1e-267 */
    { 0xd5605fcdcf32e1d6ull, 0xfb1e4a9a90880a65ull }, /* 1e-240 */
    { 0xac5d37d5b79b6239ull, 0x311c2875c522ced6ull }, /* 1e-213 */
    { 0x8b3c113c38f9f37eull, 0xde83bc408dd3dd05ull }, /* 1e-186 */
    { 0xe0f218b8d25088b8ull, 0x306869c13ec3532dull }, /* 1e-159 */
    { 0xb5b5ada8aaff80b8ull, 0x0d819992132456bbull }, /* 1e-132 */
    { 0x92c8ae6b464fc96full, 0x3b0b8bc90012929eull }, /* 1e-105 */
    { 0xed246723473e3813ull, 0x290123e9aab23b69ull }, /* 1e-78 */
    { 0xbf8fdb78849a5f96ull, 0xde98520472bdd034ull }, /* 1e-51 */
    { 0x9abe14cd44753b52ull, 0xc4926a9672793543ull }, /* 1e-24 */
    { 0xfa00000000000000ull, 0x0000000000000000ull }, /* 1e3 */
    { 0xc9f2c9cd04674edeull, 0xa400000000000000ull }, /* 1e30 */
    { 0xa321f2d7226895c7ull, 0xaff72d52192b6a0eull }, /* 1e57 */
    { 0x83c7088e1aab65dbull, 0x792667c6da79e0fbull }, /* 1e84 */
    { 0xd4e5e2cdc1d1ea96ull, 0x6c9e18ac7007c91bull }, /* 1e111 */
    { 0xabfa45da0edbde69ull, 0x0487db9d17636893ull }, /* 1e138 */
    { 0x8aec23d680043beeull, 0x25de7bb9480d5855ull }, /* 1e165 */
    { 0xe070f78d3927556aull, 0x85bbe253f47b1418ull }, /* 1e192 */
    { 0xb54d5e4a127f59c8ull, 0x2503beb6d00cab4cull }, /* 1e219 */
    { 0x92746b9be2f8552cull, 0x32fd3cf5b4e49bb5ull }, /* 1e246 */
    { 0xec9c459d51852ba2ull, 0xddf8e7d60ed1219full }, /* 1e273 */
    { 0xbf21e44003acdd2cull, 0xe0470a63e6bd56c4ull }, /* 1e300 */
    { 0x9a65406d44a5c903ull, 0x737f74f1dc043329ull }, /* 1e327 */
};

/* 5**r */
static const ULONG64 g_Pow5[NTCRT_POW10_STEP] = {
    1ull, 5ull, 25ull,
    125ull, 625ull, 3125ull,
    15625ull, 78125ull, 390625ull,
    1953125ull, 9765625ull, 48828125ull,
    244140625ull, 1220703125ull, 6103515625ull,
    30517578125ull, 152587890625ull, 762939453125ull,
    3814697265625ull, 19073486328125ull, 95367431640625ull,
    476837158203125ull, 2384185791015625ull, 11920928955078125ull,
    59604644775390625ull, 298023223876953125ull, 1490116119384765625ull,
};

static const ULONG64 g_Pow10Fix[] = {
    0xaa6a56a6aaa95a69ull, 0xa95559aaaaaaaaaaull, 0x5555555596956965ull,
    0xaaa6aaaaa9655955ull, 0xa6a6a9a6666a65a6ull, 0x55595965a95aaa5aull,
    0x69995a5595569956ull, 0x555595555996a6a9ull, 0xaaaa6aa996556555ull,
    0xaa956655a696a9aaull, 0x555aaa65aa6aa965ull, 0x5555555555555555ull,
    0x5555565555555555ull, 0x1555454555655555ull, 0x566a659596545155ull,
    0x6655555596965955ull, 0x65aaaaaaaaaaa9a6ull, 0x955455565556aa96ull,
    0xaa666a5196555995ull, 0x99aaaa6966aaa95aull, 0x6aa95a665555aa56ull,
    0x0000569695655566ull,
};
