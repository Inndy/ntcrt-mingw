// The float parsing tests of Go 1.27 for strtod, wcstod and strtof of NtCRT: the tables of atof_test.go, the
// testbase samples in testdata/atof1k.txt and testdata/ftoa1k.txt, and the parsing direction of testdata/testfp.txt.
//
// The tables are copied from src/internal/strconv of Go 1.27, Copyright 2009 The Go Authors, under the BSD 3-clause
// license whose full notice is in src/Float.cpp of this repository. The data files are read from GOROOT.

package gofloat

import (
	"bufio"
	"errors"
	"fmt"
	"math"
	"math/rand/v2"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"testing"
)

type atofTest struct {
	in  string
	out string
	err error
}

var (
	errSyntax = errors.New("invalid syntax")
	errRange  = errors.New("value out of range")
)

// Go reports errSyntax where C converts a prefix, and reads underscores that C stops at: TestAtofTable checks those
// against refStrtod. errRange is overflow only, the errno of NtCRT is wantErrno.

var atoftests = []atofTest{
	{"", "0", errSyntax},
	{"1", "1", nil},
	{"+1", "1", nil},
	{"1x", "0", errSyntax},
	{"1.1.", "0", errSyntax},
	{"1e23", "1e+23", nil},
	{"1E23", "1e+23", nil},
	{"100000000000000000000000", "1e+23", nil},
	{"1e-100", "1e-100", nil},
	{"123456700", "1.234567e+08", nil},
	{"99999999999999974834176", "9.999999999999997e+22", nil},
	{"100000000000000000000001", "1.0000000000000001e+23", nil},
	{"100000000000000008388608", "1.0000000000000001e+23", nil},
	{"100000000000000016777215", "1.0000000000000001e+23", nil},
	{"100000000000000016777216", "1.0000000000000003e+23", nil},
	{"-1", "-1", nil},
	{"-0.1", "-0.1", nil},
	{"-0", "-0", nil},
	{"1e-20", "1e-20", nil},
	{"625e-3", "0.625", nil},

	// Hexadecimal floating-point.
	{"0x1p0", "1", nil},
	{"0x1p1", "2", nil},
	{"0x1p-1", "0.5", nil},
	{"0x1ep-1", "15", nil},
	{"-0x1ep-1", "-15", nil},
	{"-0x1_ep-1", "-15", nil},
	{"0x1p-200", "6.223015277861142e-61", nil},
	{"0x1p200", "1.6069380442589903e+60", nil},
	{"0x1fFe2.p0", "131042", nil},
	{"0x1fFe2.P0", "131042", nil},
	{"-0x2p3", "-16", nil},
	{"0x0.fp4", "15", nil},
	{"0x0.fp0", "0.9375", nil},
	{"0x1e2", "0", errSyntax},
	{"1p2", "0", errSyntax},

	// zeros
	{"0", "0", nil},
	{"0e0", "0", nil},
	{"-0e0", "-0", nil},
	{"+0e0", "0", nil},
	{"0e-0", "0", nil},
	{"-0e-0", "-0", nil},
	{"+0e-0", "0", nil},
	{"0e+0", "0", nil},
	{"-0e+0", "-0", nil},
	{"+0e+0", "0", nil},
	{"0e+01234567890123456789", "0", nil},
	{"0.00e-01234567890123456789", "0", nil},
	{"-0e+01234567890123456789", "-0", nil},
	{"-0.00e-01234567890123456789", "-0", nil},
	{"0x0p+01234567890123456789", "0", nil},
	{"0x0.00p-01234567890123456789", "0", nil},
	{"-0x0p+01234567890123456789", "-0", nil},
	{"-0x0.00p-01234567890123456789", "-0", nil},

	{"0e291", "0", nil}, // issue 15364
	{"0e292", "0", nil}, // issue 15364
	{"0e347", "0", nil}, // issue 15364
	{"0e348", "0", nil}, // issue 15364
	{"-0e291", "-0", nil},
	{"-0e292", "-0", nil},
	{"-0e347", "-0", nil},
	{"-0e348", "-0", nil},
	{"0x0p126", "0", nil},
	{"0x0p127", "0", nil},
	{"0x0p128", "0", nil},
	{"0x0p129", "0", nil},
	{"0x0p130", "0", nil},
	{"0x0p1022", "0", nil},
	{"0x0p1023", "0", nil},
	{"0x0p1024", "0", nil},
	{"0x0p1025", "0", nil},
	{"0x0p1026", "0", nil},
	{"-0x0p126", "-0", nil},
	{"-0x0p127", "-0", nil},
	{"-0x0p128", "-0", nil},
	{"-0x0p129", "-0", nil},
	{"-0x0p130", "-0", nil},
	{"-0x0p1022", "-0", nil},
	{"-0x0p1023", "-0", nil},
	{"-0x0p1024", "-0", nil},
	{"-0x0p1025", "-0", nil},
	{"-0x0p1026", "-0", nil},

	// NaNs
	{"nan", "NaN", nil},
	{"NaN", "NaN", nil},
	{"NAN", "NaN", nil},

	// Infs
	{"inf", "+Inf", nil},
	{"-Inf", "-Inf", nil},
	{"+INF", "+Inf", nil},
	{"-Infinity", "-Inf", nil},
	{"+INFINITY", "+Inf", nil},
	{"Infinity", "+Inf", nil},

	// largest float64
	{"1.7976931348623157e308", "1.7976931348623157e+308", nil},
	{"-1.7976931348623157e308", "-1.7976931348623157e+308", nil},
	{"0x1.fffffffffffffp1023", "1.7976931348623157e+308", nil},
	{"-0x1.fffffffffffffp1023", "-1.7976931348623157e+308", nil},
	{"0x1fffffffffffffp+971", "1.7976931348623157e+308", nil},
	{"-0x1fffffffffffffp+971", "-1.7976931348623157e+308", nil},
	{"0x.1fffffffffffffp1027", "1.7976931348623157e+308", nil},
	{"-0x.1fffffffffffffp1027", "-1.7976931348623157e+308", nil},

	// next float64 - too large
	{"1.7976931348623159e308", "+Inf", errRange},
	{"-1.7976931348623159e308", "-Inf", errRange},
	{"0x1p1024", "+Inf", errRange},
	{"-0x1p1024", "-Inf", errRange},
	{"0x2p1023", "+Inf", errRange},
	{"-0x2p1023", "-Inf", errRange},
	{"0x.1p1028", "+Inf", errRange},
	{"-0x.1p1028", "-Inf", errRange},
	{"0x.2p1027", "+Inf", errRange},
	{"-0x.2p1027", "-Inf", errRange},

	// the border is ...158079
	// borderline - okay
	{"1.7976931348623158e308", "1.7976931348623157e+308", nil},
	{"-1.7976931348623158e308", "-1.7976931348623157e+308", nil},
	{"0x1.fffffffffffff7fffp1023", "1.7976931348623157e+308", nil},
	{"-0x1.fffffffffffff7fffp1023", "-1.7976931348623157e+308", nil},
	// borderline - too large
	{"1.797693134862315808e308", "+Inf", errRange},
	{"-1.797693134862315808e308", "-Inf", errRange},
	{"0x1.fffffffffffff8p1023", "+Inf", errRange},
	{"-0x1.fffffffffffff8p1023", "-Inf", errRange},
	{"0x1fffffffffffff.8p+971", "+Inf", errRange},
	{"-0x1fffffffffffff8p+967", "-Inf", errRange},
	{"0x.1fffffffffffff8p1027", "+Inf", errRange},
	{"-0x.1fffffffffffff9p1027", "-Inf", errRange},

	// a little too large
	{"1e308", "1e+308", nil},
	{"2e308", "+Inf", errRange},
	{"1e309", "+Inf", errRange},
	{"0x1p1025", "+Inf", errRange},

	// way too large
	{"1e310", "+Inf", errRange},
	{"-1e310", "-Inf", errRange},
	{"1e400", "+Inf", errRange},
	{"-1e400", "-Inf", errRange},
	{"1e400000", "+Inf", errRange},
	{"-1e400000", "-Inf", errRange},
	{"0x1p1030", "+Inf", errRange},
	{"0x1p2000", "+Inf", errRange},
	{"0x1p2000000000", "+Inf", errRange},
	{"-0x1p1030", "-Inf", errRange},
	{"-0x1p2000", "-Inf", errRange},
	{"-0x1p2000000000", "-Inf", errRange},

	// denormalized
	{"1e-305", "1e-305", nil},
	{"1e-306", "1e-306", nil},
	{"1e-307", "1e-307", nil},
	{"1e-308", "1e-308", nil},
	{"1e-309", "1e-309", nil},
	{"1e-310", "1e-310", nil},
	{"1e-322", "1e-322", nil},
	// smallest denormal
	{"5e-324", "5e-324", nil},
	{"4e-324", "5e-324", nil},
	{"3e-324", "5e-324", nil},
	// too small
	{"2e-324", "0", nil},
	// way too small
	{"1e-350", "0", nil},
	{"1e-400000", "0", nil},
	{"1e-345", "0", nil}, // picked off in atof64
	{"1e-343", "0", nil}, // large c.s in parseFloat64
	{"9.999999999999999999e-343", "0", nil},

	// Near denormals and denormals.
	{"0x2.00000000000000p-1010", "1.8227805048890994e-304", nil}, // 0x00e0000000000000
	{"0x1.fffffffffffff0p-1010", "1.8227805048890992e-304", nil}, // 0x00dfffffffffffff
	{"0x1.fffffffffffff7p-1010", "1.8227805048890992e-304", nil}, // rounded down
	{"0x1.fffffffffffff8p-1010", "1.8227805048890994e-304", nil}, // rounded up
	{"0x1.fffffffffffff9p-1010", "1.8227805048890994e-304", nil}, // rounded up

	{"0x2.00000000000000p-1022", "4.450147717014403e-308", nil},  // 0x0020000000000000
	{"0x1.fffffffffffff0p-1022", "4.4501477170144023e-308", nil}, // 0x001fffffffffffff
	{"0x1.fffffffffffff7p-1022", "4.4501477170144023e-308", nil}, // rounded down
	{"0x1.fffffffffffff8p-1022", "4.450147717014403e-308", nil},  // rounded up
	{"0x1.fffffffffffff9p-1022", "4.450147717014403e-308", nil},  // rounded up

	{"0x1.00000000000000p-1022", "2.2250738585072014e-308", nil}, // 0x0010000000000000
	{"0x0.fffffffffffff0p-1022", "2.225073858507201e-308", nil},  // 0x000fffffffffffff
	{"0x0.ffffffffffffe0p-1022", "2.2250738585072004e-308", nil}, // 0x000ffffffffffffe
	{"0x0.ffffffffffffe7p-1022", "2.2250738585072004e-308", nil}, // rounded down
	{"0x1.ffffffffffffe8p-1023", "2.225073858507201e-308", nil},  // rounded up
	{"0x1.ffffffffffffe9p-1023", "2.225073858507201e-308", nil},  // rounded up

	{"0x0.00000003fffff0p-1022", "2.072261e-317", nil},  // 0x00000000003fffff
	{"0x0.00000003456780p-1022", "1.694649e-317", nil},  // 0x0000000000345678
	{"0x0.00000003456787p-1022", "1.694649e-317", nil},  // rounded down
	{"0x0.00000003456788p-1022", "1.694649e-317", nil},  // rounded down (half to even)
	{"0x0.00000003456790p-1022", "1.6946496e-317", nil}, // 0x0000000000345679
	{"0x0.00000003456789p-1022", "1.6946496e-317", nil}, // rounded up

	{"0x0.0000000345678800000000000000000000000001p-1022", "1.6946496e-317", nil}, // rounded up

	{"0x0.000000000000f0p-1022", "7.4e-323", nil}, // 0x000000000000000f
	{"0x0.00000000000060p-1022", "3e-323", nil},   // 0x0000000000000006
	{"0x0.00000000000058p-1022", "3e-323", nil},   // rounded up
	{"0x0.00000000000057p-1022", "2.5e-323", nil}, // rounded down
	{"0x0.00000000000050p-1022", "2.5e-323", nil}, // 0x0000000000000005

	{"0x0.00000000000010p-1022", "5e-324", nil},  // 0x0000000000000001
	{"0x0.000000000000081p-1022", "5e-324", nil}, // rounded up
	{"0x0.00000000000008p-1022", "0", nil},       // rounded down
	{"0x0.00000000000007fp-1022", "0", nil},      // rounded down

	// try to overflow exponent
	{"1e-4294967296", "0", nil},
	{"1e+4294967296", "+Inf", errRange},
	{"1e-18446744073709551616", "0", nil},
	{"1e+18446744073709551616", "+Inf", errRange},
	{"0x1p-4294967296", "0", nil},
	{"0x1p+4294967296", "+Inf", errRange},
	{"0x1p-18446744073709551616", "0", nil},
	{"0x1p+18446744073709551616", "+Inf", errRange},

	// Parse errors
	{"1e", "0", errSyntax},
	{"1e-", "0", errSyntax},
	{".e-1", "0", errSyntax},
	{"1\x00.2", "0", errSyntax},
	{"0x", "0", errSyntax},
	{"0x.", "0", errSyntax},
	{"0x1", "0", errSyntax},
	{"0x.1", "0", errSyntax},
	{"0x1p", "0", errSyntax},
	{"0x.1p", "0", errSyntax},
	{"0x1p+", "0", errSyntax},
	{"0x.1p+", "0", errSyntax},
	{"0x1p-", "0", errSyntax},
	{"0x.1p-", "0", errSyntax},
	{"0x1p+2", "4", nil},
	{"0x.1p+2", "0.25", nil},
	{"0x1p-2", "0.25", nil},
	{"0x.1p-2", "0.015625", nil},

	// https://www.exploringbinary.com/java-hangs-when-converting-2-2250738585072012e-308/
	{"2.2250738585072012e-308", "2.2250738585072014e-308", nil},
	// https://www.exploringbinary.com/php-hangs-on-numeric-value-2-2250738585072011e-308/
	{"2.2250738585072011e-308", "2.225073858507201e-308", nil},

	// A very large number (initially wrongly parsed by the fast algorithm).
	{"4.630813248087435e+307", "4.630813248087435e+307", nil},

	// A different kind of very large number.
	{"22.222222222222222", "22.22222222222222", nil},
	{"2." + strings.Repeat("2", 4000) + "e+1", "22.22222222222222", nil},
	{"0x1.1111111111111p222", "7.18931911124017e+66", nil},
	{"0x2.2222222222222p221", "7.18931911124017e+66", nil},
	{"0x2." + strings.Repeat("2", 4000) + "p221", "7.18931911124017e+66", nil},

	// Exactly halfway between 1 and math.Nextafter(1, 2).
	// Round to even (down).
	{"1.00000000000000011102230246251565404236316680908203125", "1", nil},
	{"0x1.00000000000008p0", "1", nil},
	// Slightly lower; still round down.
	{"1.00000000000000011102230246251565404236316680908203124", "1", nil},
	{"0x1.00000000000007Fp0", "1", nil},
	// Slightly higher; round up.
	{"1.00000000000000011102230246251565404236316680908203126", "1.0000000000000002", nil},
	{"0x1.000000000000081p0", "1.0000000000000002", nil},
	{"0x1.00000000000009p0", "1.0000000000000002", nil},
	// Slightly higher, but you have to read all the way to the end.
	{"1.00000000000000011102230246251565404236316680908203125" + strings.Repeat("0", 10000) + "1", "1.0000000000000002", nil},
	{"0x1.00000000000008" + strings.Repeat("0", 10000) + "1p0", "1.0000000000000002", nil},

	// Halfway between x := math.Nextafter(1, 2) and math.Nextafter(x, 2)
	// Round to even (up).
	{"1.00000000000000033306690738754696212708950042724609375", "1.0000000000000004", nil},
	{"0x1.00000000000018p0", "1.0000000000000004", nil},

	// Halfway between 1090544144181609278303144771584 and 1090544144181609419040633126912
	// (15497564393479157p+46, should round to even 15497564393479156p+46, issue 36657)
	{"1090544144181609348671888949248", "1.0905441441816093e+30", nil},
	// slightly above, rounds up
	{"1090544144181609348835077142190", "1.0905441441816094e+30", nil},

	// Underscores.
	{"1_23.50_0_0e+1_2", "1.235e+14", nil},
	{"-_123.5e+12", "0", errSyntax},
	{"+_123.5e+12", "0", errSyntax},
	{"_123.5e+12", "0", errSyntax},
	{"1__23.5e+12", "0", errSyntax},
	{"123_.5e+12", "0", errSyntax},
	{"123._5e+12", "0", errSyntax},
	{"123.5_e+12", "0", errSyntax},
	{"123.5__0e+12", "0", errSyntax},
	{"123.5e_+12", "0", errSyntax},
	{"123.5e+_12", "0", errSyntax},
	{"123.5e_-12", "0", errSyntax},
	{"123.5e-_12", "0", errSyntax},
	{"123.5e+1__2", "0", errSyntax},
	{"123.5e+12_", "0", errSyntax},

	{"0x_1_2.3_4_5p+1_2", "74565", nil},
	{"-_0x12.345p+12", "0", errSyntax},
	{"+_0x12.345p+12", "0", errSyntax},
	{"_0x12.345p+12", "0", errSyntax},
	{"0x__12.345p+12", "0", errSyntax},
	{"0x1__2.345p+12", "0", errSyntax},
	{"0x12_.345p+12", "0", errSyntax},
	{"0x12._345p+12", "0", errSyntax},
	{"0x12.3__45p+12", "0", errSyntax},
	{"0x12.345_p+12", "0", errSyntax},
	{"0x12.345p_+12", "0", errSyntax},
	{"0x12.345p+_12", "0", errSyntax},
	{"0x12.345p_-12", "0", errSyntax},
	{"0x12.345p-_12", "0", errSyntax},
	{"0x12.345p+1__2", "0", errSyntax},
	{"0x12.345p+12_", "0", errSyntax},

	{"1e100x", "0", errSyntax},
	{"1e1000x", "0", errSyntax},
}

