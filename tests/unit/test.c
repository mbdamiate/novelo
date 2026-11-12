/* Unit test runner: test [-k TEXT] [-v] [--no-fork] */
#include "test.h"

#include "value.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

extern const TestSuite buffer_suite;
extern const TestSuite utf8_suite;
extern const TestSuite value_suite;

static const TestSuite *const suites[] = {
    &buffer_suite,
    &utf8_suite,
    &value_suite,
};

static int failures; /* failed checks in the current test */

void test_fail(const char *file, int line, const char *format, ...) {
    failures++;
    fprintf(stderr, "    %s:%d: ", file, line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

/* Runs one test in this process; returns true if it passed. */
static bool run_here(const TestCase *test) {
    failures = 0;
    size_t live_before = lm_live_objects();
    test->fn();
    size_t live_after = lm_live_objects();
    if (live_after != live_before) {
        test_fail(__FILE__, __LINE__, "leak: %zu live objects before, %zu after",
                  live_before, live_after);
    }
    return failures == 0;
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

int main(int argc, char **argv) {
    const char *filter = nullptr;
    bool verbose = false;
    bool fork_tests = true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            filter = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--no-fork") == 0) {
            fork_tests = false;
        } else {
            fprintf(stderr, "usage: %s [-k TEXT] [-v] [--no-fork]\n", argv[0]);
            return 2;
        }
    }

    int passed = 0, failed = 0;
    char full_name[512];
    for (size_t s = 0; s < sizeof suites / sizeof suites[0]; s++) {
        for (const TestCase *test = suites[s]->cases; test->name; test++) {
            snprintf(full_name, sizeof full_name, "%s/%s", suites[s]->name, test->name);
            if (filter && !strstr(full_name, filter)) continue;
            bool ok = fork_tests ? run_forked(test) : run_here(test);
            if (ok) {
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
