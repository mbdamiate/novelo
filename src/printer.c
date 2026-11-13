/* core §10 — printing.
 *
 * display! differs from write! only for a String given directly to it. The
 * spec leaves open how display! prints strings inside lists and maps (§12.4);
 * this implementation takes "everything else comes out as with write!"
 * literally, so nested strings keep their quotes and escapes.
 */
#include "printer.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- floats (§10) ------------------------------------------------------------ */

/* Does mantissa × 10^exponent read back (§4.2) as `number`? */
static bool reads_back(uint64_t mantissa, int exponent, double number) {
    char text[48];
    snprintf(text, sizeof text, "%" PRIu64 "e%d", mantissa, exponent);
    return strtod(text, nullptr) == number;
}

/* Finds the shortest digits for a positive finite `number`: `mantissa` with
 * its decimal digits, worth mantissa × 10^exponent. For each length from 1 to
 * 17 digits, printf gives the correctly rounded digits, the closest of that
 * length (ties to even). They may fail to read back while a neighbor of the
 * same length does, because the interval of numbers that read back as
 * `number` is narrower below a power of two than above it; so the neighbors
 * are tried too. Seventeen digits always read back. */
static void shortest_digits(double number, uint64_t *mantissa, int *exponent) {
    for (int precision = 0; precision <= 16; precision++) {
        char text[48];
        snprintf(text, sizeof text, "%.*e", precision, number); /* "d.ddde±XX" */
        uint64_t digits = 0;
        const char *cursor = text;
        for (; *cursor != 'e'; cursor++) {
            if (*cursor != '.') digits = digits * 10 + (uint64_t)(*cursor - '0');
        }
        int scale = atoi(cursor + 1) - precision; /* number ≈ digits × 10^scale */
        uint64_t smallest = 1;                   /* 10^precision: the smallest of this length */
        for (int count = 0; count < precision; count++) smallest *= 10;

        if (reads_back(digits, scale, number)) {
            *mantissa = digits;
            *exponent = scale;
            return;
        }
        if (digits == smallest) { /* below 10^precision, the same length has a finer step */
            if (reads_back(smallest * 10 - 1, scale - 1, number)) {
                *mantissa = smallest * 10 - 1;
                *exponent = scale - 1;
                return;
            }
        } else if (reads_back(digits - 1, scale, number)) {
            *mantissa = digits - 1;
            *exponent = scale;
            return;
        }
        if (reads_back(digits + 1, scale, number)) {
            *mantissa = digits + 1;
            *exponent = scale;
            return;
        }
    }
    abort(); /* unreachable: 17 significant digits always read back */
}

void lisp_format_float(double number, char text[static LISP_FLOAT_TEXT_SIZE]) {
    char *output = text;
    if (signbit(number)) *output++ = '-';
    number = fabs(number);
    if (number == 0.0) {
        strcpy(output, "0.0");
        return;
    }

    uint64_t mantissa;
    int scale;
    shortest_digits(number, &mantissa, &scale);
    char digits[24];
    int count = snprintf(digits, sizeof digits, "%" PRIu64, mantissa);
    while (count > 1 && digits[count - 1] == '0') { /* 1000 × 10^-3 is 1 × 10^0 */
        digits[--count] = '\0';
        scale++;
    }
    int exponent = scale + count - 1; /* number = d1.d2…dn × 10^exponent */

    if (exponent >= -6 && exponent < 21) {
        if (exponent >= 0) {
            for (int index = 0; index <= exponent; index++) *output++ = index < count ? digits[index] : '0';
            *output++ = '.';
            if (count > exponent + 1) {
                for (int index = exponent + 1; index < count; index++) *output++ = digits[index];
            } else {
                *output++ = '0';
            }
        } else {
            *output++ = '0';
            *output++ = '.';
            for (int index = 0; index < -exponent - 1; index++) *output++ = '0';
            for (int index = 0; index < count; index++) *output++ = digits[index];
        }
        *output = '\0';
    } else {
        *output++ = digits[0];
        *output++ = '.';
        if (count > 1) {
            for (int index = 1; index < count; index++) *output++ = digits[index];
        } else {
            *output++ = '0';
        }
        snprintf(output, (size_t)(text + LISP_FLOAT_TEXT_SIZE - output), "e%d", exponent);
    }
}

