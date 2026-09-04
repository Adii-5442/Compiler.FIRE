# Fire compiler — build, test and install.
#
# Only a C++20 compiler is required. nasm and ld are needed at run time by
# `fire build`, not to build the compiler itself.
#
#   make            build bin/fire
#   make test       build and run the whole test suite
#   make examples   run every program in examples/
#   make debug      build with -O0 -g and the sanitizers
#   make install    copy bin/fire to $(PREFIX)/bin  (default /usr/local)
#   make clean

CXX      ?= c++
PREFIX   ?= /usr/local

WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
            -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference
CXXFLAGS ?= -std=c++20 -O2
CXXFLAGS += $(WARNINGS) -Iinclude
LDFLAGS  ?=

SOURCES  := $(wildcard src/*.cpp)
OBJECTS  := $(patsubst src/%.cpp,build/obj/%.o,$(SOURCES))
DEPS     := $(OBJECTS:.o=.d)
BINARY   := bin/fire

# Every source except the CLI entry point, so tests can link the library.
LIB_OBJECTS := $(filter-out build/obj/main.o,$(OBJECTS))

TEST_SOURCES := $(wildcard tests/unit/*.cpp)
TEST_BINARY  := build/fire-tests

.PHONY: all test unit e2e examples debug clean install uninstall format help

all: $(BINARY)

$(BINARY): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "built $@"

build/obj/%.o: src/%.cpp
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

# -- variants ----------------------------------------------------------------

debug:
	@$(MAKE) --no-print-directory \
	  CXXFLAGS="-std=c++20 -O0 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined $(WARNINGS) -Iinclude" \
	  LDFLAGS="-fsanitize=address,undefined" all

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
	@sed -n '1,14p' Makefile
