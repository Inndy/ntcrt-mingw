// Test strtold and wcstold of NtCRT, the 80-bit long double of MinGW-w64, against a reference: the syntax by scanC
// (ref_test.go), the exact value by math/big rounded here to 64 bits of significand, denormals at 2**-16445, ties
// to even. go test -fuzz FuzzStrtold80

package gofloat

import (
	"fmt"
	"math"
	"math/big"
	"math/rand/v2"
	"strconv"
	"strings"
	"testing"
)

// ld80 is an 80-bit long double: the significand with its explicit integer bit, the sign and 15-bit biased exponent
type ld80 struct {
	mant    uint64
	signExp uint16
}

var (
	ldOne    = ld80{1 << 63, 0x3FFF}
	ldMax    = ld80{math.MaxUint64, 0x7FFE}
	ldInf    = ld80{1 << 63, 0x7FFF}
	ldNaN    = ld80{3 << 62, 0x7FFF}
	ldTiny   = ld80{1, 0}         // the smallest denormal, 2**-16445
	ldNormal = ld80{1 << 63, 1}   // the smallest normal, 2**-16382
	ldDenMax = ld80{1<<63 - 1, 0} // the largest denormal
	ldPoint1 = ld80{0xCCCCCCCCCCCCCCCD, 0x3FFB}
)

func (x ld80) String() string { return fmt.Sprintf("%04x:%016x", x.signExp, x.mant) }

func (x ld80) neg() ld80 { return ld80{x.mant, x.signExp ^ 0x8000} }

func (x ld80) exp() int { return int(x.signExp & 0x7FFF) }

func (x ld80) isNaN() bool { return x.exp() == 0x7FFF && x.mant>>62 == 3 }

func (x ld80) isInf() bool { return x.exp() == 0x7FFF && x.mant == 1<<63 }

// pow2 is m * 2**e: the value of a finite long double is mant * 2**(max(exp, 1) - 16383 - 63)
func pow2(m *big.Int, e int) *big.Rat {
	if e >= 0 {
		return new(big.Rat).SetInt(new(big.Int).Lsh(m, uint(e)))
	}
	return new(big.Rat).SetFrac(m, new(big.Int).Lsh(big.NewInt(1), uint(-e)))
}

func (x ld80) rat() *big.Rat {
	r := pow2(new(big.Int).SetUint64(x.mant), max(x.exp(), 1)-16446)
	if x.signExp>>15 != 0 {
		r.Neg(r)
	}
	return r
}

// next is the long double after a positive finite x, infinity after the largest
func (x ld80) next() ld80 {
	switch {
	case x.mant == math.MaxUint64:
		return ld80{1 << 63, x.signExp + 1}
	case x.exp() == 0 && x.mant+1 == 1<<63:
		return ld80{1 << 63, 1}
	}
	return ld80{x.mant + 1, x.signExp}
}

// binaryFormat is a binary floating point format: prec bits of significand, emin and emax the exponents of its
// normal numbers, denormals below 2**emin at 2**(emin - prec + 1)
type binaryFormat struct{ prec, emin, emax int }

var (
	x87Format    = binaryFormat{64, -16382, 16383}
	doubleFormat = binaryFormat{53, -1022, 1023}
	floatFormat  = binaryFormat{24, -126, 127}
)

// round rounds num / den > 0 to f, ties to even: q * 2**(e - prec + 1), q below 2**prec, at least 2**(prec-1)
// unless e is emin (a denormal or 0); inf if it overflows
func (f binaryFormat) round(num, den *big.Int) (q uint64, e int, inf bool) {
	// 2**e <= num / den < 2**(e+1)
	e = num.BitLen() - den.BitLen()
	n, d := new(big.Int).Set(num), new(big.Int).Set(den)
	if e >= 0 {
		d.Lsh(d, uint(e))
	} else {
		n.Lsh(n, uint(-e))
	}
	if n.Cmp(d) < 0 {
		e--
	}
	e = max(e, f.emin)
	n.Set(num)
	d.Set(den)
	if shift := f.prec - 1 - e; shift >= 0 {
		n.Lsh(n, uint(shift))
	} else {
		d.Lsh(d, uint(-shift))
	}
	quo, rem := new(big.Int).QuoRem(n, d, new(big.Int))
	if c := rem.Lsh(rem, 1).Cmp(d); c > 0 || (c == 0 && quo.Bit(0) == 1) {
		quo.Add(quo, big.NewInt(1))
	}
	// rounded up to 2**prec, which is exact one bit lower
	if quo.BitLen() > f.prec {
		quo.Rsh(quo, 1)
		e++
	}
	if e > f.emax {
		return 0, 0, true
	}
	return quo.Uint64(), e, false
}

