/* Unit test runner: test [-k TEXT] [-v] [--no-fork] */
#include "test.h"

#include "printer.h"
#include "reader.h"
#include "value.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

extern const TestSuite buffer_suite;
extern const TestSuite utf8_suite;
extern const TestSuite value_suite;
extern const TestSuite positions_suite;
extern const TestSuite reader_suite;
extern const TestSuite printer_suite;

static const TestSuite *const suites[] = {
    &buffer_suite,
    &utf8_suite,
    &value_suite,
    &positions_suite,
    &reader_suite,
    &printer_suite,
};

static int failed_checks; /* in the current test */

void test_fail(const char *file, int line, const char *format, ...) {
    failed_checks++;
    fprintf(stderr, "    %s:%d: ", file, line);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

void test_check_equal(const char *file, int line, const Value *value, const char *expected_text) {
    Value *datums;
    LmReadError error;
    if (!lm_read(expected_text, strlen(expected_text), &datums, &error)) {
        test_fail(file, line, "CHECK_EQUAL: cannot read \"%s\": %s", expected_text,
                  lm_read_error_name(error.code));
        return;
    }
    if (datums == LM_EMPTY || lm_rest(datums) != LM_EMPTY) {
        test_fail(file, line, "CHECK_EQUAL: \"%s\" is not exactly one datum", expected_text);
    } else if (!lm_equal(value, lm_first(datums))) {
        LmBuffer printed;
        lm_buffer_init(&printed);
        lm_print(&printed, value, LM_PRINT_WRITE);
        test_fail(file, line, "CHECK_EQUAL: got %s, expected %s", printed.data, expected_text);
        lm_buffer_free(&printed);
    }
    lm_release(datums);
}

void test_check_read_error(const char *file, int line, const char *source, const char *code,
                           int expected_line, int expected_column) {
    Value *datums;
    LmReadError error;
    if (lm_read(source, strlen(source), &datums, &error)) {
        lm_release(datums);
        test_fail(file, line, "CHECK_READ_ERROR: \"%s\" read without error, expected %s", source, code);
        return;
    }
    const char *actual = lm_read_error_name(error.code);
    if (strcmp(actual, code) != 0 || (int)error.position.line != expected_line
        || (int)error.position.column != expected_column) {
        test_fail(file, line, "CHECK_READ_ERROR: \"%s\" gave %s at %u:%u, expected %s at %d:%d",
                  source, actual, error.position.line, error.position.column, code,
                  expected_line, expected_column);
    }
}

/* Runs one test in this process; returns true if it passed. */
static bool run_here(const TestCase *test) {
    failed_checks = 0;
    size_t live_before = lm_live_objects();
    test->function();
    size_t live_after = lm_live_objects();
    if (live_after != live_before) {
        test_fail(__FILE__, __LINE__, "leak: %zu live objects before, %zu after",
                  live_before, live_after);
    }
    return failed_checks == 0;
}

static bool run_forked(const TestCase *test) {
    fflush(stdout);
    fflush(stderr);
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        exit(2);
    }
    if (child == 0) _exit(run_here(test) ? 0 : 1);
    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        perror("waitpid");
        exit(2);
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "    crashed with signal %d\n", WTERMSIG(status));
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(int argument_count, char **arguments) {
    const char *filter = nullptr;
    bool verbose = false;
    bool fork_tests = true;
    for (int index = 1; index < argument_count; index++) {
        if (strcmp(arguments[index], "-k") == 0 && index + 1 < argument_count) {
            filter = arguments[++index];
        } else if (strcmp(arguments[index], "-v") == 0) {
            verbose = true;
        } else if (strcmp(arguments[index], "--no-fork") == 0) {
            fork_tests = false;
        } else {
            fprintf(stderr, "usage: %s [-k TEXT] [-v] [--no-fork]\n", arguments[0]);
            return 2;
        }
    }

    int passed = 0, failed = 0;
    char full_name[512];
    for (size_t suite_index = 0; suite_index < sizeof suites / sizeof suites[0]; suite_index++) {
        const TestSuite *suite = suites[suite_index];
        for (const TestCase *test = suite->cases; test->name; test++) {
            snprintf(full_name, sizeof full_name, "%s/%s", suite->name, test->name);
            if (filter && !strstr(full_name, filter)) continue;
            bool test_passed = fork_tests ? run_forked(test) : run_here(test);
            if (test_passed) {
                passed++;
                if (verbose) printf("  ok    %s\n", full_name);
            } else {
                failed++;
                printf("  FAIL  %s\n", full_name);
            }
        }
    }
    printf("unit: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
