# Novelo — build, tests and sanitizers.
#
#   make              build the interpreter, build/novelo
#   make test         unit tests + conformance suites + examples
#   make unit         unit tests only
#   make conformance  conformance suites only
#   make examples     run examples/*.nvl with the interpreter and check their output
#   make asan         everything above, rebuilt with AddressSanitizer + UBSan in build/asan
#   make clean

CC      ?= cc
BUILD   ?= build
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c23 -Wall -Wextra -Wpedantic -Werror -MMD -MP
# POSIX.1-2008 (getcwd, isatty, mkdtemp, mkstemp, pthreads): strict -std=c23
# hides them on glibc unless asked for. On macOS, _POSIX_C_SOURCE alone hides
# mkdtemp, so _DARWIN_C_SOURCE restores the full set; glibc ignores it.
CFLAGS  += -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE
LDFLAGS += -pthread

ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
LDFLAGS += -fsanitize=address,undefined
endif

LIB_SRC  := $(filter-out src/main.c,$(wildcard src/*.c))
LIB_OBJ  := $(LIB_SRC:src/%.c=$(BUILD)/src/%.o)
UNIT_SRC := $(wildcard tests/unit/*.c)
UNIT_OBJ := $(UNIT_SRC:tests/unit/%.c=$(BUILD)/tests/unit/%.o)
UNIT_BIN := $(BUILD)/unit-tests
CONFORMANCE_SRC := $(wildcard tests/conformance/*.c)
CONFORMANCE_OBJ := $(CONFORMANCE_SRC:tests/conformance/%.c=$(BUILD)/tests/conformance/%.o)
CONFORMANCE_BIN := $(BUILD)/conformance
CLI_BIN := $(BUILD)/novelo

.PHONY: all test unit conformance examples asan clean

all: $(CLI_BIN)

test: unit conformance examples

unit: $(UNIT_BIN)
	./$(UNIT_BIN)

conformance: $(CONFORMANCE_BIN)
	./$(CONFORMANCE_BIN) conformance.json stdlib-conformance.json

examples: $(CLI_BIN)
	tests/examples.sh $(CLI_BIN)

asan:
	$(MAKE) test BUILD=build/asan SANITIZE=1

clean:
	rm -rf build

$(CLI_BIN): $(BUILD)/src/main.o $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(UNIT_BIN): $(UNIT_OBJ) $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CONFORMANCE_BIN): $(CONFORMANCE_OBJ) $(LIB_OBJ)
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

-include $(BUILD)/src/main.d $(LIB_OBJ:.o=.d) $(UNIT_OBJ:.o=.d) $(CONFORMANCE_OBJ:.o=.d)
