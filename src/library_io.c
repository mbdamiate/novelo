/* core §8.7, §8.9, §9, §10 — the native library io: write!, display!,
   read-line!, read-lines!, write-to-error!, display-to-error!,
   program-arguments!, exit!, read-file!, write-file!, append-file!,
   list-directory!, file-kind!. */
#include "library.h"

#include "error.h"
#include "include.h"
#include "primitives.h"
#include "printer.h"
#include "utf8.h"

#include <stdlib.h>
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

/* ---- files (§9) ---------------------------------------------------------------------------- */

/* (:ok value), taking the value. */
static Value *ok_take(Value *value) {
    Value *result = LISP_LIST(lisp_keyword_cstr("ok"), value);
    lisp_release(value);
    return result;
}

/* (:error reason path extra…), with the extra values taken. */
static Value *failure_take(const char *reason, Value *path, size_t extra_count, Value **extra) {
    Value *tail = LISP_EMPTY;
    for (size_t index = extra_count; index > 0; index--) tail = lisp_prepend_take(extra[index - 1], tail);
    tail = lisp_prepend_take(lisp_retain(path), tail);
    tail = lisp_prepend_take(lisp_keyword_cstr(reason), tail);
    return lisp_prepend_take(lisp_keyword_cstr("error"), tail);
}

static const char *status_reason(LispFileStatus status) {
    switch (status) {
    case LISP_FILE_NOT_FOUND:
        return "not-found";
    case LISP_FILE_PERMISSION_DENIED:
        return "permission-denied";
    case LISP_FILE_NOT_A_FILE:
        return "not-a-file";
    case LISP_FILE_NOT_A_DIRECTORY:
        return "not-a-directory";
    default:
        return "failed";
    }
}

/* An expected failure as a result; a failure of the system as :io-error. */
static Value *file_failure(LispState *state, const char *name, LispFileStatus status, Value *path) {
    if (status == LISP_FILE_FAILED) {
        return lisp_error(state, "io-error", LISP_LIST(lisp_keyword_cstr("failed"), path),
                          "%s: the system failed on %s", name, lisp_as_string(path)->data);
    }
    return failure_take(status_reason(status), path, 0, nullptr);
}

/* The path argument made absolute against the working directory (§9), or
   nullptr with :wrong-type pending. A path holding U+0000 names no file:
   *absolute stays nullptr and the call returns true. */
static bool file_path(LispState *state, const char *name, Value *path, Value **absolute) {
    *absolute = nullptr;
    if (!lisp_expect_kind(state, name, path, LISP_KIND_STRING, "String")) return false;
    const LispString *text = lisp_as_string(path);
    if (memchr(text->data, '\0', text->bytes)) return true;
    *absolute = lisp_path_resolve(state->working_directory, text->data);
    return true;
}

/* Line and column (§6.1) of the byte at `offset`, in text valid up to it. */
static void text_position(const char *text, size_t offset, int64_t *line, int64_t *column) {
    *line = 1;
    *column = 1;
    for (size_t index = 0; index < offset; index++) {
        if (text[index] == '\n') {
            (*line)++;
            *column = 1;
        } else if (((unsigned char)text[index] & 0xC0) != 0x80) {
            (*column)++;
        }
    }
}

static Value *read_file(LispState *state, size_t, Value *const *arguments) {
    Value *absolute;
    if (!file_path(state, "read-file!", arguments[0], &absolute)) return nullptr;
    if (!absolute) return failure_take("not-found", arguments[0], 0, nullptr);
    LispBuffer contents;
    lisp_buffer_init(&contents);
    LispFileStatus status = state->files.read_file(state->files.context, lisp_as_string(absolute)->data, &contents);
    lisp_release(absolute);
    Value *result;
    size_t invalid_offset;
    const char *bytes = contents.data ? contents.data : "";
    if (status != LISP_FILE_OK) {
        result = file_failure(state, "read-file!", status, arguments[0]);
    } else if (!lisp_utf8_valid((const unsigned char *)bytes, contents.length, &invalid_offset)) {
        int64_t line, column;
        text_position(bytes, invalid_offset, &line, &column);
        Value *position[] = {lisp_integer(line), lisp_integer(column)};
        result = failure_take("invalid-utf8", arguments[0], 2, position);
    } else {
        result = ok_take(lisp_string(bytes, contents.length));
    }
    lisp_buffer_free(&contents);
    return result;
}

