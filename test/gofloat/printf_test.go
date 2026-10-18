// Test %e, %f, %g and %a of the printf of NtCRT: double against strconv, long double against math/big, %a
// against a model from the bits. go test -fuzz FuzzPrintfDouble, go test -fuzz FuzzPrintfLongDouble

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

// spec is one conversion: width 0 if none, prec -1 if none
type spec struct {
	minus, plus, space, zero, alt bool
	width, prec                   int
	conv                          byte
}

func (s spec) format(long bool) string {
	b := []byte{'%'}
	for i, on := range []bool{s.minus, s.plus, s.space, s.zero, s.alt} {
		if on {
			b = append(b, "-+ 0#"[i])
		}
	}
	if s.width > 0 {
		b = strconv.AppendInt(b, int64(s.width), 10)
	}
	if s.prec >= 0 {
		b = append(b, '.')
		b = strconv.AppendInt(b, int64(s.prec), 10)
	}
	if long {
		b = append(b, 'L')
	}
	return string(append(b, s.conv))
}

// decodeSpec maps fuzz inputs to a spec: prec is none, 0..60 or (1 in 16) up to maxF for %f and maxE otherwise
func decodeSpec(conv byte, prec int, flags, width byte, maxE, maxF int) spec {
	s := spec{conv: "eEfFgGaA"[conv%8]}
	u := uint(prec)
	switch u % 16 {
	case 0:
		s.prec = -1
	case 1:
		m := maxE
		if s.conv|0x20 == 'f' {
			m = maxF
		}
		s.prec = int(u / 16 % uint(m+1))
	default:
		s.prec = int(u / 16 % 61)
	}
	s.minus = flags&0x01 != 0
	s.plus = flags&0x02 != 0
	s.space = flags&0x04 != 0
	s.zero = flags&0x08 != 0
	s.alt = flags&0x10 != 0
	if flags&0x20 != 0 {
		s.width = int(width)
	}
	return s
}

func sign(s spec, neg bool) string {
	switch {
	case neg:
		return "-"
	case s.plus:
		return "+"
	case s.space:
		return " "
	}
	return ""
}

// field pads prefix+body to the width: spaces on the left, on the right (-), or zeros after the prefix (0)
func field(s spec, prefix, body string, zeroOK bool) string {
	pad := s.width - len(prefix) - len(body)
	switch {
	case pad <= 0:
		return prefix + body
	case s.minus:
		return prefix + body + strings.Repeat(" ", pad)
	case s.zero && zeroOK:
		return prefix + strings.Repeat("0", pad) + body
	}
	return strings.Repeat(" ", pad) + prefix + body
}

// textFunc formats |x| as strconv and math/big do: 'e', 'f' or 'g' with prec
type textFunc func(c byte, prec int) string

func doubleText(f float64) textFunc {
	a := math.Abs(f)
	return func(c byte, prec int) string { return strconv.FormatFloat(a, c, prec, 64) }
}

// longText is the 80-bit long double mant * 2**(max(e, 1) - 16383 - 63), exact in a big.Float
func longText(mant uint64, e int) textFunc {
	x := new(big.Float).SetMantExp(new(big.Float).SetUint64(mant), max(e, 1)-16383-63)
	return func(c byte, prec int) string { return x.Text(c, prec) }
}

// wantFloat is %e, %f or %g of a finite number by C rules
func wantFloat(s spec, neg bool, text textFunc) string {
	p := s.prec
	if p < 0 {
		p = 6
	}
	var body string
	switch s.conv | 0x20 {
	case 'e':
		body = text('e', p)
		if s.alt && p == 0 {
			body = strings.Replace(body, "e", ".e", 1)
		}
	case 'f':
		body = text('f', p)
		if s.alt && p == 0 {
			body += "."
		}
	case 'g':
		// strconv and big take 'g' with prec 0 as 1 and drop the trailing zeros as C does
		body = text('g', p)
		if s.alt {
			body = altG(p, text)
		}
	}
	if s.conv < 'a' {
		body = strings.ToUpper(body)
	}
	return field(s, sign(s, neg), body, true)
}

