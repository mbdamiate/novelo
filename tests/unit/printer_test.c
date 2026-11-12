/* core §10 */
#include "test.h"

#include "printer.h"
#include "reader.h"

#include <string.h>

static Value *dummy_primitive(LispState *, size_t, Value *const *) { return LISP_VOID; }

/* Prints the single datum read from source and checks the text. */
static void check_printed(const char *file, int line, const char *source, LispPrintMode mode,
                          const char *expected) {
    Value *datums;
    LispReadError error;
    if (!lisp_read(source, strlen(source), &datums, &error)) {
        test_fail(file, line, "cannot read %s", source);
        return;
    }
    LispBuffer output;
    lisp_buffer_init(&output);
    lisp_print(&output, lisp_first(datums), mode);
    if (strcmp(output.data, expected) != 0) {
        test_fail(file, line, "printed %s as %s, expected %s", source, output.data, expected);
    }
    lisp_buffer_free(&output);
    lisp_release(datums);
}
#define CHECK_WRITE(source, expected) check_printed(__FILE__, __LINE__, (source), LISP_PRINT_WRITE, (expected))
#define CHECK_DISPLAY(source, expected) check_printed(__FILE__, __LINE__, (source), LISP_PRINT_DISPLAY, (expected))

static void t_atoms(void) {
    CHECK_WRITE("#true", "#true");
    CHECK_WRITE("#false", "#false");
    CHECK_WRITE("#void", "#void");
    CHECK_WRITE("-42", "-42");
    CHECK_WRITE("-9223372036854775808", "-9223372036854775808");
    CHECK_WRITE("foo", "foo");
    CHECK_WRITE(":key", ":key");
    CHECK_WRITE("()", "()");
}

static void t_strings(void) {
    CHECK_WRITE("\"a\\nb\\t\\r\\\"\\\\\"", "\"a\\nb\\t\\r\\\"\\\\\"");
    CHECK_WRITE("\"\\u{0}\\u{1B}\\u{7F}\\u{1f}\"", "\"\\u{0}\\u{1B}\\u{7F}\\u{1F}\"");
    CHECK_WRITE("\"\xC3\xA9 \xF0\x9F\x98\x80\"", "\"\xC3\xA9 \xF0\x9F\x98\x80\"");
    CHECK_DISPLAY("\"a\\nb\"", "a\nb");
    CHECK_DISPLAY("(\"a\")", "(\"a\")"); /* only a top-level string is raw */
}

static void t_collections(void) {
    CHECK_WRITE("(1 (2 3) ())", "(1 (2 3) ())");
    CHECK_WRITE("'x", "(quote x)");
    CHECK_WRITE("{:a 1}", "{:a 1}");
    CHECK_WRITE("{}", "{}");
}

static void t_functions(void) {
    Value *primitive = lisp_primitive("dummy", 0, 0, dummy_primitive);
    LispBuffer output;
    lisp_buffer_init(&output);
    lisp_print(&output, primitive, LISP_PRINT_WRITE);
    CHECK_STR(output.data, "#<function>");
    lisp_buffer_free(&output);
    lisp_release(primitive);
}

/* Reading the printed text gives an equal? value (§10). */
static void t_round_trip(void) {
    const char *sources[] = {
        "(1 -2 \"x\\ny\" :k sym #true #false #void () {:a (1 2)})",
        "\"\\u{0}\\u{7F}\\u{1F600}\\\\\\\"\"",
        "{\"k\" {:nested {}} (1) 2}",
    };
    for (size_t index = 0; index < sizeof sources / sizeof sources[0]; index++) {
        Value *datums;
        LispReadError error;
        CHECK(lisp_read(sources[index], strlen(sources[index]), &datums, &error));
        LispBuffer output;
        lisp_buffer_init(&output);
        lisp_print(&output, lisp_first(datums), LISP_PRINT_WRITE);
        test_check_equal(__FILE__, __LINE__, lisp_first(datums), output.data);
        lisp_buffer_free(&output);
        lisp_release(datums);
    }
}

const TestSuite printer_suite = {"printer", (const TestCase[]){
    {"atoms", t_atoms},
    {"strings and escapes", t_strings},
    {"lists and maps", t_collections},
    {"functions", t_functions},
    {"printed text reads back", t_round_trip},
    {},
}};
