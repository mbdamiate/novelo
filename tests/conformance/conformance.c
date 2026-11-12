/* Conformance runner (core §12.4): conformance [-k TEXT] [-v] FILE…
 *
 * Results are compared with equal?, never by printed text. Each case starts
 * from a fresh state, and afterwards no object and no position entry may
 * remain alive.
 */
#include "json.h"

#include "buffer.h"
#include "error.h"
#include "io.h"
#include "positions.h"
#include "printer.h"
#include "program.h"
#include "reader.h"
#include "value.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum Outcome { PASSED, FAILED, SKIPPED } Outcome;

typedef struct CaseResult {
    Outcome outcome;
    char message[512];
} CaseResult;

[[gnu::format(printf, 2, 3)]]
static Outcome failure(CaseResult *result, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(result->message, sizeof result->message, format, arguments);
    va_end(arguments);
    return result->outcome = FAILED;
}

static const char *string_member(const Json *test_case, const char *name) {
    const Json *member = json_member(test_case, name);
    return member && member->kind == JSON_STRING ? member->string : nullptr;
}

/* Reads a JSON string member as source text, keeping embedded NULs. */
static bool source_member(const Json *test_case, const char *name, const char **text, size_t *length) {
    const Json *member = json_member(test_case, name);
    if (!member || member->kind != JSON_STRING) return false;
    *text = member->string;
    *length = member->string_length;
    return true;
}

static Outcome read_or_fail(CaseResult *result, const char *source, size_t length, Value **datums) {
    LispReadError error;
    if (lisp_read(source, length, datums, &error)) return PASSED;
    return failure(result, "read failed: %s at %u:%u (%s)", lisp_read_error_name(error.code),
                   error.position.line, error.position.column, error.message);
}

/* ---- read ------------------------------------------------------------------------- */

static Outcome run_read(const Json *test_case, CaseResult *result) {
    const char *input;
    size_t input_length;
    if (!source_member(test_case, "input", &input, &input_length)) return failure(result, "no input");
    Value *datums;
    if (read_or_fail(result, input, input_length, &datums) != PASSED) return FAILED;

    Outcome outcome = PASSED;
    const char *equivalent;
    size_t equivalent_length;
    if (source_member(test_case, "equivalent", &equivalent, &equivalent_length)) {
        Value *expected;
        if (read_or_fail(result, equivalent, equivalent_length, &expected) != PASSED) {
            lisp_release(datums);
            return FAILED;
        }
        if (!lisp_equal(datums, expected)) outcome = failure(result, "not equal? to the equivalent text");
        lisp_release(expected);
    }

    const Json *types = json_member(test_case, "types");
    if (outcome == PASSED && types) {
        const Value *datum = datums;
        for (size_t index = 0; index < types->count; index++, datum = lisp_rest(datum)) {
            if (datum == LISP_EMPTY) {
                outcome = failure(result, "read %zu datums, expected %zu", index, types->count);
                break;
            }
            const char *actual = lisp_type_name(lisp_first(datum));
            if (strcmp(actual, types->items[index].string) != 0) {
                outcome = failure(result, "datum %zu is %s, expected %s", index, actual,
                                  types->items[index].string);
                break;
            }
        }
        if (outcome == PASSED && datum != LISP_EMPTY) {
            outcome = failure(result, "read more than %zu datums", types->count);
        }
    }
    lisp_release(datums);
    return outcome;
}

/* ---- read-error --------------------------------------------------------------------- */

static int hex_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

/* The bytes of `input`, or of `input_hex`; false if the case has neither. */
static bool case_input(const Json *test_case, LispBuffer *bytes) {
    const char *input;
    size_t input_length;
    const char *hex = string_member(test_case, "input_hex");
    if (source_member(test_case, "input", &input, &input_length)) {
        lisp_buffer_append(bytes, input, input_length);
    } else if (hex) {
        for (size_t index = 0; hex[index] && hex[index + 1]; index += 2) {
            lisp_buffer_append_byte(bytes, (char)(hex_value(hex[index]) * 16 + hex_value(hex[index + 1])));
        }
    } else {
        return false;
    }
    return true;
}

