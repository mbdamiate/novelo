/* core §3–7 */
#include "test.h"

#include "reader.h"

#include <string.h>

/* Reads exactly one datum (a new reference), or nullptr after a failed check. */
static Value *read_one(const char *source) {
    Value *datums;
    LispReadError error;
    if (!lisp_read(source, strlen(source), &datums, &error)) {
        test_fail(__FILE__, __LINE__, "read \"%s\" failed: %s", source, lisp_read_error_name(error.code));
        return nullptr;
    }
    if (datums == LISP_EMPTY || lisp_rest(datums) != LISP_EMPTY) {
        test_fail(__FILE__, __LINE__, "\"%s\" is not exactly one datum", source);
        lisp_release(datums);
        return nullptr;
    }
    Value *datum = lisp_retain(lisp_first(datums));
    lisp_release(datums);
    return datum;
}

static size_t datum_count(const char *source) {
    Value *datums;
    LispReadError error;
    if (!lisp_read(source, strlen(source), &datums, &error)) return (size_t)-1;
    size_t count = lisp_list_length(datums);
    lisp_release(datums);
    return count;
}

static void t_integers(void) {
    Value *datum = read_one("007");
    CHECK(datum && lisp_integer_value(datum) == 7);
    lisp_release(datum);
    datum = read_one("9223372036854775807");
    CHECK(datum && lisp_integer_value(datum) == INT64_MAX);
    lisp_release(datum);
    datum = read_one("-9223372036854775808");
    CHECK(datum && lisp_integer_value(datum) == INT64_MIN);
    lisp_release(datum);
    CHECK_READ_ERROR("9223372036854775808", "integer-overflow", 1, 1);
    CHECK_READ_ERROR("-9223372036854775809", "integer-overflow", 1, 1);
}

static void t_token_classification(void) {
    const char *symbols[] = {"-", "-x", "...", "&rest", "empty?", "\xCE\xBB", "a#b", "a:b",
                             "\xC2\xA0x", "\xEF\xBB\xBF"};
    for (size_t index = 0; index < sizeof symbols / sizeof symbols[0]; index++) {
        Value *datum = read_one(symbols[index]);
        CHECK(datum && datum->kind == LISP_KIND_SYMBOL);
        lisp_release(datum);
    }
    const char *invalid_numbers[] = {"3.14", "1e3", "1+", "5.", ".5", "+5", "-.5", "12abc"};
    for (size_t index = 0; index < sizeof invalid_numbers / sizeof invalid_numbers[0]; index++) {
        CHECK_READ_ERROR(invalid_numbers[index], "invalid-number", 1, 1);
    }
    CHECK_READ_ERROR(".", "dotted-pair", 1, 1);
    CHECK_READ_ERROR("(a . b)", "dotted-pair", 1, 4);
}

static void t_parse_integer(void) {
    int64_t value;
    CHECK_INT(lisp_parse_integer("42", 2, &value), LISP_INTEGER_OK);
    CHECK_INT(value, 42);
    CHECK_INT(lisp_parse_integer("-0", 2, &value), LISP_INTEGER_OK);
    CHECK_INT(value, 0);
    CHECK_INT(lisp_parse_integer("+5", 2, &value), LISP_INTEGER_INVALID);
    CHECK_INT(lisp_parse_integer("abc", 3, &value), LISP_INTEGER_NOT_NUMERIC);
    CHECK_INT(lisp_parse_integer("", 0, &value), LISP_INTEGER_NOT_NUMERIC);
    CHECK_INT(lisp_parse_integer("-", 1, &value), LISP_INTEGER_NOT_NUMERIC);
    CHECK_INT(lisp_parse_integer("99999999999999999999", 20, &value), LISP_INTEGER_OVERFLOW);
}

static void t_literals(void) {
    Value *datum = read_one("#true");
    CHECK(datum == LISP_TRUE);
    datum = read_one("#void");
    CHECK(datum == LISP_VOID);
    datum = read_one(":a:b");
    CHECK(datum == lisp_keyword_cstr("a:b"));
    datum = read_one(":1");
    CHECK(datum == lisp_keyword_cstr("1"));
    CHECK_READ_ERROR(":", "empty-keyword", 1, 1);
    CHECK_READ_ERROR("#t", "unknown-dispatch", 1, 1);
    CHECK_READ_ERROR("#truex", "unknown-dispatch", 1, 1);
    CHECK_READ_ERROR("#", "unknown-dispatch", 1, 1);
    CHECK_READ_ERROR("#;", "unknown-dispatch", 1, 1);
    CHECK_READ_ERROR("#|", "unknown-dispatch", 1, 1);
    CHECK_READ_ERROR("#{1}", "unknown-dispatch", 1, 1);
    CHECK_READ_ERROR("#<function>", "unknown-dispatch", 1, 1);
}

