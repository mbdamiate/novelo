/* core §3.2 — UTF-8 decoding and encoding.
 *
 * Decoding is strict (Unicode Table 3-7): overlong forms, surrogates and code
 * points above U+10FFFF are invalid. When a sequence is invalid, the reported
 * position is that of its first byte, the "first invalid byte" of §3.2.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint32_t LISP_MAX_CODE_POINT = 0x10FFFF;

/* True for Unicode scalar values: 0..U+10FFFF without the surrogates. */
static inline bool lisp_is_scalar(uint32_t code_point) {
    return code_point <= LISP_MAX_CODE_POINT && !(code_point >= 0xD800 && code_point <= 0xDFFF);
}

/* Decodes the code point at bytes[*position] (requires *position < length).
 * On success stores it in *code_point, advances *position past it and returns
 * true. On failure returns false and leaves *position at the invalid byte. */
bool lisp_utf8_next(const unsigned char *bytes, size_t length, size_t *position, uint32_t *code_point);

/* Checks that bytes[0..length) is valid UTF-8. On failure stores the offset of
 * the first invalid byte in *invalid_offset (if not null). */
bool lisp_utf8_valid(const unsigned char *bytes, size_t length, size_t *invalid_offset);

/* Encodes a scalar value; returns the number of bytes written (1 to 4). */
size_t lisp_utf8_encode(uint32_t code_point, unsigned char output[static 4]);

/* Number of code points in valid UTF-8 text. */
size_t lisp_utf8_count(const char *text, size_t length);

/* Byte offset of the code point with the given index in valid UTF-8 text;
 * the index may equal the code point count, which gives length. */
size_t lisp_utf8_offset(const char *text, size_t length, size_t index);
