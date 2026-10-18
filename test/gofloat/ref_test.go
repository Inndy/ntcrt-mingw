// The reference for strtod: the syntax of C scanned here, the value by strconv.ParseFloat of Go where it is exact,
// by math/big where it is not (long_test.go).

package gofloat

import (
	"math"
	"math/big"
	"strconv"
	"strings"
)

type cFloat struct {
	end     int    // bytes converted, 0 if none
	token   string // the number in the syntax of Go
	nan     bool
	nonzero bool     // a digit that is not 0 before the exponent
	num     goNumber // the token in parts if it is finite
}

func isCSpace(c byte) bool { return c == ' ' || (c >= '\t' && c <= '\r') }

func isDigit(c byte) bool { return c >= '0' && c <= '9' }

func isHex(c byte) bool {
	return isDigit(c) || (c|0x20 >= 'a' && c|0x20 <= 'f')
}

func hasPrefixFold(s, p string) bool {
	return len(s) >= len(p) && strings.EqualFold(s[:len(p)], p)
}

// digits returns the length of the digits at s[i:] and whether one is not 0
func digits(s string, i int, hex bool) (n int, nonzero bool) {
	for i+n < len(s) && (isDigit(s[i+n]) || (hex && isHex(s[i+n]))) {
		nonzero = nonzero || s[i+n] != '0'
		n++
	}
	return n, nonzero
}

// scanC scans the longest prefix of s that strtod converts, after the characters space takes
func scanC(s string, space func(byte) bool) cFloat {
	i := 0
	for i < len(s) && space(s[i]) {
		i++
	}
	start := i
	sign := ""
	if i < len(s) && (s[i] == '+' || s[i] == '-') {
		sign = s[i : i+1]
		i++
	}
	r := s[i:]
	switch {
	case hasPrefixFold(r, "infinity"):
		return cFloat{end: i + 8, token: sign + "Inf"}
	case hasPrefixFold(r, "inf"):
		return cFloat{end: i + 3, token: sign + "Inf"}
	case hasPrefixFold(r, "nan"):
		end := i + 3
		if end < len(s) && s[end] == '(' {
			j := end + 1
			for j < len(s) && (isDigit(s[j]) || (s[j]|0x20 >= 'a' && s[j]|0x20 <= 'z') || s[j] == '_') {
				j++
			}
			if j < len(s) && s[j] == ')' {
				end = j + 1
			}
		}
		return cFloat{end: end, nan: true}
	}
	hex := len(r) >= 2 && r[0] == '0' && r[1]|0x20 == 'x'
	if hex {
		// "0x" is hexadecimal only if a hex digit follows it, or a point and a hex digit
		h := r[2:]
		if !(len(h) > 0 && isHex(h[0])) && !(len(h) > 1 && h[0] == '.' && isHex(h[1])) {
			hex = false
		}
	}
	j := i
	if hex {
		j += 2
	}
	num := goNumber{neg: sign == "-", hex: hex}
	m := j
	n1, nz1 := digits(s, j, hex)
	j += n1
	n2, nz2 := 0, false
	if j < len(s) && s[j] == '.' {
		n2, nz2 = digits(s, j+1, hex)
		if n1+n2 > 0 {
			j += 1 + n2
		}
	}
	if n1+n2 == 0 {
		return cFloat{}
	}
	mant := s[start:j]
	num.mant, num.point = s[m:j], n1
	expChar := byte('e')
	if hex {
		expChar = 'p'
	}
	exp := ""
	if j < len(s) && s[j]|0x20 == expChar {
		k := j + 1
		if k < len(s) && (s[k] == '+' || s[k] == '-') {
			k++
		}
		if n, _ := digits(s, k, false); n > 0 {
			exp = s[j : k+n]
			num.expNeg, num.exp = s[k-1] == '-', s[k:k+n]
			j = k + n
		}
	}
	if hex && exp == "" {
		exp = "p0"
	}
	return cFloat{end: j, token: mant + exp, nonzero: nz1 || nz2, num: num}
}

// refStrtod is what strtod should return for s: the bits, the end, and errno
func refStrtod(s string, bitSize int) (bits uint64, end int, errno int, nan bool) {
	return refConvert(s, bitSize, isCSpace)
}

// refWcstod is what wcstod should return for the bytes of s as U+0000..U+00FF: it skips what iswspace takes
func refWcstod(s string) (bits uint64, end int, errno int, nan bool) {
	return refConvert(s, 64, cIsWspace)
}

func refConvert(s string, bitSize int, space func(byte) bool) (bits uint64, end int, errno int, nan bool) {
	if i := strings.IndexByte(s, 0); i >= 0 {
		s = s[:i]
	}
	c := scanC(s, space)
	if c.end == 0 {
		return 0, 0, 0, false
	}
	if c.nan {
		return 0, c.end, 0, true
	}
	bits = refBits(c, bitSize, false)
	finite := !strings.HasSuffix(c.token, "Inf")
	if bitSize == 32 {
		if (finite && bits&0x7FFFFFFF == 0x7F800000) || (c.nonzero && bits&0x7FFFFFFF == 0) {
			errno = erange
		}
	} else {
		if (finite && bits&0x7FFFFFFFFFFFFFFF == 0x7FF0000000000000) || (c.nonzero && bits&0x7FFFFFFFFFFFFFFF == 0) {
			errno = erange
		}
	}
	return bits, c.end, errno, false
}