static Outcome run_read_error(const Json *test_case, CaseResult *result) {
    LispBuffer source;
    lisp_buffer_init(&source);
    if (!case_input(test_case, &source)) return failure(result, "no input");

    const char *expected_code = string_member(test_case, "error");
    const Json *expected_position = json_member(test_case, "position");
    int64_t expected_line = expected_position->items[0].integer;
    int64_t expected_column = expected_position->items[1].integer;

    Value *datums = nullptr;
    LispReadError error;
    bool read_succeeded = lisp_read(source.data ? source.data : "", source.length, &datums, &error);
    lisp_buffer_free(&source);
    if (read_succeeded) {
        lisp_release(datums);
        return failure(result, "read succeeded, expected %s", expected_code);
    }
    const char *actual_code = lisp_read_error_name(error.code);
    if (strcmp(actual_code, expected_code) != 0 || error.position.line != expected_line
        || error.position.column != expected_column) {
        return failure(result, "got %s at %u:%u, expected %s at %lld:%lld", actual_code,
                       error.position.line, error.position.column, expected_code,
                       (long long)expected_line, (long long)expected_column);
    }
    return PASSED;
}

/* ---- read-positions ------------------------------------------------------------------- */

static const Value *list_element(const Value *list, int64_t index) {
    if (!lisp_is_list(list)) return nullptr;
    for (int64_t step = 0; step < index && list != LISP_EMPTY; step++) list = lisp_rest(list);
    return list == LISP_EMPTY ? nullptr : lisp_first(list);
}

static Outcome run_read_positions(const Json *test_case, CaseResult *result) {
    const char *input;
    size_t input_length;
    if (!source_member(test_case, "input", &input, &input_length)) return failure(result, "no input");
    Value *datums;
    if (read_or_fail(result, input, input_length, &datums) != PASSED) return FAILED;

    Outcome outcome = PASSED;
    const Json *entries = json_member(test_case, "positions");
    for (size_t entry_index = 0; entry_index < entries->count && outcome == PASSED; entry_index++) {
        const Json *entry = &entries->items[entry_index];
        const Json *path = json_member(entry, "path");
        const Json *expected = json_member(entry, "position");
        const Value *node = datums;
        for (size_t step = 0; step < path->count && node; step++) {
            node = list_element(node, path->items[step].integer);
        }
        LispPosition position;
        if (!node) {
            outcome = failure(result, "path %zu does not exist", entry_index);
        } else if (!lisp_position_lookup(node, &position)) {
            outcome = failure(result, "path %zu has no position", entry_index);
        } else if (position.line != expected->items[0].integer
                   || position.column != expected->items[1].integer) {
            outcome = failure(result, "path %zu is at %u:%u, expected %lld:%lld", entry_index,
                              position.line, position.column, (long long)expected->items[0].integer,
                              (long long)expected->items[1].integer);
        }
    }
    lisp_release(datums);
    return outcome;
}

/* ---- eval ------------------------------------------------------------------------------ */

typedef struct EvalCall {
    const Json *test_case;
    CaseResult *result;
} EvalCall;

static void describe_value(LispBuffer *output, const Value *value) {
    lisp_print(output, value, LISP_PRINT_WRITE);
}