// ldRound rounds num / den > 0 to a long double
func ldRound(num, den *big.Int) ld80 {
	q, e, inf := x87Format.round(num, den)
	switch {
	case inf:
		return ldInf
	case q>>63 == 0:
		return ld80{q, 0}
	}
	return ld80{q, uint16(e + 16383)}
}

// ieeeBits packs the result of round to the bits of a double or a float of f
func (f binaryFormat) ieeeBits(q uint64, e int, inf bool) uint64 {
	frac := f.prec - 1
	switch {
	case inf:
		return uint64(2*f.emax+1) << frac
	case q>>frac == 0:
		return q
	}
	return uint64(e+f.emax)<<frac | q&(1<<frac-1)
}

// long rounds |n| exactly to a long double
func (n goNumber) long() ld80 {
	sig, first := n.significant()
	if sig == "" {
		return ld80{}
	}
	// 10**4940 and 2**16396 overflow, 10**-4960 and 2**-16460 are below half the smallest denormal
	m, low, top := n.scaled(sig, first)
	if (n.hex && top > 16400) || (!n.hex && top > 4940) {
		return ldInf
	}
	if (n.hex && top < -16460) || (!n.hex && top < -4960) {
		return ld80{}
	}
	return ldRound(n.fraction(m, low))
}

// refStrtold is what strtold should return for s, wcstold if wide (the bytes of s as U+0000..U+00FF, iswspace
// skipped): the long double, the end and errno
func refStrtold(s string, wide bool) (x ld80, end int, errno int, nan bool) {
	space := isCSpace
	if wide {
		space = cIsWspace
	}
	if i := strings.IndexByte(s, 0); i >= 0 {
		s = s[:i]
	}
	c := scanC(s, space)
	if c.end == 0 {
		return ld80{}, 0, 0, false
	}
	i := 0
	for space(s[i]) {
		i++
	}
	switch {
	case c.nan:
		x, nan = ldNaN, true
	case strings.HasSuffix(c.token, "Inf"):
		x = ldInf
	default:
		x = c.num.long()
		if x.exp() == 0x7FFF || (c.nonzero && x.exp() == 0) {
			errno = erange
		}
	}
	if s[i] == '-' {
		x = x.neg()
	}
	return x, c.end, errno, nan
}

var longFuncs = []struct {
	name string
	wide bool
	fn   func(string) (ld80, int, int)
}{{"strtold", false, cStrtold}, {"wcstold", true, cWcstold}}

// sameLong compares the results: any NaN with the sign of want
func sameLong(got, want ld80, nan bool) bool {
	if nan {
		return got.isNaN() && got.signExp>>15 == want.signExp>>15
	}
	return got == want
}

// checkStrtold checks strtold and wcstold of s against the reference
func checkStrtold(t *testing.T, s string) {
	t.Helper()
	for _, f := range longFuncs {
		want, wantEnd, wantErr, nan := refStrtold(s, f.wide)
		got, end, errno := f.fn(s)
		if end != wantEnd || errno != wantErr || !sameLong(got, want, nan) {
			t.Errorf("%s(%s) = %v end %d errno %d, want %v end %d errno %d", f.name, short(s), got, end, errno, want, wantEnd, wantErr)
		}
	}
}

// wantLongErrno is the errno of NtCRT for the number s converted to x
func wantLongErrno(s string, x ld80) int {
	s = strings.TrimLeft(s, " \t\n\v\f\r")
	if x.isInf() && !hasPrefixFold(strings.TrimLeft(s, "+-"), "inf") {
		return erange
	}
	if x.exp() == 0 && nonzeroDigits(s) {
		return erange
	}
	return 0
}

