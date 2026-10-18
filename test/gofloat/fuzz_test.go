// Fuzz strtod, strtof and wcstod of NtCRT against the reference (ref_test.go):
// go test -fuzz FuzzStrtod

package gofloat

import (
	"math"
	"testing"
)

var fuzzSeeds = []string{
	"0", "-0", "1", "1.5", ".5", "5.", "1e10", "1e-10", "1E+308", "1e309", "4.9e-324", "2.4e-324", "2.5e-324",
	"0x1p-1074", "0x1.fffffffffffffp1023", "0x1.8", "0X.8P1", "0x", "0x.", "0xg", "1e", "1e+", "1e+x",
	"inf", "-INFINITY", "infinit", "nan", "nan()", "nan(abc_123)", "nan(-)", "  \t\n+1.25e-3xyz",
	"179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497791",
	"2.4703282292062327208828439643411068618252990130716238221279284125033775363510437593264991818081799618989828234772285886546332835517796989819938739800539093906315035659515570226392290858392449105184435931802849936536152500319370457678249219365623669863658480757001585769269903706311928279558551332927834338409351978015531246597263579574622766465272827220056374006485499977096599470454020828166226237857393450736339007967761930577506740176324673600968951340535537458516661134223766678604162159680461914467291840300530057530849048765391711386591646239524912623653881879636239373280423891018672348497668235089863388587925628302755995657524455507255189313690836254779186948667994968324049705821028513185451396213837722826145437693412532098591327667236328125e-324",
	"8.68203285000000048828125e12", "6.5171236328125e4", "1.00000000000000011102230246251565404236316680908203125",
}

func FuzzStrtod(f *testing.F) {
	for _, s := range fuzzSeeds {
		f.Add(s)
	}
	f.Fuzz(func(t *testing.T, s string) {
		checkStrtod(t, s)
	})
}

func checkStrtod(t *testing.T, s string) {
	t.Helper()
	wantBits, wantEnd, wantErr, nan := refStrtod(s, 64)
	gotBits, gotEnd, gotErr := cStrtod(s)
	if gotEnd != wantEnd || gotErr != wantErr || !sameFloat64(gotBits, wantBits, nan) {
		t.Errorf("strtod(%q) = %x end %d errno %d, want %x end %d errno %d", s, gotBits, gotEnd, gotErr, wantBits, wantEnd, wantErr)
	}
	wWant, wWantEnd, wWantErr, wNan := refWcstod(s)
	wBits, wEnd, wErr := cWcstod(s)
	if wEnd != wWantEnd || wErr != wWantErr || !sameFloat64(wBits, wWant, wNan) {
		t.Errorf("wcstod(%q) = %x end %d errno %d, want %x end %d errno %d", s, wBits, wEnd, wErr, wWant, wWantEnd, wWantErr)
	}
	want32, wantEnd32, wantErr32, _ := refStrtod(s, 32)
	got32, gotEnd32, gotErr32 := cStrtof(s)
	if gotEnd32 != wantEnd32 || gotErr32 != wantErr32 || !sameFloat32(got32, uint32(want32), nan) {
		t.Errorf("strtof(%q) = %x end %d errno %d, want %x end %d errno %d", s, got32, gotEnd32, gotErr32, want32, wantEnd32, wantErr32)
	}
}

func sameFloat64(got, want uint64, nan bool) bool {
	if nan {
		return math.IsNaN(math.Float64frombits(got))
	}
	return got == want
}

func sameFloat32(got, want uint32, nan bool) bool {
	if nan {
		return math.IsNaN(float64(math.Float32frombits(got)))
	}
	return got == want
}

func TestSeeds(t *testing.T) {
	for _, s := range fuzzSeeds {
		checkStrtod(t, s)
	}
}