// altG is %#g: P significant digits, X the exponent, %f if P > X >= -4 else %e, zeros and the point kept
func altG(p int, text textFunc) string {
	P := max(p, 1)
	e := text('e', P-1)
	X, err := strconv.Atoi(e[strings.IndexByte(e, 'e')+1:])
	if err != nil {
		panic(err)
	}
	if P > X && X >= -4 {
		f := text('f', P-1-X)
		if !strings.Contains(f, ".") {
			f += "."
		}
		return f
	}
	if !strings.Contains(e, ".") {
		e = strings.Replace(e, "e", ".e", 1)
	}
	return e
}

// wantSpecial is inf or nan with its sign; the 0 flag pads with spaces
func wantSpecial(s spec, neg, nan bool) string {
	body := "inf"
	if nan {
		body = "nan"
	}
	if s.conv < 'a' {
		body = strings.ToUpper(body)
	}
	return field(s, sign(s, neg), body, false)
}

// wantHex is %a of lead.frac * 2**exp, frac nd hex digits: ties to even at s.prec digits, else exact without
// the trailing zeros. A carry into lead 16 makes it 1 with exp+4 (long double).
func wantHex(s spec, neg bool, lead, frac uint64, nd, exp int) string {
	digits := nd
	if s.prec >= 0 && s.prec < nd {
		drop := uint(nd-s.prec) * 4
		v := lead<<(uint(nd)*4) | frac
		q, r, half := v>>drop, v&(1<<drop-1), uint64(1)<<(drop-1)
		if r > half || r == half && q&1 == 1 {
			q++
		}
		lead, frac = q>>(uint(s.prec)*4), q&(1<<(uint(s.prec)*4)-1)
		if lead == 16 {
			lead, exp = 1, exp+4
		}
		digits = s.prec
	}
	fs := ""
	if digits > 0 {
		fs = fmt.Sprintf("%0*x", digits, frac)
	}
	if s.prec < 0 {
		fs = strings.TrimRight(fs, "0")
	} else {
		fs += strings.Repeat("0", s.prec-len(fs))
	}
	body := strconv.FormatUint(lead, 16)
	if fs != "" || s.alt {
		body += "." + fs
	}
	body += fmt.Sprintf("p%+d", exp)
	prefix := sign(s, neg) + "0x"
	if s.conv < 'a' {
		prefix, body = strings.ToUpper(prefix), strings.ToUpper(body)
	}
	return field(s, prefix, body, true)
}

// hexDouble splits a finite double as %a prints it: 1.frac * 2**exp, denormal 0.frac * 2**-1022, 0 as 0p+0
func hexDouble(bits uint64) (lead, frac uint64, exp int) {
	e := int(bits>>52) & 0x7FF
	frac = bits & (1<<52 - 1)
	switch {
	case e == 0 && frac == 0:
		return 0, 0, 0
	case e == 0:
		return 0, frac, -1022
	}
	return 1, frac, e - 1023
}

// hexLong splits a finite long double as NtCRT prints it: the 4 high bits of the mantissa lead
func hexLong(mant uint64, e int) (lead, frac uint64, exp int) {
	if mant == 0 {
		return 0, 0, 0
	}
	return mant >> 60, mant & (1<<60 - 1), max(e, 1) - 16383 - 63 + 60
}

func wantDouble(s spec, bits uint64) string {
	f := math.Float64frombits(bits)
	neg := bits>>63 != 0
	switch {
	case math.IsInf(f, 0) || math.IsNaN(f):
		return wantSpecial(s, neg, math.IsNaN(f))
	case s.conv|0x20 == 'a':
		lead, frac, exp := hexDouble(bits)
		return wantHex(s, neg, lead, frac, 13, exp)
	}
	return wantFloat(s, neg, doubleText(f))
}

func wantLongDouble(s spec, mant uint64, signExp uint16) string {
	e := int(signExp & 0x7FFF)
	neg := signExp>>15 != 0
	switch {
	case e == 0x7FFF:
		return wantSpecial(s, neg, mant<<1 != 0)
	case s.conv|0x20 == 'a':
		lead, frac, exp := hexLong(mant, e)
		return wantHex(s, neg, lead, frac, 15, exp)
	}
	return wantFloat(s, neg, longText(mant, e))
}

