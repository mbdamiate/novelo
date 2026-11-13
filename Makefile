# Novelo — build, tests and sanitizers.
#
#   make              build the interpreter, build/novelo
#   make test         unit tests + conformance suites + examples + command line
#   make unit         unit tests only
#   make conformance  conformance suites only
#   make examples     run examples/*/main.nvl with the interpreter and check their output
#   make cli          check the command line: exit codes and messages
#   make asan         everything above, rebuilt with AddressSanitizer + UBSan in build/asan
#   make coverage     everything above, rebuilt with clang's coverage in build/coverage, then a report
#   make stress       the programs of tests/stress at full size: volume, depth, constant memory
#   make asan-stress  the same at a hundredth of the size, with AddressSanitizer + UBSan
#   make clean

CC      ?= cc
BUILD   ?= build
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c23 -Wall -Wextra -Wpedantic -Werror -MMD -MP
# POSIX.1-2008 (getcwd, isatty, mkdtemp, mkstemp, pthreads): strict -std=c23
# hides them on glibc unless asked for. On macOS, _POSIX_C_SOURCE alone hides
# mkdtemp, so _DARWIN_C_SOURCE restores the full set; glibc ignores it.
CFLAGS  += -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE
# libm: sqrt (core §9) on glibc.
LDFLAGS += -pthread -lm

ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
LDFLAGS += -fsanitize=address,undefined
endif

ifeq ($(COVERAGE),1)
CFLAGS  += -fprofile-instr-generate -fcoverage-mapping
LDFLAGS += -fprofile-instr-generate
endif

# llvm-cov and llvm-profdata ship with Xcode; elsewhere they are on the PATH.
LLVM_COV      ?= $(shell xcrun -f llvm-cov 2>/dev/null || command -v llvm-cov)
LLVM_PROFDATA ?= $(shell xcrun -f llvm-profdata 2>/dev/null || command -v llvm-profdata)
COVERAGE_DIR  := build/coverage

LIB_SRC  := $(filter-out src/main.c,$(wildcard src/*.c))
LIB_OBJ  := $(LIB_SRC:src/%.c=$(BUILD)/src/%.o)
UNIT_SRC := $(wildcard tests/unit/*.c)
UNIT_OBJ := $(UNIT_SRC:tests/unit/%.c=$(BUILD)/tests/unit/%.o)
UNIT_BIN := $(BUILD)/unit-tests
CONFORMANCE_SRC := $(wildcard tests/conformance/*.c)
CONFORMANCE_OBJ := $(CONFORMANCE_SRC:tests/conformance/%.c=$(BUILD)/tests/conformance/%.o)
CONFORMANCE_BIN := $(BUILD)/conformance
STRESS_SRC := tests/stress/stress.c
STRESS_OBJ := $(BUILD)/tests/stress/stress.o
STRESS_BIN := $(BUILD)/stress
CLI_BIN := $(BUILD)/novelo

.PHONY: all test unit conformance examples cli asan coverage stress asan-stress clean

all: $(CLI_BIN)

test: unit conformance examples cli

unit: $(UNIT_BIN)
	./$(UNIT_BIN)

conformance: $(CONFORMANCE_BIN)
	./$(CONFORMANCE_BIN) conformance.json stdlib-conformance.json

examples: $(CLI_BIN)
	tests/examples.sh $(CLI_BIN)

cli: $(CLI_BIN)
	tests/cli.sh $(CLI_BIN)

asan:
	$(MAKE) test BUILD=build/asan SANITIZE=1

stress: $(STRESS_BIN)
	./$(STRESS_BIN) $(STRESS_FLAGS) tests/stress

asan-stress:
	$(MAKE) stress BUILD=build/asan SANITIZE=1 STRESS_FLAGS=--quick

# Every process writes its own profile (%p): the unit runner forks per test.
coverage:
	rm -rf $(COVERAGE_DIR)/profiles
	LLVM_PROFILE_FILE=$(CURDIR)/$(COVERAGE_DIR)/profiles/%p.profraw $(MAKE) test BUILD=$(COVERAGE_DIR) COVERAGE=1
	$(LLVM_PROFDATA) merge -sparse $(COVERAGE_DIR)/profiles/*.profraw -o $(COVERAGE_DIR)/merged.profdata
	$(LLVM_COV) show -format=html -output-dir=$(COVERAGE_DIR)/html -instr-profile=$(COVERAGE_DIR)/merged.profdata \
	    $(COVERAGE_DIR)/novelo -object $(COVERAGE_DIR)/unit-tests -object $(COVERAGE_DIR)/conformance $(LIB_SRC) src/main.c
	$(LLVM_COV) report -instr-profile=$(COVERAGE_DIR)/merged.profdata \
	    $(COVERAGE_DIR)/novelo -object $(COVERAGE_DIR)/unit-tests -object $(COVERAGE_DIR)/conformance $(LIB_SRC) src/main.c

clean:
	rm -rf build

$(CLI_BIN): $(BUILD)/src/main.o $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(UNIT_BIN): $(UNIT_OBJ) $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CONFORMANCE_BIN): $(CONFORMANCE_OBJ) $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(STRESS_BIN): $(STRESS_OBJ) $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# The standard library document read by import (stdlib §4); NOVELO_STDLIB overrides it.
$(BUILD)/src/library.o: CFLAGS += -DLISP_STDLIB_DEFAULT='"$(CURDIR)/standard-library.md"'

$(BUILD)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/tests/unit/%.o: tests/unit/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/tests/conformance/%.o: tests/conformance/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/tests/stress/%.o: tests/stress/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

-include $(BUILD)/src/main.d $(LIB_OBJ:.o=.d) $(UNIT_OBJ:.o=.d) $(CONFORMANCE_OBJ:.o=.d) $(STRESS_OBJ:.o=.d)
