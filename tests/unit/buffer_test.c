#include "test.h"

#include "buffer.h"

#include <stdlib.h>

static void t_appends(void) {
    LispBuffer buffer;
    lisp_buffer_init(&buffer);
    lisp_buffer_append_string(&buffer, "ab");
    lisp_buffer_append_byte(&buffer, 'c');
    lisp_buffer_append(&buffer, "de", 2);
    CHECK_STR(buffer.data, "abcde");
    CHECK_INT(buffer.length, 5);
    lisp_buffer_clear(&buffer);
    CHECK_STR(buffer.data, "");
    lisp_buffer_free(&buffer);
}

static void t_grows(void) {
    LispBuffer buffer;
    lisp_buffer_init(&buffer);
    for (int count = 0; count < 10000; count++) lisp_buffer_append_byte(&buffer, 'x');
    CHECK_INT(buffer.length, 10000);
    CHECK_INT(buffer.data[9999], 'x');
    CHECK_INT(buffer.data[10000], '\0');
    lisp_buffer_free(&buffer);
}

static void t_formats(void) {
    LispBuffer buffer;
    lisp_buffer_init(&buffer);
    lisp_buffer_appendf(&buffer, "%d-%s", 42, "x");
    lisp_buffer_appendf(&buffer, "%s", "");
    CHECK_STR(buffer.data, "42-x");
    lisp_buffer_free(&buffer);
}

static void t_code_points_and_take(void) {
    LispBuffer buffer;
    lisp_buffer_init(&buffer);
    lisp_buffer_append_code_point(&buffer, 0xE9);
    lisp_buffer_append_code_point(&buffer, 0x1F600);
    size_t length = 0;
    char *bytes = lisp_buffer_take(&buffer, &length);
    CHECK_INT(length, 6);
    CHECK_STR(bytes, "\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(buffer.data == nullptr);
    free(bytes);
}

static void t_take_empty(void) {
    LispBuffer buffer;
    lisp_buffer_init(&buffer);
    size_t length = 99;
    char *bytes = lisp_buffer_take(&buffer, &length);
    CHECK_INT(length, 0);
    CHECK_STR(bytes, "");
    free(bytes);
}

const TestSuite buffer_suite = {"buffer", (const TestCase[]){
    {"appends bytes and strings", t_appends},
    {"grows", t_grows},
    {"formats", t_formats},
    {"appends code points and hands over the bytes", t_code_points_and_take},
    {"hands over an empty buffer as an empty string", t_take_empty},
    {},
}};
