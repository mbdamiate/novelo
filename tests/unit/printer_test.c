/* core §10 */
#include "test.h"

#include "printer.h"
#include "reader.h"

#include <string.h>

static Value *dummy_primitive(LmState *, size_t, Value *const *) { return LM_VOID; }

/* Prints the single datum read from source and checks the text. */
static void check_printed(const char *file, int line, const char *source, LmPrintMode mode,
                          const char *expected) {
    Value *datums;
    LmReadError error;
    if (!lm_read(source, strlen(source), &datums, &error)) {
        test_fail(file, line, "cannot read %s", source);
        return;
    }
    LmBuffer output;
    lm_buffer_init(&output);
    lm_print(&output, lm_first(datums), mode);
    if (strcmp(output.data, expected) != 0) {
        test_fail(file, line, "printed %s as %s, expected %s", source, output.data, expected);
    }
    lm_buffer_free(&output);
    lm_release(datums);
}
#define CHECK_WRITE(source, expected) check_printed(__FILE__, __LINE__, (source), LM_PRINT_WRITE, (expected))
#define CHECK_DISPLAY(source, expected) check_printed(__FILE__, __LINE__, (source), LM_PRINT_DISPLAY, (expected))

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
    Value *primitive = lm_primitive("dummy", 0, 0, dummy_primitive);
    LmBuffer output;
    lm_buffer_init(&output);
    lm_print(&output, primitive, LM_PRINT_WRITE);
    CHECK_STR(output.data, "#<function>");
    lm_buffer_free(&output);
    lm_release(primitive);
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
        LmReadError error;
        CHECK(lm_read(sources[index], strlen(sources[index]), &datums, &error));
        LmBuffer output;
        lm_buffer_init(&output);
        lm_print(&output, lm_first(datums), LM_PRINT_WRITE);
        test_check_equal(__FILE__, __LINE__, lm_first(datums), output.data);
        lm_buffer_free(&output);
        lm_release(datums);
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