// checkLong checks that strtold, wcstold and the reference convert end bytes of s to want, -1 all of them
func checkLong(t *testing.T, s string, want ld80, end int) {
	t.Helper()
	if end < 0 {
		end = len(s)
	}
	wantErr := wantLongErrno(s[:end], want)
	nan := want.isNaN()
	for _, f := range longFuncs {
		got, n, errno := f.fn(s)
		if n != end || errno != wantErr || !sameLong(got, want, nan) {
			t.Errorf("%s(%s) = %v end %d errno %d, want %v end %d errno %d", f.name, short(s), got, n, errno, want, end, wantErr)
		}
		ref, refEnd, refErr, refNaN := refStrtold(s, f.wide)
		if refEnd != end || refErr != wantErr || refNaN != nan || !sameLong(ref, want, nan) {
			t.Errorf("reference %s(%s) = %v end %d errno %d, want %v end %d errno %d", f.name, short(s), ref, refEnd, refErr, want, end, wantErr)
		}
	}
}

// decimal is m * 2**e exactly as digits * 10**exp, digits without trailing zeros
func decimal(m *big.Int, e int) (digits string, exp int) {
	if m.Sign() == 0 {
		return "0", 0
	}
	if e >= 0 {
		digits = new(big.Int).Lsh(m, uint(e)).String()
	} else {
		digits, exp = new(big.Int).Mul(m, new(big.Int).Exp(big.NewInt(5), big.NewInt(int64(-e)), nil)).String(), e
	}
	t := strings.TrimRight(digits, "0")
	return t, exp + len(digits) - len(t)
}

// exactText is a positive finite x in decimal exactly
func exactText(x ld80) string {
	d, e := decimal(new(big.Int).SetUint64(x.mant), max(x.exp(), 1)-16446)
	return d + "e" + strconv.Itoa(e)
}

// midTexts is the decimal of the midpoint after a positive finite x exactly, a digit more, and less
func midTexts(x ld80) (mid, more, less string) {
	m := new(big.Int).SetUint64(x.mant)
	m.Lsh(m, 1).Add(m, big.NewInt(1))
	d, e := decimal(m, max(x.exp(), 1)-16447)
	lower := []byte(d)
	lower[len(lower)-1]--
	return d + "e" + strconv.Itoa(e), d + "1e" + strconv.Itoa(e-1), string(lower) + "9999e" + strconv.Itoa(e-4)
}

// moreDigits appends digits to the significand of the decimal text "digits e exp", the value kept to them
func moreDigits(text, digits string) string {
	d, e, _ := strings.Cut(text, "e")
	exp, _ := strconv.Atoi(e)
	return d + digits + "e" + strconv.Itoa(exp-len(digits))
}

// even is the one of x and its next with an even significand, what their midpoint rounds to
func even(x ld80) ld80 {
	if x.mant&1 == 0 {
		return x
	}
	return x.next()
}

// hexText is a positive finite x in hex as the significand of 16 digits
func hexText(x ld80) string {
	return fmt.Sprintf("0x%016xp%d", x.mant, max(x.exp(), 1)-16446)
}

