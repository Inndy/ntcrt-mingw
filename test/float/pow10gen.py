# pow10gen.py src/FloatPow10.inl src/FloatPow10Small.inl: the compact table of _NtCRT_GetPow10 (FloatPow10Small.cpp)
import re, sys

STEP = 27
rows = re.findall(r'\{ 0x([0-9a-f]+)ull, 0x([0-9a-f]+)ull \}, /\* 1e(-?\d+) \* 2\*\*(-?\d+) \*/', open(sys.argv[1]).read())
T = {int(p): (int(hi, 16) << 64) - int(lo, 16) for hi, lo, p, k in rows}
assert sorted(T) == list(range(-348, 348))

bases = []
fix = [0] * 696
for i in range(696):
    p = i - 348
    q, r = divmod(i, STEP)
    base = T[-348 + q * STEP]
    if r == 0:
        bases.append(base)
        c = base
    else:
        # as the C code: the top 128 bits of the 192-bit product, truncated
        P = base * 5 ** r
        n = 64 - (P >> 128).bit_length()
        assert 3 <= n <= 62
        c = (P << n) >> 64 & ((1 << 128) - 1)
    d = T[p] - c
    assert d in (-1, 0, 1), (p, d)
    fix[i] = d + 1

words = []
for w in range(0, 696, 32):
    v = 0
    for j, f in enumerate(fix[w:w + 32]):
        v |= f << (2 * j)
    words.append(v)

with open(sys.argv[2], 'w', newline='\n') as o:
    o.write('''/*
 * The 128-bit mantissas of the powers of 10 from 1e-348 to 1e347 (high bit set, rounded up) that _NtCRT_GetPow10
 * computes: those of 1e-348, 1e-321, ... (every 27th) as hi * 2**64 + lo, and for each power the difference, plus
 * one, of its mantissa from the top 128 bits of that of the power below times 5**r, 2 bits each. Made by
 * test/float/pow10gen.py from FloatPow10.inl, the table of pow10tab.go of internal/strconv of Go 1.27, see Float.cpp
 * for its license.
 */

#define NTCRT_POW10_STEP %d

static const NTCRT_POW10 g_Pow10Base[] = {
''' % STEP)
    for q, b in enumerate(bases):
        o.write('    { 0x%016xull, 0x%016xull }, /* 1e%d */\n' % (b >> 64, b & ((1 << 64) - 1), -348 + q * STEP))
    o.write('};\n\n/* 5**r */\nstatic const ULONG64 g_Pow5[NTCRT_POW10_STEP] = {\n')
    for k in range(0, STEP, 3):
        o.write('    ' + ' '.join('%dull,' % 5 ** r for r in range(k, min(k + 3, STEP))) + '\n')
    o.write('};\n\nstatic const ULONG64 g_Pow10Fix[] = {\n')
    for k in range(0, len(words), 3):
        o.write('    ' + ' '.join('0x%016xull,' % w for w in words[k:k + 3]) + '\n')
    o.write('};\n')
print(len(bases), 'bases,', len(words), 'words')