var atof32tests = []atofTest{
	// Hex
	{"0x1p-100", "7.888609e-31", nil},
	{"0x1p100", "1.2676506e+30", nil},

	// Exactly halfway between 1 and the next float32.
	// Round to even (down).
	{"1.000000059604644775390625", "1", nil},
	{"0x1.000001p0", "1", nil},
	// Slightly lower.
	{"1.000000059604644775390624", "1", nil},
	{"0x1.0000008p0", "1", nil},
	{"0x1.000000fp0", "1", nil},
	// Slightly higher.
	{"1.000000059604644775390626", "1.0000001", nil},
	{"0x1.000002p0", "1.0000001", nil},
	{"0x1.0000018p0", "1.0000001", nil},
	{"0x1.0000011p0", "1.0000001", nil},
	// Slightly higher, but you have to read all the way to the end.
	{"1.000000059604644775390625" + strings.Repeat("0", 10000) + "1", "1.0000001", nil},
	{"0x1.000001" + strings.Repeat("0", 10000) + "1p0", "1.0000001", nil},

	// largest float32: (1<<128) * (1 - 2^-24)
	{"340282346638528859811704183484516925440", "3.4028235e+38", nil},
	{"-340282346638528859811704183484516925440", "-3.4028235e+38", nil},
	{"0x.ffffffp128", "3.4028235e+38", nil},
	{"-340282346638528859811704183484516925440", "-3.4028235e+38", nil},
	{"-0x.ffffffp128", "-3.4028235e+38", nil},
	// next float32 - too large
	{"3.4028236e38", "+Inf", errRange},
	{"-3.4028236e38", "-Inf", errRange},
	{"0x1.0p128", "+Inf", errRange},
	{"-0x1.0p128", "-Inf", errRange},
	// the border is 3.40282356779...e+38
	// borderline - okay
	{"3.402823567e38", "3.4028235e+38", nil},
	{"-3.402823567e38", "-3.4028235e+38", nil},
	{"0x.ffffff7fp128", "3.4028235e+38", nil},
	{"-0x.ffffff7fp128", "-3.4028235e+38", nil},
	// borderline - too large
	{"3.4028235678e38", "+Inf", errRange},
	{"-3.4028235678e38", "-Inf", errRange},
	{"0x.ffffff8p128", "+Inf", errRange},
	{"-0x.ffffff8p128", "-Inf", errRange},

	// Denormals: less than 2^-126
	{"1e-38", "1e-38", nil},
	{"1e-39", "1e-39", nil},
	{"1e-40", "1e-40", nil},
	{"1e-41", "1e-41", nil},
	{"1e-42", "1e-42", nil},
	{"1e-43", "1e-43", nil},
	{"1e-44", "1e-44", nil},
	{"6e-45", "6e-45", nil}, // 4p-149 = 5.6e-45
	{"5e-45", "6e-45", nil},

	// Smallest denormal
	{"1e-45", "1e-45", nil}, // 1p-149 = 1.4e-45
	{"2e-45", "1e-45", nil},
	{"3e-45", "3e-45", nil},

	// Near denormals and denormals.
	{"0x0.89aBcDp-125", "1.2643093e-38", nil},  // 0x0089abcd
	{"0x0.8000000p-125", "1.1754944e-38", nil}, // 0x00800000
	{"0x0.1234560p-125", "1.671814e-39", nil},  // 0x00123456
	{"0x0.1234567p-125", "1.671814e-39", nil},  // rounded down
	{"0x0.1234568p-125", "1.671814e-39", nil},  // rounded down
	{"0x0.1234569p-125", "1.671815e-39", nil},  // rounded up
	{"0x0.1234570p-125", "1.671815e-39", nil},  // 0x00123457
	{"0x0.0000010p-125", "1e-45", nil},         // 0x00000001
	{"0x0.00000081p-125", "1e-45", nil},        // rounded up
	{"0x0.0000008p-125", "0", nil},             // rounded down
	{"0x0.0000007p-125", "0", nil},             // rounded down

	{"1e-70", "0", nil}, // picked off in atof32
	{"1e-65", "0", nil}, // picked off in atof32
	{"1e-64", "0", nil}, // large c.s in parseFloat32
	{"9.999999999999999999e-64", "0", nil},

	// 2^92 = 8388608p+69 = 4951760157141521099596496896 (4.9517602e27)
	// is an exact power of two that needs 8 decimal digits to be correctly
	// parsed back.
	// The float32 before is 16777215p+68 = 4.95175986e+27
	// The halfway is 4.951760009. A bad algorithm that thinks the previous
	// float32 is 8388607p+69 will shorten incorrectly to 4.95176e+27.
	{"4951760157141521099596496896", "4.9517602e+27", nil},
}