static Value *store_file(LispState *state, const char *name, Value *const *arguments, bool append) {
    Value *absolute;
    if (!file_path(state, name, arguments[0], &absolute)) return nullptr;
    if (!lisp_expect_kind(state, name, arguments[1], LISP_KIND_STRING, "String")) {
        lisp_release(absolute);
        return nullptr;
    }
    if (!absolute) return failure_take("not-found", arguments[0], 0, nullptr);
    const LispString *text = lisp_as_string(arguments[1]);
    LispFileStatus status = state->files.write_file(state->files.context, lisp_as_string(absolute)->data, text->data,
                                                    text->bytes, append);
    lisp_release(absolute);
    if (status != LISP_FILE_OK) return file_failure(state, name, status, arguments[0]);
    return ok_take(LISP_VOID);
}

static Value *write_file(LispState *state, size_t, Value *const *arguments) {
    return store_file(state, "write-file!", arguments, false);
}

static Value *append_file(LispState *state, size_t, Value *const *arguments) {
    return store_file(state, "append-file!", arguments, true);
}

static int compare_names(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* The names, sorted by code point: for UTF-8, the order of the bytes. */
static Value *list_directory(LispState *state, size_t, Value *const *arguments) {
    Value *absolute;
    if (!file_path(state, "list-directory!", arguments[0], &absolute)) return nullptr;
    if (!absolute) return failure_take("not-found", arguments[0], 0, nullptr);
    LispBuffer names;
    lisp_buffer_init(&names);
    LispFileStatus status =
        state->files.list_directory(state->files.context, lisp_as_string(absolute)->data, &names);
    lisp_release(absolute);
    if (status != LISP_FILE_OK) {
        lisp_buffer_free(&names);
        return file_failure(state, "list-directory!", status, arguments[0]);
    }
    size_t count = 0;
    for (size_t offset = 0; offset < names.length; offset += strlen(names.data + offset) + 1) count++;
    const char **sorted = lisp_xmalloc((count ? count : 1) * sizeof(char *));
    size_t index = 0;
    for (size_t offset = 0; offset < names.length; offset += strlen(names.data + offset) + 1) {
        sorted[index++] = names.data + offset;
    }
    qsort(sorted, count, sizeof(char *), compare_names);
    Value *result = LISP_EMPTY;
    for (index = count; index > 0 && result; index--) {
        size_t length = strlen(sorted[index - 1]);
        size_t invalid_offset;
        if (!lisp_utf8_valid((const unsigned char *)sorted[index - 1], length, &invalid_offset)) {
            lisp_release(result);
            result = lisp_error(state, "io-error", LISP_LIST(lisp_keyword_cstr("invalid-utf8"), arguments[0]),
                                "list-directory!: a name in %s is not valid UTF-8",
                                lisp_as_string(arguments[0])->data);
        } else {
            result = lisp_prepend_take(lisp_string(sorted[index - 1], length), result);
        }
    }
    free(sorted);
    lisp_buffer_free(&names);
    return result ? ok_take(result) : nullptr;
}

static Value *file_kind(LispState *state, size_t, Value *const *arguments) {
    Value *absolute;
    if (!file_path(state, "file-kind!", arguments[0], &absolute)) return nullptr;
    if (!absolute) return lisp_keyword_cstr("none");
    LispFileKind kind;
    LispFileStatus status = state->files.kind(state->files.context, lisp_as_string(absolute)->data, &kind);
    lisp_release(absolute);
    if (status != LISP_FILE_OK) {
        return lisp_error(state, "io-error", LISP_LIST(lisp_keyword_cstr(status_reason(status)), arguments[0]),
                          "file-kind!: cannot examine %s", lisp_as_string(arguments[0])->data);
    }
    return lisp_keyword_cstr(kind == LISP_FILE_KIND_FILE ? "file" : kind == LISP_FILE_KIND_DIRECTORY ? "directory"
                                                                                                     : "none");
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
    {"read-file!", 1, 1, read_file},
    {"write-file!", 2, 2, write_file},
    {"append-file!", 2, 2, append_file},
    {"list-directory!", 1, 1, list_directory},
    {"file-kind!", 1, 1, file_kind},
};

const LispLibrary lisp_library_io = {
    .name = "io",
    .functions = io_functions,
    .function_count = sizeof io_functions / sizeof io_functions[0],
};
