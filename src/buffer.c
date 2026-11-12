/* Growable byte buffer and allocation helpers. */
#include "buffer.h"

#include "utf8.h"

#include <stdarg.h>
#include <stdckdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

[[noreturn]] static void out_of_memory(void) {
    fputs("error: out of memory\n", stderr);
    abort();
}

void *lisp_xmalloc(size_t size) {
    void *memory = malloc(size ? size : 1);
    if (!memory) out_of_memory();
    return memory;
}

void *lisp_xcalloc(size_t count, size_t size) {
    void *memory = calloc(count ? count : 1, size ? size : 1);
    if (!memory) out_of_memory();
    return memory;
}

void *lisp_xrealloc(void *pointer, size_t size) {
    void *memory = realloc(pointer, size ? size : 1);
    if (!memory) out_of_memory();
    return memory;
}

void lisp_buffer_init(LispBuffer *buffer) { *buffer = (LispBuffer){}; }

void lisp_buffer_free(LispBuffer *buffer) {
    free(buffer->data);
    *buffer = (LispBuffer){};
}

void lisp_buffer_clear(LispBuffer *buffer) {
    buffer->length = 0;
    if (buffer->data) buffer->data[0] = '\0';
}

static void reserve(LispBuffer *buffer, size_t extra) {
    size_t needed;
    if (ckd_add(&needed, buffer->length, extra) || ckd_add(&needed, needed, 1)) out_of_memory();
    if (needed <= buffer->capacity) return;
    size_t capacity = buffer->capacity ? buffer->capacity : 64;
    while (capacity < needed) {
        if (ckd_mul(&capacity, capacity, 2)) out_of_memory();
    }
    buffer->data = lisp_xrealloc(buffer->data, capacity);
    buffer->capacity = capacity;
}

void lisp_buffer_append(LispBuffer *buffer, const void *bytes, size_t length) {
    reserve(buffer, length);
    if (length) memcpy(buffer->data + buffer->length, bytes, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

void lisp_buffer_append_byte(LispBuffer *buffer, char byte) { lisp_buffer_append(buffer, &byte, 1); }

void lisp_buffer_append_string(LispBuffer *buffer, const char *text) {
    lisp_buffer_append(buffer, text, strlen(text));
}

void lisp_buffer_append_code_point(LispBuffer *buffer, uint32_t code_point) {
    unsigned char bytes[4];
    lisp_buffer_append(buffer, bytes, lisp_utf8_encode(code_point, bytes));
}

void lisp_buffer_appendf(LispBuffer *buffer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int length = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (length > 0) {
        reserve(buffer, (size_t)length);
        vsnprintf(buffer->data + buffer->length, (size_t)length + 1, format, arguments);
        buffer->length += (size_t)length;
    }
    va_end(arguments);
}

char *lisp_buffer_take(LispBuffer *buffer, size_t *length) {
    reserve(buffer, 0);
    buffer->data[buffer->length] = '\0'; /* an empty buffer may never have been written */
    char *data = buffer->data;
    if (length) *length = buffer->length;
    *buffer = (LispBuffer){};
    return data;
}