var roundTripCases = []struct {
	f float64
	s string
}{
	// Issue 2917.
	// This test will break the optimized conversion if the
	// FPU is using 80-bit registers instead of 64-bit registers,
	// usually because the operating system initialized the
	// thread with 80-bit precision and the Go runtime didn't
	// fix the FP control word.
	{8865794286000691 << 39, "4.87402195346389e+27"},
	{8865794286000692 << 39, "4.8740219534638903e+27"},
}

// suffixes that do not extend a number, from TestParseFloatPrefix; "i" and "init" leave "inf" alone
var prefixSuffixes = []string{" ", "q", "+", "-", "<", "=", ">", "(", ")", "i", "init"}

// nonzeroDigits reports whether the mantissa of the number s has a digit that is not 0
func nonzeroDigits(s string) bool {
	s = strings.TrimLeft(s, "+-")
	stop := "eE"
	if len(s) > 1 && s[0] == '0' && s[1]|0x20 == 'x' {
		s, stop = s[2:], "pP"
	}
	if i := strings.IndexAny(s, stop); i >= 0 {
		s = s[:i]
	}
	return strings.ContainsAny(s, "123456789abcdefABCDEF")
}

// wantErrno is the errno of NtCRT for the number s converted to f: ERANGE on overflow, and on underflow to zero from a
// nonzero input as UCRT, not for a denormal
func wantErrno(s string, f float64) int {
	if math.IsInf(f, 0) && !hasPrefixFold(strings.TrimLeft(s, "+-"), "inf") {
		return erange
	}
	if f == 0 && nonzeroDigits(s) {
		return erange
	}
	return 0
}

