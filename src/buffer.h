/* Growable byte buffer, plus allocation helpers that abort when memory runs out. */
#pragma once

#include <stddef.h>
#include <stdint.h>

[[nodiscard]] void *lm_xmalloc(size_t size);
[[nodiscard]] void *lm_xcalloc(size_t count, size_t size);
[[nodiscard]] void *lm_xrealloc(void *pointer, size_t size);

/* The bytes are always followed by a NUL, which is not counted in length. */
typedef struct LmBuffer {
    char *data;
    size_t length;
    size_t capacity;
} LmBuffer;

void lm_buffer_init(LmBuffer *buffer);
void lm_buffer_free(LmBuffer *buffer);
void lm_buffer_clear(LmBuffer *buffer);
void lm_buffer_append(LmBuffer *buffer, const void *bytes, size_t length);
void lm_buffer_append_byte(LmBuffer *buffer, char byte);
void lm_buffer_append_string(LmBuffer *buffer, const char *text);
void lm_buffer_append_code_point(LmBuffer *buffer, uint32_t code_point);

[[gnu::format(printf, 2, 3)]]
void lm_buffer_appendf(LmBuffer *buffer, const char *format, ...);

/* Hands the bytes to the caller (who frees them) and resets the buffer. */
[[nodiscard]] char *lm_buffer_take(LmBuffer *buffer, size_t *length);
