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

void *lm_xmalloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) out_of_memory();
    return p;
}

void *lm_xcalloc(size_t count, size_t size) {
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p) out_of_memory();
    return p;
}

void *lm_xrealloc(void *pointer, size_t size) {
    void *p = realloc(pointer, size ? size : 1);
    if (!p) out_of_memory();
    return p;
}

void lm_buffer_init(LmBuffer *buffer) { *buffer = (LmBuffer){}; }

void lm_buffer_free(LmBuffer *buffer) {
    free(buffer->data);
    *buffer = (LmBuffer){};
}

void lm_buffer_clear(LmBuffer *buffer) {
    buffer->length = 0;
    if (buffer->data) buffer->data[0] = '\0';
}

static void reserve(LmBuffer *buffer, size_t extra) {
    size_t needed;
    if (ckd_add(&needed, buffer->length, extra) || ckd_add(&needed, needed, 1)) out_of_memory();
    if (needed <= buffer->capacity) return;
    size_t capacity = buffer->capacity ? buffer->capacity : 64;
    while (capacity < needed) {
        if (ckd_mul(&capacity, capacity, 2)) out_of_memory();
    }
    buffer->data = lm_xrealloc(buffer->data, capacity);
    buffer->capacity = capacity;
}

void lm_buffer_append(LmBuffer *buffer, const void *bytes, size_t length) {
    reserve(buffer, length);
    if (length) memcpy(buffer->data + buffer->length, bytes, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

void lm_buffer_append_byte(LmBuffer *buffer, char byte) { lm_buffer_append(buffer, &byte, 1); }

void lm_buffer_append_string(LmBuffer *buffer, const char *text) {
    lm_buffer_append(buffer, text, strlen(text));
}

void lm_buffer_append_code_point(LmBuffer *buffer, uint32_t cp) {
    unsigned char bytes[4];
    lm_buffer_append(buffer, bytes, lm_utf8_encode(cp, bytes));
}

void lm_buffer_appendf(LmBuffer *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (length > 0) {
        reserve(buffer, (size_t)length);
        vsnprintf(buffer->data + buffer->length, (size_t)length + 1, format, args);
        buffer->length += (size_t)length;
    }
    va_end(args);
}

char *lm_buffer_take(LmBuffer *buffer, size_t *length) {
    reserve(buffer, 0);
    char *data = buffer->data;
    if (length) *length = buffer->length;
    *buffer = (LmBuffer){};
    return data;
}