// diff describes got against want, long strings by their first difference
func diff(got string, n int, want string) string {
	if len(got) <= 100 && len(want) <= 100 {
		return fmt.Sprintf("%q (%d), want %q", got, n, want)
	}
	i := 0
	for i < len(got) && i < len(want) && got[i] == want[i] {
		i++
	}
	lo := max(i-20, 0)
	return fmt.Sprintf("returns %d, len %d, want len %d; first difference at %d: %q, want %q",
		n, len(got), len(want), i, got[lo:min(i+40, len(got))], want[lo:min(i+40, len(want))])
}

func checkDouble(t *testing.T, s spec, bits uint64) {
	t.Helper()
	want := wantDouble(s, bits)
	format := s.format(false)
	got, n := cFormatDouble(format, math.Float64frombits(bits))
	if got != want || n != len(want) {
		t.Errorf("printf(%q, %#016x) = %s", format, bits, diff(got, n, want))
	}
}

func checkLongDouble(t *testing.T, s spec, mant uint64, signExp uint16) {
	t.Helper()
	want := wantLongDouble(s, mant, signExp)
	format := s.format(true)
	got, n := cFormatLongDouble(format, mant, signExp)
	if got != want || n != len(want) {
		t.Errorf("printf(%q, mant %#016x signexp %#04x) = %s", format, mant, signExp, diff(got, n, want))
	}
}

func FuzzPrintfDouble(f *testing.F) {
	for _, v := range []float64{0, 1, 0.5, 2.5, 0.125, 9.5, 1e23, 123456789012345680, math.MaxFloat64,
		math.SmallestNonzeroFloat64, 2.2250738585072014e-308, 1.0 / 3, math.Pi, -1e-300} {
		bits := math.Float64bits(v)
		f.Add(bits, byte(0), 2, byte(0), byte(0))
		f.Add(bits, byte(2), 0, byte(0), byte(0))
		f.Add(bits, byte(4), 3*16+2, byte(0x10), byte(0))
		f.Add(bits, byte(6), 1*16+2, byte(0), byte(0))
		f.Add(bits, byte(3), 800*16+1, byte(0x2A), byte(30))
		f.Add(bits, byte(1), 760*16+1, byte(0x26), byte(12))
	}
	f.Add(uint64(0x7FF0000000000000), byte(0), 0, byte(0x2A), byte(8))
	f.Add(uint64(0xFFF8000000000001), byte(5), 0, byte(0x20), byte(8))
	f.Fuzz(func(t *testing.T, bits uint64, conv byte, prec int, flags, width byte) {
		checkDouble(t, decodeSpec(conv, prec, flags, width, 900, 1100), bits)
	})
}

func FuzzPrintfLongDouble(f *testing.F) {
	for _, v := range [][2]uint64{{1 << 63, 16383}, {0xC000000000000000, 16383}, {1, 0}, {1 << 63, 1},
		{0xFFFFFFFFFFFFFFFF, 0x7FFE}, {0x8000000000000001, 16383 + 63}, {0xA000000000000000, 16380},
		{1 << 63, 0x7FFF}, {0xC000000000000000, 0xFFFF}} {
		f.Add(v[0], uint16(v[1]), byte(0), 2, byte(0), byte(0))
		f.Add(v[0], uint16(v[1]), byte(2), 5*16+2, byte(0), byte(0))
		f.Add(v[0], uint16(v[1]), byte(4), 0, byte(0x10), byte(0))
		f.Add(v[0], uint16(v[1]), byte(6), 1*16+2, byte(0x2A), byte(20))
		f.Add(v[0], uint16(v[1]), byte(1), 12000*16+1, byte(0), byte(0))
	}
	f.Fuzz(func(t *testing.T, mant uint64, signExp uint16, conv byte, prec int, flags, width byte) {
		if signExp&0x7FFF != 0 {
			mant |= 1 << 63 // no unnormals
		}
		checkLongDouble(t, decodeSpec(conv, prec, flags, width, 12100, 17100), mant, signExp)
	})
}