// refBits converts what scanC read to the bits of a float64 or float32: by strconv.ParseFloat if the number is
// inside its limits and viaBig is false, else by math/big
func refBits(c cFloat, bitSize int, viaBig bool) uint64 {
	if !strings.HasSuffix(c.token, "Inf") && (viaBig || !c.num.goSafe()) {
		return c.num.bigBits(bitSize)
	}
	return parseBits(c.token, bitSize)
}

// parseBits is strconv.ParseFloat of the token in bits
func parseBits(token string, bitSize int) uint64 {
	f, err := strconv.ParseFloat(token, bitSize)
	if err != nil && !strings.Contains(err.Error(), "range") {
		panic("ParseFloat " + token + ": " + err.Error())
	}
	if bitSize == 32 {
		return uint64(math.Float32bits(float32(f)))
	}
	return math.Float64bits(f)
}

// goNumber is a finite number scanned by scanC in parts
type goNumber struct {
	neg, hex bool
	mant     string // the digits and the point
	point    int    // the number of digits before the point
	expNeg   bool
	exp      string // the digits of the exponent, "" if none
}

// goSafe reports whether n is well inside the limits of strconv.ParseFloat of Go 1.27: the decimal of its slow path
// keeps 800 digits and places the point after them if there are more before it (the digits after it only round), and
// readFloat stops adding the digits of the exponent once it is 10000 (exact below 100000). Here at most 700 digits
// before the point from the first that is not 0, an exponent below 10000.
func (n goNumber) goSafe() bool {
	return len(strings.TrimLeft(n.mant[:n.point], "0")) <= 700 && len(strings.TrimLeft(n.exp, "0")) <= 4
}

// bigBits rounds n exactly by math/big to the bits of a float64 or float32, ties to even
func (n goNumber) bigBits(bitSize int) uint64 {
	var f float64
	if sig, first := n.significant(); sig != "" {
		f = n.bigAbs(sig, first, bitSize)
	}
	if n.neg {
		f = -f
	}
	if bitSize == 32 {
		return uint64(math.Float32bits(float32(f)))
	}
	return math.Float64bits(f)
}

// significant is the digits of n from the first to the last that is not 0 and the index of the first, "" if n is 0
func (n goNumber) significant() (sig string, first int) {
	digits := strings.Replace(n.mant, ".", "", 1)
	notZero := func(r rune) bool { return r != '0' }
	first = strings.IndexFunc(digits, notZero)
	if first < 0 {
		return "", 0
	}
	return digits[first : strings.LastIndexFunc(digits, notZero)+1], first
}

// scaled splits |n|, sig its digits from the first to the last that is not 0, first the index of sig: |n| is
// m * 10**low, at least 10**(top-1) and below 10**top; m * 2**low, 2**(top-4) to 2**top in hex
func (n goNumber) scaled(sig string, first int) (m *big.Int, low, top int64) {
	// the exponent saturates far past any position of a digit
	e := strings.TrimLeft(n.exp, "0")
	if len(e) > 15 {
		e = "999999999999999"
	}
	exp, _ := strconv.ParseInt(e, 10, 64)
	if n.expNeg {
		exp = -exp
	}
	m = new(big.Int)
	if n.hex {
		m.SetString(sig, 16)
		return m, exp + 4*int64(n.point-first-len(sig)), exp + 4*int64(n.point-first)
	}
	m.SetString(sig, 10)
	return m, exp + int64(n.point-first-len(sig)), exp + int64(n.point-first)
}

// fraction is m * 10**low (2**low in hex) as num / den
func (n goNumber) fraction(m *big.Int, low int64) (num, den *big.Int) {
	p := new(big.Int)
	if n.hex {
		p.Lsh(big.NewInt(1), uint(max(low, -low)))
	} else {
		p.Exp(big.NewInt(10), big.NewInt(max(low, -low)), nil)
	}
	if low >= 0 {
		return p.Mul(m, p), big.NewInt(1)
	}
	return m, p
}

// bigAbs is |n| rounded to bitSize, sig its digits from the first to the last that is not 0, first the index of sig
func (n goNumber) bigAbs(sig string, first, bitSize int) float64 {
	m, low, top := n.scaled(sig, first)
	if (n.hex && top > 1100) || (!n.hex && top > 310) {
		return math.Inf(1)
	}
	if (n.hex && top < -1200) || (!n.hex && top < -330) {
		return 0
	}
	r := new(big.Rat).SetFrac(n.fraction(m, low))
	// rounded to even, to subnormals and infinity
	if bitSize == 32 {
		f, _ := r.Float32()
		return float64(f)
	}
	f, _ := r.Float64()
	return f
}