/* Runs on a thread with a large stack (see lisp_run_on_large_stack). */
static void run_eval_on_thread(void *argument) {
    EvalCall *call = argument;
    const Json *test_case = call->test_case;
    CaseResult *result = call->result;

    const char *program;
    size_t program_length;
    if (!source_member(test_case, "program", &program, &program_length)) {
        failure(result, "no program");
        return;
    }
    LispBuffer input; /* standard input: `input` or `input_hex`, empty if absent */
    lisp_buffer_init(&input);
    case_input(test_case, &input);

    /* §12.4: the program is main.novelo and the files are relative to its
     * directory, here the root of an in-memory file system. A path missing
     * from `files` cannot be opened. */
    const Json *files = json_member(test_case, "files");
    size_t file_count = files ? files->count : 0;
    LispMemoryFile *memory_files = calloc(file_count + 1, sizeof(LispMemoryFile));
    char **paths = calloc(file_count + 1, sizeof(char *));
    memory_files[0] = (LispMemoryFile){.path = "/main.novelo", .text = program, .length = program_length};
    for (size_t index = 0; index < file_count; index++) {
        size_t length = strlen(files->keys[index]) + 2;
        paths[index] = malloc(length);
        snprintf(paths[index], length, "/%s", files->keys[index]);
        memory_files[index + 1] = (LispMemoryFile){
            .path = paths[index],
            .text = files->items[index].string,
            .length = files->items[index].string_length,
        };
    }
    LispMemoryFiles file_system = {.files = memory_files, .count = file_count + 1};

    LispBuffer output;
    lisp_buffer_init(&output);
    LispTextInput text_input = {.text = input.data ? input.data : "", .length = input.length};
    LispStateOptions options = {
        .output = lisp_output_to_buffer(&output),
        .input = lisp_input_from_text(&text_input),
        .files = lisp_file_system_from_memory(&file_system),
        .working_directory = "/",
        .stack_limit = lisp_large_stack_limit(),
    };
    LispState *state = lisp_state_new(&options);
    Value *last_value = nullptr;
    LispReadError read_error;
    LispRunResult run = lisp_run_source(state, program, program_length, "main.novelo", &last_value, &read_error);

    const char *expected_value = string_member(test_case, "value");
    const char *expected_error = string_member(test_case, "error");
    const char *expected_output = string_member(test_case, "output");
    LispBuffer message;
    lisp_buffer_init(&message);

    if (run == LISP_RUN_READ_ERROR) {
        failure(result, "program has a read error: %s at %u:%u", lisp_read_error_name(read_error.code),
                read_error.position.line, read_error.position.column);
    } else if (expected_error) {
        Value *kind = run == LISP_RUN_ERROR ? lisp_error_kind(state) : nullptr;
        LispBuffer actual;
        lisp_buffer_init(&actual);
        if (kind) describe_value(&actual, kind);
        if (run == LISP_RUN_OK) {
            lisp_buffer_append_string(&message, "finished with ");
            describe_value(&message, last_value);
            failure(result, "%s, expected error %s", message.data, expected_error);
        } else if (!kind || strcmp(actual.data, expected_error) != 0) {
            lisp_describe_error(&message, state->error);
            failure(result, "error %s, expected %s", message.data, expected_error);
        }
        lisp_buffer_free(&actual);
    } else if (run == LISP_RUN_ERROR) {
        lisp_describe_error(&message, state->error);
        failure(result, "uncaught error %s", message.data);
    } else if (expected_value) {
        Value *expected;
        if (read_or_fail(result, expected_value, strlen(expected_value), &expected) == PASSED) {
            if (expected == LISP_EMPTY || !lisp_equal(last_value, lisp_first(expected))) {
                describe_value(&message, last_value);
                failure(result, "value %s, expected %s", message.data, expected_value);
            }
            lisp_release(expected);
        }
    }

    const char *actual_output = output.data ? output.data : "";
    if (result->outcome != FAILED && expected_output
        && (output.length != strlen(expected_output) || memcmp(actual_output, expected_output, output.length) != 0)) {
        failure(result, "output \"%s\", expected \"%s\"", actual_output, expected_output);
    }

    lisp_buffer_free(&message);
    lisp_release(last_value);
    lisp_state_free(state);
    lisp_buffer_free(&output);
    lisp_buffer_free(&input);
    for (size_t index = 0; index < file_count; index++) free(paths[index]);
    free(paths);
    free(memory_files);
}

static Outcome run_eval(const Json *test_case, CaseResult *result) {
    EvalCall call = {test_case, result};
    if (!lisp_run_on_large_stack(run_eval_on_thread, &call)) return failure(result, "cannot create a thread");
    return result->outcome;
}