// Literal expectations, also checked against the model
var printfDoubleCases = []struct {
	format string
	v      float64
	want   string
}{
	// ties to even
	{"%.0f", 0.5, "0"},
	{"%.0f", 1.5, "2"},
	{"%.0f", 2.5, "2"},
	{"%.0f", 9.5, "10"},
	{"%.1f", 0.25, "0.2"},
	{"%.1f", 0.75, "0.8"},
	{"%.2f", 0.125, "0.12"},
	{"%.2f", 0.375, "0.38"},
	{"%.0e", 25, "2e+01"},
	{"%.0e", 35, "4e+01"},
	{"%.1e", 0.125, "1.2e-01"},
	{"%.0e", 2.5e21, "2e+21"},
	{"%.10e", 0x1p-16, "1.5258789062e-05"},
	{"%.15f", 0x1p-16, "0.000015258789062"},
	{"%.2g", 0.125, "0.12"},
	{"%.0g", 2.5, "2"},
	{"%.3g", 1.125, "1.12"},
	{"%.0e", 1.5e16, "2e+16"},
	{"%.0e", 2.5e16, "2e+16"},
	// %g
	{"%g", 100000, "100000"},
	{"%g", 1e6, "1e+06"},
	{"%g", 0.0001, "0.0001"},
	{"%g", 0.00001, "1e-05"},
	{"%.0g", 0, "0"},
	{"%#.0g", 0, "0."},
	{"%#g", 1, "1.00000"},
	{"%#.3g", 100, "100."},
	{"%G", 1e-10, "1E-10"},
	{"%g", 9.9999995, "10"},
	// flags and width
	{"%#.0f", 1, "1."},
	{"%#.0e", 1, "1.e+00"},
	{"% .3e", 1.5, " 1.500e+00"},
	{"%+010.2f", 3.14159, "+000003.14"},
	{"%-10.1e", -2.0, "-2.0e+00  "},
	{"%010.1f", math.Copysign(0, -1), "-0000000.0"},
	{"%e", 1e300, "1.000000e+300"},
	{"%.3e", 5e-324, "4.941e-324"},
	{"%f", 1e15, "1000000000000000.000000"},
	// inf and nan: the 0 flag pads with spaces
	{"%f", math.Inf(1), "inf"},
	{"%E", math.Inf(-1), "-INF"},
	{"%f", math.NaN(), "nan"},
	{"%G", math.Float64frombits(0xFFF8000000000000), "-NAN"},
	{"%+08f", math.Inf(1), "    +inf"},
	{"%-6g", math.NaN(), "nan   "},
	// %a
	{"%a", 1, "0x1p+0"},
	{"%a", 0.5, "0x1p-1"},
	{"%a", 0, "0x0p+0"},
	{"%A", -1, "-0X1P+0"},
	{"%a", 0x1.8p10, "0x1.8p+10"},
	{"%.1a", 0x1.08p0, "0x1.0p+0"},
	{"%.1a", 0x1.18p0, "0x1.2p+0"},
	{"%.0a", 1.5, "0x2p+0"},
	{"%.0a", 0x1.7p0, "0x1p+0"},
	{"%.0a", 0x1.fp0, "0x2p+0"},
	{"%#.0a", 1, "0x1.p+0"},
	{"%.3a", 1, "0x1.000p+0"},
	{"%.20a", 1, "0x1.00000000000000000000p+0"},
	{"%a", math.SmallestNonzeroFloat64, "0x0.0000000000001p-1022"},
	{"%a", 0x1p-1023, "0x0.8p-1022"},
	{"%.0a", 0x0.fffffffffffffp-1022, "0x1p-1022"},
	{"%a", math.MaxFloat64, "0x1.fffffffffffffp+1023"},
	{"%012a", 1, "0x0000001p+0"},
	{"%+.2A", 0x1.abcp-5, "+0X1.ACP-5"},
}

