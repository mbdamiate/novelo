#include "test.h"

#include "buffer.h"

#include <stdlib.h>

static void t_appends(void) {
    LmBuffer buffer;
    lm_buffer_init(&buffer);
    lm_buffer_append_string(&buffer, "ab");
    lm_buffer_append_byte(&buffer, 'c');
    lm_buffer_append(&buffer, "de", 2);
    CHECK_STR(buffer.data, "abcde");
    CHECK_INT(buffer.length, 5);
    lm_buffer_clear(&buffer);
    CHECK_STR(buffer.data, "");
    lm_buffer_free(&buffer);
}

static void t_grows(void) {
    LmBuffer buffer;
    lm_buffer_init(&buffer);
    for (int count = 0; count < 10000; count++) lm_buffer_append_byte(&buffer, 'x');
    CHECK_INT(buffer.length, 10000);
    CHECK_INT(buffer.data[9999], 'x');
    CHECK_INT(buffer.data[10000], '\0');
    lm_buffer_free(&buffer);
}

static void t_formats(void) {
    LmBuffer buffer;
    lm_buffer_init(&buffer);
    lm_buffer_appendf(&buffer, "%d-%s", 42, "x");
    lm_buffer_appendf(&buffer, "%s", "");
    CHECK_STR(buffer.data, "42-x");
    lm_buffer_free(&buffer);
}

static void t_code_points_and_take(void) {
    LmBuffer buffer;
    lm_buffer_init(&buffer);
    lm_buffer_append_code_point(&buffer, 0xE9);
    lm_buffer_append_code_point(&buffer, 0x1F600);
    size_t length = 0;
    char *bytes = lm_buffer_take(&buffer, &length);
    CHECK_INT(length, 6);
    CHECK_STR(bytes, "\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(buffer.data == nullptr);
    free(bytes);
}

const TestSuite buffer_suite = {"buffer", (const TestCase[]){
    {"appends bytes and strings", t_appends},
    {"grows", t_grows},
    {"formats", t_formats},
    {"appends code points and hands over the bytes", t_code_points_and_take},
    {},
}};
