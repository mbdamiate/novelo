/* core §3.2 — UTF-8 decoding and encoding. */
#include "utf8.h"

static bool is_continuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

bool lm_utf8_next(const unsigned char *bytes, size_t length, size_t *position, uint32_t *code_point) {
    size_t start = *position;
    unsigned char lead = bytes[start];
    if (lead < 0x80) {
        *code_point = lead;
        *position = start + 1;
        return true;
    }

    size_t sequence_length;
    uint32_t value;
    unsigned char second_minimum = 0x80, second_maximum = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        sequence_length = 2;
        value = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        sequence_length = 3;
        value = lead & 0x0F;
        if (lead == 0xE0) second_minimum = 0xA0; /* no overlongs */
        if (lead == 0xED) second_maximum = 0x9F; /* no surrogates */
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        sequence_length = 4;
        value = lead & 0x07;
        if (lead == 0xF0) second_minimum = 0x90; /* no overlongs */
        if (lead == 0xF4) second_maximum = 0x8F; /* nothing above U+10FFFF */
    } else {
        return false;
    }

    if (length - start < sequence_length) return false;
    unsigned char second = bytes[start + 1];
    if (second < second_minimum || second > second_maximum) return false;
    value = (value << 6) | (second & 0x3F);
    for (size_t offset = 2; offset < sequence_length; offset++) {
        unsigned char continuation = bytes[start + offset];
        if (!is_continuation(continuation)) return false;
        value = (value << 6) | (continuation & 0x3F);
    }
    *code_point = value;
    *position = start + sequence_length;
    return true;
}

bool lm_utf8_valid(const unsigned char *bytes, size_t length, size_t *invalid_offset) {
    size_t position = 0;
    uint32_t code_point;
    while (position < length) {
        if (!lm_utf8_next(bytes, length, &position, &code_point)) {
            if (invalid_offset) *invalid_offset = position;
            return false;
        }
    }
    return true;
}

size_t lm_utf8_encode(uint32_t code_point, unsigned char output[static 4]) {
    if (code_point < 0x80) {
        output[0] = (unsigned char)code_point;
        return 1;
    }
    if (code_point < 0x800) {
        output[0] = (unsigned char)(0xC0 | (code_point >> 6));
        output[1] = (unsigned char)(0x80 | (code_point & 0x3F));
        return 2;
    }
    if (code_point < 0x10000) {
        output[0] = (unsigned char)(0xE0 | (code_point >> 12));
        output[1] = (unsigned char)(0x80 | ((code_point >> 6) & 0x3F));
        output[2] = (unsigned char)(0x80 | (code_point & 0x3F));
        return 3;
    }
    output[0] = (unsigned char)(0xF0 | (code_point >> 18));
    output[1] = (unsigned char)(0x80 | ((code_point >> 12) & 0x3F));
    output[2] = (unsigned char)(0x80 | ((code_point >> 6) & 0x3F));
    output[3] = (unsigned char)(0x80 | (code_point & 0x3F));
    return 4;
}

size_t lm_utf8_count(const char *text, size_t length) {
    size_t count = 0;
    for (size_t position = 0; position < length; position++) {
        if (!is_continuation((unsigned char)text[position])) count++;
    }
    return count;
}

size_t lm_utf8_offset(const char *text, size_t length, size_t index) {
    for (size_t position = 0; position < length; position++) {
        if (!is_continuation((unsigned char)text[position])) {
            if (index == 0) return position;
            index--;
        }
    }
    return length;
}