// short quotes s, cut to keep the message readable
func short(s string) string {
	if len(s) > 80 {
		return fmt.Sprintf("%q...(%d bytes)", s[:80], len(s))
	}
	return fmt.Sprintf("%q", s)
}

// checkFloat64 checks that strtod and wcstod convert end bytes of s to want
func checkFloat64(t *testing.T, s string, want float64, end int) {
	t.Helper()
	nan := math.IsNaN(want)
	wantBits := math.Float64bits(want)
	wantErr := wantErrno(s[:end], want)
	for _, c := range []struct {
		name string
		fn   func(string) (uint64, int, int)
	}{{"strtod", cStrtod}, {"wcstod", cWcstod}} {
		bits, n, errno := c.fn(s)
		if n != end || errno != wantErr || !sameFloat64(bits, wantBits, nan) {
			t.Errorf("%s(%s) = %x end %d errno %d, want %x end %d errno %d", c.name, short(s), bits, n, errno, wantBits, end, wantErr)
		}
	}
}

// checkFloat32 checks that strtof converts end bytes of s to want
func checkFloat32(t *testing.T, s string, want float32, end int) {
	t.Helper()
	nan := math.IsNaN(float64(want))
	wantBits := math.Float32bits(want)
	wantErr := wantErrno(s[:end], float64(want))
	bits, n, errno := cStrtof(s)
	if n != end || errno != wantErr || !sameFloat32(bits, wantBits, nan) {
		t.Errorf("strtof(%s) = %x end %d errno %d, want %x end %d errno %d", short(s), bits, n, errno, wantBits, end, wantErr)
	}
}