static void t_strings(void) {
    Value *datum = read_one("\"a\nb\"");
    CHECK_EQUAL(datum, "\"a\\nb\"");
    lisp_release(datum);
    datum = read_one("\"\\r\\u{41}\\u{000042}\\u{1f600}\\u{1F600}\\u{0}\"");
    CHECK(datum && lisp_as_string(datum)->length == 6);
    CHECK(datum && memcmp(lisp_as_string(datum)->data,
                          "\rAB\xF0\x9F\x98\x80\xF0\x9F\x98\x80\0", 12) == 0);
    lisp_release(datum);
    CHECK_READ_ERROR("\"abc", "unterminated-string", 1, 1);
    CHECK_READ_ERROR("\"\\q\"", "invalid-escape", 1, 2);
    CHECK_READ_ERROR("\"\\u{D800}\"", "invalid-escape", 1, 2);
    CHECK_READ_ERROR("\"\\u{110000}\"", "invalid-escape", 1, 2);
    CHECK_READ_ERROR("\"\\u{}\"", "invalid-escape", 1, 2);
    CHECK_READ_ERROR("\"\\u{1234567}\"", "invalid-escape", 1, 2);
    CHECK_READ_ERROR("\"\\u41\"", "invalid-escape", 1, 2);
    CHECK_READ_ERROR("\"a \\u{4G}\"", "invalid-escape", 1, 4);
    CHECK_READ_ERROR("\"\\u{41", "unterminated-string", 1, 1);
    CHECK_READ_ERROR("\"\\", "unterminated-string", 1, 1);
}

static void t_collections(void) {
    Value *datum = read_one("(1 (2) ())");
    CHECK_EQUAL(datum, "(1 (2) ())");
    CHECK(datum && lisp_list_length(datum) == 3);
    lisp_release(datum);
    datum = read_one("{:b 2 :a 1}");
    CHECK_EQUAL(datum, "{:a 1 :b 2}");
    lisp_release(datum);
    datum = read_one("{}");
    CHECK(datum && datum->kind == LISP_KIND_MAP && lisp_map_count(datum) == 0);
    lisp_release(datum);
    CHECK_READ_ERROR("{:a 1 :b}", "odd-map", 1, 1);
    CHECK_READ_ERROR("{:a 1 :a 2}", "duplicate-key", 1, 1);
    CHECK_READ_ERROR("{(1 2) 1 (1 2) 2}", "duplicate-key", 1, 1);
    CHECK_READ_ERROR("{:a 1 :a \"x}", "unterminated-string", 1, 10);
}

static void t_delimiter_errors(void) {
    CHECK_READ_ERROR("(1 2", "unclosed-delimiter", 1, 1);
    CHECK_READ_ERROR("(a (b", "unclosed-delimiter", 1, 4);
    CHECK_READ_ERROR("(a\n  (b", "unclosed-delimiter", 2, 3);
    CHECK_READ_ERROR(")", "unexpected-delimiter", 1, 1);
    CHECK_READ_ERROR("(a ')", "unexpected-delimiter", 1, 5);
    CHECK_READ_ERROR("(1 2}", "mismatched-delimiter", 1, 5);
    CHECK_READ_ERROR("(\xCE\xBB ]", "reserved-character", 1, 4);
    CHECK_READ_ERROR("[1 2]", "reserved-character", 1, 1);
    CHECK_READ_ERROR("'", "unexpected-eof", 1, 2);
    CHECK_READ_ERROR("' ; comment\n", "unexpected-eof", 2, 1);
}

static void t_prefixes_and_atmosphere(void) {
    Value *datum = read_one("' x");
    CHECK_EQUAL(datum, "(quote x)");
    lisp_release(datum);
    datum = read_one("`(a ,b ,@c)");
    CHECK_EQUAL(datum, "(quasiquote (a (unquote b) (unquote-splicing c)))");
    lisp_release(datum);
    datum = read_one("(a ; comment\n b)");
    CHECK_EQUAL(datum, "(a b)");
    lisp_release(datum);
    CHECK_INT(datum_count("a,b"), 2);
    CHECK_INT(datum_count(""), 0);
    CHECK_INT(datum_count("   ; just a comment "), 0);
    CHECK_INT(datum_count("\xEF\xBB\xBF(a)"), 2); /* BOM is a constituent */
    CHECK_INT(datum_count("a\fb\rc\td"), 4);
}

