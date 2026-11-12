#include "test.h"

#include "buffer.h"

#include <stdlib.h>

static void t_appends(void) {
    LmBuffer b;
    lm_buffer_init(&b);
    lm_buffer_append_string(&b, "ab");
    lm_buffer_append_byte(&b, 'c');
    lm_buffer_append(&b, "de", 2);
    CHECK_STR(b.data, "abcde");
    CHECK_INT(b.length, 5);
    lm_buffer_clear(&b);
    CHECK_STR(b.data, "");
    lm_buffer_free(&b);
}

static void t_grows(void) {
    LmBuffer b;
    lm_buffer_init(&b);
    for (int i = 0; i < 10000; i++) lm_buffer_append_byte(&b, 'x');
    CHECK_INT(b.length, 10000);
    CHECK_INT(b.data[9999], 'x');
    CHECK_INT(b.data[10000], '\0');
    lm_buffer_free(&b);
}

static void t_formats(void) {
    LmBuffer b;
    lm_buffer_init(&b);
    lm_buffer_appendf(&b, "%d-%s", 42, "x");
    lm_buffer_appendf(&b, "%s", "");
    CHECK_STR(b.data, "42-x");
    lm_buffer_free(&b);
}

static void t_code_points_and_take(void) {
    LmBuffer b;
    lm_buffer_init(&b);
    lm_buffer_append_code_point(&b, 0xE9);
    lm_buffer_append_code_point(&b, 0x1F600);
    size_t length = 0;
    char *bytes = lm_buffer_take(&b, &length);
    CHECK_INT(length, 6);
    CHECK_STR(bytes, "\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(b.data == nullptr);
    free(bytes);
}

const TestSuite buffer_suite = {"buffer", (const TestCase[]){
    {"appends bytes and strings", t_appends},
    {"grows", t_grows},
    {"formats", t_formats},
    {"appends code points and hands over the bytes", t_code_points_and_take},
    {},
}};
