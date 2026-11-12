/* Conformance runner (core §12.4): conformance [-k TEXT] [-v] [--stdlib] FILE…
 *
 * Results are compared with equal?, never by printed text. Each case starts
 * from a fresh state, and afterwards no object and no position entry may
 * remain alive.
 */
#include "json.h"

#include "buffer.h"
#include "positions.h"
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
    LmReadError error;
    if (lm_read(source, length, datums, &error)) return PASSED;
    return failure(result, "read failed: %s at %u:%u (%s)", lm_read_error_name(error.code),
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
            lm_release(datums);
            return FAILED;
        }
        if (!lm_equal(datums, expected)) outcome = failure(result, "not equal? to the equivalent text");
        lm_release(expected);
    }

    const Json *types = json_member(test_case, "types");
    if (outcome == PASSED && types) {
        const Value *datum = datums;
        for (size_t index = 0; index < types->count; index++, datum = lm_rest(datum)) {
            if (datum == LM_EMPTY) {
                outcome = failure(result, "read %zu datums, expected %zu", index, types->count);
                break;
            }
            const char *actual = lm_type_name(lm_first(datum));
            if (strcmp(actual, types->items[index].string) != 0) {
                outcome = failure(result, "datum %zu is %s, expected %s", index, actual,
                                  types->items[index].string);
                break;
            }
        }
        if (outcome == PASSED && datum != LM_EMPTY) {
            outcome = failure(result, "read more than %zu datums", types->count);
        }
    }
    lm_release(datums);
    return outcome;
}

/* ---- read-error --------------------------------------------------------------------- */

static int hex_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

static Outcome run_read_error(const Json *test_case, CaseResult *result) {
    LmBuffer source;
    lm_buffer_init(&source);
    const char *input;
    size_t input_length;
    const char *hex = string_member(test_case, "input_hex");
    if (source_member(test_case, "input", &input, &input_length)) {
        lm_buffer_append(&source, input, input_length);
    } else if (hex) {
        for (size_t index = 0; hex[index] && hex[index + 1]; index += 2) {
            lm_buffer_append_byte(&source, (char)(hex_value(hex[index]) * 16 + hex_value(hex[index + 1])));
        }
    } else {
        return failure(result, "no input");
    }

    const char *expected_code = string_member(test_case, "error");
    const Json *expected_position = json_member(test_case, "position");
    int64_t expected_line = expected_position->items[0].integer;
    int64_t expected_column = expected_position->items[1].integer;

    Value *datums = nullptr;
    LmReadError error;
    bool read_succeeded = lm_read(source.data ? source.data : "", source.length, &datums, &error);
    lm_buffer_free(&source);
    if (read_succeeded) {
        lm_release(datums);
        return failure(result, "read succeeded, expected %s", expected_code);
    }
    const char *actual_code = lm_read_error_name(error.code);
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
    if (!lm_is_list(list)) return nullptr;
    for (int64_t step = 0; step < index && list != LM_EMPTY; step++) list = lm_rest(list);
    return list == LM_EMPTY ? nullptr : lm_first(list);
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
        LmPosition position;
        if (!node) {
            outcome = failure(result, "path %zu does not exist", entry_index);
        } else if (!lm_position_lookup(node, &position)) {
            outcome = failure(result, "path %zu has no position", entry_index);
        } else if (position.line != expected->items[0].integer
                   || position.column != expected->items[1].integer) {
            outcome = failure(result, "path %zu is at %u:%u, expected %lld:%lld", entry_index,
                              position.line, position.column, (long long)expected->items[0].integer,
                              (long long)expected->items[1].integer);
        }
    }
    lm_release(datums);
    return outcome;
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
        result.outcome = SKIPPED; /* evaluation arrives in phase 3 */
    } else {
        failure(&result, "unknown kind %s", kind);
    }

    if (result.outcome != FAILED && (lm_live_objects() != 0 || lm_position_count() != 0)) {
        failure(&result, "leak: %zu live objects and %zu position entries after the case",
                lm_live_objects(), lm_position_count());
    }
    return result;
}

static char *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file) return nullptr;
    LmBuffer buffer;
    lm_buffer_init(&buffer);
    char chunk[65536];
    size_t chunk_length;
    while ((chunk_length = fread(chunk, 1, sizeof chunk, file)) > 0) {
        lm_buffer_append(&buffer, chunk, chunk_length);
    }
    fclose(file);
    return lm_buffer_take(&buffer, length);
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
        } else if (strcmp(arguments[index], "--stdlib") == 0) {
            /* the standard library arrives in phase 5; its cases are eval cases */
        } else if (arguments[index][0] == '-') {
            fprintf(stderr, "usage: %s [-k TEXT] [-v] [--stdlib] FILE...\n", arguments[0]);
            return 2;
        } else {
            if (!run_suite(arguments[index], filter, verbose, &totals)) return 2;
            suite_count++;
        }
    }
    if (suite_count == 0) {
        fprintf(stderr, "usage: %s [-k TEXT] [-v] [--stdlib] FILE...\n", arguments[0]);
        return 2;
    }
    return totals.failed ? 1 : 0;
}
