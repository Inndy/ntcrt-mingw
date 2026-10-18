// The float conversions of NtCRT, from float.dll (dll/float.c) built by make test-go. NTCRT_FLOAT_DLL names the
// DLL, ../../build/gofloat/float.dll by default.

package gofloat

import (
	"encoding/binary"
	"math"
	"os"
	"strings"
	"syscall"
	"unsafe"
)

var (
	dll         = syscall.NewLazyDLL(dllPath())
	procStrtod  = dll.NewProc("strtod_bits")
	procStrtof  = dll.NewProc("strtof_bits")
	procWcstod  = dll.NewProc("wcstod_bits")
	procStrtold = dll.NewProc("strtold_bytes")
	procWcstold = dll.NewProc("wcstold_bytes")
	procWspace  = dll.NewProc("is_wspace")
	procFmtD    = dll.NewProc("format_double")
	procFmtLD   = dll.NewProc("format_long_double")
)

const erange = 34

func dllPath() string {
	if p := os.Getenv("NTCRT_FLOAT_DLL"); p != "" {
		return p
	}
	return "../../build/gofloat/float.dll"
}

// cString is s up to its first NUL, NUL-terminated: what C sees of s
func cString(s string) []byte {
	if i := strings.IndexByte(s, 0); i >= 0 {
		s = s[:i]
	}
	return append([]byte(s), 0)
}

// cStrtod returns the bits of strtod(s), the number of bytes it converts and errno
func cStrtod(s string) (bits uint64, end int, errno int) {
	b := cString(s)
	var e int32
	n, _, _ := procStrtod.Call(uintptr(unsafe.Pointer(&b[0])), uintptr(unsafe.Pointer(&bits)), uintptr(unsafe.Pointer(&e)))
	return bits, int(n), int(e)
}

func cStrtof(s string) (bits uint32, end int, errno int) {
	b := cString(s)
	var e int32
	n, _, _ := procStrtof.Call(uintptr(unsafe.Pointer(&b[0])), uintptr(unsafe.Pointer(&bits)), uintptr(unsafe.Pointer(&e)))
	return bits, int(n), int(e)
}

// cWcstod converts the bytes of s as wide characters U+0000..U+00FF, end counts them
func cWcstod(s string) (bits uint64, end int, errno int) {
	b := cString(s)
	w := make([]uint16, len(b))
	for i, c := range b {
		w[i] = uint16(c)
	}
	var e int32
	n, _, _ := procWcstod.Call(uintptr(unsafe.Pointer(&w[0])), uintptr(unsafe.Pointer(&bits)), uintptr(unsafe.Pointer(&e)))
	return bits, int(n), int(e)
}

// cStrtold returns strtold(s) as an 80-bit long double, the number of bytes it converts and errno
func cStrtold(s string) (x ld80, end int, errno int) {
	b := cString(s)
	return callLong(procStrtold, unsafe.Pointer(&b[0]))
}

// cWcstold converts the bytes of s as wide characters U+0000..U+00FF, end counts them
func cWcstold(s string) (x ld80, end int, errno int) {
	b := cString(s)
	w := make([]uint16, len(b))
	for i, c := range b {
		w[i] = uint16(c)
	}
	return callLong(procWcstold, unsafe.Pointer(&w[0]))
}

func callLong(proc *syscall.LazyProc, s unsafe.Pointer) (x ld80, end int, errno int) {
	var b [16]byte
	var e int32
	n, _, _ := proc.Call(uintptr(s), uintptr(unsafe.Pointer(&b[0])), uintptr(unsafe.Pointer(&e)))
	return ld80{binary.LittleEndian.Uint64(b[:]), binary.LittleEndian.Uint16(b[8:])}, int(n), int(e)
}

// cIsWspace is iswspace of NtCRT
func cIsWspace(c byte) bool {
	r, _, _ := procWspace.Call(uintptr(c))
	return r != 0
}

// cFormat runs call with a buffer, growing it until the output fits; call returns the result of snprintf
func cFormat(call func(buf []byte) uintptr) (string, int) {
	buf := make([]byte, 4096)
	for {
		n := int(int32(call(buf)))
		if n < 0 {
			return "", n
		}
		if n < len(buf) {
			return string(buf[:n]), n
		}
		buf = make([]byte, n+1)
	}
}

// cFormatDouble is __mingw_snprintf(format, f) of NtCRT and its return value
func cFormatDouble(format string, f float64) (string, int) {
	fb := cString(format)
	return cFormat(func(buf []byte) uintptr {
		r, _, _ := procFmtD.Call(uintptr(unsafe.Pointer(&buf[0])), uintptr(len(buf)), uintptr(unsafe.Pointer(&fb[0])), uintptr(math.Float64bits(f)))
		return r
	})
}

// cFormatLongDouble formats the 80-bit long double of mant (explicit integer bit) and signExp (sign, 15-bit exponent)
func cFormatLongDouble(format string, mant uint64, signExp uint16) (string, int) {
	fb := cString(format)
	b := make([]byte, 10)
	binary.LittleEndian.PutUint64(b, mant)
	binary.LittleEndian.PutUint16(b[8:], signExp)
	return cFormat(func(buf []byte) uintptr {
		r, _, _ := procFmtLD.Call(uintptr(unsafe.Pointer(&buf[0])), uintptr(len(buf)), uintptr(unsafe.Pointer(&fb[0])), uintptr(unsafe.Pointer(&b[0])))
		return r
	})
}
