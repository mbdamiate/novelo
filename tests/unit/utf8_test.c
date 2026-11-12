/* core §3.2 */
#include "test.h"

#include "utf8.h"

static bool decode(const char *bytes, size_t length, uint32_t *code_point, size_t *next_position) {
    size_t position = 0;
    bool decoded = lisp_utf8_next((const unsigned char *)bytes, length, &position, code_point);
    *next_position = position;
    return decoded;
}

static void t_decodes_valid(void) {
    uint32_t code_point;
    size_t next_position;
    CHECK(decode("A", 1, &code_point, &next_position));
    CHECK_INT(code_point, 'A');
    CHECK_INT(next_position, 1);
    CHECK(decode("\xC3\xA9", 2, &code_point, &next_position));
    CHECK_INT(code_point, 0xE9);
    CHECK(decode("\xE2\x82\xAC", 3, &code_point, &next_position));
    CHECK_INT(code_point, 0x20AC);
    CHECK(decode("\xF0\x9F\x98\x80", 4, &code_point, &next_position));
    CHECK_INT(code_point, 0x1F600);
    CHECK_INT(next_position, 4);
    CHECK(decode("\xF4\x8F\xBF\xBF", 4, &code_point, &next_position));
    CHECK_INT(code_point, 0x10FFFF);
    CHECK(decode("\xEF\xBB\xBF", 3, &code_point, &next_position)); /* BOM is an ordinary code point */
    CHECK_INT(code_point, 0xFEFF);
}

static void t_rejects_invalid(void) {
    uint32_t code_point;
    size_t next_position;
    CHECK(!decode("\xFF", 1, &code_point, &next_position));
    CHECK(!decode("\x80", 1, &code_point, &next_position));         /* lone continuation */
    CHECK(!decode("\xC0\x80", 2, &code_point, &next_position));     /* overlong */
    CHECK(!decode("\xE0\x80\x80", 3, &code_point, &next_position)); /* overlong */
    CHECK(!decode("\xED\xA0\x80", 3, &code_point, &next_position)); /* surrogate */
    CHECK(!decode("\xF4\x90\x80\x80", 4, &code_point, &next_position)); /* above U+10FFFF */
    CHECK(!decode("\xC3", 1, &code_point, &next_position));         /* truncated */
    CHECK(!decode("\xE2\x82\x41", 3, &code_point, &next_position)); /* invalid_offset continuation */
    CHECK_INT(next_position, 0);
}

static void t_reports_first_invalid_byte(void) {
    size_t invalid_offset = 99;
    CHECK(lisp_utf8_valid((const unsigned char *)"abc", 3, &invalid_offset));
    CHECK(!lisp_utf8_valid((const unsigned char *)"\xFF", 1, &invalid_offset));
    CHECK_INT(invalid_offset, 0);
    CHECK(!lisp_utf8_valid((const unsigned char *)"a\xC3", 2, &invalid_offset));
    CHECK_INT(invalid_offset, 1);
    CHECK(!lisp_utf8_valid((const unsigned char *)"\xC3\xA9x\xED\xA0\x80", 6, &invalid_offset));
    CHECK_INT(invalid_offset, 3);
}

static void t_encodes_round_trip(void) {
    const uint32_t samples[] = {0, 0x41, 0x7F, 0x80, 0x7FF, 0x800, 0xFFFF, 0x10000, 0x1F600, 0x10FFFF};
    for (size_t index = 0; index < sizeof samples / sizeof samples[0]; index++) {
        unsigned char bytes[4];
        size_t length = lisp_utf8_encode(samples[index], bytes);
        size_t position = 0;
        uint32_t code_point = 0;
        CHECK(lisp_utf8_next(bytes, length, &position, &code_point));
        CHECK_INT(code_point, samples[index]);
        CHECK_INT(position, length);
    }
}

static void t_counts_and_offsets(void) {
    const char *text = "a\xC3\xA9\xF0\x9F\x98\x80z"; /* a é 😀 z */
    CHECK_INT(lisp_utf8_count(text, 8), 4);
    CHECK_INT(lisp_utf8_offset(text, 8, 0), 0);
    CHECK_INT(lisp_utf8_offset(text, 8, 1), 1);
    CHECK_INT(lisp_utf8_offset(text, 8, 2), 3);
    CHECK_INT(lisp_utf8_offset(text, 8, 3), 7);
    CHECK_INT(lisp_utf8_offset(text, 8, 4), 8);
}

static void t_scalar_values(void) {
    CHECK(lisp_is_scalar(0));
    CHECK(lisp_is_scalar(0xD7FF));
    CHECK(!lisp_is_scalar(0xD800));
    CHECK(!lisp_is_scalar(0xDFFF));
    CHECK(lisp_is_scalar(0x10FFFF));
    CHECK(!lisp_is_scalar(0x110000));
}

const TestSuite utf8_suite = {"utf8", (const TestCase[]){
    {"decodes valid sequences", t_decodes_valid},
    {"rejects invalid sequences", t_rejects_invalid},
    {"reports the first invalid byte", t_reports_first_invalid_byte},
    {"encodes and decodes round trip", t_encodes_round_trip},
    {"counts code points and finds offsets", t_counts_and_offsets},
    {"recognizes scalar values", t_scalar_values},
    {},
}};