// goOnly reports whether Go and C read tt.in differently, so the expected value of the table does not apply
func goOnly(tt atofTest) bool {
	return tt.err == errSyntax || strings.Contains(tt.in, "_")
}

func TestAtofTable(t *testing.T) {
	n := 0
	for _, tt := range atoftests {
		n++
		if goOnly(tt) {
			checkStrtod(t, tt.in)
			continue
		}
		want, _ := strconv.ParseFloat(tt.out, 64)
		checkFloat64(t, tt.in, want, len(tt.in))
		for _, suffix := range prefixSuffixes {
			checkFloat64(t, tt.in+suffix, want, len(tt.in))
		}
		// like testAtof, strtof too when the result is a float32
		if float64(float32(want)) == want || math.IsNaN(want) {
			checkFloat32(t, tt.in, float32(want), len(tt.in))
		}
	}
	for _, tt := range atof32tests {
		n++
		want, _ := strconv.ParseFloat(tt.out, 32)
		checkFloat32(t, tt.in, float32(want), len(tt.in))
		for _, suffix := range prefixSuffixes {
			checkFloat32(t, tt.in+suffix, float32(want), len(tt.in))
		}
	}
	for _, tt := range roundTripCases {
		n++
		checkFloat64(t, tt.s, tt.f, len(tt.s))
	}
	t.Logf("tested %d cases", n)
}

