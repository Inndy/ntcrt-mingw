# NtCRT for MinGW-w64 (x64, x86 in progress): link C/C++ programs that import ntdll.dll only
#
#   make            build build/libntcrt.a, the support libraries and build/ntcrt.specs
#   make test       build and run the tests
#   g++ -static -specs=build/ntcrt.specs ...
#
#   make LLVM=1     build with clang into build-llvm, programs link by lld (LLVM=<dir>/ for clang in <dir>)
#   clang++ --config=build-llvm/ntcrt.cfg -static ...
#
# The target is that of g++ (-dumpmachine): with an i686 toolchain first in PATH, make builds for x86 into build-i686.

CXX      ?= g++
ifeq ($(origin CC),default)
CC       := gcc
endif
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra
CXXFLAGS += -Iinclude -ffunction-sections -fno-exceptions -fno-rtti

TARGET   := $(shell $(CXX) -dumpmachine)
ifneq ($(filter i686-% i386-%,$(TARGET)),)
X86      := 1
# NtCRT itself has the 64-bit time_t of x64, the headers of msvcrt on x86 default to 32 bits for programs: the names
# of msvcrt without a size (time, _stati64, ...) are the 32-bit ones there (NTCRT_ARCH_ALIAS), src/Time32.cpp and
# src/PthreadTime32.cpp have the functions that take a 32-bit time
CXXFLAGS += -D_TIME_BITS=64
endif

# UNWIND=0: NtCRT without unwind tables, into build-...-nounwind. The .eh_frame of an object keeps all its functions
# under --gc-sections of GNU ld on x86; without the tables a C++ exception cannot go through a callback that NtCRT
# calls (qsort, bsearch, pthread_once of std::call_once). See README.md "x86".
ifeq ($(UNWIND),0)
NOUNWIND := -nounwind
CXXFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables
endif

# POW10_SMALL=1: the powers of 10 of the floating point conversions computed from every 27th (NTCRT_POW10_SMALL,
# src/FloatPow10Small.cpp), into build-...-pow10small: about 10 KB less in a program with printf or scanf, a slower
# conversion. See README.md "Size". The define is in LIB_DEFS, which a CXXFLAGS of the command line does not replace.
ifeq ($(POW10_SMALL),1)
POW10S   := -pow10small
LIB_DEFS += -DNTCRT_POW10_SMALL
endif