func TestStrtoldTable(t *testing.T) {
	zeros := func(n int) string { return strings.Repeat("0", n) }
	maxMid, maxMore, maxLess := midTexts(ldMax)
	halfTrue, halfMore, halfLess := midTexts(ld80{})
	denMid, denMore, denLess := midTexts(ldDenMax)
	oneMid, oneMore, oneLess := midTexts(ldOne)
	oneUp := ld80{1<<63 | 1, 0x3FFF}
	tests := []struct {
		in   string
		want ld80
		end  int
	}{
		{"1", ldOne, -1}, {"+1", ldOne, -1}, {"-1", ldOne.neg(), -1}, {"1.0", ldOne, -1}, {"10e-1", ldOne, -1},
		{".1e1", ldOne, -1}, {"1.", ldOne, -1}, {"0.1", ldPoint1, -1}, {"-0.1", ldPoint1.neg(), -1},
		{"0.10000000000000000000000000000000000001", ldPoint1, -1}, {"3", ld80{3 << 62, 0x4000}, -1},
		{"18446744073709551615", ld80{math.MaxUint64, 0x403E}, -1}, {"18446744073709551616", ld80{1 << 63, 0x403F}, -1},
		// 2**65 - 1 and 2**65 - 3 are ties of 64 bits
		{"36893488147419103231", ld80{1 << 63, 0x4040}, -1}, {"36893488147419103229", ld80{math.MaxUint64 - 1, 0x403F}, -1},
		{"0", ld80{}, -1}, {"-0", ld80{0, 0x8000}, -1}, {"0e99999", ld80{}, -1}, {"-0x0p0", ld80{0, 0x8000}, -1},
		{"0." + zeros(10000), ld80{}, -1}, {"-.0e-5", ld80{0, 0x8000}, -1},

		// the largest, the midpoint toward overflow rounds to even: infinity
		{exactText(ldMax), ldMax, -1}, {maxMid, ldInf, -1}, {maxMore, ldInf, -1}, {maxLess, ldMax, -1},
		{"-" + maxMid, ldInf.neg(), -1}, {"1e4933", ldInf, -1}, {"-1e99999", ldInf.neg(), -1},
		{"0x1.fffffffffffffffep16383", ldMax, -1}, {"0x1.ffffffffffffffffp16383", ldInf, -1},
		{"0x1.fffffffffffffffefffffffffffffp16383", ldMax, -1}, {"0x1p16384", ldInf, -1}, {"0x1p99999999999", ldInf, -1},
		// the smallest denormal, half of it rounds to even: 0
		{exactText(ldTiny), ldTiny, -1}, {halfTrue, ld80{}, -1}, {halfMore, ldTiny, -1}, {halfLess, ld80{}, -1},
		{"-" + halfTrue, ld80{0, 0x8000}, -1}, {"-" + halfMore, ldTiny.neg(), -1}, {"1e-4952", ld80{}, -1},
		{"0x1p-16445", ldTiny, -1}, {"0x1p-16446", ld80{}, -1}, {"0x1.0000000000000000001p-16446", ldTiny, -1},
		{"0x1.8p-16445", ld80{2, 0}, -1}, {"0x1p-16383", ld80{1 << 62, 0}, -1}, {"-0x1p-99999999999", ld80{0, 0x8000}, -1},
		// the largest denormal and the smallest normal: the midpoint rounds to the normal, no ERANGE
		{exactText(ldDenMax), ldDenMax, -1}, {exactText(ldNormal), ldNormal, -1}, {denMid, ldNormal, -1},
		{denMore, ldNormal, -1}, {denLess, ldDenMax, -1}, {"0x1p-16382", ldNormal, -1},
		{"0x0.fffffffffffffffep-16382", ldDenMax, -1}, {"0x0.ffffffffffffffffp-16382", ldNormal, -1},
		// 1 + 2**-64 is a tie of 64 bits
		{oneMid, ldOne, -1}, {oneMore, oneUp, -1}, {oneLess, ldOne, -1}, {"0x1.0000000000000001p0", ldOne, -1},
		{"0x1.00000000000000018p0", oneUp, -1}, {"0x1.0000000000000003p0", ld80{1<<63 | 2, 0x3FFF}, -1},
		{"0x1.0000000000000001" + zeros(40) + "1p0", oneUp, -1}, {"0x8p-3", ldOne, -1}, {"0X.8P1", ldOne, -1},
		{"0x1.8p1", ld80{3 << 62, 0x4000}, -1}, {"0x0000000000000000000000000000000001p0", ldOne, -1},

		// long: past the 12000 digits of the decimal of NtCRT, the digits after them only round
		{"1" + zeros(5000) + "e-5000", ldOne, -1}, {"0." + zeros(20000) + "1e20001", ldOne, -1},
		{zeros(15000) + "1", ldOne, -1}, {"1." + zeros(15000) + "1", ldOne, -1},
		{moreDigits(oneMid, zeros(13000)+"1"), oneUp, -1}, {moreDigits(oneMid, zeros(13000)), ldOne, -1},
		{moreDigits(denMid, zeros(2000)), ldNormal, -1}, {moreDigits(halfTrue, zeros(2000)+"1"), ldTiny, -1},
		{moreDigits(maxLess, "9999"+zeros(5000)), ldMax, -1}, {moreDigits(maxMid, zeros(12000)), ldInf, -1},
		{"0x1" + zeros(5000) + "p-20000", ldOne, -1}, {"0x0." + zeros(5000) + "1p20004", ldOne, -1},

		// inf, nan and what is not a number: 0, not -0, and nothing converted
		{"inf", ldInf, -1}, {"INF", ldInf, -1}, {"-inf", ldInf.neg(), -1}, {"infinity", ldInf, -1},
		{"-Infinity", ldInf.neg(), -1}, {"infinit", ldInf, 3}, {"+infx", ldInf, 4}, {"nan", ldNaN, -1},
		{"-nan", ldNaN.neg(), -1}, {"NaN(123abc_)", ldNaN, -1}, {"nan(", ldNaN, 3}, {"-nan(-)", ldNaN.neg(), 4},
		{"", ld80{}, 0}, {"x", ld80{}, 0}, {"-", ld80{}, 0}, {"+", ld80{}, 0}, {".", ld80{}, 0}, {"-.", ld80{}, 0},
		{"e5", ld80{}, 0}, {"-x", ld80{}, 0}, {" -e1", ld80{}, 0}, {"-in", ld80{}, 0}, {"- 1", ld80{}, 0},
		{"0x", ld80{}, 1}, {"-0xg", ld80{0, 0x8000}, 2}, {"0x.p1", ld80{}, 1}, {"1e", ldOne, 1}, {"1e+", ldOne, 1},
		{"1e+x", ldOne, 1}, {"0x1p", ldOne, 3}, {"0x1p-", ldOne, 3}, {"1.5L", ld80{3 << 62, 0x3FFF}, 3},
		{"  \t\n-1.5e+0xyz", ld80{3 << 62, 0xBFFF}, 11}, {"1\x002", ldOne, 1},
	}
	for _, tt := range tests {
		checkLong(t, tt.in, tt.want, tt.end)
		checkStrtold(t, tt.in)
	}
	t.Logf("tested %d cases", len(tests))
}

