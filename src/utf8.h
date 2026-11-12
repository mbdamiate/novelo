/* core §3.2 — UTF-8 decoding and encoding.
 *
 * Decoding is strict (Unicode Table 3-7): overlong forms, surrogates and code
 * points above U+10FFFF are invalid. When a sequence is invalid, the reported
 * position is that of its first byte, the "first invalid byte" of §3.2.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint32_t LM_MAX_CODE_POINT = 0x10FFFF;

/* True for Unicode scalar values: 0..U+10FFFF without the surrogates. */
static inline bool lm_is_scalar(uint32_t cp) {
    return cp <= LM_MAX_CODE_POINT && !(cp >= 0xD800 && cp <= 0xDFFF);
}

/* Decodes the code point at s[*i] (requires *i < n). On success stores it in
 * *cp, advances *i past it and returns true. On failure returns false and
 * leaves *i at the invalid byte. */
bool lm_utf8_next(const unsigned char *s, size_t n, size_t *i, uint32_t *cp);

/* Checks that s[0..n) is valid UTF-8. On failure stores the offset of the
 * first invalid byte in *bad (if not null). */
bool lm_utf8_valid(const unsigned char *s, size_t n, size_t *bad);

/* Encodes a scalar value; returns the number of bytes written (1 to 4). */
size_t lm_utf8_encode(uint32_t cp, unsigned char out[static 4]);

/* Number of code points in valid UTF-8 text. */
size_t lm_utf8_count(const char *s, size_t n);

/* Byte offset of the code point with the given index in valid UTF-8 text;
 * index may equal the code point count, which gives n. */
size_t lm_utf8_offset(const char *s, size_t n, size_t index);