ifdef TCC
BUILD    ?= build-tcc$(POW10S)
else ifdef LLVM
# A section for each variable, which lld drops unused. GCC would put the zero data in .data$ sections, which take
# space in the file unlike .bss, and ld links whole objects anyway.
CXXFLAGS += -fdata-sections
BUILD    ?= build-llvm$(if $(X86),-i686)$(NOUNWIND)$(POW10S)
else
BUILD    ?= build$(if $(X86),-i686)$(NOUNWIND)$(POW10S)
endif
# tcc defines __STDC_NO_THREADS__, its library has no thread functions
SRCS     := $(filter-out $(if $(TCC),$(filter-out src/PthreadTime.cpp,$(wildcard src/Pthread*.cpp)) src/C11%.cpp),$(wildcard src/*.cpp))
OBJS     := $(SRCS:src/%.cpp=$(BUILD)/%.o)
# Replacements of libmingw32 and libgcc members, linked before them
START_SRCS := $(wildcard src/start/*.cpp)
START_OBJS := $(START_SRCS:src/start/%.cpp=$(BUILD)/start/%.o)
# libntcrt_glob: the wildcards of argv, linked by -lntcrt_glob (src/glob/Glob.cpp)
GLOB_OBJS := $(BUILD)/glob/Glob.o
HEADERS  := src/NtCRT.h $(wildcard src/*.inl) $(wildcard include/*.h) $(wildcard include/KNSoft/NDK/*) \
            $(wildcard polyfill/*.inl)
MSVCRT_A := $(shell $(CXX) -print-file-name=libmsvcrt.a)
# The root of the toolchain, by the shell: $(abspath) and $(dir) would split a path with spaces
GCC_ROOT := $(shell cygpath -m "$$(realpath -s "$$(dirname "$$($(CXX) -print-libgcc-file-name)")/../../../..")")

# The compilers of the runtime and of the programs on it
ifdef LLVM
LLVM_PREFIX := $(filter %/,$(LLVM))
# clang uses the headers and libraries of GCC (libstdc++, the msvcrt flavor of MinGW-w64). The __cpuidex of
# MinGW-w64 headers before 12 clashes with the one of <cpuid.h> of clang and is left out, and their <intrin.h>
# includes <mm3dnow.h>, which clang deprecates. Thread-local variables are emulated as GCC does, libstdc++ has
# std::call_once by them (__emutls_get_address on pthread keys).
LLVM_FLAGS := --target=$(TARGET) -stdlib=libstdc++ -D__INTRINSIC_DEFINED___cpuidex \
              -D_CLANG_DISABLE_CRT_DEPRECATION_WARNINGS -femulated-tls
ifeq ($(X86)$(NOUNWIND),1)
# The DWARF unwind tables of GCC on x86, which clang leaves out without exceptions: a C++ exception goes through the
# callbacks that NtCRT calls (qsort, pthread_once of std::call_once)
CXXFLAGS += -funwind-tables
endif
LIB_CXX  := $(LLVM_PREFIX)clang++ $(LLVM_FLAGS) "--sysroot=$(GCC_ROOT)"
LIB_AS   := $(LLVM_PREFIX)clang --target=$(TARGET)
NTCRT_CC := $(LLVM_PREFIX)clang --config=$(BUILD)/ntcrt-strict.cfg
NTCRT_CXX := $(LLVM_PREFIX)clang++ --config=$(BUILD)/ntcrt-strict.cfg
KERNEL32_CC := $(LLVM_PREFIX)clang --config=$(BUILD)/ntcrt.cfg
DRIVER   := $(BUILD)/ntcrt.cfg
else
LIB_CXX  := $(CXX)
LIB_AS   := $(CXX)
NTCRT_CC := $(CC) -specs=$(BUILD)/ntcrt.specs -ntcrt-strict
NTCRT_CXX := $(CXX) -specs=$(BUILD)/ntcrt.specs -ntcrt-strict
KERNEL32_CC := $(CC) -specs=$(BUILD)/ntcrt.specs
DRIVER   := $(BUILD)/ntcrt.specs
endif

LIBS := $(BUILD)/libntcrt.a $(BUILD)/libntcrt_start.a $(BUILD)/libntdll_crt.a $(BUILD)/libmsvcrt_static.a \
        $(BUILD)/libpthread.a $(BUILD)/libwinpthread.a $(BUILD)/libdl.a $(BUILD)/libntcrt_glob.a

# make TCC=1: build-tcc/libntcrt.a and build-tcc/ntdll.def for tcc in place of msvcrt.def, x64 (NTCRT_TCC in NtCRT.h).
# tcc links ELF objects, which clang makes for Windows (LLVM=<dir>/ for clang in <dir>). tcc links whole objects: a
# section for each function would only be more sections, and it reads no unwind table of ELF. -Os: the code of NtCRT
# is 30% smaller than by -O2, that of the programs of tcc is not optimized.
ifdef TCC
ifdef X86
$(error TCC=1 builds for x64 only)
endif
TCC_FLAGS := --target=x86_64-w64-windows-gnu-elf "--sysroot=$(GCC_ROOT)" -stdlib=libstdc++ \
             -D__INTRINSIC_DEFINED___cpuidex -D_CLANG_DISABLE_CRT_DEPRECATION_WARNINGS -DNTCRT_TCC \
             -fno-asynchronous-unwind-tables
LIB_CXX  := $(filter %/,$(LLVM))clang++ $(TCC_FLAGS)
LIB_AS   := $(filter %/,$(LLVM))clang --target=x86_64-w64-windows-gnu-elf
CXXFLAGS := $(patsubst -O2,-Os,$(filter-out -ffunction-sections,$(CXXFLAGS)))
LIBS     := $(BUILD)/lib/libntcrt.a $(BUILD)/lib/ntdll.def $(BUILD)/lib/libntcrt_glob.a
DRIVER   :=
# What tcc-built programs get beyond msvcrt.dll, as a GCC build of NtCRT: the C99 math of <math.h> (double and float,
# the long double of tcc is a double), strnlen, wcsnlen, snprintf, vsnprintf
TCC_SEEDS := __fpclassify __fpclassifyf __signbit __signbitf acos acosf asin asinf atan atan2 atan2f atanf cbrt cbrtf \
             ceil ceilf copysign copysignf cos cosf cosh coshf erf erfc erfcf erff exp exp2 exp2f expf expm1 expm1f \
             fabs fabsf fdim fdimf floor floorf fma fmaf fmax fmaxf fmin fminf fmod fmodf frexp hypot hypotf ilogb \
             ilogbf ldexp ldexpf lgamma lgammaf llrint llrintf llround llroundf log log10 log10f log1p log1pf log2 \
             log2f logb logbf logf lrint lrintf lround lroundf modf modff nan nanf nearbyint nearbyintf nextafter \
             nextafterf pow powf remainder remainderf remquo remquof rint rintf round roundf scalbln scalblnf scalbn \
             scalbnf sin sinf sinh sinhf sqrt sqrtf tan tanf tanh tanhf tgamma tgammaf trunc truncf \
             strnlen wcsnlen snprintf vsnprintf
endif

all: $(LIBS) $(DRIVER)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.cpp $(HEADERS) | $(BUILD)
	$(LIB_CXX) $(CXXFLAGS) $(LIB_DEFS) -c -o $@ $<

$(BUILD)/start/%.o: src/start/%.cpp $(HEADERS) | $(BUILD)
	mkdir -p $(@D)
	$(LIB_CXX) $(CXXFLAGS) $(LIB_DEFS) -c -o $@ $<

$(BUILD)/libntcrt_start.a: $(START_OBJS)
	rm -f $@
	ar rcs $@ $^

$(BUILD)/glob/%.o: src/glob/%.cpp $(HEADERS) | $(BUILD)
	mkdir -p $(@D)
	$(LIB_CXX) $(CXXFLAGS) $(LIB_DEFS) -c -o $@ $<

$(BUILD)/libntcrt_glob.a $(BUILD)/lib/libntcrt_glob.a: $(GLOB_OBJS)
	mkdir -p $(@D)
	rm -f $@
	ar rcs $@ $^

# The NTCRT_IMPORT / NTCRT_IMPORT_ONLY lines of the sources, read by every make and written only when they change:
# an edit of a function assembles no stub
$(BUILD)/exports.txt: FORCE | $(BUILD)
	sed -n -E 's/^NTCRT_IMPORT(_ONLY)?\(([A-Za-z0-9_]+), *([A-Za-z0-9_]+)\);.*/\1 \2 \3/p' $(SRCS) > $@.tmp
	if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

# An object for each name, see NtCRT.h. Only the stubs that Exports.awk writes are assembled, those of names that are
# gone are removed. The stubs written are removed when they fail to assemble, so that the next make writes them again.
# On x86 the stubs of stdcall functions take the size of the arguments from the names in the objects, for tcc the
# stubs of NTCRT_IMPORT_ONLY whether the name is of a function.
$(BUILD)/exports.stamp: $(BUILD)/exports.txt src/Exports.awk $(if $(X86)$(TCC),$(BUILD)/objects.syms)
	rm -f $@
	mkdir -p $(BUILD)/exports
	awk -v dir=$(BUILD)/exports -v keep=$(BUILD)/exports.keep -v x86=$(X86) -v elf=$(TCC) -v syms=$(BUILD)/objects.syms \
	    -f src/Exports.awk $< > $(BUILD)/exports.new
	cd $(BUILD)/exports && ls | grep -v -x -F -f ../exports.keep | xargs -r rm -f
	cd $(BUILD)/exports && xargs -r -P 8 -n 64 $(LIB_AS) -c < ../exports.new || { xargs rm -f < ../exports.new; exit 1; }
	touch $@

$(BUILD)/libntcrt.a: $(OBJS) $(BUILD)/exports.stamp
	rm -f $@
	ar rcs $@ $(OBJS)
	cd $(BUILD)/exports && ar rs ../libntcrt.a *.o

# libntcrt has the pthread functions (src/Pthread*.cpp) and those of <dlfcn.h> (src/Dlfcn.cpp). Empty libraries take
# the place of winpthreads for -lpthread and -lwinpthread, and of libdl for -ldl, the directory of the build precedes
# that of the toolchain.
$(BUILD)/libpthread.a $(BUILD)/libwinpthread.a $(BUILD)/libdl.a: | $(BUILD)
	rm -f $@
	ar rc $@

$(BUILD)/ntcrt.syms: $(BUILD)/libntcrt.a
	nm -g --defined-only $< | awk 'NF == 3 { print $$3 }' | sort -u > $@

$(BUILD)/objects.syms: $(OBJS)
	nm -g --defined-only $^ | awk 'NF == 3 && $$2 == "T" { print $$3 }' | sort -u > $@.tmp
	if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

# ntdll exports the classic C runtime, libntdll.a does not import it.
# Functions implemented by libntcrt.a are left out so they never come from ntdll (the names of x86 without "_"). The
# ntdll of x86 has neither the SEH of x64 nor setjmp.
NTDLL_X64 := __C_specific_handler _local_unwind _setjmp _setjmpex longjmp setjmp

$(BUILD)/ntdll_crt.def: src/ntdll_crt.txt $(BUILD)/ntcrt.syms
	printf 'LIBRARY ntdll.dll\nEXPORTS\n' > $@
	awk -v x86=$(X86) -v x64='$(NTDLL_X64)' 'BEGIN { split(x64, a); for (i in a) X64[a[i]] = 1 } \
	    NR == FNR { n = $$1; if (x86) sub(/^_/, "", n); Defined[n] = 1; next } \
	    !($$1 in Defined) && !(x86 && $$1 in X64)' $(BUILD)/ntcrt.syms src/ntdll_crt.txt >> $@

$(BUILD)/libntdll_crt.a: $(BUILD)/ntdll_crt.def
	dlltool -d $< -l $@

# ntdll for tcc: its C runtime as above, and the functions of libntdll.a of MinGW-w64. The .def of tcc has no aliases
# ("strlwr == _strlwr") and no comments, tcc takes "_strlwr" for "strlwr" itself.
$(BUILD)/ntdll.def: $(BUILD)/ntdll_crt.def
	{ grep -v -e '==' -e '^;' $<; nm -g "$(shell $(CXX) -print-file-name=libntdll.a)" | sed -n 's/.* I __imp_//p'; } \
	    > $@

# The library for tcc in lib/: libntcrt.a with the members of libgcc, libmingw32, libmingwex and libmsvcrt (its static
# members) that NtCRT and TCC_SEEDS use, those of a GCC build, made ELF objects (src/Members.awk); and ntdll.def.
# As there, libmingwex precedes ntdll. The members of libmingwex that src/MathX87.cpp wraps (fdim and sqrt) give their
# names as _NtCRT_X87_<name>.
TCC_SOURCES := $(foreach l,libgcc.a libmingw32.a libmingwex.a,"$(shell $(CXX) -print-file-name=$(l))")

$(BUILD)/members.txt: $(BUILD)/libntcrt.a $(BUILD)/ntcrt.syms src/Members.awk
	rm -rf $(BUILD)/coff
	mkdir -p $(BUILD)/coff
	cd $(BUILD)/coff && for l in $(TCC_SOURCES); do ar x "$$l"; done && \
	    ar x "$(MSVCRT_A)" $$(ar t "$(MSVCRT_A)" | grep '^lib64_libmsvcrt_')
	cd $(BUILD)/coff && for o in $(X87_MINGWEX); do \
	    objcopy $$(nm -g --defined-only $$o | awk 'NF == 3 { print "--redefine-sym", $$3 "=_NtCRT_X87_" $$3 }') $$o; \
	done
	{ printf '%s\n' $(TCC_SEEDS); nm -g --undefined-only $(BUILD)/libntcrt.a | awk '$$1 == "U" { print $$2 }'; } \
	    > $(BUILD)/seeds.txt
	cd $(BUILD)/coff && nm -A -g *.o | awk -v have=../ntcrt.syms -v seeds=../seeds.txt -f "$(CURDIR)/src/Members.awk" \
	    ../ntcrt.syms ../seeds.txt - > ../members.txt

# objcopy makes the ELF objects without the unwind tables and debug information, coffrel the relocations. The long
# double functions of the members (exp2l that exp2 calls) take 80 bits, not the long double of tcc: they are renamed.
$(BUILD)/coffrel.exe: tools/coffrel.c | $(BUILD)
	$(CC) -std=c17 -O2 -Wall -Wextra -o $@ $<

$(BUILD)/lib/libntcrt.a: $(BUILD)/libntcrt.a $(BUILD)/members.txt $(BUILD)/coffrel.exe
	rm -rf $(BUILD)/elf
	mkdir -p $(BUILD)/elf $(@D)
	cd $(BUILD)/coff && nm -g --defined-only $$(cat ../members.txt) | \
	    awk -v seeds='$(TCC_SEEDS)' 'BEGIN { n = split(seeds, s); for (i = 1; i <= n; i++) Long[s[i] "l"] = 1 } \
	        NF == 3 && $$3 in Long { print $$3, "__ntcrt_" $$3 }' | sort -u > ../longdouble.txt
	cd $(BUILD)/coff && xargs -I{} objcopy -O elf64-x86-64 --strip-debug -R .pdata -R .xdata -R '.rdata$$zzz' \
	    --redefine-syms=../longdouble.txt {} ../elf/{} < ../members.txt
	cd $(BUILD)/elf && xargs ../coffrel.exe < ../members.txt
	cp $(BUILD)/libntcrt.a $@
	cd $(BUILD)/elf && xargs ar rs ../lib/libntcrt.a < ../members.txt

$(BUILD)/lib/ntdll.def: $(BUILD)/ntdll.def
	mkdir -p $(@D)
	cp $< $@

# The math functions that src/MathX87.cpp rounds (src/MathX87.awk), renamed _NtCRT_X87_<name> in the members of
# libmsvcrt that define them, and those of libmingwex (the _WRAP lines) in copies of their members, all names renamed.
# The assembler names of x86 have the prefix "_".
X87_ROUND := $(shell awk -v x86=$(X86) -f src/MathX87.awk src/MathX87.cpp)
X87_WRAP := $(shell awk -v x86=$(X86) -v wrap=1 -f src/MathX87.awk src/MathX87.cpp)
X87_MINGWEX := $(X87_WRAP:%=lib$(if $(X86),32,64)_libmingwex_a-%.o)
define x87-rename
cd $(BUILD)/msvcrt && ar x "$(MINGWEX_A)" $(X87_MINGWEX)
cd $(BUILD)/msvcrt && { $(foreach f,$(X87_ROUND),echo $(if $(X86),_)$(f) $(if $(X86),_)_NtCRT_X87_$(f);) \
    nm -g --defined-only $(X87_MINGWEX) | \
    awk -v p=$(if $(X86),_) 'NF == 3 { print $$3, p "_NtCRT_X87_" substr($$3, length(p) + 1) }'; } > ../x87.syms
cd $(BUILD)/msvcrt && nm -A -g --defined-only *.o | \
    awk 'NR == FNR { Rename[$$1] = 1; next } $$NF in Rename { sub(/:.*/, ""); print }' ../x87.syms - | sort -u | \
    xargs -r -n 1 objcopy --redefine-syms=../x87.syms
endef

# The static (non-import) members of libmsvcrt.a, minus those defining a symbol of libntcrt.a
$(BUILD)/libmsvcrt_static.a: $(BUILD)/ntcrt.syms src/MathX87.cpp src/MathX87.awk
	rm -rf $(BUILD)/msvcrt
	mkdir -p $(BUILD)/msvcrt
	cd $(BUILD)/msvcrt && ar x "$(MSVCRT_A)" $$(ar t "$(MSVCRT_A)" | grep '^lib$(if $(X86),32,64)_libmsvcrt_')
	$(x87-rename)
	for o in $(BUILD)/msvcrt/*.o; do \
	    if nm -g --defined-only $$o | awk 'NF == 3 { print $$3 }' | grep -qxF -f $(BUILD)/ntcrt.syms; then rm $$o; fi; \
	done
	rm -f $@
	ar rcs $@ $(BUILD)/msvcrt/*.o

# Functions of libmingwex replaced by libntcrt: the printf of mingw-w64 by src/FormatNarrow.cpp and
# src/FormatWide.cpp, its scanf (and the __ms_v*scanf that call msvcrt) by src/Scanf.cpp, src/ScanfWide.cpp,
# src/StdioScanf.cpp and src/StdioWscanf.cpp, the strtod family of mingw-w64 by src/FloatParse.cpp, wcstoll,
# wcstoull, wcstoimax and wcstoumax by src/Integer.cpp, btowc and wctob by src/MultibyteChar.cpp, ftruncate64 by
# src/LowIOFile.cpp, sleep and usleep by src/Clock.cpp, lgamma and lgammaf of x86, fdim and sqrt of x64 by
# src/MathX87.cpp. libntcrt defines them under their names too, for lld.
WRAPS := $(foreach f,vfprintf vprintf vsprintf vsnprintf vasprintf fprintf printf sprintf snprintf asprintf \
                     vfwprintf vwprintf vswprintf vsnwprintf fwprintf wprintf swprintf snwprintf,--wrap=__mingw_$(f)) \
         $(foreach f,vfscanf vscanf vsscanf fscanf scanf sscanf vfwscanf vwscanf vswscanf fwscanf wscanf swscanf, \
                     --wrap=__mingw_$(f)) \
         $(foreach f,vfscanf vscanf vsscanf vfwscanf vwscanf vswscanf,--wrap=__ms_$(f)) \
         $(foreach f,strtof strtod strtold wcstof wcstod wcstold,--wrap=__mingw_$(f)) \
         $(foreach f,strtold wcstold wcstoll wcstoull wcstoimax wcstoumax,--wrap=$(f)) \
         --wrap=btowc --wrap=wctob --wrap=ftruncate64 --wrap=sleep --wrap=usleep $(X87_WRAP:%=--wrap=%)

# Replace the msvcrt and kernel32 libraries, the group resolves the cycles between them. libmingwex precedes
# libntcrt, the math functions libntcrt calls are of ntdll. libntcrt_start replaces the pseudo relocator and _matherr
# of libmingw32, which would link the printf family into every program, and emutls of libgcc. libntcrt has the
# pthread functions, winpthreads is not linked. include/ (<threads.h>) is a system include directory of the programs.
# kernel32 follows the group: it gives only the functions a program calls that NtCRT does not have, -ntcrt-strict
# leaves it out. -ntcrt-glob links libntcrt_glob before the group, as -lntcrt_glob would: its _setargv and _wsetargv
# precede those of libmingw32. -Bstatic links libstdc++ and the others statically without -static. The specs of an
# installation (install) have no paths, its drivers pass them. A libgcc built for a shared libgcc has the unwinder in
# libgcc_eh (that of i686 in MSYS2, DWARF).
GCC_EH := $(shell test -f "$$($(CXX) -print-file-name=libgcc_eh.a)" && echo gcc_eh)
SPECS_LIBGCC := %{ntcrt-glob:-lntcrt_glob} --start-group -lntcrt_start -lmingw32 -lgcc $(addprefix -l,$(GCC_EH)) \
                -lmingwex -lntcrt -lmsvcrt_static -lntdll_crt -lntdll --end-group %{!ntcrt-strict:-lkernel32}

# GNU ld drops the unused functions on x86, x64 keeps them by .pdata (README.md "Size")
SPECS_LINK := $(if $(X86),--gc-sections )$(WRAPS)

# $(call build-specs,<include dir>,<library dir>,<file>): the specs of a build. GCC splits the arguments of specs at
# spaces, a backslash makes a space of a path part of the argument. cpp is the preprocessor of C, cc1plus that of C++.
empty :=
space := $(empty) $(empty)
define build-specs
printf '*cpp:\n+ -isystem %s\n\n' '$(subst $(space),\ ,$(1))' > "$(3)"
printf '*cc1plus:\n+ -isystem %s\n\n' '$(subst $(space),\ ,$(1))' >> "$(3)"
printf '*link:\n+ -L%s -Bstatic %s\n\n' '$(subst $(space),\ ,$(2))' "$(SPECS_LINK)" >> "$(3)"
printf '*libgcc:\n%s\n\n' '$(SPECS_LIBGCC)' >> "$(3)"
printf '*lib:\n%%{pg:-lgmon}\n\n' >> "$(3)"
endef

$(BUILD)/ntcrt.specs: Makefile src/MathX87.cpp src/MathX87.awk | $(BUILD)
	$(call build-specs,$(CURDIR)/include,$(CURDIR)/$(BUILD),$@)

$(BUILD)/install/ntcrt.specs: Makefile src/MathX87.cpp src/MathX87.awk
	mkdir -p $(@D)
	printf '*link:\n+ -Bstatic %s\n\n' "$(SPECS_LINK)" > $@
	printf '*libgcc:\n%s\n\n' '$(SPECS_LIBGCC)' >> $@
	printf '*lib:\n%%{pg:-lgmon}\n\n' >> $@

# The same for clang, the options prefixed with $ are for linking and follow the inputs. clang splits the file
# like a shell (GNU style), the paths are quoted for spaces. The directory of the libraries precedes those of the
# toolchain, which -Wl,-L would follow: -lpthread is the empty library. lld drops the unused functions of
# objects built with -ffunction-sections by clang, which makes their .pdata associative. lld resolves a symbol to
# the first library that defines it, wherever the reference is: libntcrt precedes libmingwex for the functions it
# replaces, without --wrap (with it lld would load the members of libmingwex and every object of a __wrap_
# function). The math functions both libmingwex and ntdll define are of libmingwex. ntcrt.cfg adds kernel32 after
# them to ntcrt-strict.cfg. Those of an installation find include/ and the libraries relative to themselves.
NTCRT_LINK := -Wl,--gc-sections \
              $(addprefix -l,ntcrt_start mingw32 stdc++ gcc $(GCC_EH) ntcrt mingwex msvcrt_static ntdll_crt ntdll)

# $(call build-cfg,<sysroot>,<include dir>,<library dir>,<file>)
define build-cfg
printf '"%s"\n' $(LLVM_FLAGS) "--sysroot=$(1)" -fuse-ld=lld -nodefaultlibs -static "-isystem$(2)" "-L$(3)" > "$(4)"
printf '$$%s\n' $(NTCRT_LINK) >> "$(4)"
endef

$(BUILD)/ntcrt.cfg $(BUILD)/install/ntcrt.cfg: %/ntcrt.cfg: %/ntcrt-strict.cfg
	printf '@ntcrt-strict.cfg\n$$-lkernel32\n' > $@

$(BUILD)/ntcrt-strict.cfg: Makefile | $(BUILD)
	$(call build-cfg,$(GCC_ROOT),$(CURDIR)/include,$(CURDIR)/$(BUILD),$@)

$(BUILD)/install/ntcrt-strict.cfg: Makefile
	mkdir -p $(@D)
	$(call build-cfg,$(GCC_ROOT),<CFGDIR>/../../include,<CFGDIR>,$@)

TEST_SRCS := $(wildcard test/upstream/*.cpp)

$(BUILD)/Test.exe: $(TEST_SRCS) $(LIBS) $(DRIVER)
	$(NTCRT_CXX) -std=c++23 -O2 -Iinclude -static -o $@ $(TEST_SRCS)

# Runs in the build directory: the tests write files by fixed names in the current directory
test-upstream: $(BUILD)/Test.exe
	objdump -p $< | grep 'DLL Name'
	cd $(BUILD) && ./Test.exe

# Differential fuzz of the UTF-8 codec against kernel32 and of printf, scanf, strtod and strtol against
# ucrtbase.dll, FUZZ_ITERATIONS=n for a longer run
FUZZ_ITERATIONS ?= 200000

$(BUILD)/fuzz/utf8.exe: test/fuzz/utf8.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -o $@ $<

$(BUILD)/fuzz/format.exe: test/fuzz/format.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -o $@ $< -lkernel32

# strtold and wcstold against the gdtoa of libmingwex in a DLL built with the default runtime
$(BUILD)/fuzz/gdtoa.dll: test/fuzz/gdtoa.c
	mkdir -p $(@D)
	$(CC) -O2 -shared -o $@ $<

$(BUILD)/fuzz/strtold.exe: test/fuzz/strtold.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -o $@ $< -lkernel32

test-fuzz: $(BUILD)/fuzz/utf8.exe $(BUILD)/fuzz/format.exe $(BUILD)/fuzz/strtold.exe $(BUILD)/fuzz/gdtoa.dll
	./$(BUILD)/fuzz/utf8.exe $(FUZZ_ITERATIONS)
	./$(BUILD)/fuzz/format.exe $(FUZZ_ITERATIONS)
	./$(BUILD)/fuzz/strtold.exe $(FUZZ_ITERATIONS) $(BUILD)/fuzz/gdtoa.dll

# The strtod family and the floating point conversions of printf in a DLL, tested by Go: the tables of Go strconv,
# fuzz against strconv and math/big, GO_FUZZTIME=d for longer fuzzing. Not in "test", it needs go.
# -count=1: go would reuse a result without knowing which DLL NTCRT_FLOAT_DLL names.
GO ?= go
GO_FUZZTIME ?= 30s

$(BUILD)/gofloat/float.dll: test/gofloat/dll/float.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -shared -o $@ $<

test-go: $(BUILD)/gofloat/float.dll
	cd test/gofloat && NTCRT_FLOAT_DLL="$(CURDIR)/$<" $(GO) test -count=1 -run Test . && \
	    for f in FuzzStrtod FuzzStrtodLong FuzzStrtold80 FuzzPrintfDouble FuzzPrintfLongDouble; do \
	        NTCRT_FLOAT_DLL="$(CURDIR)/$<" $(GO) test -run XXX -fuzz "^$$f\$$" -fuzztime $(GO_FUZZTIME) . || exit 1; \
	    done

# strtod, strtof and wcstod against parse-number-fxx-test-data, FLOAT_CORPUS=its data directory. Not in "test", the
# data is 270 MB: git clone https://github.com/nigeltao/parse-number-fxx-test-data
$(BUILD)/float/corpus.exe: test/float/corpus.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -o $@ $<

test-float-corpus: $(BUILD)/float/corpus.exe
	./$< $(FLOAT_CORPUS)/*.txt

# printf where the fuzz cannot compare with UCRT: long double, positional arguments, %Z, %n, errors
$(BUILD)/printf/printf.exe: test/printf/printf.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -Wno-format -static -o $@ $<

test-printf: $(BUILD)/printf/printf.exe
	./$<

# The math functions return a value of their type, the x87 ones of x86 too (src/MathX87.cpp)
$(BUILD)/math/round.exe: test/math/round.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -fno-builtin -static -o $@ $<

# _NtCRT_GetPow10 of POW10_SMALL=1, in every library, against the table it is made from (test/float/pow10gen.py)
$(BUILD)/float/pow10.exe: test/float/pow10.cpp src/FloatPow10.inl $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CXX) -std=c++23 -O2 -static -o $@ $<

test-math: $(BUILD)/math/round.exe $(BUILD)/float/pow10.exe
	./$<
	./$(BUILD)/float/pow10.exe

# scanf where the fuzz cannot compare with UCRT: the examples of C11, streams, the wide, _s and _l functions, msvcrt,
# the locale and floating point numbers that would round twice. The map shows that libmingwex gives no scanf, the
# test makes malloc and realloc of the library fail by --wrap.
$(BUILD)/scanf/scanf.exe: test/scanf/scanf.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -Wno-format -static -o $@ $< -Wl,-Map=$(BUILD)/scanf/scanf.map \
	    -Wl,--wrap=_NtCRT_malloc,--wrap=_NtCRT_realloc

test-scanf: $(BUILD)/scanf/scanf.exe
	if grep -E 'libmingwex_a-[a-z0-9_]*(scanf|gdtoa|pformat|strtod|strtof)' $(BUILD)/scanf/scanf.map; then \
	    echo "scanf of libmingwex is linked"; exit 1; fi
	./$<

# Console reads and writes in a pseudo console, conpty.exe is built with the default runtime
$(BUILD)/console/child.exe: test/console/child.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -o $@ $< -lkernel32

$(BUILD)/console/conpty.exe: test/console/conpty.c
	mkdir -p $(@D)
	$(CC) -std=c17 -O2 -o $@ $<

test-console: $(BUILD)/console/conpty.exe $(BUILD)/console/child.exe
	./$< $(BUILD)/console/child.exe

# libstdc++ on NtCRT
$(BUILD)/cxx/stdcxx.exe: test/cxx/stdcxx.cpp $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CXX) -std=c++23 -O2 -Wno-deprecated-declarations -static -o $@ $<

test-cxx: $(BUILD)/cxx/stdcxx.exe
	./$<

# The pthread functions, <threads.h> and the threads of libstdc++. -lpthread and -pthread link no winpthreads.
# PARKER=event: the thread tests (test-thread, test-pthread) wait on the parker of Windows 7 and earlier, an event per
# thread, in place of NtWaitForAlertByThreadId: test/thread/parker.c is linked in, in directories of their own.
ifeq ($(PARKER),event)
PARKER_OBJ := $(BUILD)/parker-event.o
THREAD := $(BUILD)/thread-event
PTHREAD := $(BUILD)/pthread-event
else
THREAD := $(BUILD)/thread
PTHREAD := $(BUILD)/pthread
endif

$(BUILD)/parker-event.o: test/thread/parker.c | $(BUILD)
	$(CC) -O2 -c -o $@ $<

THREAD_TESTS := $(THREAD)/pthread.exe $(THREAD)/c11.exe $(THREAD)/cxx.exe \
                $(THREAD)/dllimport.exe $(THREAD)/exit.exe $(THREAD)/tlsslot.exe

$(THREAD)/pthread.exe: test/thread/pthread.c $(LIBS) $(DRIVER) $(PARKER_OBJ)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -pthread -o $@ $< $(PARKER_OBJ) -lpthread

$(THREAD)/c11.exe: test/thread/c11.c $(LIBS) $(DRIVER) $(PARKER_OBJ)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -o $@ $< $(PARKER_OBJ)

$(THREAD)/dllimport.exe: test/thread/dllimport.c $(LIBS) $(DRIVER) $(PARKER_OBJ)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -o $@ $< $(PARKER_OBJ)

$(THREAD)/tlsslot.exe: test/thread/tlsslot.c $(LIBS) $(DRIVER) $(PARKER_OBJ)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -o $@ $< $(PARKER_OBJ)

$(THREAD)/exit.exe: test/thread/exit.c $(LIBS) $(DRIVER) $(PARKER_OBJ)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -o $@ $< $(PARKER_OBJ)

$(THREAD)/cxx.exe: test/thread/cxx.cpp $(LIBS) $(DRIVER) $(PARKER_OBJ)
	mkdir -p $(@D)
	$(NTCRT_CXX) -std=c++23 -O2 -static -pthread $(if $(NOUNWIND),-DNTCRT_NO_UNWIND) -o $@ $< $(PARKER_OBJ) \
	    -Wl,-Map=$(THREAD)/cxx.map

# exit.exe: the end of the process without exit (the last thread, ExitProcess), its status and output (a pipe). The
# libmingw32 of x86 (MinGW-w64 15) runs no key destructor of __mingwthr_key_dtor at DLL_PROCESS_DETACH, x64 does
test-thread: $(THREAD_TESTS)
	if grep -i winpthread $(THREAD)/cxx.map; then echo "winpthreads is linked"; exit 1; fi
	if grep 'libgcc\.a(emutls\.o)' $(THREAD)/cxx.map; then echo "emutls of libgcc is linked"; exit 1; fi
	for t in $(THREAD_TESTS); do \
	    if objdump -p $$t | grep 'DLL Name' | grep -v -i -q ntdll.dll; then echo "$$t imports more than ntdll.dll"; exit 1; fi; \
	done
	for t in $(filter-out %/exit.exe,$(THREAD_TESTS)); do ./$$t || exit 1; done
	t=$(THREAD)/exit.exe; \
	check() { out=$$($$t $$1); s=$$?; test $$s = $$2 && test "$$out" = "$$3" || { echo "exit.exe $$1: $$s '$$out'"; exit 1; }; }; \
	check main 7 "main dtor:main atexit" && \
	check c11 7 "c11 dtor:main atexit" && \
	check last 9 "last dtor:main return dtor:last atexit" && \
	check createthread 11 "createthread dtor:main worker atexit" && \
	check exitprocess 5 "exitprocess atexit" && \
	check locked 6 "locked atexit" && \
	rm -f $(THREAD)/exit.txt && \
	check "lockedfd $(THREAD)/exit.txt" 8 "lockedfd dup2:-1,1 atexit" && \
	test "$$(cat $(THREAD)/exit.txt)" = lockedfd && \
	check "mingwthr $(THREAD)/exit.txt" 4 "mingwthr atexit" && \
	test "$$(cat $(THREAD)/exit.txt)" = "$(if $(X86),,mingwthr)" && \
	out=$$($(THREAD)/cxx.exe last) && test "$$out" = "AB"
	@echo "end of the process: ok"

# libntcrt_start: pseudo relocations of data auto-imported from a DLL (built with the default runtime), _matherr
$(BUILD)/start/data.dll: test/start/data.c
	mkdir -p $(@D)
	$(CC) -O2 -shared -o $@ $< -Wl,--out-implib,$(BUILD)/start/libdata.a

$(BUILD)/start/reloc.exe: test/start/reloc.c $(BUILD)/start/data.dll $(LIBS) $(DRIVER)
	$(NTCRT_CC) -O2 -static -o $@ $< -L$(BUILD)/start -ldata

$(BUILD)/start/matherr.exe $(BUILD)/start/matherr_custom.exe: test/start/matherr.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -O2 -w -static -o $(BUILD)/start/matherr.exe $<
	$(NTCRT_CC) -O2 -w -static -DCUSTOM -o $(BUILD)/start/matherr_custom.exe $<

$(BUILD)/start/argv.exe: test/start/argv.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -O2 -static -o $@ $<

# The wildcards of the arguments, by libntcrt_glob (-ntcrt-glob of the specs): alone, with int _dowildcard = -1 and
# with CRT_glob.o of MinGW-w64 (wmain). Not expanded without it and with _dowildcard = 0. _dowildcard = -1 and
# CRT_glob.o without it stop with an error.
CRT_GLOB := "$(shell $(CXX) -print-file-name=CRT_glob.o)"
GLOB_EXES := $(addprefix $(BUILD)/start/,glob.exe globl.exe globw.exe glob0.exe globz.exe globerr.exe globwerr.exe)

$(GLOB_EXES) &: test/start/glob.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -O2 -static -DDEFINE=-1 -o $(BUILD)/start/glob.exe $< -lntcrt_glob
	$(NTCRT_CC) -O2 -static -o $(BUILD)/start/globl.exe $< $(if $(LLVM),-lntcrt_glob,-ntcrt-glob)
	$(NTCRT_CC) -O2 -static -municode -DWIDE -o $(BUILD)/start/globw.exe $< $(CRT_GLOB) -lntcrt_glob
	$(NTCRT_CC) -O2 -static -DNONE -o $(BUILD)/start/glob0.exe $<
	$(NTCRT_CC) -O2 -static -DNONE -DDEFINE=0 -o $(BUILD)/start/globz.exe $< -lntcrt_glob
	$(NTCRT_CC) -O2 -static -DDEFINE=-1 -o $(BUILD)/start/globerr.exe $<
	$(NTCRT_CC) -O2 -static -municode -DWIDE -o $(BUILD)/start/globwerr.exe $< $(CRT_GLOB)

$(BUILD)/start/names.exe: test/start/names.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -O2 -static -o $@ $<

# The code pages of the PEB of Windows 7, the .nls data of Windows 10 and before
$(BUILD)/start/nlsdata.exe: test/start/nlsdata.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -O2 -static -o $@ $<

# The process attach of the TLS callbacks, which Windows 7 does not send to a program that imports ntdll only
$(BUILD)/start/tlsattach.exe: test/start/tlsattach.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -O2 -static -o $@ $<

# LoadLibrary with the PEB of Windows 7, whose LdrLoadDll takes a search path, not flags
$(BUILD)/start/loadlib.exe: test/start/loadlib.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -O2 -static -o $@ $<

# The time_t of the names of msvcrt without a size, 32 bits on x86
$(BUILD)/start/time32.exe: test/start/time32.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -o $@ $<

# Without -ntcrt-strict, the functions it imports from kernel32
$(BUILD)/start/kernel32.exe: test/start/kernel32.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(KERNEL32_CC) -O2 -static -o $@ $<

# The PEB and TEB of NDK.h, by kernel32
$(BUILD)/start/pebteb.exe: test/start/pebteb.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(KERNEL32_CC) -O2 -static -o $@ $<

# <dlfcn.h>: the program runs in the directory of its DLLs, dllocal.dll for RTLD_LOCAL
$(BUILD)/dl/dllib.dll $(BUILD)/dl/dllocal.dll: test/dl/lib.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -O2 -shared -o $(BUILD)/dl/dllib.dll $<
	$(NTCRT_CC) -O2 -shared -DLOCAL -o $(BUILD)/dl/dllocal.dll $<

$(BUILD)/dl/dl.exe: test/dl/dl.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -std=c17 -O2 -static -o $@ $<

# min.exe has only the functions of <dlfcn.h>, which must link no object of libntcrt.a but Dlfcn.o and Library.o
# beside those of an empty program (not the printf engine, stdio or low-level I/O), and the import stubs of ntdll
$(BUILD)/dl/min.o: test/dl/min.c $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -O2 -c -o $@ $<

$(BUILD)/dl/empty.o: test/size/empty.c $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -O2 -c -o $@ $<

$(BUILD)/dl/min.exe $(BUILD)/dl/empty.exe: $(BUILD)/dl/%.exe: $(BUILD)/dl/%.o $(LIBS) $(DRIVER)
	$(NTCRT_CC) -s -static -o $@ $< -Wl,-Map=$(BUILD)/dl/$*.map

test-dl: $(BUILD)/dl/dl.exe $(BUILD)/dl/min.exe $(BUILD)/dl/empty.exe $(BUILD)/dl/dllib.dll
	cd $(BUILD)/dl && ./dl.exe && ./min.exe
	cd $(BUILD)/dl && for m in empty min; do \
	    grep -o -E '[A-Za-z0-9_]+\.o\b' $$m.map | grep -v -x -E 'empty\.o|min\.o|(lib|build_).*(s[0-9]+|_?[ht])\.o' | sort -u > $$m.objs || exit 1; \
	done && extra=$$(comm -13 empty.objs min.objs | grep -v -x -e Dlfcn.o -e Library.o); \
	if [ -n "$$extra" ]; then echo "dl: the functions of <dlfcn.h> link" $$extra; exit 1; fi

test-start: $(BUILD)/start/reloc.exe $(BUILD)/start/matherr.exe $(BUILD)/start/matherr_custom.exe \
            $(BUILD)/start/argv.exe $(BUILD)/start/names.exe $(BUILD)/start/kernel32.exe $(BUILD)/start/pebteb.exe \
            $(BUILD)/start/time32.exe $(BUILD)/start/nlsdata.exe $(BUILD)/start/tlsattach.exe \
            $(BUILD)/start/loadlib.exe $(GLOB_EXES)
	cd $(BUILD)/start && ./reloc.exe
	$(BUILD)/start/argv.exe
	cd $(BUILD)/start && rm -rf glob.tmp && for t in glob globl globw glob0 globz; do \
	    ./$$t.exe | grep -q '^glob: ok$$' || { echo "$$t.exe fails"; exit 1; }; done
	cd $(BUILD)/start && for t in globerr globwerr; do \
	    ./$$t.exe 2>&1 | grep -q '^_dowildcard requires -lntcrt_glob' && { ./$$t.exe 2>/dev/null; test $$? = 255; } || \
	    { echo "$$t.exe does not stop"; exit 1; }; done
	$(BUILD)/start/names.exe | grep -q '^names: ok$$'
	$(BUILD)/start/kernel32.exe | grep -q '^kernel32: ok$$'
	$(BUILD)/start/pebteb.exe | grep -q '^pebteb: ok$$'
	cd $(BUILD)/start && ./time32.exe
	$(BUILD)/start/nlsdata.exe | grep -q '^nlsdata: ok$$'
	cd $(BUILD)/start && ./tlsattach.exe | grep -q '^tlsattach: ok$$'
	$(BUILD)/start/loadlib.exe | grep -q '^loadlib: ok$$'
	test "$$(objdump -p $(BUILD)/start/kernel32.exe | awk '/DLL Name/ { k = tolower($$3) == "kernel32.dll"; next } k && /<none>/ { print $$NF }')" = GetSystemPowerStatus
	$(BUILD)/start/matherr.exe 2>&1 | grep -q '^_matherr(): Argument domain error (DOMAIN) in log()$$'
	$(BUILD)/start/matherr_custom.exe | grep -q '^custom _matherr type=1 name=log$$'
	@echo "start: ok"

# The CRT tests of Wine (LGPL, fetched at the tag of test/wine/fetch.sh into $(BUILD)/wine/src) on NtCRT, one
# program for each, the output in $(BUILD)/wine/log. Not in "test", it needs the network, and failures are expected
# (the differences of README.md and more): run.sh fails only when a test does not finish. GetProcAddress of msvcrt.dll and
# ucrtbase.dll finds the functions of NtCRT (test/wine/crt.c, the names exported by the DLLs of System32), the
# kernel32 functions that NtCRT does not have are imported from kernel32.dll. test/wine/skip.txt leaves out the tests
# of UCRT-only APIs and of what NtCRT does not have. The C++ test (cpp.c) needs the MSVC C++ ABI of msvcrt.
WINE := $(BUILD)/wine
WINE_TESTS := $(addprefix msvcrt/,data dir environ file heap locale misc printf scanf signal string time) \
              $(addprefix ucrtbase/,environ file misc string)
WINE_CFLAGS := -std=gnu17 -O2 -w -Wno-implicit-function-declaration -Wno-int-conversion -DSTANDALONE \
               -Itest/wine/include -I$(WINE)/src/include -include test/wine/compat.h
KERNEL32_A := $(shell $(CXX) -print-file-name=libkernel32.a)
MINGWEX_A := $(shell $(CXX) -print-file-name=libmingwex.a)
SYSTEM32 := $(subst \,/,$(SYSTEMROOT))/System32

$(WINE)/src.stamp: test/wine/fetch.sh
	sh $< $(WINE)/src
	touch $@

$(WINE)/%.c: $(WINE)/src.stamp test/wine/skip.awk test/wine/skip.txt
	mkdir -p $(@D)
	awk -v test=$* -f test/wine/skip.awk test/wine/skip.txt $(WINE)/src/dlls/$(*D)/tests/$(*F).c > $@

$(WINE)/table.c: test/wine/table.sh $(LIBS)
	mkdir -p $(@D)
	sh $< $(SYSTEM32)/msvcrt.dll $(SYSTEM32)/ucrtbase.dll $(BUILD)/libntcrt.a $(BUILD)/libntdll_crt.a \
	    $(BUILD)/libmsvcrt_static.a "$(MINGWEX_A)" > $@

# kernel32 without the functions of NtCRT
$(WINE)/libkernel32_rest.a: $(BUILD)/ntcrt.syms
	mkdir -p $(@D)
	printf 'LIBRARY kernel32.dll\nEXPORTS\n' > $(WINE)/kernel32_rest.def
	nm -g "$(KERNEL32_A)" | awk '$$2 == "I" && $$3 ~ /^__imp_/ { print substr($$3, 7) }' | sort -u | \
	    awk 'NR == FNR { Defined[$$1] = 1; next } !($$1 in Defined)' $(BUILD)/ntcrt.syms - >> $(WINE)/kernel32_rest.def
	dlltool -d $(WINE)/kernel32_rest.def -l $@

$(WINE)/%.exe: $(WINE)/%.c $(WINE)/table.c test/wine/crt.c test/wine/compat.h test/wine/include/wine/debug.h \
               $(WINE)/libkernel32_rest.a $(LIBS) $(DRIVER)
	$(NTCRT_CC) $(WINE_CFLAGS) -static -o $@ $< test/wine/crt.c $(WINE)/table.c -L$(WINE) -lkernel32_rest \
	    -ladvapi32 -luser32

.PRECIOUS: $(WINE)/%.c

test-wine: $(WINE_TESTS:%=$(WINE)/%.exe)
	sh test/wine/run.sh $(WINE) $(WINE_TESTS)

# The thread tests of pthreads4w (Apache-2.0) and of musl libc-test (MIT), fetched at the versions of
# test/pthread/fetch.sh into $(BUILD)/pthread/src, on NtCRT. Not in "test", it needs the network. run.sh builds each
# test (incrementally) and runs it with a timeout, test/pthread/skip.txt leaves out what NtCRT does not support or
# what is specific to pthreads4w or Linux. PTHREAD_TESTS="suite/test ..." (shell patterns) runs some, RUNS=n each n
# times, TIMEOUT=s.
$(PTHREAD)/src.stamp: test/pthread/fetch.sh
	sh $< $(PTHREAD)/src
	touch $@

test-pthread: $(PTHREAD)/src.stamp $(LIBS) $(DRIVER) $(PARKER_OBJ)
	set -f; NTCRT_CC="$(NTCRT_CC)$(if $(PARKER_OBJ), $(CURDIR)/$(PARKER_OBJ))" sh test/pthread/run.sh $(PTHREAD) $(PTHREAD_TESTS)

# The tests of musl libc-test (MIT) but those of threads, its functional/ and regression/, fetched at the commit of
# test/libc-test/fetch.sh into $(BUILD)/libc-test/src, from the clone of test/pthread, on NtCRT. Not in "test", it
# needs the network. test/libc-test/skip.txt leaves out what tests POSIX, GNU or BSD alone. LIBC_TESTS="suite/test
# ..." (shell patterns) runs some, TIMEOUT=s.
LIBC := $(BUILD)/libc-test

$(LIBC)/src.stamp: test/libc-test/fetch.sh
	sh $< $(LIBC)/src $(BUILD)/pthread/src/libc-test.git
	touch $@

test-libc: $(LIBC)/src.stamp $(LIBS) $(DRIVER)
	set -f; NTCRT_CC="$(NTCRT_CC)" sh test/libc-test/run.sh $(LIBC) $(LIBC_TESTS)

test: test-upstream test-fuzz test-printf test-math test-scanf test-console test-cxx test-thread test-start test-install \
      test-dl

# Size of small programs, stripped. ld cannot drop unused functions of x64 PE (.pdata keeps them), so the size
# depends on which objects of libntcrt.a a program links.
SIZE_SRCS := $(wildcard test/size/*.c test/size/*.cpp)

size: $(LIBS) $(DRIVER)
	@mkdir -p $(BUILD)/size
	@for f in $(SIZE_SRCS); do \
	    n=$$(basename $${f%.*}); c="$(NTCRT_CC)"; case $$f in *.cpp) c="$(NTCRT_CXX)";; esac; \
	    $$c -O2 -s -static -o $(BUILD)/size/$$n.exe $$f && \
	    printf '%-10s %8d\n' $$n $$(wc -c < $(BUILD)/size/$$n.exe); \
	done

# Compare with UCRT, needs cl of Visual Studio
compare: $(LIBS) $(DRIVER)
	BUILD=$(BUILD) NTCRT_CC="$(NTCRT_CC)" sh test/compare/run.sh

# make install PREFIX=<dir>: include/ into <dir>/include, the libraries and the specs into <dir>/lib/gcc (with LLVM=
# the configuration files of clang into <dir>/lib/clang) and the drivers x86_64-w64-ntcrt-mingw32-gcc, g++, cc, c++
# (clang, clang++) into <dir>/bin, which run the compiler of this build with the files relative to themselves (see
# tools/driver.c). A GCC and an LLVM build install into the same <dir>.
ifdef LLVM
INSTALL_LIB := lib/clang
INSTALL_FILES := $(BUILD)/install/ntcrt.cfg $(BUILD)/install/ntcrt-strict.cfg
INSTALL_TOOLS := clang clang++
DRIVER_DIRS = -DNTCRT_LLVM_DIR='L"$(if $(LLVM_PREFIX),$(shell cygpath -m $(LLVM_PREFIX))/)"'
else
INSTALL_LIB := lib/gcc
INSTALL_FILES := $(BUILD)/install/ntcrt.specs
INSTALL_TOOLS := gcc g++ cc c++
DRIVER_DIRS = -DNTCRT_GCC_DIR='L"$(shell cygpath -m "$$(dirname "$$($(CC) -print-libgcc-file-name)")")/../../../../bin/"'
endif

$(BUILD)/install/driver.exe: tools/driver.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(KERNEL32_CC) -O2 -s -static -municode $(DRIVER_DIRS) -o $@ $<

INSTALL_DEPS := $(LIBS) $(INSTALL_FILES) $(BUILD)/install/driver.exe

# $(call install-to,<dir>), not a recursive make, which would build the libraries beside the make that runs it
define install-to
mkdir -p "$(1)/bin" "$(1)/$(INSTALL_LIB)" "$(1)/include"
cp -r include/. "$(1)/include"
cp $(LIBS) $(INSTALL_FILES) "$(1)/$(INSTALL_LIB)"
for t in $(INSTALL_TOOLS); do cp $(BUILD)/install/driver.exe "$(1)/bin/x86_64-w64-ntcrt-mingw32-$$t.exe"; done
endef

ifdef TCC
# make TCC=1 install PREFIX=<dir>: libntcrt.a and ntdll.def for tcc into <dir>/lib/tcc, beside a GCC or LLVM build
INSTALL_DEPS := $(LIBS)
define install-to
mkdir -p "$(1)/lib/tcc"
cp $(LIBS) "$(1)/lib/tcc"
endef
endif

install: $(INSTALL_DEPS)
	test -n "$(PREFIX)"
	$(call install-to,$(PREFIX))

# An installation in a directory with a space: C and C++ by the drivers without -static, an argument with quotes
# and spaces, -ntcrt-strict, -ntcrt-glob. Then the specs (GCC) or the configuration (clang) of a build, made for the
# include/ and the libraries of that directory: for clang with a sysroot of a space too, a junction to the toolchain,
# which rm removes without what it points to. TCC=1: the files of tcc in that directory.
INSTALL_TEST := $(BUILD)/install test
INSTALL_FULL := $(CURDIR)/$(INSTALL_TEST)

ifdef TCC
test-install: $(INSTALL_DEPS)
	rm -rf "$(INSTALL_TEST)"
	$(call install-to,$(INSTALL_TEST))
	for f in $(LIBS); do cmp "$$f" "$(INSTALL_TEST)/lib/tcc/$${f##*/}" || exit 1; done
	@echo "install: ok"
else
test-install: $(INSTALL_DEPS)
	rm -rf "$(INSTALL_TEST)"
	$(call install-to,$(INSTALL_TEST))
	"$(INSTALL_TEST)/bin/x86_64-w64-ntcrt-mingw32-$(firstword $(INSTALL_TOOLS)).exe" -O2 '-DTEXT="a \"b\" c\\"' \
	    -o "$(INSTALL_TEST)/hello.exe" test/start/install.c
	"$(INSTALL_TEST)/hello.exe" | tr -d '\r' | grep -q -x -F 'a "b" c\'
	"$(INSTALL_TEST)/bin/x86_64-w64-ntcrt-mingw32-$(word 2,$(INSTALL_TOOLS)).exe" -O2 -ntcrt-strict -x c++ \
	    -DTEXT='"c++"' -o "$(INSTALL_TEST)/hello++.exe" test/start/install.c
	"$(INSTALL_TEST)/hello++.exe" | tr -d '\r' | grep -q -x -F 'c++'
	"$(INSTALL_TEST)/bin/x86_64-w64-ntcrt-mingw32-$(firstword $(INSTALL_TOOLS)).exe" -O2 -ntcrt-glob -DGLOB \
	    -o "$(INSTALL_TEST)/glob.exe" test/start/install.c
	cd "$(INSTALL_TEST)" && test "$$(./glob.exe 'hel*.exe' | tr -d '\r' | tr '\n' ' ')" = "hello++.exe hello.exe "
ifdef LLVM
	cmd //c mklink //J "$$(cygpath -w "$(INSTALL_TEST)/sys root")" "$$(cygpath -w "$(GCC_ROOT)")" > /dev/null
	$(call build-cfg,$(INSTALL_FULL)/sys root,$(INSTALL_FULL)/include,$(INSTALL_FULL)/$(INSTALL_LIB),$(INSTALL_TEST)/build.cfg)
	$(LLVM_PREFIX)clang --config="$(INSTALL_TEST)/build.cfg" -O2 -DTEXT='"build"' -o "$(INSTALL_TEST)/build.exe" \
	    test/start/install.c; s=$$?; rm "$(INSTALL_TEST)/sys root"; exit $$s
else
	$(call build-specs,$(INSTALL_FULL)/include,$(INSTALL_FULL)/$(INSTALL_LIB),$(INSTALL_TEST)/build.specs)
	$(CC) -specs="$(INSTALL_TEST)/build.specs" -static -O2 -DTEXT='"build"' -o "$(INSTALL_TEST)/build.exe" \
	    test/start/install.c
endif
	"$(INSTALL_TEST)/build.exe" | tr -d '\r' | grep -q -x -F 'build'
	for t in hello hello++ glob build; do \
	    if objdump -p "$(INSTALL_TEST)/$$t.exe" | grep 'DLL Name' | grep -v -i -q ntdll.dll; then \
	        echo "$$t.exe imports more than ntdll.dll"; exit 1; fi; \
	done
	@echo "install: ok"
endif

clean:
	rm -rf $(BUILD)

.PHONY: FORCE all test test-upstream test-fuzz test-go test-float-corpus test-printf test-math test-scanf test-console \
        test-cxx test-thread test-start test-install test-dl test-libc test-wine test-pthread size compare install clean
