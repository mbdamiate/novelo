/* core §8.7 — the REPL: inputs over several lines, printing, errors. */
#include "test.h"

#include "repl.h"

#include <string.h>

/* Runs a REPL session over `input`; checks what it printed and reported. */
static void check_session(const char *file, int line, const char *input, const char *expected_output,
                          const char *expected_errors) {
    LispBuffer output;
    lisp_buffer_init(&output);
    LispBuffer errors;
    lisp_buffer_init(&errors);
    LispTextInput text = {input, strlen(input), 0};
    LispStateOptions options = {.output = lisp_output_to_buffer(&output), .input = lisp_input_from_text(&text)};
    LispState *state = lisp_state_new(&options);
    LispReplOptions repl = {.errors = lisp_output_to_buffer(&errors), .prompt = nullptr};
    lisp_repl(state, &repl);
    lisp_state_free(state);
    const char *got_output = output.data ? output.data : "";
    const char *got_errors = errors.data ? errors.data : "";
    if (strcmp(got_output, expected_output) != 0) {
        test_fail(file, line, "output \"%s\", expected \"%s\"", got_output, expected_output);
    }
    if (strcmp(got_errors, expected_errors) != 0) {
        test_fail(file, line, "errors \"%s\", expected \"%s\"", got_errors, expected_errors);
    }
    lisp_buffer_free(&output);
    lisp_buffer_free(&errors);
}
#define CHECK_SESSION(input, output, errors) check_session(__FILE__, __LINE__, (input), (output), (errors))

static void t_values(void) {
    CHECK_SESSION("(+ 1 2)\n\"text\"\n(define x 1)\nx\n", "3\n\"text\"\n1\n", "");
    CHECK_SESSION("1 2 3\n", "1\n2\n3\n", "");     /* several datums on a line */
    CHECK_SESSION("", "", "");
    CHECK_SESSION("\n   \n; a comment\n", "", "");
}

static void t_libraries_imported(void) {
    CHECK_SESSION("(map (lambda (x) (+ x 1)) '(1 2))\n(join \"-\" '(\"a\" \"b\"))\n((partial + 1) 2)\n",
                  "(2 3)\n\"a-b\"\n3\n", "");
    /* write! prints, then the REPL prints its #void result: nothing */
    CHECK_SESSION("(display! \"hi\")\n", "hi", "");
}

static void t_continuation(void) {
    CHECK_SESSION("(list 1\n  2\n  3)\n", "(1 2 3)\n", "");
    CHECK_SESSION("\"two\nlines\"\n", "\"two\\nlines\"\n", "");
    CHECK_SESSION("{:a\n 1}\n", "{:a 1}\n", "");
}

static void t_errors(void) {
    /* an error discards the rest of its input; the session goes on */
    CHECK_SESSION("(car 1) 5\n6\n", "6\n", "error :unbound-symbol: unbound symbol car car\n");
    CHECK_SESSION(")\n7\n", "7\n", "1:1: read error :unexpected-delimiter: ')' without an opener\n");
    /* an input left open at the end */
    CHECK_SESSION("(list 1\n", "", "1:1: read error :unclosed-delimiter: '(' is never closed\n");
    /* definitions made before an error in the same input remain */
    CHECK_SESSION("(define y 2) (error \"no\") 3\ny\n", "2\n", "error :error: no\n");
}

/* read-line! reads the lines that follow the input in the same stream; at
 * the end it returns #void, which the REPL does not print. */
static void t_read_line(void) {
    CHECK_SESSION("(read-line!)\nnext line\n(read-line!)\n", "\"next line\"\n", "");
}

const TestSuite repl_suite = {"repl", (const TestCase[]){
    {"values", t_values},
    {"every library is imported", t_libraries_imported},
    {"inputs over several lines", t_continuation},
    {"errors", t_errors},
    {"read-line! shares the input", t_read_line},
    {},
}};
