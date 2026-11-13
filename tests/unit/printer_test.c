/* core §10 */
#include "test.h"

#include "printer.h"
#include "reader.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* §10 — the shortest decimal, laid out by its exponent. */
static void t_floats(void) {
    CHECK_WRITE("3.0", "3.0");
    CHECK_WRITE("-0.5", "-0.5");
    CHECK_WRITE("0.0", "0.0");
    CHECK_WRITE("-0.0", "-0.0");
    CHECK_WRITE("2.50", "2.5");
    CHECK_WRITE("1.0e2", "100.0");
    CHECK_WRITE("1.0e20", "100000000000000000000.0");
    CHECK_WRITE("1.0e21", "1.0e21");
    CHECK_WRITE("0.000001", "0.000001");
    CHECK_WRITE("0.0000001", "1.0e-7");
    CHECK_WRITE("1.25e-7", "1.25e-7");
    CHECK_WRITE("5.0e-324", "5.0e-324");
    CHECK_WRITE("1.7976931348623157e308", "1.7976931348623157e308");
    CHECK_WRITE("0.30000000000000004", "0.30000000000000004");
    CHECK_WRITE("9007199254740993.0", "9007199254740992.0");
    CHECK_WRITE("(0.1 -2.0)", "(0.1 -2.0)");
    /* Powers of two whose correctly rounded digits of the shortest length do
     * not read back, while the neighbor above does: below a power of two the
     * interval that reads back is half as wide. */
    char text[LISP_FLOAT_TEXT_SIZE];
    lisp_format_float(ldexp(1.0, -1017), text);
    CHECK_STR(text, "7.120236347223045e-307"); /* %.15e gives ...044 */
    lisp_format_float(ldexp(1.0, -808), text);
    CHECK_STR(text, "5.858190679279809e-244");
}

/* Every finite binary64 prints as a Float that reads back to the same bits,
 * with no more digits than the first precision at which %e reads back. */
static void t_float_round_trip(void) {
    uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (int iteration = 0; iteration < 200000; iteration++) {
        state ^= state << 13; /* xorshift64 */
        state ^= state >> 7;
        state ^= state << 17;
        double number;
        memcpy(&number, &state, sizeof number);
        if (!isfinite(number)) continue;
        char text[LISP_FLOAT_TEXT_SIZE];
        lisp_format_float(number, text);
        double read_back;
        if (lisp_parse_float(text, strlen(text), &read_back) != LISP_FLOAT_OK
            || memcmp(&read_back, &number, sizeof number) != 0) {
            test_fail(__FILE__, __LINE__, "%s does not read back as %a", text, number);
            return;
        }
        int digits = 0;
        for (const char *cursor = text; *cursor && *cursor != 'e'; cursor++) digits += *cursor >= '0' && *cursor <= '9';
        int naive = 1;
        for (; naive < 17; naive++) {
            char candidate[48];
            snprintf(candidate, sizeof candidate, "%.*e", naive - 1, number);
            if (strtod(candidate, nullptr) == number) break;
        }
        /* the layout pads with zeros, so compare significant digits only */
        const char *start = text + (text[0] == '-');
        while (*start == '0' || *start == '.') {
            if (*start == '0') digits--;
            start++;
        }
        int trailing = 0;
        const char *end = strchr(text, 'e');
        if (!end) end = text + strlen(text);
        for (const char *cursor = end - 1; cursor > start && (*cursor == '0' || *cursor == '.'); cursor--) {
            trailing += *cursor == '0';
        }
        if (digits - trailing > naive) {
            test_fail(__FILE__, __LINE__, "%s has more digits than %d", text, naive);
            return;
        }
    }
}

static void check_fixed(int line, double number, int decimals, const char *expected) {
    char text[LISP_FIXED_TEXT_SIZE];
    lisp_format_fixed(number, decimals, text);
    if (strcmp(text, expected) != 0) {
        test_fail(__FILE__, line, "fixed %.17g with %d decimals: \"%s\" != \"%s\"", number, decimals, text,
                  expected);
    }
}

