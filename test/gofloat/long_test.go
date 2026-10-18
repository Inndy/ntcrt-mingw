// Long numbers that strconv.ParseFloat of Go 1.27 converts wrongly, so refStrtod does not apply: the expected bits
// are written here.

package gofloat

import (
	"math"
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
	}
}
