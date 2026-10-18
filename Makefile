# NtCRT for MinGW-w64 (x64): link C/C++ programs that import ntdll.dll only
#
#   make            build build/libntcrt.a, the support libraries and build/ntcrt.specs
#   make test       build and run test/upstream
#   g++ -static -specs=build/ntcrt.specs ...

CXX      ?= g++
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra
CXXFLAGS += -Iinclude -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti

BUILD    := build
SRCS     := $(wildcard src/*.cpp)
OBJS     := $(SRCS:src/%.cpp=$(BUILD)/%.o)
HEADERS  := src/NtCRT.h $(wildcard include/KNSoft/NDK/*) $(wildcard polyfill/*.inl)
MSVCRT_A := $(shell $(CXX) -print-file-name=libmsvcrt.a)

LIBS := $(BUILD)/libntcrt.a $(BUILD)/libntdll_crt.a $(BUILD)/libmsvcrt_static.a

all: $(LIBS) $(BUILD)/ntcrt.specs

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

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

# Replace the msvcrt and kernel32 libraries, the group resolves the cycles between them
$(BUILD)/ntcrt.specs: Makefile | $(BUILD)
	printf '*link:\n+ -L%s\n\n' "$(CURDIR)/$(BUILD)" > $@
	printf '*libgcc:\n--start-group -lmingw32 -lgcc -lmingwex -lpthread -lntcrt -lmsvcrt_static -lntdll_crt -lntdll --end-group\n\n' >> $@
	printf '*lib:\n%%{pg:-lgmon}\n\n' >> $@

TEST_SRCS := $(wildcard test/upstream/*.cpp)

$(BUILD)/Test.exe: $(TEST_SRCS) $(LIBS) $(BUILD)/ntcrt.specs
	$(CXX) -std=c++23 -O2 -Iinclude -static -specs=$(BUILD)/ntcrt.specs -o $@ $(TEST_SRCS)

test: $(BUILD)/Test.exe
	objdump -p $< | grep 'DLL Name'
	./$<

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
