/* core §8.9, §9, §10 — the native library io: write!, display!, read-line!. */
#include "library.h"

#include "error.h"
#include "printer.h"
#include "utf8.h"

static Value *print_value(LispState *state, const Value *value, LispPrintMode mode) {
    LispBuffer text;
    lisp_buffer_init(&text);
    lisp_print(&text, value, mode);
    state->output.write(state->output.context, text.data ? text.data : "", text.length);
    lisp_buffer_free(&text);
    return LISP_VOID;
}

static Value *write_value(LispState *state, size_t, Value *const *arguments) {
    return print_value(state, arguments[0], LISP_PRINT_WRITE);
}

static Value *display_value(LispState *state, size_t, Value *const *arguments) {
    return print_value(state, arguments[0], LISP_PRINT_DISPLAY);
}

/* A line without its U+000A, or #void at the end of input. */
static Value *read_line(LispState *state, size_t, Value *const *) {
    if (!state->input.read_line(state->input.context, &state->line)) return LISP_VOID;
    const char *bytes = state->line.data ? state->line.data : "";
    size_t invalid_offset;
    if (!lisp_utf8_valid((const unsigned char *)bytes, state->line.length, &invalid_offset)) {
        return lisp_error(state, "io-error", LISP_EMPTY, "read-line!: invalid UTF-8 at byte %zu of the line",
                          invalid_offset);
    }
    return lisp_string(bytes, state->line.length);
}

static const LispNativeFunction io_functions[] = {
    {"write!", 1, 1, write_value},
    {"display!", 1, 1, display_value},
    {"read-line!", 0, 0, read_line},
};

const LispLibrary lisp_library_io = {
    .name = "io",
    .functions = io_functions,
    .function_count = sizeof io_functions / sizeof io_functions[0],
};
