# Fire compiler — build, test and install.
#
# Only a C++20 compiler is required. nasm and ld are needed at run time by
# `fire build`, not to build the compiler itself.
#
#   make               build bin/fire
#   make test          build and run the whole test suite
#   make examples      run every program in examples/
#   make bench         time the benchmarks on both backends
#   make debug         build with -O0 -g and the sanitizers
#   make SANITIZE=1 test
#                      run the suite under ASan and UBSan
#   make install       copy bin/fire to $(PREFIX)/bin  (default /usr/local)
#   make clean

CXX      ?= c++
PREFIX   ?= /usr/local
SANITIZE ?= 0

WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
            -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference

# Sanitized and optimised objects live in separate directories, so switching
# between them rebuilds rather than linking a mixture of the two.
ifeq ($(SANITIZE),1)
    BUILD_DIR := build/sanitize
    CXXFLAGS  := -std=c++20 -O0 -g3 -fno-omit-frame-pointer \
                 -fsanitize=address,undefined -fno-sanitize-recover=undefined
    LDFLAGS   += -fsanitize=address,undefined
else
    BUILD_DIR := build/release
    CXXFLAGS  ?= -std=c++20 -O2
endif
CXXFLAGS += $(WARNINGS) -Iinclude

SOURCES  := $(wildcard src/*.cpp)
OBJECTS  := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS     := $(OBJECTS:.o=.d)
BINARY   := bin/fire

# Every source except the CLI entry point, so the tests can link the library.
LIB_OBJECTS := $(filter-out $(BUILD_DIR)/main.o,$(OBJECTS))

TEST_SOURCES := $(wildcard tests/unit/*.cpp)
TEST_BINARY  := $(BUILD_DIR)/fire-tests

.PHONY: all test unit e2e examples bench debug clean install uninstall format help

all: $(BINARY)

$(BINARY): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "built $@"

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

# -- tests -------------------------------------------------------------------

$(TEST_BINARY): $(TEST_SOURCES) $(LIB_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Itests $(TEST_SOURCES) $(LIB_OBJECTS) -o $@ $(LDFLAGS)

unit: $(TEST_BINARY)
	@$(TEST_BINARY)

e2e: $(BINARY)
	@tests/run_e2e.sh

test: unit e2e
	@echo "all tests passed"

examples: $(BINARY)
	@tests/run_examples.sh

bench: $(BINARY)
	@tools/bench.sh

# -- variants ----------------------------------------------------------------

# A convenience wrapper; `make SANITIZE=1 <target>` works for every target.
debug:
	@$(MAKE) --no-print-directory SANITIZE=1 all

# -- housekeeping ------------------------------------------------------------

install: $(BINARY)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BINARY) $(DESTDIR)$(PREFIX)/bin/fire
	@echo "installed $(DESTDIR)$(PREFIX)/bin/fire"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/fire

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
	clang-format -i $(SOURCES) include/fire/*.hpp $(TEST_SOURCES)

clean:
	rm -rf build bin
	@echo "cleaned"

help:
	@sed -n '1,16p' Makefile
