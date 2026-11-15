/* core §8.7, §8.9, §9, §10 — the native library io: write!, display!,
   read-line!, read-lines!, write-to-error!, display-to-error!,
   program-arguments!, exit!. */
#include "library.h"

#include "error.h"
#include "primitives.h"
#include "printer.h"
#include "utf8.h"

#include <string.h>

static Value *print_value(LispOutput output, const Value *value, LispPrintMode mode) {
    LispBuffer text;
    lisp_buffer_init(&text);
    lisp_print(&text, value, mode);
    output.write(output.context, text.data ? text.data : "", text.length);
    lisp_buffer_free(&text);
    return LISP_VOID;
}

static Value *write_value(LispState *state, size_t, Value *const *arguments) {
    return print_value(state->output, arguments[0], LISP_PRINT_WRITE);
}

static Value *display_value(LispState *state, size_t, Value *const *arguments) {
    return print_value(state->output, arguments[0], LISP_PRINT_DISPLAY);
}

static Value *write_to_error(LispState *state, size_t, Value *const *arguments) {
    return print_value(state->error_output, arguments[0], LISP_PRINT_WRITE);
}

static Value *display_to_error(LispState *state, size_t, Value *const *arguments) {
    return print_value(state->error_output, arguments[0], LISP_PRINT_DISPLAY);
}

/* The line just read into state->line as a String, or an :io-error when it
   is not valid UTF-8. */
static Value *line_string(LispState *state, const char *name) {
    const char *bytes = state->line.data ? state->line.data : "";
    size_t invalid_offset;
    if (!lisp_utf8_valid((const unsigned char *)bytes, state->line.length, &invalid_offset)) {
        return lisp_error(state, "io-error", LISP_LIST(lisp_keyword_cstr("invalid-utf8")),
                          "%s: invalid UTF-8 at byte %zu of the line", name, invalid_offset);
    }
    return lisp_string(bytes, state->line.length);
}

/* A line without its U+000A, or #void at the end of input. */
static Value *read_line(LispState *state, size_t, Value *const *) {
    if (!state->input.read_line(state->input.context, &state->line)) return LISP_VOID;
    return line_string(state, "read-line!");
}

/* Every line left, in order; an invalid line drops the ones read before it. */
static Value *read_lines(LispState *state, size_t, Value *const *) {
    LispListBuilder lines;
    lisp_list_builder_init(&lines);
    while (state->input.read_line(state->input.context, &state->line)) {
        Value *line = line_string(state, "read-lines!");
        if (line == nullptr) {
            lisp_list_builder_discard(&lines);
            return nullptr;
        }
        lisp_list_append_take(&lines, line);
    }
    return lisp_list_builder_finish(&lines);
}

/* §8.7 — the arguments the program was started with, as a new List. */
static Value *program_arguments(LispState *state, size_t, Value *const *) {
    LispListBuilder arguments;
    lisp_list_builder_init(&arguments);
    for (size_t index = 0; index < state->argument_count; index++) {
        const char *argument = state->arguments[index];
        size_t length = strlen(argument);
        size_t invalid_offset;
        if (!lisp_utf8_valid((const unsigned char *)argument, length, &invalid_offset)) {
            lisp_list_builder_discard(&arguments);
            Value *position = lisp_integer((int64_t)index);
            Value *values = LISP_LIST(lisp_keyword_cstr("invalid-utf8"), position);
            lisp_release(position);
            return lisp_error(state, "io-error", values,
                              "program-arguments!: argument %zu is not valid UTF-8 at byte %zu", index,
                              invalid_offset);
        }
        lisp_list_append_take(&arguments, lisp_string(argument, length));
    }
    return lisp_list_builder_finish(&arguments);
}

/* §9 — ends the program. It returns nullptr like an error, with no
   descriptor pending, so every caller unwinds and releases what it holds;
   catch lets it through (§8.5). */
static Value *exit_program(LispState *state, size_t, Value *const *arguments) {
    Value *code = arguments[0];
    if (!lisp_expect_kind(state, "exit!", code, LISP_KIND_INTEGER, "Integer")) return nullptr;
    int64_t value = lisp_integer_value(code);
    if (value < 0 || value > 255) {
        return lisp_error(state, "domain-error", LISP_LIST(code), "exit! expects a code from 0 to 255");
    }
    state->exiting = true;
    state->exit_code = (int)value;
    return nullptr;
}

static const LispNativeFunction io_functions[] = {
    {"write!", 1, 1, write_value},
    {"display!", 1, 1, display_value},
    {"read-line!", 0, 0, read_line},
    {"read-lines!", 0, 0, read_lines},
    {"write-to-error!", 1, 1, write_to_error},
    {"display-to-error!", 1, 1, display_to_error},
    {"program-arguments!", 0, 0, program_arguments},
    {"exit!", 1, 1, exit_program},
};

const LispLibrary lisp_library_io = {
    .name = "io",
    .functions = io_functions,
    .function_count = sizeof io_functions / sizeof io_functions[0],
};