static void t_interning_while_reading(void) {
    Value *datum = read_one("(foo foo :foo)");
    CHECK(datum != nullptr);
    if (!datum) return;
    Value *first = lisp_first(datum);
    Value *second = lisp_first(lisp_rest(datum));
    Value *third = lisp_first(lisp_rest(lisp_rest(datum)));
    CHECK(first == second);
    CHECK(first != third);
    lisp_release(datum);
}

static void t_nesting_limit(void) {
    char source[2001];
    memset(source, '(', 1000);
    memset(source + 1000, ')', 1000);
    source[2000] = '\0';
    Value *datum = read_one(source);
    CHECK(datum != nullptr);
    lisp_release(datum);

    char too_deep[1002];
    memset(too_deep, '(', 1001);
    too_deep[1001] = '\0';
    CHECK_READ_ERROR(too_deep, "nesting-too-deep", 1, 1001);

    char prefixes[1002];
    memset(prefixes, '\'', 1001);
    prefixes[1001] = '\0';
    CHECK_READ_ERROR(prefixes, "nesting-too-deep", 1, 1001);
}

static void t_positions(void) {
    Value *datum = read_one("(define x\n  {:a 1})");
    LispPosition position;
    CHECK(datum && lisp_position_lookup(datum, &position));
    CHECK_INT(position.line, 1);
    CHECK_INT(position.column, 1);
    CHECK(lisp_position_lookup(lisp_first(lisp_rest(lisp_rest(datum))), &position));
    CHECK_INT(position.line, 2);
    CHECK_INT(position.column, 3);
    lisp_release(datum);

    datum = read_one("(a 'b)");
    CHECK(datum && lisp_position_lookup(lisp_first(lisp_rest(datum)), &position));
    CHECK_INT(position.column, 4);
    lisp_release(datum);

    datum = read_one("(\xCE\xBB\t(x))"); /* columns count code points; tab is one column */
    CHECK(datum && lisp_position_lookup(lisp_first(lisp_rest(datum)), &position));
    CHECK_INT(position.column, 4);
    lisp_release(datum);
    CHECK_INT(lisp_position_count(), 0);
}

static void t_invalid_utf8(void) {
    CHECK_READ_ERROR("\xFF", "invalid-utf8", 1, 1);
    CHECK_READ_ERROR("a\xC3", "invalid-utf8", 1, 2);
    CHECK_READ_ERROR("(a\n \xC0\x80)", "invalid-utf8", 2, 2);
    CHECK_READ_ERROR("\"abc\xFF\"", "invalid-utf8", 1, 5);  /* met before the end of the string */
    CHECK_READ_ERROR("; \xFF\n1", "invalid-utf8", 1, 3);     /* comments must be valid too */
    CHECK_READ_ERROR(") \xFF", "unexpected-delimiter", 1, 1); /* earlier errors win */
    CHECK_READ_ERROR("(1 2} \xFF", "mismatched-delimiter", 1, 5);
    CHECK_READ_ERROR("(1 2 \xFF", "invalid-utf8", 1, 6);    /* met before the end of input */
}

static void t_incomplete_errors(void) {
    CHECK(lisp_read_error_is_incomplete(LISP_READ_UNCLOSED_DELIMITER));
    CHECK(lisp_read_error_is_incomplete(LISP_READ_UNTERMINATED_STRING));
    CHECK(lisp_read_error_is_incomplete(LISP_READ_UNEXPECTED_EOF));
    CHECK(!lisp_read_error_is_incomplete(LISP_READ_ODD_MAP));
}

const TestSuite reader_suite = {"reader", (const TestCase[]){
    {"integers", t_integers},
    {"token classification", t_token_classification},
    {"integer parsing", t_parse_integer},
    {"booleans, void, keywords and dispatch", t_literals},
    {"strings and escapes", t_strings},
    {"lists and maps", t_collections},
    {"delimiter errors", t_delimiter_errors},
    {"prefixes and atmosphere", t_prefixes_and_atmosphere},
    {"symbols are interned while reading", t_interning_while_reading},
    {"nesting limit", t_nesting_limit},
    {"positions", t_positions},
    {"invalid UTF-8 in reading order", t_invalid_utf8},
    {"incomplete-input errors", t_incomplete_errors},
    {},
}};