// TestAtofRandom parses random float64 bits formatted by Go
func TestAtofRandom(t *testing.T) {
	count := 10000
	if testing.Short() {
		count = 100
	}
	r := rand.New(rand.NewPCG(1, 2))
	for range count {
		f := math.Float64frombits(r.Uint64())
		s := strconv.FormatFloat(f, 'g', -1, 64)
		checkFloat64(t, s, f, len(s))
	}
	t.Logf("tested %d random numbers", count)
}

// TestRoundTrip32 parses a fraction of all finite float32 values formatted by Go
func TestRoundTrip32(t *testing.T) {
	step := uint32(997)
	if testing.Short() {
		step = 99991
	}
	count := 0
	for i := uint32(0); i < 0xff<<23; i += step {
		f := math.Float32frombits(i)
		if i&1 == 1 {
			f = -f
		}
		s := strconv.FormatFloat(float64(f), 'g', -1, 32)
		checkFloat32(t, s, f, len(s))
		count++
	}
	t.Logf("tested %d float32's", count)
}

// goTestdata reads testdata/name of internal/strconv in GOROOT, skipping the test if it is absent
func goTestdata(t *testing.T, name string) string {
	t.Helper()
	root := runtime.GOROOT()
	if root == "" {
		if out, err := exec.Command("go", "env", "GOROOT").Output(); err == nil {
			root = strings.TrimSpace(string(out))
		}
	}
	data, err := os.ReadFile(filepath.Join(root, "src", "internal", "strconv", "testdata", name))
	if err != nil {
		t.Skip(err)
	}
	return string(data)
}

