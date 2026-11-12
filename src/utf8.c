/* core §3.2 — UTF-8 decoding and encoding. */
#include "utf8.h"

static bool is_continuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

bool lm_utf8_next(const unsigned char *s, size_t n, size_t *i, uint32_t *cp) {
    size_t at = *i;
    unsigned char lead = s[at];
    if (lead < 0x80) {
        *cp = lead;
        *i = at + 1;
        return true;
    }

    size_t length;
    uint32_t value;
    unsigned char low = 0x80, high = 0xBF; /* allowed range of the 2nd byte */
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        value = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        value = lead & 0x0F;
        if (lead == 0xE0) low = 0xA0;  /* no overlongs */
        if (lead == 0xED) high = 0x9F; /* no surrogates */
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        value = lead & 0x07;
        if (lead == 0xF0) low = 0x90;  /* no overlongs */
        if (lead == 0xF4) high = 0x8F; /* nothing above U+10FFFF */
    } else {
        return false;
    }

    if (n - at < length) return false;
    unsigned char second = s[at + 1];
    if (second < low || second > high) return false;
    value = (value << 6) | (second & 0x3F);
    for (size_t k = 2; k < length; k++) {
        if (!is_continuation(s[at + k])) return false;
        value = (value << 6) | (s[at + k] & 0x3F);
    }
    *cp = value;
    *i = at + length;
    return true;
}

bool lm_utf8_valid(const unsigned char *s, size_t n, size_t *bad) {
    size_t i = 0;
    uint32_t cp;
    while (i < n) {
        if (!lm_utf8_next(s, n, &i, &cp)) {
            if (bad) *bad = i;
            return false;
        }
    }
    return true;
}

size_t lm_utf8_encode(uint32_t cp, unsigned char out[static 4]) {
    if (cp < 0x80) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (unsigned char)(0xF0 | (cp >> 18));
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t lm_utf8_count(const char *s, size_t n) {
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (!is_continuation((unsigned char)s[i])) count++;
    }
    return count;
}

size_t lm_utf8_offset(const char *s, size_t n, size_t index) {
    size_t i = 0;
    while (i < n) {
        if (!is_continuation((unsigned char)s[i])) {
            if (index == 0) return i;
            index--;
        }
        i++;
    }
    return n;
}