// TestLongRound checks the rounding of the reference: in the formats of double and float against Rat.Float64 and
// Rat.Float32, in the long double format on random long doubles, their midpoints and a little off
func TestLongRound(t *testing.T) {
	r := rand.New(rand.NewPCG(9, 10))
	count := 20000
	if testing.Short() {
		count = 2000
	}
	randInt := func(bits int) *big.Int {
		b := make([]byte, (bits+7)/8)
		for i := range b {
			b[i] = byte(r.Uint32())
		}
		m := new(big.Int).SetBytes(b)
		return m.Rsh(m, uint(len(b)*8-bits)).SetBit(m, bits-1, 1)
	}
	check := func(num, den *big.Int) {
		rat := new(big.Rat).SetFrac(num, den)
		f64, _ := rat.Float64()
		if got, want := doubleFormat.ieeeBits(doubleFormat.round(num, den)), math.Float64bits(f64); got != want {
			t.Errorf("round %s to double = %x, Rat.Float64 %x", rat.FloatString(5), got, want)
		}
		f32, _ := rat.Float32()
		if got, want := floatFormat.ieeeBits(floatFormat.round(num, den)), uint64(math.Float32bits(f32)); got != want {
			t.Errorf("round %s to float = %x, Rat.Float32 %x", rat.FloatString(5), got, want)
		}
	}
	// a little off a midpoint: 2**-40 of the unit
	off := func(x *big.Rat, unit *big.Rat, sign int64) *big.Rat {
		d := new(big.Rat).Mul(unit, big.NewRat(sign, 1<<40))
		return d.Add(d, x)
	}
	for range count {
		// random fractions across the range of double, from overflow to below half the smallest subnormal
		num, den := randInt(1+r.IntN(200)), randInt(1+r.IntN(200))
		if s := r.IntN(2400) - 1200; s >= 0 {
			num.Lsh(num, uint(s))
		} else {
			den.Lsh(den, uint(-s))
		}
		check(num, den)
		// the midpoints after random doubles and floats, and a little off
		b := r.Uint64N(0x7FF0000000000000)
		unit := pow2(big.NewInt(1), max(int(b>>52), 1)-1075)
		b32 := r.Uint32N(0x7F800000)
		unit32 := pow2(big.NewInt(1), max(int(b32>>23), 1)-150)
		for _, m := range []struct{ x, unit *big.Rat }{
			{new(big.Rat).SetFloat64(math.Float64frombits(b)), unit},
			{new(big.Rat).SetFloat64(float64(math.Float32frombits(b32))), unit32},
		} {
			mid := new(big.Rat).Add(m.x, new(big.Rat).Mul(m.unit, big.NewRat(1, 2)))
			for _, v := range []*big.Rat{m.x, mid, off(mid, m.unit, 1), off(mid, m.unit, -1)} {
				if v.Sign() > 0 {
					check(v.Num(), v.Denom())
				}
			}
		}
		// long doubles, their midpoints to even, a little off to the nearer
		mant, signExp := randLongDouble(r)
		x := ld80{mant, signExp &^ 0x8000}
		unitL := pow2(big.NewInt(1), max(x.exp(), 1)-16446)
		midL := new(big.Rat).Add(x.rat(), new(big.Rat).Mul(unitL, big.NewRat(1, 2)))
		for _, c := range []struct {
			v    *big.Rat
			want ld80
		}{{x.rat(), x}, {midL, even(x)}, {off(midL, unitL, 1), x.next()}, {off(midL, unitL, -1), x}} {
			if c.v.Sign() == 0 {
				continue
			}
			if got := ldRound(c.v.Num(), c.v.Denom()); got != c.want {
				t.Errorf("round %v + %s units = %v, want %v", x, new(big.Rat).Quo(new(big.Rat).Sub(c.v, x.rat()), unitL).FloatString(12), got, c.want)
			}
		}
	}
}