func TestPrintfDoubleCases(t *testing.T) {
	for _, c := range printfDoubleCases {
		got, n := cFormatDouble(c.format, c.v)
		if got != c.want || n != len(c.want) {
			t.Errorf("printf(%q, %v) = %q (%d), want %q", c.format, c.v, got, n, c.want)
		}
		if s, ok := parseSpec(c.format); ok {
			if m := wantDouble(s, math.Float64bits(c.v)); m != c.want {
				t.Errorf("model of printf(%q, %v) = %q, want %q", c.format, c.v, m, c.want)
			}
		}
	}
}

// parseSpec parses the formats of the tests: %[flags][width][.prec]conv
func parseSpec(f string) (spec, bool) {
	s := spec{prec: -1}
	if len(f) < 2 || f[0] != '%' {
		return s, false
	}
	i := 1
	flags := []*bool{&s.minus, &s.plus, &s.space, &s.zero, &s.alt}
	for ; i < len(f) && strings.IndexByte("-+ 0#", f[i]) >= 0; i++ {
		*flags[strings.IndexByte("-+ 0#", f[i])] = true
	}
	for ; i < len(f) && f[i] >= '0' && f[i] <= '9'; i++ {
		s.width = s.width*10 + int(f[i]-'0')
	}
	if i < len(f) && f[i] == '.' {
		s.prec = 0
		for i++; i < len(f) && f[i] >= '0' && f[i] <= '9'; i++ {
			s.prec = s.prec*10 + int(f[i]-'0')
		}
	}
	if i != len(f)-1 || strings.IndexByte("eEfFgGaA", f[i]) < 0 {
		return s, false
	}
	s.conv = f[i]
	return s, true
}

// exactDigits is the exact decimal of |f|: the significant digits and the digits after the point
func exactDigits(f float64) (sig, frac int) {
	e := strconv.FormatFloat(math.Abs(f), 'e', 1100, 64)
	i := strings.IndexByte(e, 'e')
	m := strings.TrimRight(strings.Replace(e[:i], ".", "", 1), "0")
	x, _ := strconv.Atoi(e[i+1:])
	return len(m), max(len(m)-1-x, 0)
}

// TestPrintfDoubleTies formats values at the precision that makes them exact decimal ties
func TestPrintfDoubleTies(t *testing.T) {
	var vs []float64
	for k := 1; k <= 70; k++ {
		for j := 0; j < 20; j++ {
			vs = append(vs, float64(2*j+1)*math.Ldexp(1, -k))
		}
	}
	for k := 1; k <= 22; k++ {
		for j := -40; j <= 60; j += 3 {
			vs = append(vs, math.Ldexp(math.Pow(5, float64(k)), j), math.Ldexp(3*math.Pow(5, float64(k)), j))
		}
	}
	for _, b := range []uint64{1, 3, 5, 0x1234567, 1<<52 - 1, 1 << 52, 1<<52 + 1, 0x0010000000000005} {
		vs = append(vs, math.Float64frombits(b))
	}
	ties := 0
	for _, v := range vs {
		sig, frac := exactDigits(v)
		for _, c := range []byte{'e', 'f', 'g', 'E', 'G'} {
			s := spec{conv: c, prec: sig - 2}
			switch c | 0x20 {
			case 'f':
				s.prec = frac - 1
			case 'g':
				s.prec = sig - 1
			}
			if s.prec < 0 {
				continue
			}
			checkDouble(t, s, math.Float64bits(v))
			checkDouble(t, s, math.Float64bits(-v))
			ties += 2
		}
	}
	t.Logf("%d ties", ties)
}

func randSpec(r *rand.Rand, convs string, maxE, maxF int) spec {
	s := spec{conv: convs[r.IntN(len(convs))], prec: -1}
	switch x := r.IntN(100); {
	case x < 60:
		s.prec = r.IntN(21)
	case x < 85:
		s.prec = r.IntN(61)
	case x < 92:
		m := maxE
		if s.conv|0x20 == 'f' {
			m = maxF
		}
		s.prec = r.IntN(m + 1)
	}
	if r.IntN(5) == 0 {
		s.minus, s.plus, s.space, s.zero, s.alt = r.IntN(4) == 0, r.IntN(3) == 0, r.IntN(3) == 0, r.IntN(2) == 0, r.IntN(3) == 0
		s.width = r.IntN(40)
	}
	return s
}