/* ---- values ---------------------------------------------------------------------- */

static void print_string_literal(LispBuffer *output, const LispString *string) {
    lisp_buffer_append_byte(output, '"');
    for (size_t position = 0; position < string->bytes; position++) {
        unsigned char byte = (unsigned char)string->data[position];
        switch (byte) {
        case '\n': lisp_buffer_append_string(output, "\\n"); break;
        case '\t': lisp_buffer_append_string(output, "\\t"); break;
        case '\r': lisp_buffer_append_string(output, "\\r"); break;
        case '"': lisp_buffer_append_string(output, "\\\""); break;
        case '\\': lisp_buffer_append_string(output, "\\\\"); break;
        default:
            if (byte < 0x20 || byte == 0x7F) {
                lisp_buffer_appendf(output, "\\u{%X}", byte); /* uppercase, no leading zeros */
            } else {
                lisp_buffer_append_byte(output, (char)byte); /* other bytes, including UTF-8, as they are */
            }
        }
    }
    lisp_buffer_append_byte(output, '"');
}

static void print_value(LispBuffer *output, const Value *value) {
    switch ((LispKind)value->kind) {
    case LISP_KIND_BOOLEAN:
        lisp_buffer_append_string(output, value == LISP_TRUE ? "#true" : "#false");
        return;
    case LISP_KIND_VOID:
        lisp_buffer_append_string(output, "#void");
        return;
    case LISP_KIND_INTEGER:
        lisp_buffer_appendf(output, "%" PRId64, lisp_integer_value(value));
        return;
    case LISP_KIND_FLOAT: {
        char text[LISP_FLOAT_TEXT_SIZE];
        lisp_format_float(lisp_float_value(value), text);
        lisp_buffer_append_string(output, text);
        return;
    }
    case LISP_KIND_STRING:
        print_string_literal(output, lisp_as_string(value));
        return;
    case LISP_KIND_KEYWORD:
        lisp_buffer_append_byte(output, ':');
        [[fallthrough]];
    case LISP_KIND_SYMBOL:
        lisp_buffer_append(output, lisp_as_name(value)->text, lisp_as_name(value)->bytes);
        return;
    case LISP_KIND_EMPTY:
    case LISP_KIND_PAIR: {
        lisp_buffer_append_byte(output, '(');
        for (const Value *rest = value; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
            if (rest != value) lisp_buffer_append_byte(output, ' ');
            print_value(output, lisp_first(rest));
        }
        lisp_buffer_append_byte(output, ')');
        return;
    }
    case LISP_KIND_MAP: {
        lisp_buffer_append_byte(output, '{');
        Value *key, *entry_value;
        bool first_entry = true;
        for (size_t cursor = 0; lisp_map_next(value, &cursor, &key, &entry_value);) {
            if (!first_entry) lisp_buffer_append_byte(output, ' ');
            first_entry = false;
            print_value(output, key);
            lisp_buffer_append_byte(output, ' ');
            print_value(output, entry_value);
        }
        lisp_buffer_append_byte(output, '}');
        return;
    }
    case LISP_KIND_SET: {
        lisp_buffer_append_string(output, "#{");
        Value *element;
        for (size_t cursor = 0; lisp_set_next(value, &cursor, &element);) {
            if (cursor > 1) lisp_buffer_append_byte(output, ' ');
            print_value(output, element);
        }
        lisp_buffer_append_byte(output, '}');
        return;
    }
    case LISP_KIND_CLOSURE:
    case LISP_KIND_PRIMITIVE:
        lisp_buffer_append_string(output, "#<function>");
        return;
    case LISP_KIND_OPAQUE:
        lisp_buffer_appendf(output, "#<%s>", lisp_type_name(value));
        return;
    }
}

void lisp_print(LispBuffer *output, const Value *value, LispPrintMode mode) {
    if (mode == LISP_PRINT_DISPLAY && value->kind == LISP_KIND_STRING) {
        lisp_buffer_append(output, lisp_as_string(value)->data, lisp_as_string(value)->bytes);
        return;
    }
    print_value(output, value);
}
