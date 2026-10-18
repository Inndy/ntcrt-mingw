# NtCRT for MinGW-w64 (x64): link C/C++ programs that import ntdll.dll only
#
#   make            build build/libntcrt.a, the support libraries and build/ntcrt.specs
#   make test       build and run test/upstream, test/fuzz, test/console and test/cxx
#   g++ -static -specs=build/ntcrt.specs ...

CXX      ?= g++
ifeq ($(origin CC),default)
CC       := gcc
endif
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra
CXXFLAGS += -Iinclude -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti

BUILD    := build
SRCS     := $(wildcard src/*.cpp)
OBJS     := $(SRCS:src/%.cpp=$(BUILD)/%.o)
# Replacements of libmingw32 members, linked before it
START_SRCS := $(wildcard src/start/*.cpp)
START_OBJS := $(START_SRCS:src/start/%.cpp=$(BUILD)/start/%.o)
HEADERS  := src/NtCRT.h $(wildcard include/KNSoft/NDK/*) $(wildcard polyfill/*.inl)
MSVCRT_A := $(shell $(CXX) -print-file-name=libmsvcrt.a)

LIBS := $(BUILD)/libntcrt.a $(BUILD)/libntcrt_start.a $(BUILD)/libntdll_crt.a $(BUILD)/libmsvcrt_static.a

all: $(LIBS) $(BUILD)/ntcrt.specs

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/start/%.o: src/start/%.cpp $(HEADERS) | $(BUILD)
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/libntcrt_start.a: $(START_OBJS)
	rm -f $@
	ar rcs $@ $^

# An object for each NTCRT_IMPORT / NTCRT_IMPORT_ONLY name, see NtCRT.h
$(BUILD)/exports.stamp: $(SRCS) src/Exports.awk | $(BUILD)
	rm -rf $(BUILD)/exports
	mkdir -p $(BUILD)/exports
	sed -n -E 's/^NTCRT_IMPORT(_ONLY)?\(([A-Za-z0-9_]+), *([A-Za-z0-9_]+)\);.*/\1 \2 \3/p' $(SRCS) | \
	    awk -v dir=$(BUILD)/exports -f src/Exports.awk
	cd $(BUILD)/exports && ls *.s | xargs -P 8 -n 64 $(CXX) -c
	touch $@

$(BUILD)/libntcrt.a: $(OBJS) $(BUILD)/exports.stamp
	rm -f $@
	ar rcs $@ $(OBJS)
	cd $(BUILD)/exports && ar rs ../libntcrt.a *.o

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

# printf of mingw-w64 wrapped by src/Format.cpp, btowc and wctob of libmingwex by src/Multibyte.cpp, ftruncate64
# of libmingwex by src/LowIO.cpp
WRAPS := $(foreach f,vfprintf vprintf vsprintf vsnprintf vasprintf fprintf printf sprintf snprintf asprintf \
                     vfwprintf vwprintf vswprintf vsnwprintf fwprintf wprintf swprintf snwprintf,--wrap=__mingw_$(f)) \
         --wrap=btowc --wrap=wctob --wrap=ftruncate64

# Replace the msvcrt and kernel32 libraries, the group resolves the cycles between them. libmingwex precedes
# libntcrt, the math functions libntcrt calls are of ntdll. libntcrt_start replaces the pseudo relocator and
# _matherr of libmingw32, which would link the printf family into every program.
$(BUILD)/ntcrt.specs: Makefile | $(BUILD)
	printf '*link:\n+ -L%s %s\n\n' "$(CURDIR)/$(BUILD)" "$(WRAPS)" > $@
	printf '*libgcc:\n--start-group -lntcrt_start -lmingw32 -lgcc -lmingwex -lpthread -lntcrt -lmsvcrt_static -lntdll_crt -lntdll --end-group\n\n' >> $@
	printf '*lib:\n%%{pg:-lgmon}\n\n' >> $@

TEST_SRCS := $(wildcard test/upstream/*.cpp)

$(BUILD)/Test.exe: $(TEST_SRCS) $(LIBS) $(BUILD)/ntcrt.specs
	$(CXX) -std=c++23 -O2 -Iinclude -static -specs=$(BUILD)/ntcrt.specs -o $@ $(TEST_SRCS)

test-upstream: $(BUILD)/Test.exe
	objdump -p $< | grep 'DLL Name'
	./$<

# Differential fuzz of the UTF-8 codec against kernel32, FUZZ_ITERATIONS=n for a longer run
FUZZ_ITERATIONS ?= 200000

$(BUILD)/fuzz/utf8.exe: test/fuzz/utf8.c $(LIBS) $(BUILD)/ntcrt.specs
	mkdir -p $(@D)
	$(CC) -std=c17 -O2 -static -specs=$(BUILD)/ntcrt.specs -o $@ $<

test-fuzz: $(BUILD)/fuzz/utf8.exe
	./$< $(FUZZ_ITERATIONS)

# Console reads and writes in a pseudo console, conpty.exe is built with the default runtime
$(BUILD)/console/child.exe: test/console/child.c $(LIBS) $(BUILD)/ntcrt.specs
	mkdir -p $(@D)
	$(CC) -std=c17 -O2 -static -specs=$(BUILD)/ntcrt.specs -o $@ $< -lkernel32

$(BUILD)/console/conpty.exe: test/console/conpty.c
	mkdir -p $(@D)
	$(CC) -std=c17 -O2 -o $@ $<

test-console: $(BUILD)/console/conpty.exe $(BUILD)/console/child.exe
	./$< $(BUILD)/console/child.exe

# libstdc++ on NtCRT
$(BUILD)/cxx/stdcxx.exe: test/cxx/stdcxx.cpp $(LIBS) $(BUILD)/ntcrt.specs
	mkdir -p $(@D)
	$(CXX) -std=c++23 -O2 -Wno-deprecated-declarations -static -specs=$(BUILD)/ntcrt.specs -o $@ $<

test-cxx: $(BUILD)/cxx/stdcxx.exe
	./$<

# libntcrt_start: pseudo relocations of data auto-imported from a DLL (built with the default runtime), _matherr
$(BUILD)/start/data.dll: test/start/data.c
	mkdir -p $(@D)
	$(CC) -O2 -shared -o $@ $< -Wl,--out-implib,$(BUILD)/start/libdata.a

$(BUILD)/start/reloc.exe: test/start/reloc.c $(BUILD)/start/data.dll $(LIBS) $(BUILD)/ntcrt.specs
	$(CC) -O2 -static -specs=$(BUILD)/ntcrt.specs -o $@ $< -L$(BUILD)/start -ldata

$(BUILD)/start/matherr.exe $(BUILD)/start/matherr_custom.exe: test/start/matherr.c $(LIBS) $(BUILD)/ntcrt.specs
	mkdir -p $(@D)
	$(CC) -O2 -w -static -specs=$(BUILD)/ntcrt.specs -o $(BUILD)/start/matherr.exe $<
	$(CC) -O2 -w -static -specs=$(BUILD)/ntcrt.specs -DCUSTOM -o $(BUILD)/start/matherr_custom.exe $<

test-start: $(BUILD)/start/reloc.exe $(BUILD)/start/matherr.exe $(BUILD)/start/matherr_custom.exe
	cd $(BUILD)/start && ./reloc.exe
	$(BUILD)/start/matherr.exe 2>&1 | grep -q '^_matherr(): Argument domain error (DOMAIN) in log()$$'
	$(BUILD)/start/matherr_custom.exe | grep -q '^custom _matherr type=1 name=log$$'
	@echo "start: ok"

test: test-upstream test-fuzz test-console test-cxx test-start

# Size of small programs, stripped. ld cannot drop unused functions of x64 PE (.pdata keeps them), so the size
# depends on which objects of libntcrt.a a program links.
SIZE_SRCS := $(wildcard test/size/*.c test/size/*.cpp)

size: $(LIBS) $(BUILD)/ntcrt.specs
	@mkdir -p $(BUILD)/size
	@for f in $(SIZE_SRCS); do \
	    n=$$(basename $${f%.*}); c=$(CC); case $$f in *.cpp) c=$(CXX);; esac; \
	    $$c -O2 -s -static -specs=$(BUILD)/ntcrt.specs -o $(BUILD)/size/$$n.exe $$f && \
	    printf '%-10s %8d\n' $$n $$(wc -c < $(BUILD)/size/$$n.exe); \
	done

# Compare with UCRT, needs cl of Visual Studio
compare: $(LIBS) $(BUILD)/ntcrt.specs
	sh test/compare/run.sh

clean:
	rm -rf $(BUILD)

.PHONY: all test test-upstream test-fuzz test-console test-cxx test-start size compare clean