// randDouble is random bits, often with a short mantissa (short decimals make ties) or a special exponent
func randDouble(r *rand.Rand) uint64 {
	b := r.Uint64()
	switch r.IntN(6) {
	case 0:
		b &^= 1<<r.IntN(53) - 1
	case 1:
		b &= 0x800FFFFFFFFFFFFF // denormal
	case 2:
		b = b&^(0x7FF<<52) | uint64(1023-60+r.IntN(120))<<52 // near 1
	case 3:
		b = b&^(0x7FF<<52) | uint64(1023+r.IntN(64))<<52 // integers
		b &^= 1<<r.IntN(53) - 1
	}
	return b
}

func TestPrintfDoubleRandom(t *testing.T) {
	r := rand.New(rand.NewPCG(1, 2))
	n := 30000
	if testing.Short() {
		n = 3000
	}
	for range n {
		checkDouble(t, randSpec(r, "eEfFgGaA", 900, 1100), randDouble(r))
	}
}

// TestPrintfHexModel checks the %a model against strconv 'x', which writes the same digits with a 2-digit
// exponent for normal numbers; it carries 0x1.f into 0x1p+1 where C and the model write 0x2p+0
func TestPrintfHexModel(t *testing.T) {
	r := rand.New(rand.NewPCG(3, 4))
	for range 20000 {
		bits := randDouble(r)
		if e := bits >> 52 & 0x7FF; e == 0 || e == 0x7FF {
			continue
		}
		s := spec{conv: 'a', prec: r.IntN(16) - 1}
		f := math.Float64frombits(bits)
		got := wantDouble(s, bits)
		x := strconv.FormatFloat(f, 'x', s.prec, 64)
		i := strings.IndexByte(x, 'p')
		e, _ := strconv.Atoi(x[i+1:])
		x = fmt.Sprintf("%sp%+d", x[:i], e)
		if strings.Contains(got, "0x2") {
			continue
		}
		if got != x {
			t.Errorf("model %%.%da of %#016x = %q, strconv %q", s.prec, bits, got, x)
		}
	}
}

func TestPrintfLongDoubleCases(t *testing.T) {
	cases := []struct {
		format  string
		mant    uint64
		signExp uint16
		want    string
	}{
		{"%Lf", 1 << 63, 16383, "1.000000"},
		{"%.0Lf", 0xC000000000000000, 16383, "2"},     // 1.5
		{"%.0Lf", 0xA000000000000000, 16384, "2"},     // 2.5
		{"%.2Lf", 1 << 63, 16380, "0.12"},             // 0.125
		{"%.0Le", 0xC800000000000000, 16387, "2e+01"}, // 25
		{"%Lg", 1 << 63, 16383 + 20, "1.04858e+06"},   // 2**20
		{"%#.3Lg", 0xC800000000000000, 16389, "100."}, // 100
		{"%.3Le", 1, 0, "3.645e-4951"},                // 2**-16445
		{"%Le", 0xFFFFFFFFFFFFFFFF, 0x7FFE, "1.189731e+4932"},
		{"%La", 1 << 63, 16383, "0x8p-3"},
		{"%La", 1, 0, "0x0.000000000000001p-16385"},
		{"%.0La", 0xF800000000000000, 16383, "0x1p+1"}, // 1.9375: f.8 rounds to 0x10
		{"%Lf", 1 << 63, 0x7FFF, "inf"},
		{"%LF", 0xC000000000000000, 0xFFFF, "-NAN"},
		{"%Lf", 0, 0x8000, "-0.000000"},
	}
	for _, c := range cases {
		got, n := cFormatLongDouble(c.format, c.mant, c.signExp)
		if got != c.want || n != len(c.want) {
			t.Errorf("printf(%q, %#016x %#04x) = %q (%d), want %q", c.format, c.mant, c.signExp, got, n, c.want)
		}
		if s, ok := parseSpec(strings.Replace(c.format, "L", "", 1)); ok {
			if m := wantLongDouble(s, c.mant, c.signExp); m != c.want {
				t.Errorf("model of printf(%q, %#016x %#04x) = %q, want %q", c.format, c.mant, c.signExp, m, c.want)
			}
		}
	}
}

