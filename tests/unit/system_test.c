/* core §9 — the functions on the system: the real environment, clocks and
 * entropy, and what the conformance cases cannot give (a value that is not
 * UTF-8, failures of the system). */
#include "test.h"

#include "error.h"
#include "io.h"
#include "program.h"

#include <stdlib.h>

/* Runs a program over the system given; returns its value, or nullptr with
 * *kind set to the :kind of its error and *values to its :values. */
static Value *run_with(LispSystem system, const char *program, Value **kind, Value **values) {
    LispBuffer output;
    lisp_buffer_init(&output);
    LispStateOptions options = {.output = lisp_output_to_buffer(&output), .system = system};
    LispState *state = lisp_state_new(&options);
    Value *value = nullptr;
    LispReadError read_error;
    LispRunResult result = lisp_run_source(state, program, strlen(program), nullptr, &value, &read_error);
    *kind = nullptr;
    *values = nullptr;
    if (result == LISP_RUN_ERROR) {
        *kind = lisp_error_kind(state); /* keywords are immortal */
        *values = lisp_retain(lisp_map_get(state->error, lisp_keyword_cstr("values")));
    }
    lisp_state_free(state);
    lisp_buffer_free(&output);
    return value;
}

static void t_real_system(void) {
    LispSystem system = lisp_system_real();
    int64_t first, second, now, seed;
    CHECK(system.monotonic_time(system.context, &first));
    CHECK(system.monotonic_time(system.context, &second));
    CHECK(second >= first);
    CHECK(system.current_time(system.context, &now));
    CHECK(now > INT64_C(1700000000000)); /* after November 2023 */
    CHECK(system.random_seed(system.context, &seed));
    CHECK(seed >= 0);
    CHECK(system.environment_variable(system.context, "NOVELO_SURELY_NOT_SET_42") == nullptr);
    setenv("NOVELO_UNIT_TEST", "set", 1);
    const char *value = system.environment_variable(system.context, "NOVELO_UNIT_TEST");
    CHECK(value != nullptr && strcmp(value, "set") == 0);

    /* the state uses the real system when the options give none */
    Value *kind, *values;
    Value *result = run_with((LispSystem){}, "(import io)\n(environment-variable! \"NOVELO_UNIT_TEST\")", &kind, &values);
    CHECK(result != nullptr);
    if (result) CHECK_EQUAL(result, "\"set\"");
    lisp_release(result);
    unsetenv("NOVELO_UNIT_TEST");
}

static void t_fixed_system(void) {
    LispFixedSystem fixed = {.current_time = 9};
    LispSystem system = lisp_system_fixed(&fixed);
    int64_t value;
    CHECK(system.monotonic_time(system.context, &value));
    CHECK_INT(value, 0);
    CHECK(system.random_seed(system.context, &value));
    CHECK_INT(value, 0);
    CHECK(system.current_time(system.context, &value));
    CHECK_INT(value, 9);
    CHECK(system.environment_variable(system.context, "ANY") == nullptr);
}

static void t_invalid_value(void) {
    LispVariable variables[] = {{"BROKEN", "ok\xff"}};
    LispFixedSystem fixed = {.variables = variables, .variable_count = 1};
    Value *kind, *values;
    Value *value = run_with(lisp_system_fixed(&fixed), "(import io)\n(environment-variable! \"BROKEN\")", &kind, &values);
    CHECK(value == nullptr);
    CHECK(kind == lisp_keyword_cstr("io-error"));
    CHECK(values != nullptr);
    if (values) CHECK_EQUAL(values, "(:invalid-utf8 \"BROKEN\")");
    lisp_release(values);
}

/* A system whose every operation fails: io raises :io-error (:failed). */
static const char *no_variable(void *, const char *) { return nullptr; }
static bool fail_integer(void *, int64_t *) { return false; }

static void t_system_failures(void) {
    LispSystem broken = {no_variable, fail_integer, fail_integer, fail_integer, nullptr};
    const char *programs[] = {
        "(import io)\n(current-time!)",
        "(import io)\n(monotonic-time!)",
        "(import io)\n(random-seed!)",
    };
    for (size_t index = 0; index < sizeof programs / sizeof programs[0]; index++) {
        Value *kind, *values;
        Value *value = run_with(broken, programs[index], &kind, &values);
        CHECK(value == nullptr);
        CHECK(kind == lisp_keyword_cstr("io-error"));
        if (values) CHECK_EQUAL(values, "(:failed)");
        lisp_release(values);
    }
}

const TestSuite system_suite = {"system", (const TestCase[]){
    {"the real system", t_real_system},
    {"the fixed system", t_fixed_system},
    {"a value that is not UTF-8", t_invalid_value},
    {"failures of the system", t_system_failures},
    {},
}};