// TestTestbase parses the samples of testbase (testdata/README), a number per line whose value is ParseFloat of Go
func TestTestbase(t *testing.T) {
	for _, name := range []string{"atof1k.txt", "ftoa1k.txt"} {
		count := 0
		sc := bufio.NewScanner(strings.NewReader(goTestdata(t, name)))
		for lineno := 1; sc.Scan(); lineno++ {
			s := strings.TrimSpace(sc.Text())
			if s == "" || strings.HasPrefix(s, "#") {
				continue
			}
			want, err := strconv.ParseFloat(s, 64)
			if err != nil {
				t.Errorf("%s:%d: ParseFloat(%q): %v", name, lineno, s, err)
				continue
			}
			checkFloat64(t, s, want, len(s))
			want32, _ := strconv.ParseFloat(s, 32)
			checkFloat32(t, s, float32(want32), len(s))
			count++
		}
		t.Logf("%s: tested %d numbers", name, count)
	}
}

// binaryExp converts N p E of testfp.txt, N * 2**E, to a hex float of C and its value
func binaryExp(s string) (hex string, f float64, ok bool) {
	mant, exp, ok := strings.Cut(s, "p")
	if !ok {
		return "", 0, false
	}
	n, err1 := strconv.ParseUint(mant, 10, 64)
	e, err2 := strconv.Atoi(exp)
	if err1 != nil || err2 != nil {
		return "", 0, false
	}
	return fmt.Sprintf("0x%xp%d", n, e), math.Ldexp(float64(n), e), true
}

