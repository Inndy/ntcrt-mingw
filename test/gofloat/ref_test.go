// The reference for strtod: the syntax of C scanned here, the value by strconv.ParseFloat of Go.

package gofloat

import (
	"math"
	"strconv"
	"strings"
)

type cFloat struct {
	end     int    // bytes converted, 0 if none
	token   string // the number in the syntax of Go
	nan     bool
	nonzero bool // a digit that is not 0 before the exponent
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
			j = k + n
		}
	}
	if hex && exp == "" {
		exp = "p0"
	}
	return cFloat{end: j, token: mant + exp, nonzero: nz1 || nz2}
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
	f, err := strconv.ParseFloat(c.token, bitSize)
	if err != nil && !strings.Contains(err.Error(), "range") {
		panic("ParseFloat " + c.token + ": " + err.Error())
	}
	if bitSize == 32 {
		f32 := float32(f)
		bits = uint64(math.Float32bits(f32))
		if (math.IsInf(f, 0) && !strings.HasSuffix(c.token, "Inf")) || (c.nonzero && bits&0x7F800000 == 0) {
			errno = erange
		}
	} else {
		bits = math.Float64bits(f)
		if (math.IsInf(f, 0) && !strings.HasSuffix(c.token, "Inf")) || (c.nonzero && bits&0x7FF0000000000000 == 0) {
			errno = erange
		}
	}
	return bits, c.end, errno, false
}
