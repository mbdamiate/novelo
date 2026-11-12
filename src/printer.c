/* core §10 — printing.
 *
 * display! differs from write! only for a String given directly to it. The
 * spec leaves open how display! prints strings inside lists and maps (§12.4);
 * this implementation takes "everything else comes out as with write!"
 * literally, so nested strings keep their quotes and escapes.
 */
#include "printer.h"

#include <inttypes.h>

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
