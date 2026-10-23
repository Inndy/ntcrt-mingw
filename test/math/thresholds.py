# Overflow thresholds of tgamma and lgamma: where the mathematical result is beyond DBL_MAX / FLT_MAX (mpmath),
# by a bisection on the bits of the positive values of the type (ordered as integers)
import struct
import mpmath

mpmath.mp.prec = 300


def from_bits(i, bits):
    return struct.unpack('<d', struct.pack('<q', i))[0] if bits == 64 else struct.unpack('<f', struct.pack('<i', i))[0]


def to_bits(x, bits):
    return struct.unpack('<q', struct.pack('<d', x))[0] if bits == 64 else struct.unpack('<i', struct.pack('<f', x))[0]


MAX = {64: mpmath.mpf(2) ** 1024 - mpmath.mpf(2) ** 971, 32: mpmath.mpf(2) ** 128 - mpmath.mpf(2) ** 104}


def first_true(pred, lo, hi, bits):
    """The smallest bits in [lo, hi] for which pred holds, pred false at lo and true at hi"""
    assert not pred(from_bits(lo, bits)) and pred(from_bits(hi, bits))
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if pred(from_bits(mid, bits)):
            hi = mid
        else:
            lo = mid
    return hi


for bits, name, big, huge in ((64, 'double', 172.0, 1e306), (32, 'float', 36.0, 1e37)):
    over = lambda f: (lambda x: abs(f(mpmath.mpf(x))) > MAX[bits])
    th = from_bits(first_true(over(mpmath.gamma), to_bits(2.0, bits), to_bits(big, bits), bits), bits)
    lg = from_bits(first_true(over(mpmath.loggamma), to_bits(2.0, bits), to_bits(huge, bits), bits), bits)
    # tiny: gamma decreasing from +0: the first x that does not overflow, the threshold is the one below
    notover = lambda x: not abs(mpmath.gamma(mpmath.mpf(x))) > MAX[bits]
    tl = from_bits(first_true(notover, 1, to_bits(1.0, bits), bits) - 1, bits)
    negok = abs(mpmath.gamma(-mpmath.mpf(tl))) > MAX[bits] and not abs(mpmath.gamma(-mpmath.mpf(from_bits(to_bits(tl, bits) + 1, bits)))) > MAX[bits]
    print(name, 'tgamma x >=', th.hex(), repr(th), '| tgamma |x| <=', tl.hex(), repr(tl), '(negative same:', negok,
          ') | lgamma x >=', lg.hex(), repr(lg))
