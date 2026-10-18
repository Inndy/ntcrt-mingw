// Long numbers that strconv.ParseFloat of Go 1.27 converts wrongly, with the expected bits written here: refStrtod
// converts them by math/big. TestRefBig checks that path against ParseFloat where Go is exact.

package gofloat

import (
	"math"
	"math/big"
	"math/rand/v2"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

type longTest struct {
	in     string
	bits   uint64 // of strtod and wcstod
	bits32 uint32 // of strtof
}

func longTests() []longTest {
	// 1 + 2**-53 and 1 + 2**-24, the midpoints after 1 of double and float
	const mid64 = "100000000000000011102230246251565404236316680908203125"
	const mid32 = "1000000059604644775390625"
	zeros := func(n int) string { return strings.Repeat("0", n) }
	return []longTest{
		// More than the 800 digits of the decimal before the point: decimal.set of Go places the point after 800
		{mid64 + zeros(747) + "e-800", 0x3FF0000000000000, 0x3F800000},
		{mid64 + zeros(747) + ".0e-800", 0x3FF0000000000000, 0x3F800000},
		{mid64 + zeros(747) + "1e-801", 0x3FF0000000000001, 0x3F800000},
		{mid32 + zeros(800) + "e-824", 0x3FF0000010000000, 0x3F800000},
		{mid32 + zeros(800) + "1e-825", 0x3FF0000010000000, 0x3F800001},

		// An exponent past 10000 that the position of the digits brings back: Go saturates it at 10000 first
		{"0." + zeros(99999) + "1e100000", 0x3FF0000000000000, 0x3F800000},
		{"1" + zeros(100000) + "e-100000", 0x3FF0000000000000, 0x3F800000},
		{"-0." + zeros(99999) + "15e+100000", 0xBFF8000000000000, 0xBFC00000},
		{"0x0." + zeros(30000) + "1p120004", 0x3FF0000000000000, 0x3F800000},
		{"0x1" + zeros(30000) + "p-120000", 0x3FF0000000000000, 0x3F800000},
		// Exponents too big for any position of the digits still overflow or underflow
		{"1e2147483648", 0x7FF0000000000000, 0x7F800000},
		{"1e-99999999999999999999", 0, 0},
		{"0." + zeros(100000) + "1e99999999999", 0x7FF0000000000000, 0x7F800000},
		{"1" + zeros(100000) + "e-99999999999", 0, 0},
		{"0x1p2147483648", 0x7FF0000000000000, 0x7F800000},
		{"-0x1p-99999999999", 0x8000000000000000, 0x80000000},
	}
}

func TestLong(t *testing.T) {
	for _, tt := range longTests() {
		checkFloat64(t, tt.in, math.Float64frombits(tt.bits), len(tt.in))
		checkFloat32(t, tt.in, math.Float32frombits(tt.bits32), len(tt.in))
		bits, _, _, _ := refStrtod(tt.in, 64)
		wbits, _, _, _ := refWcstod(tt.in)
		bits32, _, _, _ := refStrtod(tt.in, 32)
		if bits != tt.bits || wbits != tt.bits || uint32(bits32) != tt.bits32 {
			t.Errorf("refStrtod(%s) = %x, refWcstod %x, 32 bits %x, want %x and %x", short(tt.in), bits, wbits, bits32, tt.bits, tt.bits32)
		}
		checkStrtod(t, tt.in)
	}
}

// goExact reports whether strconv.ParseFloat of Go 1.27 converts n exactly: in a decimal at most 800 digits before
// the point from the first that is not 0, an exponent below 100000 (readFloat adds its digits while it is below 10000)
func goExact(n goNumber) bool {
	return (n.hex || len(strings.TrimLeft(n.mant[:n.point], "0")) <= 800) && len(strings.TrimLeft(n.exp, "0")) <= 5
}

// checkBig checks the math/big path of refBits against strconv.ParseFloat for s where Go is exact
func checkBig(t *testing.T, s string) bool {
	t.Helper()
	c := scanC(s, isCSpace)
	if c.end == 0 || c.nan || strings.HasSuffix(c.token, "Inf") || !goExact(c.num) {
		return false
	}
	for _, bitSize := range []int{64, 32} {
		if got, want := refBits(c, bitSize, true), parseBits(c.token, bitSize); got != want {
			t.Errorf("math/big %d bits of %s = %x, ParseFloat %x", bitSize, short(c.token), got, want)
		}
	}
	return true
}

// exactDecimal is the exact value of x in the 'e' format, without the trailing zeros of its digits
func exactDecimal(x *big.Float) (mant, exp string) {
	mant, exp, _ = strings.Cut(x.Text('e', 800), "e")
	return strings.TrimRight(mant, "0"), exp
}

// randDigits is n random decimal or hex digits, often in runs of 0 or 9 (f)
func randDigits(r *rand.Rand, n int, hex bool) string {
	const dec, hx = "0123456789", "0123456789abcdefABCDEF"
	b := make([]byte, n)
	for i := range b {
		switch {
		case r.IntN(4) == 0:
			b[i] = '0'
		case r.IntN(4) == 0 && hex:
			b[i] = 'f'
		case r.IntN(4) == 0:
			b[i] = '9'
		case hex:
			b[i] = hx[r.IntN(len(hx))]
		default:
			b[i] = dec[r.IntN(len(dec))]
		}
	}
	return string(b)
}

// TestRefBig checks the math/big path of the reference against ParseFloat: on the inputs of the tables, the seeds and
// the corpus of the fuzz, the midpoints of random doubles and floats and a digit more or less, random long decimal
// and hex numbers
func TestRefBig(t *testing.T) {
	var in []string
	in = append(append(in, fuzzSeeds...), fuzzLongSeeds...)
	for _, tt := range atoftests {
		in = append(in, tt.in)
	}
	for _, tt := range atof32tests {
		in = append(in, tt.in)
	}
	for _, tt := range roundTripCases {
		in = append(in, tt.s)
	}
	files, _ := filepath.Glob("testdata/fuzz/FuzzStrtod/*")
	for _, name := range files {
		data, err := os.ReadFile(name)
		if err != nil {
			t.Fatal(err)
		}
		lines := strings.Split(strings.TrimSpace(string(data)), "\n")
		if s, err := strconv.Unquote(strings.TrimSuffix(strings.TrimPrefix(lines[len(lines)-1], "string("), ")")); err == nil {
			in = append(in, s)
		}
	}
	count := 500
	if testing.Short() {
		count = 50
	}
	r := rand.New(rand.NewPCG(3, 4))
	for range count {
		// midpoints of doubles and floats, normal and subnormal, and a digit more or less
		x64 := math.Float64frombits(r.Uint64N(0x7FF0000000000000))
		x32 := math.Float32frombits(r.Uint32N(0x7F800000))
		for _, m := range []*big.Float{
			new(big.Float).SetPrec(200).Quo(new(big.Float).Add(big.NewFloat(x64), big.NewFloat(math.Nextafter(x64, math.Inf(1)))), big.NewFloat(2)),
			new(big.Float).SetPrec(200).Quo(new(big.Float).Add(big.NewFloat(float64(x32)), big.NewFloat(float64(math.Nextafter32(x32, float32(math.Inf(1)))))), big.NewFloat(2)),
		} {
			mant, exp := exactDecimal(m)
			lower := []byte(mant)
			lower[len(lower)-1]--
			in = append(in, mant+"e"+exp, mant+"0001e"+exp, string(lower)+"9999e"+exp)
		}
		// strconv of random doubles
		f := math.Float64frombits(r.Uint64())
		in = append(in, strconv.FormatFloat(f, 'e', r.IntN(40)-1, 64), strconv.FormatFloat(f, 'x', r.IntN(20)-1, 64))
		// long decimals and hex numbers with leading zeros, the exponent near the range
		zeros := strings.Repeat("0", r.IntN(1200))
		d := randDigits(r, 1+r.IntN(900), false)
		point := r.IntN(len(d) + 1)
		top := r.IntN(700) - 360
		in = append(in, zeros+d[:point]+"."+d[point:]+"e"+strconv.Itoa(top-point))
		in = append(in, "0."+zeros+d+"e"+strconv.Itoa(top+len(zeros)))
		h := randDigits(r, 1+r.IntN(300), true)
		point = r.IntN(len(h) + 1)
		top = r.IntN(2400) - 1200
		in = append(in, "0x"+zeros+h[:point]+"."+h[point:]+"p"+strconv.Itoa(top-4*point))
		in = append(in, "-0x0."+zeros+h+"p"+strconv.Itoa(top+4*len(zeros)))
	}
	n := 0
	for _, s := range in {
		if checkBig(t, s) {
			n++
		}
	}
	t.Logf("tested %d numbers", n)
}
