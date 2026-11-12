/* core §10 — printing.
 *
 * display! differs from write! only for a String given directly to it. The
 * spec leaves open how display! prints strings inside lists and maps (§12.4);
 * this implementation takes "everything else comes out as with write!"
 * literally, so nested strings keep their quotes and escapes.
 */
#include "printer.h"

#include <inttypes.h>

static void print_string_literal(LmBuffer *output, const LmString *string) {
    lm_buffer_append_byte(output, '"');
    for (size_t position = 0; position < string->bytes; position++) {
        unsigned char byte = (unsigned char)string->data[position];
        switch (byte) {
        case '\n': lm_buffer_append_string(output, "\\n"); break;
        case '\t': lm_buffer_append_string(output, "\\t"); break;
        case '\r': lm_buffer_append_string(output, "\\r"); break;
        case '"': lm_buffer_append_string(output, "\\\""); break;
        case '\\': lm_buffer_append_string(output, "\\\\"); break;
        default:
            if (byte < 0x20 || byte == 0x7F) {
                lm_buffer_appendf(output, "\\u{%X}", byte); /* uppercase, no leading zeros */
            } else {
                lm_buffer_append_byte(output, (char)byte); /* other bytes, including UTF-8, as they are */
            }
        }
    }
    lm_buffer_append_byte(output, '"');
}

static void print_value(LmBuffer *output, const Value *value) {
    switch ((LmKind)value->kind) {
    case LM_KIND_BOOLEAN:
        lm_buffer_append_string(output, value == LM_TRUE ? "#true" : "#false");
        return;
    case LM_KIND_VOID:
        lm_buffer_append_string(output, "#void");
        return;
    case LM_KIND_INTEGER:
        lm_buffer_appendf(output, "%" PRId64, lm_integer_value(value));
        return;
    case LM_KIND_STRING:
        print_string_literal(output, lm_as_string(value));
        return;
    case LM_KIND_KEYWORD:
        lm_buffer_append_byte(output, ':');
        [[fallthrough]];
    case LM_KIND_SYMBOL:
        lm_buffer_append(output, lm_as_name(value)->text, lm_as_name(value)->bytes);
        return;
    case LM_KIND_EMPTY:
    case LM_KIND_PAIR: {
        lm_buffer_append_byte(output, '(');
        for (const Value *rest = value; rest->kind == LM_KIND_PAIR; rest = lm_rest(rest)) {
            if (rest != value) lm_buffer_append_byte(output, ' ');
            print_value(output, lm_first(rest));
        }
        lm_buffer_append_byte(output, ')');
        return;
    }
    case LM_KIND_MAP: {
        lm_buffer_append_byte(output, '{');
        Value *key, *entry_value;
        bool first_entry = true;
        for (size_t cursor = 0; lm_map_next(value, &cursor, &key, &entry_value);) {
            if (!first_entry) lm_buffer_append_byte(output, ' ');
            first_entry = false;
            print_value(output, key);
            lm_buffer_append_byte(output, ' ');
            print_value(output, entry_value);
        }
        lm_buffer_append_byte(output, '}');
        return;
    }
    case LM_KIND_CLOSURE:
    case LM_KIND_PRIMITIVE:
        lm_buffer_append_string(output, "#<function>");
        return;
    case LM_KIND_OPAQUE:
        lm_buffer_appendf(output, "#<%s>", lm_type_name(value));
        return;
    }
}

void lm_print(LmBuffer *output, const Value *value, LmPrintMode mode) {
    if (mode == LM_PRINT_DISPLAY && value->kind == LM_KIND_STRING) {
        lm_buffer_append(output, lm_as_string(value)->data, lm_as_string(value)->bytes);
        return;
    }
    print_value(output, value);
}
