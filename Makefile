# lisp-min — build, tests and sanitizers.
#
#   make              build the library objects (and, from phase 5, the CLI)
#   make test         unit tests + conformance suites
#   make unit         unit tests only
#   make conformance  conformance suites only
#   make asan         everything above, rebuilt with AddressSanitizer + UBSan in build/asan
#   make clean

CC      ?= cc
BUILD   ?= build
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c23 -Wall -Wextra -Wpedantic -Werror -MMD -MP
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

.PHONY: all test unit conformance asan clean

all: $(LIB_OBJ)

test: unit conformance

unit: $(UNIT_BIN)
	./$(UNIT_BIN)

conformance: $(CONFORMANCE_BIN)
	./$(CONFORMANCE_BIN) conformance.json
	./$(CONFORMANCE_BIN) stdlib-conformance.json

asan:
	$(MAKE) test BUILD=build/asan SANITIZE=1

clean:
	rm -rf build

$(UNIT_BIN): $(UNIT_OBJ) $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CONFORMANCE_BIN): $(CONFORMANCE_OBJ) $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/tests/unit/%.o: tests/unit/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/tests/conformance/%.o: tests/conformance/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

-include $(LIB_OBJ:.o=.d) $(UNIT_OBJ:.o=.d) $(CONFORMANCE_OBJ:.o=.d)
