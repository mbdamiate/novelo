/* core §8.5 — error descriptors. */
#include "error.h"

#include "printer.h"
#include "program.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

Value *lisp_list_of(size_t count, Value *const *items) {
    Value *list = LISP_EMPTY;
    for (size_t index = count; index > 0; index--) list = lisp_prepend_take(lisp_retain(items[index - 1]), list);
    return list;
}

static void set_pending(LispState *state, Value *descriptor) {
    lisp_release(state->error);
    state->error = descriptor;
}

Value *lisp_error(LispState *state, const char *kind, Value *values, const char *format, ...) {
    LispBuffer message;
    lisp_buffer_init(&message);
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int length = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (length > 0) {
        char *text = lisp_xmalloc((size_t)length + 1);
        vsnprintf(text, (size_t)length + 1, format, arguments);
        lisp_buffer_append(&message, text, (size_t)length);
        free(text);
    }
    va_end(arguments);

    Value *descriptor = lisp_map_new(3);
    Value *message_string = lisp_string(message.data ? message.data : "", message.length);
    lisp_buffer_free(&message);
    (void)lisp_map_insert_take(&descriptor, lisp_keyword_cstr("kind"), lisp_keyword_cstr(kind));
    (void)lisp_map_insert_take(&descriptor, lisp_keyword_cstr("message"), message_string);
    (void)lisp_map_insert_take(&descriptor, lisp_keyword_cstr("values"), values);
    set_pending(state, descriptor);
    return nullptr;
}

Value *lisp_error_from_descriptor(LispState *state, Value *descriptor) {
    set_pending(state, descriptor);
    return nullptr;
}

void lisp_error_locate(LispState *state, LispPosition position) {
    if (position.line == 0 || !state->error || state->error->kind != LISP_KIND_MAP) return;
    Value *key = lisp_keyword_cstr("position");
    if (lisp_map_get(state->error, key)) return;
    Value *where = lisp_map_new(3);
    (void)lisp_map_insert_take(&where, lisp_keyword_cstr("line"), lisp_integer(position.line));
    (void)lisp_map_insert_take(&where, lisp_keyword_cstr("column"), lisp_integer(position.column));
    const char *file = lisp_source_file_name(position.file);
    if (file) (void)lisp_map_insert_take(&where, lisp_keyword_cstr("file"), lisp_string_cstr(file));
    Value *descriptor = lisp_map_associate(state->error, key, where);
    lisp_release(where);
    set_pending(state, descriptor);
}

Value *lisp_take_error(LispState *state) {
    Value *descriptor = state->error;
    state->error = nullptr;
    return descriptor;
}

Value *lisp_error_kind(const LispState *state) {
    if (!state->error) return nullptr;
    return lisp_map_get(state->error, lisp_keyword_cstr("kind"));
}

void lisp_describe_error(LispBuffer *output, const Value *descriptor) {
    Value *kind = lisp_map_get(descriptor, lisp_keyword_cstr("kind"));
    Value *message = lisp_map_get(descriptor, lisp_keyword_cstr("message"));
    Value *values = lisp_map_get(descriptor, lisp_keyword_cstr("values"));
    if (kind) lisp_print(output, kind, LISP_PRINT_WRITE);
    if (message && message->kind == LISP_KIND_STRING) {
        lisp_buffer_append_string(output, ": ");
        lisp_print(output, message, LISP_PRINT_DISPLAY);
    }
    if (values && values->kind == LISP_KIND_PAIR) {
        for (const Value *rest = values; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
            lisp_buffer_append_byte(output, ' ');
            lisp_print(output, lisp_first(rest), LISP_PRINT_WRITE);
        }
    }
}

void lisp_describe_uncaught_error(LispBuffer *output, const Value *descriptor) {
    /* A descriptor built by the program may hold anything under :position. */
    Value *position = lisp_map_get(descriptor, lisp_keyword_cstr("position"));
    if (position && position->kind == LISP_KIND_MAP) {
        Value *file = lisp_map_get(position, lisp_keyword_cstr("file"));
        Value *line = lisp_map_get(position, lisp_keyword_cstr("line"));
        Value *column = lisp_map_get(position, lisp_keyword_cstr("column"));
        if (line && column && line->kind == LISP_KIND_INTEGER && column->kind == LISP_KIND_INTEGER) {
            if (file && file->kind == LISP_KIND_STRING) {
                lisp_print(output, file, LISP_PRINT_DISPLAY);
                lisp_buffer_append_byte(output, ':');
            }
            char coordinates[48];
            snprintf(coordinates, sizeof coordinates, "%lld:%lld: ", (long long)lisp_integer_value(line),
                     (long long)lisp_integer_value(column));
            lisp_buffer_append_string(output, coordinates);
        }
    }
    lisp_buffer_append_string(output, "error ");
    lisp_describe_error(output, descriptor);
}
