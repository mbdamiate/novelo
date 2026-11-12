/* Growable byte buffer, plus allocation helpers that abort when memory runs out. */
#pragma once

#include <stddef.h>
#include <stdint.h>

[[nodiscard]] void *lisp_xmalloc(size_t size);
[[nodiscard]] void *lisp_xcalloc(size_t count, size_t size);
[[nodiscard]] void *lisp_xrealloc(void *pointer, size_t size);

/* The bytes are always followed by a NUL, which is not counted in length. */
typedef struct LispBuffer {
    char *data;
    size_t length;
    size_t capacity;
} LispBuffer;

void lisp_buffer_init(LispBuffer *buffer);
void lisp_buffer_free(LispBuffer *buffer);
void lisp_buffer_clear(LispBuffer *buffer);
void lisp_buffer_append(LispBuffer *buffer, const void *bytes, size_t length);
void lisp_buffer_append_byte(LispBuffer *buffer, char byte);
void lisp_buffer_append_string(LispBuffer *buffer, const char *text);
void lisp_buffer_append_code_point(LispBuffer *buffer, uint32_t code_point);

[[gnu::format(printf, 2, 3)]]
void lisp_buffer_appendf(LispBuffer *buffer, const char *format, ...);

/* Hands the bytes to the caller (who frees them) and resets the buffer. */
[[nodiscard]] char *lisp_buffer_take(LispBuffer *buffer, size_t *length);
