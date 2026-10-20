# NtCRT for MinGW-w64 (x64): link C/C++ programs that import ntdll.dll only
#
#   make            build build/libntcrt.a, the support libraries and build/ntcrt.specs
#   make test       build and run the tests
#   g++ -static -specs=build/ntcrt.specs ...
#
#   make LLVM=1     build with clang into build-llvm, programs link by lld (LLVM=<dir>/ for clang in <dir>)
#   clang++ --config=build-llvm/ntcrt.cfg -static ...

CXX      ?= g++
ifeq ($(origin CC),default)
CC       := gcc
endif
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra
CXXFLAGS += -Iinclude -ffunction-sections -fno-exceptions -fno-rtti

ifdef LLVM
# A section for each variable, which lld drops unused. GCC would put the zero data in .data$ sections, which take
# space in the file unlike .bss, and ld links whole objects anyway.
CXXFLAGS += -fdata-sections
BUILD    ?= build-llvm
else
BUILD    ?= build
endif
SRCS     := $(wildcard src/*.cpp)
OBJS     := $(SRCS:src/%.cpp=$(BUILD)/%.o)
# Replacements of libmingw32 and libgcc members, linked before them
START_SRCS := $(wildcard src/start/*.cpp)
START_OBJS := $(START_SRCS:src/start/%.cpp=$(BUILD)/start/%.o)
HEADERS  := src/NtCRT.h $(wildcard src/*.inl) $(wildcard include/*.h) $(wildcard include/KNSoft/NDK/*) \
            $(wildcard polyfill/*.inl)
MSVCRT_A := $(shell $(CXX) -print-file-name=libmsvcrt.a)

# The compilers of the runtime and of the programs on it
ifdef LLVM
LLVM_PREFIX := $(filter %/,$(LLVM))
# clang uses the headers and libraries of GCC (libstdc++, the msvcrt flavor of MinGW-w64). The __cpuidex of
# MinGW-w64 headers before 12 clashes with the one of <cpuid.h> of clang and is left out, and their <intrin.h>
# includes <mm3dnow.h>, which clang deprecates. Thread-local variables are emulated as GCC does, libstdc++ has
# std::call_once by them (__emutls_get_address on pthread keys).
GCC_ROOT := $(abspath $(dir $(shell $(CXX) -print-libgcc-file-name))../../../..)
LLVM_FLAGS := --target=x86_64-w64-mingw32 --sysroot=$(GCC_ROOT) -stdlib=libstdc++ -D__INTRINSIC_DEFINED___cpuidex \
              -D_CLANG_DISABLE_CRT_DEPRECATION_WARNINGS -femulated-tls
LIB_CXX  := $(LLVM_PREFIX)clang++ $(LLVM_FLAGS)
LIB_AS   := $(LLVM_PREFIX)clang --target=x86_64-w64-mingw32
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
        $(BUILD)/libpthread.a $(BUILD)/libwinpthread.a

all: $(LIBS) $(DRIVER)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.cpp $(HEADERS) | $(BUILD)
	$(LIB_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/start/%.o: src/start/%.cpp $(HEADERS) | $(BUILD)
	mkdir -p $(@D)
	$(LIB_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/libntcrt_start.a: $(START_OBJS)
	rm -f $@
	ar rcs $@ $^

# The NTCRT_IMPORT / NTCRT_IMPORT_ONLY lines of the sources, read by every make and written only when they change:
# an edit of a function assembles no stub
$(BUILD)/exports.txt: FORCE | $(BUILD)
	sed -n -E 's/^NTCRT_IMPORT(_ONLY)?\(([A-Za-z0-9_]+), *([A-Za-z0-9_]+)\);.*/\1 \2 \3/p' $(SRCS) > $@.tmp
	if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

# An object for each name, see NtCRT.h. Only the stubs that Exports.awk writes are assembled, those of names that are
# gone are removed. The stubs written are removed when they fail to assemble, so that the next make writes them again.
$(BUILD)/exports.stamp: $(BUILD)/exports.txt src/Exports.awk
	rm -f $@
	mkdir -p $(BUILD)/exports
	awk -v dir=$(BUILD)/exports -v keep=$(BUILD)/exports.keep -f src/Exports.awk $< > $(BUILD)/exports.new
	cd $(BUILD)/exports && ls | grep -v -x -F -f ../exports.keep | xargs -r rm -f
	cd $(BUILD)/exports && xargs -r -P 8 -n 64 $(LIB_AS) -c < ../exports.new || { xargs rm -f < ../exports.new; exit 1; }
	touch $@

$(BUILD)/libntcrt.a: $(OBJS) $(BUILD)/exports.stamp
	rm -f $@
	ar rcs $@ $(OBJS)
	cd $(BUILD)/exports && ar rs ../libntcrt.a *.o

# libntcrt has the pthread functions (src/Pthread*.cpp). Empty libraries take the place of winpthreads for -lpthread
# and -lwinpthread, the directory of the build precedes that of the toolchain.
$(BUILD)/libpthread.a $(BUILD)/libwinpthread.a: | $(BUILD)
	rm -f $@
	ar rc $@

$(BUILD)/ntcrt.syms: $(BUILD)/libntcrt.a
	nm -g --defined-only $< | awk 'NF == 3 { print $$3 }' | sort -u > $@

# ntdll exports the classic C runtime, libntdll.a does not import it.
# Functions implemented by libntcrt.a are left out so they never come from ntdll.
$(BUILD)/libntdll_crt.a: src/ntdll_crt.txt $(BUILD)/ntcrt.syms
	printf 'LIBRARY ntdll.dll\nEXPORTS\n' > $(BUILD)/ntdll_crt.def
	awk 'NR == FNR { Defined[$$1] = 1; next } !($$1 in Defined)' $(BUILD)/ntcrt.syms src/ntdll_crt.txt >> $(BUILD)/ntdll_crt.def
	dlltool -d $(BUILD)/ntdll_crt.def -l $@

# The static (non-import) members of libmsvcrt.a, minus those defining a symbol of libntcrt.a
$(BUILD)/libmsvcrt_static.a: $(BUILD)/ntcrt.syms
	rm -rf $(BUILD)/msvcrt
	mkdir -p $(BUILD)/msvcrt
	cd $(BUILD)/msvcrt && ar x "$(MSVCRT_A)" $$(ar t "$(MSVCRT_A)" | grep '^lib64_libmsvcrt_')
	for o in $(BUILD)/msvcrt/*.o; do \
	    if nm -g --defined-only $$o | awk 'NF == 3 { print $$3 }' | grep -qxF -f $(BUILD)/ntcrt.syms; then rm $$o; fi; \
	done
	rm -f $@
	ar rcs $@ $(BUILD)/msvcrt/*.o

# Functions of libmingwex replaced by libntcrt: the printf of mingw-w64 by src/FormatNarrow.cpp and
# src/FormatWide.cpp, its scanf (and the __ms_v*scanf that call msvcrt) by src/Scanf.cpp, src/ScanfWide.cpp,
# src/StdioScanf.cpp and src/StdioWscanf.cpp, the strtod family of mingw-w64 by src/FloatParse.cpp, wcstoll,
# wcstoull, wcstoimax and wcstoumax by src/Integer.cpp, btowc and wctob by src/MultibyteChar.cpp, ftruncate64 by
# src/LowIOFile.cpp, sleep and usleep by src/Clock.cpp. libntcrt defines them under their names too, for lld.
WRAPS := $(foreach f,vfprintf vprintf vsprintf vsnprintf vasprintf fprintf printf sprintf snprintf asprintf \
                     vfwprintf vwprintf vswprintf vsnwprintf fwprintf wprintf swprintf snwprintf,--wrap=__mingw_$(f)) \
         $(foreach f,vfscanf vscanf vsscanf fscanf scanf sscanf vfwscanf vwscanf vswscanf fwscanf wscanf swscanf, \
                     --wrap=__mingw_$(f)) \
         $(foreach f,vfscanf vscanf vsscanf vfwscanf vwscanf vswscanf,--wrap=__ms_$(f)) \
         $(foreach f,strtof strtod strtold wcstof wcstod wcstold,--wrap=__mingw_$(f)) \
         $(foreach f,strtold wcstold wcstoll wcstoull wcstoimax wcstoumax,--wrap=$(f)) \
         --wrap=btowc --wrap=wctob --wrap=ftruncate64 --wrap=sleep --wrap=usleep

# Replace the msvcrt and kernel32 libraries, the group resolves the cycles between them. libmingwex precedes
# libntcrt, the math functions libntcrt calls are of ntdll. libntcrt_start replaces the pseudo relocator and _matherr
# of libmingw32, which would link the printf family into every program, and emutls of libgcc. libntcrt has the
# pthread functions, winpthreads is not linked. include/ (<threads.h>) is a system include directory of the programs.
# kernel32 follows the group: it gives only the functions a program calls that NtCRT does not have, -ntcrt-strict
# leaves it out. -Bstatic links libstdc++ and the others statically without -static. The specs of an installation
# (install) have no paths, its drivers pass them.
SPECS_LIBGCC := --start-group -lntcrt_start -lmingw32 -lgcc -lmingwex -lntcrt -lmsvcrt_static -lntdll_crt -lntdll \
                --end-group %{!ntcrt-strict:-lkernel32}

$(BUILD)/ntcrt.specs: Makefile | $(BUILD)
	printf '*cpp:\n+ -isystem %s\n\n' "$(CURDIR)/include" > $@
	printf '*link:\n+ -L%s -Bstatic %s\n\n' "$(CURDIR)/$(BUILD)" "$(WRAPS)" >> $@
	printf '*libgcc:\n%s\n\n' '$(SPECS_LIBGCC)' >> $@
	printf '*lib:\n%%{pg:-lgmon}\n\n' >> $@

$(BUILD)/install/ntcrt.specs: Makefile
	mkdir -p $(@D)
	printf '*link:\n+ -Bstatic %s\n\n' "$(WRAPS)" > $@
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
              $(addprefix -l,ntcrt_start mingw32 stdc++ gcc ntcrt mingwex msvcrt_static ntdll_crt ntdll)

$(BUILD)/ntcrt.cfg $(BUILD)/install/ntcrt.cfg: %/ntcrt.cfg: %/ntcrt-strict.cfg
	printf '@ntcrt-strict.cfg\n$$-lkernel32\n' > $@

$(BUILD)/ntcrt-strict.cfg: Makefile | $(BUILD)
	printf '%s\n' $(LLVM_FLAGS) -fuse-ld=lld -nodefaultlibs -static > $@
	printf '"%s"\n' "-isystem$(CURDIR)/include" >> $@
	printf '"%s"\n' "-L$(CURDIR)/$(BUILD)" >> $@
	printf '$$%s\n' $(NTCRT_LINK) >> $@

$(BUILD)/install/ntcrt-strict.cfg: Makefile
	mkdir -p $(@D)
	printf '%s\n' $(LLVM_FLAGS) -fuse-ld=lld -nodefaultlibs -static > $@
	printf '"%s"\n' "-isystem<CFGDIR>/../../include" "-L<CFGDIR>" >> $@
	printf '$$%s\n' $(NTCRT_LINK) >> $@

TEST_SRCS := $(wildcard test/upstream/*.cpp)

$(BUILD)/Test.exe: $(TEST_SRCS) $(LIBS) $(DRIVER)
	$(NTCRT_CXX) -std=c++23 -O2 -Iinclude -static -o $@ $(TEST_SRCS)

test-upstream: $(BUILD)/Test.exe
	objdump -p $< | grep 'DLL Name'
	./$<

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
	cd test/gofloat && NTCRT_FLOAT_DLL=$(CURDIR)/$< $(GO) test -count=1 -run Test . && \
	    for f in FuzzStrtod FuzzStrtodLong FuzzStrtold80 FuzzPrintfDouble FuzzPrintfLongDouble; do \
	        NTCRT_FLOAT_DLL=$(CURDIR)/$< $(GO) test -run XXX -fuzz "^$$f\$$" -fuzztime $(GO_FUZZTIME) . || exit 1; \
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
	$(NTCRT_CXX) -std=c++23 -O2 -static -pthread -o $@ $< $(PARKER_OBJ) -Wl,-Map=$(THREAD)/cxx.map

# exit.exe: the end of the process without exit (the last thread, ExitProcess), its status and output (a pipe)
test-thread: $(THREAD_TESTS)
	if grep -i winpthread $(THREAD)/cxx.map; then echo "winpthreads is linked"; exit 1; fi
	if grep 'libgcc\.a(emutls\.o)' $(THREAD)/cxx.map; then echo "emutls of libgcc is linked"; exit 1; fi
	for t in $(THREAD_TESTS); do \
	    if objdump -p $$t | grep 'DLL Name' | grep -v -q ntdll.dll; then echo "$$t imports more than ntdll.dll"; exit 1; fi; \
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
	test "$$(cat $(THREAD)/exit.txt)" = mingwthr && \
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

$(BUILD)/start/names.exe: test/start/names.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(NTCRT_CC) -O2 -static -o $@ $<

# Without -ntcrt-strict, the functions it imports from kernel32
$(BUILD)/start/kernel32.exe: test/start/kernel32.c $(LIBS) $(DRIVER)
	mkdir -p $(@D)
	$(KERNEL32_CC) -O2 -static -o $@ $<

test-start: $(BUILD)/start/reloc.exe $(BUILD)/start/matherr.exe $(BUILD)/start/matherr_custom.exe \
            $(BUILD)/start/argv.exe $(BUILD)/start/names.exe $(BUILD)/start/kernel32.exe
	cd $(BUILD)/start && ./reloc.exe
	$(BUILD)/start/argv.exe
	$(BUILD)/start/names.exe | grep -q '^names: ok$$'
	$(BUILD)/start/kernel32.exe | grep -q '^kernel32: ok$$'
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

test: test-upstream test-fuzz test-printf test-scanf test-console test-cxx test-thread test-start test-install

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
DRIVER_DIRS = -DNTCRT_GCC_DIR='L"$(dir $(shell cygpath -m $$($(CC) -print-libgcc-file-name)))../../../../bin/"'
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

install: $(INSTALL_DEPS)
	test -n "$(PREFIX)"
	$(call install-to,$(PREFIX))

# An installation in a directory with a space: C and C++ by the drivers without -static, an argument with quotes
# and spaces, -ntcrt-strict
INSTALL_TEST := $(BUILD)/install test

test-install: $(INSTALL_DEPS)
	rm -rf "$(INSTALL_TEST)"
	$(call install-to,$(INSTALL_TEST))
	"$(INSTALL_TEST)/bin/x86_64-w64-ntcrt-mingw32-$(firstword $(INSTALL_TOOLS)).exe" -O2 '-DTEXT="a \"b\" c\\"' \
	    -o "$(INSTALL_TEST)/hello.exe" test/start/install.c
	"$(INSTALL_TEST)/hello.exe" | tr -d '\r' | grep -q -x -F 'a "b" c\'
	"$(INSTALL_TEST)/bin/x86_64-w64-ntcrt-mingw32-$(word 2,$(INSTALL_TOOLS)).exe" -O2 -ntcrt-strict -x c++ \
	    -DTEXT='"c++"' -o "$(INSTALL_TEST)/hello++.exe" test/start/install.c
	"$(INSTALL_TEST)/hello++.exe" | tr -d '\r' | grep -q -x -F 'c++'
	for t in hello hello++; do \
	    if objdump -p "$(INSTALL_TEST)/$$t.exe" | grep 'DLL Name' | grep -v -q ntdll.dll; then \
	        echo "$$t.exe imports more than ntdll.dll"; exit 1; fi; \
	done
	@echo "install: ok"

clean:
	rm -rf $(BUILD)

.PHONY: FORCE all test test-upstream test-fuzz test-go test-float-corpus test-printf test-scanf test-console test-cxx test-thread \
        test-start test-install test-wine test-pthread size compare install clean
