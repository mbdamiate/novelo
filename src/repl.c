/* core §8.7 — the REPL: read a complete input (possibly several lines),
 * evaluate it at top level, print with write!, report errors and go on. */
#include "repl.h"

#include "error.h"
#include "library.h"
#include "printer.h"
#include "reader.h"

#include <string.h>

static void write_text(LispOutput output, const char *text) {
    if (text) output.write(output.context, text, strlen(text));
}

static void write_buffer(LispOutput output, const LispBuffer *buffer) {
    output.write(output.context, buffer->data ? buffer->data : "", buffer->length);
}

/* Reports the pending error as "line:column: error :kind: message values…"
 * (positions count from the start of the input, §8.5). */
static void report_error(LispState *state, const LispReplOptions *options) {
    Value *descriptor = lisp_take_error(state);
    LispBuffer message;
    lisp_buffer_init(&message);
    lisp_describe_uncaught_error(&message, descriptor);
    lisp_buffer_append_byte(&message, '\n');
    write_buffer(options->errors, &message);
    lisp_buffer_free(&message);
    lisp_release(descriptor);
}

static void report_read_error(const LispReplOptions *options, const LispReadError *read_error) {
    LispBuffer message;
    lisp_buffer_init(&message);
    lisp_describe_read_error(&message, nullptr, read_error);
    lisp_buffer_append_byte(&message, '\n');
    write_buffer(options->errors, &message);
    lisp_buffer_free(&message);
}

/* The session begins as if with (import io), (import list), (import text)
 * and (import function) in the empty context. */
static void import_libraries(LispState *state, const LispReplOptions *options) {
    for (const char *const *name = lisp_library_names; *name; name++) {
        Value *result = lisp_import(state, lisp_symbol_cstr(*name), nullptr, nullptr);
        if (result) {
            lisp_release(result);
        } else {
            report_error(state, options);
        }
    }
}

/* Evaluates the datums of one input in turn; an error discards the rest. */
static void evaluate(LispState *state, const LispReplOptions *options, Value *datums) {
    for (const Value *rest = datums; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
        Value *value = lisp_process_top_level(state, lisp_first(rest), nullptr);
        if (!value) {
            report_error(state, options);
            return;
        }
        if (value != LISP_VOID) {
            LispBuffer printed;
            lisp_buffer_init(&printed);
            lisp_print(&printed, value, LISP_PRINT_WRITE);
            lisp_buffer_append_byte(&printed, '\n');
            write_buffer(state->output, &printed);
            lisp_buffer_free(&printed);
        }
        lisp_release(value);
    }
}

void lisp_repl(LispState *state, const LispReplOptions *options) {
    import_libraries(state, options);
    LispBuffer input; /* the lines of the current input */
    lisp_buffer_init(&input);
    LispBuffer line;
    lisp_buffer_init(&line);
    for (;;) {
        write_text(state->output, input.length == 0 ? options->prompt : options->continuation_prompt);
        lisp_buffer_clear(&line);
        if (!state->input.read_line(state->input.context, &line)) break;
        lisp_buffer_append(&input, line.data ? line.data : "", line.length);
        lisp_buffer_append_byte(&input, '\n');

        Value *datums;
        LispReadError read_error;
        if (lisp_read(input.data, input.length, &datums, &read_error)) {
            evaluate(state, options, datums);
            lisp_release(datums);
        } else if (lisp_read_error_is_incomplete(read_error.code)) {
            continue; /* a collection or string is still open: read one more line */
        } else {
            report_read_error(options, &read_error);
        }
        lisp_buffer_clear(&input);
    }
    /* The input ended with a datum still open. */
    if (input.length > 0) {
        Value *datums;
        LispReadError read_error;
        if (!lisp_read(input.data, input.length, &datums, &read_error)) {
            report_read_error(options, &read_error);
        } else {
            lisp_release(datums);
        }
    }
    lisp_buffer_free(&line);
    lisp_buffer_free(&input);
}
