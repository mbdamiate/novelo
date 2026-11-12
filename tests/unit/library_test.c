/* core §8.9, stdlib §4 — the libraries written in the language: their source
 * in the standard library document, loading, errors. */
#include "test.h"

#include "error.h"
#include "library.h"
#include "printer.h"
#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- the source of a library ------------------------------------------------------------ */

static const char document[] = "# Title\n"                 /* 1 */
                               "## 4. Reference\n"         /* 2 */
                               "### 4.1 `first`\n"         /* 3 */
                               "Prose.\n"                  /* 4 */
                               "```lisp\n"                 /* 5 */
                               "(define a 1)\n"            /* 6 */
                               "```\n"                     /* 7 */
                               "#### A subsection\n"       /* 8 */
                               "```text\n"                 /* 9 */
                               "not code\n"                /* 10 */
                               "```\n"                     /* 11 */
                               "```lisp\n"                 /* 12 */
                               "(define b 2)\n"            /* 13 */
                               "```\n"                     /* 14 */
                               "### 4.2 `second`\n"        /* 15 */
                               "```lisp\n"                 /* 16 */
                               "(define c 3)\n"            /* 17 */
                               "```\n"                     /* 18 */
                               "## 5. After\n"             /* 19 */
                               "```lisp\n"                 /* 20 */
                               "(define d 4)\n"            /* 21 */
                               "```\n";                    /* 22 */

/* The lines of a source that are not empty, as "line:text" separated by '|'. */
static void check_source(const char *file, int line, const char *section, const char *expected) {
    LispBuffer source;
    lisp_buffer_init(&source);
    bool found = lisp_library_source(document, sizeof document - 1, section, &source);
    LispBuffer lines;
    lisp_buffer_init(&lines);
    unsigned number = 1;
    for (const char *cursor = source.data ? source.data : ""; *cursor; number++) {
        const char *end = strchr(cursor, '\n');
        if (end != cursor) lisp_buffer_appendf(&lines, "%s%u:%.*s", lines.length ? "|" : "", number,
                                               (int)(end - cursor), cursor);
        cursor = end + 1;
    }
    const char *got = found ? (lines.data ? lines.data : "") : "(none)";
    if (strcmp(got, expected) != 0) test_fail(file, line, "section %s: %s, expected %s", section, got, expected);
    lisp_buffer_free(&lines);
    lisp_buffer_free(&source);
}
#define CHECK_SOURCE(section, expected) check_source(__FILE__, __LINE__, (section), (expected))

static void t_source(void) {
    /* every lisp block of the section, subsections included, at its own line */
    CHECK_SOURCE("4.1", "6:(define a 1)|13:(define b 2)");
    CHECK_SOURCE("4.2", "17:(define c 3)");
    CHECK_SOURCE("4.3", "(none)");
    CHECK_SOURCE("4", "(none)"); /* a heading prefix is not a section */
}

/* ---- loading ------------------------------------------------------------------------------ */

typedef struct LibraryRun {
    LispState *state;
    LispBuffer output;
    LispRunResult result;
    Value *value;
} LibraryRun;

static void library_run(LibraryRun *run, const char *program, const char *library_path) {
    lisp_buffer_init(&run->output);
    LispStateOptions options = {.output = lisp_output_to_buffer(&run->output), .library_path = library_path};
    run->state = lisp_state_new(&options);
    run->value = nullptr;
    LispReadError read_error;
    run->result = lisp_run_source(run->state, program, strlen(program), nullptr, &run->value, &read_error);
}

static void library_run_finish(LibraryRun *run) {
    lisp_release(run->value);
    lisp_state_free(run->state);
    lisp_buffer_free(&run->output);
}

static void check_library_value(const char *file, int line, const char *program, const char *expected) {
    LibraryRun run;
    library_run(&run, program, nullptr);
    if (run.result != LISP_RUN_OK) {
        LispBuffer message;
        lisp_buffer_init(&message);
        if (run.state->error) lisp_describe_error(&message, run.state->error);
        test_fail(file, line, "%s failed: %s", program, message.data ? message.data : "a read error");
        lisp_buffer_free(&message);
    } else {
        test_check_equal(file, line, run.value, expected);
    }
    library_run_finish(&run);
}
#define CHECK_LIBRARY_VALUE(program, expected) check_library_value(__FILE__, __LINE__, (program), (expected))

static void t_loading(void) {
    CHECK_LIBRARY_VALUE("(import list) (map (lambda (x) (* x x)) (range 1 4))", "(1 4 9)");
    CHECK_LIBRARY_VALUE("(import text t) (list (t/join \"-\" (t/split \",\" \"a,b\")) (t/reverse '(1 2)))",
                        "(\"a-b\" (2 1))");
    CHECK_LIBRARY_VALUE("(import function) ((partial + 1 2) 3)", "6");
    /* a repeated import is ignored: a redefinition survives it */
    CHECK_LIBRARY_VALUE("(import list) (define map 5) (import list) map", "5");
}

static void check_values(const char *file, int line, LibraryRun *run, const char *expected) {
    if (run->result != LISP_RUN_ERROR) {
        test_fail(file, line, "expected an error");
        return;
    }
    LispBuffer printed;
    lisp_buffer_init(&printed);
    lisp_print(&printed, lisp_map_get(run->state->error, lisp_keyword_cstr("values")), LISP_PRINT_WRITE);
    if (strcmp(printed.data, expected) != 0) test_fail(file, line, ":values %s, expected %s", printed.data, expected);
    lisp_buffer_free(&printed);
}

static void t_errors(void) {
    LibraryRun run;
    library_run(&run, "(import list)", "/no/such/standard-library.md");
    check_values(__FILE__, __LINE__, &run, "(list :cannot-open)");
    library_run_finish(&run);

    library_run(&run, "(import io) (import widgets)", "/no/such/standard-library.md");
    check_values(__FILE__, __LINE__, &run, "(widgets :unknown-library)");
    library_run_finish(&run);

    /* a read error in a library's source, at its line in the document */
    char path[] = "/tmp/lisp-min-library-XXXXXX";
    int descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    if (descriptor < 0) return;
    const char *broken = "### 4.1 `list`\n\n```lisp\n(define fold\n  {:a})\n```\n";
    CHECK(write(descriptor, broken, strlen(broken)) == (ssize_t)strlen(broken));
    close(descriptor);
    library_run(&run, "(import list)", path);
    check_values(__FILE__, __LINE__, &run, "(list :odd-map 5 3)");
    library_run_finish(&run);
    unlink(path);
}

const TestSuite library_suite = {"library", (const TestCase[]){
    {"source from the document", t_source},
    {"loading", t_loading},
    {"errors", t_errors},
    {},
}};