// TestPrintfLargePrecision crosses the digit buffers (800 and 12000) and the %f cut-off of 17000 digits
func TestPrintfLargePrecision(t *testing.T) {
	for _, f := range []string{"e", "f", "g", "#g", "a"} {
		for _, p := range []int{766, 767, 799, 800, 801, 1074, 1075, 1100, 20000} {
			s, _ := parseSpec("%" + f[:len(f)-1] + "." + strconv.Itoa(p) + f[len(f)-1:])
			for _, b := range []uint64{1, 3, 0x000FFFFFFFFFFFFF, 0x0010000000000001, 0x7FEFFFFFFFFFFFFF, 0x3FF0000000000001} {
				checkDouble(t, s, b)
			}
		}
		for _, p := range []int{11490, 11500, 11999, 12000, 12001, 16444, 16445, 16446, 16999, 17000, 17001, 17500} {
			s, _ := parseSpec("%" + f[:len(f)-1] + "." + strconv.Itoa(p) + f[len(f)-1:])
			for _, v := range [][2]uint64{{1, 0}, {0xFFFFFFFFFFFFFFFF, 0}, {0xFFFFFFFFFFFFFFFF, 1}, {1 << 63, 1}, {0xFFFFFFFFFFFFFFFF, 0x7FFE}} {
				checkLongDouble(t, s, v[0], uint16(v[1]))
			}
		}
	}
}

// randLongDouble is a random long double: exponents over the whole range, near 1, 16383 and 0x7FFE,
// denormals, and short mantissas
func randLongDouble(r *rand.Rand) (uint64, uint16) {
	mant := r.Uint64()
	var e int
	switch r.IntN(6) {
	case 0:
		e = r.IntN(0x7FFF)
	case 1:
		e = r.IntN(40)
	case 2:
		e = 16383 - 80 + r.IntN(160)
	case 3:
		e = 0x7FFE - r.IntN(40)
	case 4:
		e = 16383 + r.IntN(64)
	case 5:
		e = 0
	}
	if r.IntN(3) == 0 {
		mant &^= 1<<r.IntN(64) - 1
	}
	if e != 0 {
		mant |= 1 << 63
	} else {
		mant &^= 1 << 63
	}
	return mant, uint16(e) | uint16(r.IntN(2))<<15
}

func TestPrintfLongDoubleRandom(t *testing.T) {
	r := rand.New(rand.NewPCG(5, 6))
	n := 20000
	if testing.Short() {
		n = 2000
	}
	for range n {
		mant, signExp := randLongDouble(r)
		checkLongDouble(t, randSpec(r, "eEfFgGaA", 12100, 17100), mant, signExp)
	}
}

// TestPrintfLongDoubleTies formats short long doubles at the precision of an exact tie
func TestPrintfLongDoubleTies(t *testing.T) {
	r := rand.New(rand.NewPCG(7, 8))
	for range 3000 {
		mant, signExp := randLongDouble(r)
		mant &^= 1<<(40+r.IntN(24)) - 1
		e := int(signExp & 0x7FFF)
		if e == 0 && mant == 0 {
			continue
		}
		x := new(big.Float).SetMantExp(new(big.Float).SetUint64(mant), max(e, 1)-16383-63)
		d := x.Text('e', 20000)
		i := strings.IndexByte(d, 'e')
		m := strings.TrimRight(strings.Replace(d[:i], ".", "", 1), "0")
		X, _ := strconv.Atoi(d[i+1:])
		sig, frac := len(m), max(len(m)-1-X, 0)
		for _, c := range []byte{'e', 'f', 'g'} {
			s := spec{conv: c, prec: sig - 2}
			switch c {
			case 'f':
				s.prec = frac - 1
			case 'g':
				s.prec = sig - 1
			}
			if s.prec < 0 {
				continue
			}
			checkLongDouble(t, s, mant, signExp)
		}
	}
}