/* ---- driver ------------------------------------------------------------------------------ */

static CaseResult run_case(const Json *test_case) {
    CaseResult result = {.outcome = PASSED};
    const char *kind = string_member(test_case, "kind");
    if (!kind) {
        failure(&result, "case without a kind");
    } else if (strcmp(kind, "read") == 0) {
        result.outcome = run_read(test_case, &result);
    } else if (strcmp(kind, "read-error") == 0) {
        result.outcome = run_read_error(test_case, &result);
    } else if (strcmp(kind, "read-positions") == 0) {
        result.outcome = run_read_positions(test_case, &result);
    } else if (strcmp(kind, "eval") == 0) {
        result.outcome = run_eval(test_case, &result);
    } else {
        failure(&result, "unknown kind %s", kind);
    }

    if (result.outcome != FAILED && (lisp_live_objects() != 0 || lisp_position_count() != 0)) {
        failure(&result, "leak: %zu live objects and %zu position entries after the case",
                lisp_live_objects(), lisp_position_count());
    }
    return result;
}

static char *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file) return nullptr;
    LispBuffer buffer;
    lisp_buffer_init(&buffer);
    char chunk[65536];
    size_t chunk_length;
    while ((chunk_length = fread(chunk, 1, sizeof chunk, file)) > 0) {
        lisp_buffer_append(&buffer, chunk, chunk_length);
    }
    fclose(file);
    return lisp_buffer_take(&buffer, length);
}

typedef struct Totals {
    int passed, failed, skipped;
} Totals;

static bool run_suite(const char *path, const char *filter, bool verbose, Totals *totals) {
    size_t length;
    char *text = read_file(path, &length);
    if (!text) {
        perror(path);
        return false;
    }
    Json document;
    char error_message[256];
    bool parsed = json_parse(text, length, &document, error_message, sizeof error_message);
    free(text);
    if (!parsed) {
        fprintf(stderr, "%s: %s\n", path, error_message);
        return false;
    }

    Totals suite = {};
    const Json *cases = json_member(&document, "cases");
    for (size_t index = 0; cases && index < cases->count; index++) {
        const Json *test_case = &cases->items[index];
        const char *identifier = string_member(test_case, "id");
        if (filter && !strstr(identifier, filter)) continue;
        CaseResult result = run_case(test_case);
        switch (result.outcome) {
        case PASSED:
            suite.passed++;
            if (verbose) printf("  ok    %s\n", identifier);
            break;
        case SKIPPED:
            suite.skipped++;
            if (verbose) printf("  skip  %s\n", identifier);
            break;
        case FAILED:
            suite.failed++;
            printf("  FAIL  %s: %s\n", identifier, result.message);
            break;
        }
    }
    json_free(&document);
    printf("%s: %d passed, %d failed, %d skipped\n", path, suite.passed, suite.failed, suite.skipped);
    totals->passed += suite.passed;
    totals->failed += suite.failed;
    totals->skipped += suite.skipped;
    return true;
}

int main(int argument_count, char **arguments) {
    const char *filter = nullptr;
    bool verbose = false;
    Totals totals = {};
    int suite_count = 0;
    for (int index = 1; index < argument_count; index++) {
        if (strcmp(arguments[index], "-k") == 0 && index + 1 < argument_count) {
            filter = arguments[++index];
        } else if (strcmp(arguments[index], "-v") == 0) {
            verbose = true;
        } else if (arguments[index][0] == '-') {
            fprintf(stderr, "usage: %s [-k TEXT] [-v] FILE...\n", arguments[0]);
            return 2;
        } else {
            if (!run_suite(arguments[index], filter, verbose, &totals)) return 2;
            suite_count++;
        }
    }
    if (suite_count == 0) {
        fprintf(stderr, "usage: %s [-k TEXT] [-v] FILE...\n", arguments[0]);
        return 2;
    }
    return totals.failed ? 1 : 0;
}