// TestStrtoldRandom converts random long doubles in decimal exactly, their midpoints exactly and a digit more or
// less, their hex form, and their decimal rounded by big.Float to a random precision
func TestStrtoldRandom(t *testing.T) {
	r := rand.New(rand.NewPCG(11, 12))
	count := 1500
	if testing.Short() {
		count = 50
	}
	for range count {
		mant, signExp := randLongDouble(r)
		x := ld80{mant, signExp &^ 0x8000}
		sign := ""
		if signExp>>15 != 0 {
			sign = "-"
		}
		signed := func(v ld80) ld80 {
			if sign != "" {
				return v.neg()
			}
			return v
		}
		mid, more, less := midTexts(x)
		checkLong(t, sign+exactText(x), signed(x), -1)
		checkLong(t, sign+hexText(x), signed(x), -1)
		checkLong(t, sign+mid, signed(even(x)), -1)
		checkLong(t, sign+more, signed(x.next()), -1)
		checkLong(t, sign+less, signed(x), -1)
		// 0.ddd form, and digits past the 12000 of NtCRT that only round
		d, e := decimal(new(big.Int).SetUint64(x.mant), max(x.exp(), 1)-16446)
		checkLong(t, sign+"0."+d+"e"+strconv.Itoa(e+len(d)), signed(x), -1)
		checkLong(t, sign+moreDigits(exactText(x), strings.Repeat("0", 12000-min(len(d), 12000))+"1"), signed(x), -1)
		f := new(big.Float).SetMantExp(new(big.Float).SetUint64(x.mant), max(x.exp(), 1)-16446)
		checkStrtold(t, sign+f.Text('e', r.IntN(30)))
		checkStrtold(t, sign+f.Text('g', r.IntN(25)))
	}
	t.Logf("tested %d random long doubles", count)
}

var longSeeds = []string{
	"0", "-0", "1", "0.1", "1e4932", "1.18973149535723176502e+4932", "1.18973149535723176508e+4932", "1e4933",
	"3.64519953188247460253e-4951", "1.82259976594123730126e-4951", "1.8225997659412373013e-4951", "3.4e-4932",
	"3.36210314311209350626e-4932", "0x1p-16445", "0x1p-16446", "0x1.00000000000001p-16446",
	"0x1.fffffffffffffffep16383", "0x1.ffffffffffffffffp16383", "0x1.0000000000000001p0", "0x1.00000000000000010001p0",
	"0x.8p-16444", "36893488147419103231", "36893488147419103229", "inf", "-INFINITY", "infinit", "nan", "-nan(n_1)",
	"nan(", "", "-", "0x", "0x.p1", "1e", "1e+x", "  \t\n+1.25e-3xyz", "1e-99999999999", "1e99999999999",
	"0." + strings.Repeat("0", 4950) + "18225997659412373012637", "1" + strings.Repeat("0", 4932) + ".5",
	"9" + strings.Repeat("9", 12100) + "e-12100", "0x" + strings.Repeat("f", 40) + "p-16600",
}

func FuzzStrtold80(f *testing.F) {
	for _, s := range longSeeds {
		f.Add(s)
	}
	f.Fuzz(func(t *testing.T, s string) {
		checkStrtold(t, s)
	})
}

// TestStrtoldSeeds checks the seeds of both fuzz targets, the tables of strtod and the numbers of TestLong
func TestStrtoldSeeds(t *testing.T) {
	in := append(append(append([]string{}, longSeeds...), fuzzSeeds...), fuzzLongSeeds...)
	for _, tt := range atoftests {
		in = append(in, tt.in)
	}
	for _, tt := range longTests() {
		in = append(in, tt.in)
	}
	for _, s := range in {
		checkStrtold(t, s)
	}
	t.Logf("tested %d numbers", len(in))
}