// TestFp parses both sides of testfp.txt: for %b the decimal input to the bits of its N p E output, for %.Ne the
// N p E input as a hex float to its bits and the decimal output to the value of ParseFloat. Printing is not tested.
func TestFp(t *testing.T) {
	count := 0
	sc := bufio.NewScanner(strings.NewReader(goTestdata(t, "testfp.txt")))
	for lineno := 1; sc.Scan(); lineno++ {
		line, _, _ := strings.Cut(sc.Text(), "#")
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		a := strings.Split(line, " ")
		if len(a) != 4 || (a[0] != "float64" && a[0] != "float32") {
			t.Errorf("testfp.txt:%d: bad line %q", lineno, line)
			continue
		}
		bitSize := 64
		if a[0] == "float32" {
			bitSize = 32
		}
		check := func(s string, want float64) {
			t.Helper()
			if bitSize == 64 {
				checkFloat64(t, s, want, len(s))
			} else {
				checkFloat32(t, s, float32(want), len(s))
			}
		}
		if a[1] == "%b" {
			_, want, ok := binaryExp(a[3])
			if !ok {
				t.Errorf("testfp.txt:%d: bad output %q", lineno, a[3])
				continue
			}
			check(a[2], want)
		} else {
			hex, want, ok := binaryExp(a[2])
			if !ok {
				t.Errorf("testfp.txt:%d: bad input %q", lineno, a[2])
				continue
			}
			check(hex, want)
			dec, _ := strconv.ParseFloat(a[3], bitSize)
			check(a[3], dec)
		}
		count++
	}
	t.Logf("tested %d lines", count)
}
