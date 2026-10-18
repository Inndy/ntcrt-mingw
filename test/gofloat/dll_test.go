// The float conversions of NtCRT, from float.dll (dll/float.c) built by make test-go. NTCRT_FLOAT_DLL names the
// DLL, ../../build/gofloat/float.dll by default.

package gofloat

import (
	"os"
	"strings"
	"syscall"
	"unsafe"
)

var (
	dll        = syscall.NewLazyDLL(dllPath())
	procStrtod = dll.NewProc("strtod_bits")
	procStrtof = dll.NewProc("strtof_bits")
	procWcstod = dll.NewProc("wcstod_bits")
	procWspace = dll.NewProc("is_wspace")
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

// cIsWspace is iswspace of NtCRT
func cIsWspace(c byte) bool {
	r, _, _ := procWspace.Call(uintptr(c))
	return r != 0
}