static void t_fixed(void) {
    check_fixed(__LINE__, 20.1, 2, "20.10");
    check_fixed(__LINE__, 2.6, 0, "3");
    check_fixed(__LINE__, 3.0, 0, "3");
    check_fixed(__LINE__, 2.5, 0, "3");     /* ties away from zero */
    check_fixed(__LINE__, -2.5, 0, "-3");
    check_fixed(__LINE__, 0.125, 2, "0.13");
    check_fixed(__LINE__, 1.005, 2, "1.00"); /* the nearest Float is below 1.005 */
    check_fixed(__LINE__, 9.996, 2, "10.00"); /* a carry into a new digit */
    check_fixed(__LINE__, -99.5, 0, "-100");
    check_fixed(__LINE__, 0.96, 1, "1.0");
    check_fixed(__LINE__, -0.001, 2, "0.00"); /* no sign when every digit is zero */
    check_fixed(__LINE__, -0.0, 0, "0");
    check_fixed(__LINE__, -0.005, 2, "-0.01"); /* -0.005000000000000000104… */
    check_fixed(__LINE__, 0x1p-1074, 20, "0.00000000000000000000");
    check_fixed(__LINE__, 0x1p-1074, 0, "0");
    check_fixed(__LINE__, 0.1, 20, "0.10000000000000000555");
    check_fixed(__LINE__, 1e21, 1, "1000000000000000000000.0");

    char text[LISP_FIXED_TEXT_SIZE];
    lisp_format_fixed(-0x1.fffffffffffffp1023, 20, text); /* the longest text */
    CHECK(strlen(text) == 1 + 309 + 1 + 20);
    CHECK(strncmp(text, "-17976931348623157081", 21) == 0);
}

/* Away from ties, the rounding agrees with printf, which rounds the exact
 * value too. A tie is a value whose exact digits stop one place after the
 * last written one, with a 5 there. */
static void t_fixed_against_printf(void) {
    uint64_t state = 0x2545F4914F6CDD1DULL;
    for (int iteration = 0; iteration < 100000; iteration++) {
        state ^= state << 13; /* xorshift64 */
        state ^= state >> 7;
        state ^= state << 17;
        /* Numbers with a few digits after the point, where ties and carries
         * happen, and random bit patterns of every magnitude. */
        double number;
        if (iteration % 2 == 0) {
            number = (double)(int64_t)(state % 2000001) / 1000.0 - 1000.0;
        } else {
            uint64_t bits = state & 0x7FEFFFFFFFFFFFFFULL; /* finite */
            memcpy(&number, &bits, sizeof number);
            if (state >> 63) number = -number;
        }
        int decimals = (int)((state >> 40) % (LISP_FIXED_MAXIMUM_DECIMALS + 1));

        char text[LISP_FIXED_TEXT_SIZE];
        lisp_format_fixed(number, decimals, text);
        char exact[LISP_FIXED_TEXT_SIZE];
        snprintf(exact, sizeof exact, "%.1074f", fabs(number));
        char *last = exact + strlen(exact) - 1;
        while (*last == '0') last--;
        const char *point = strchr(exact, '.');
        bool tie = *last == '5' && last - point == decimals + 1;
        if (tie) continue;

        char expected[LISP_FIXED_TEXT_SIZE];
        snprintf(expected, sizeof expected, "%.*f", decimals, number);
        const char *compared = expected;
        if (expected[0] == '-' && !strpbrk(expected, "123456789")) compared++; /* "-0.00" */
        if (strcmp(text, compared) != 0) {
            test_fail(__FILE__, __LINE__, "fixed %.17g with %d decimals: \"%s\", printf \"%s\"", number,
                      decimals, text, expected);
            return;
        }
    }
}

static const LispClass widget_class = {"Widget", nullptr};

static void t_functions(void) {
    Value *primitive = lisp_primitive("dummy", 0, 0, dummy_primitive);
    LispBuffer output;
    lisp_buffer_init(&output);
    lisp_print(&output, primitive, LISP_PRINT_WRITE);
    CHECK_STR(output.data, "#<function>");
    lisp_buffer_free(&output);
    lisp_release(primitive);

    /* an opaque object prints its class, inside a list too */
    Value *list = lisp_prepend_take(lisp_opaque_new(&widget_class, sizeof(LispOpaque)), LISP_EMPTY);
    lisp_buffer_init(&output);
    lisp_print(&output, list, LISP_PRINT_WRITE);
    CHECK_STR(output.data, "(#<Widget>)");
    lisp_buffer_free(&output);
    lisp_release(list);
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
    {"floats", t_floats},
    {"every Float reads back from the shortest digits", t_float_round_trip},
    {"fixed decimals", t_fixed},
    {"fixed decimals agree with printf away from ties", t_fixed_against_printf},
    {"functions and opaque objects", t_functions},
    {"printed text reads back", t_round_trip},
    {},
}};
