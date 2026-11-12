/* core §8.9 — the library registry and import; stdlib §4 — loading the
 * libraries written in the language from the standard library document.
 *
 * An import goes through the same context rules as include (§8.8): it binds
 * the library's definitions in the context of the import followed by the
 * prefix, and it happens at most once per library and resulting context.
 *
 * The libraries list, text, function and map are not copied into C: their
 * source is read from the `lisp` blocks of standard-library.md §4.1–4.4, so the
 * normative definitions are what runs.
 */
#include "library.h"

#include "error.h"
#include "include.h"
#include "reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LISP_STDLIB_DEFAULT
#define LISP_STDLIB_DEFAULT "standard-library.md"
#endif

static const LispLibrary library_list = {.name = "list", .section = "4.1"};
static const LispLibrary library_text = {.name = "text", .section = "4.2"};
static const LispLibrary library_function = {.name = "function", .section = "4.3"};
static const LispLibrary library_map = {.name = "map", .section = "4.4"};

static const LispLibrary *const libraries[] = {
    &lisp_library_io,
    &library_list,
    &library_text,
    &library_function,
    &library_map,
};

constexpr size_t LIBRARY_COUNT = sizeof libraries / sizeof libraries[0];

const char *const lisp_library_names[] = {"io", "list", "text", "function", "map", nullptr};

const char *lisp_default_library_path(void) {
    const char *path = getenv("NOVELO_STDLIB");
    return path && *path ? path : LISP_STDLIB_DEFAULT;
}

static const LispLibrary *find_library(const Value *name) {
    for (size_t index = 0; index < LIBRARY_COUNT; index++) {
        if (strcmp(libraries[index]->name, lisp_as_name(name)->text) == 0) return libraries[index];
    }
    return nullptr;
}

/* The key of a library in a context: (name) or (name full-prefix). */
static Value *imported_key(Value *name, Value *context) {
    Value *full_prefix = lisp_context_full_prefix(context);
    return full_prefix ? LISP_LIST(name, full_prefix) : LISP_LIST(name);
}

static bool was_imported(const LispState *state, Value *name, Value *context) {
    Value *key = imported_key(name, context);
    bool imported = lisp_map_get(state->imported, key) != nullptr;
    lisp_release(key);
    return imported;
}

static void mark_imported(LispState *state, Value *name, Value *context) {
    Value *key = imported_key(name, context);
    Value *imported = lisp_map_associate(state->imported, key, LISP_TRUE);
    lisp_release(state->imported);
    state->imported = imported;
    lisp_release(key);
}

/* ---- the standard library document ----------------------------------------------------- */

static bool line_is(const char *line, size_t length, const char *text) {
    size_t text_length = strlen(text);
    while (length > 0 && (line[length - 1] == ' ' || line[length - 1] == '\r')) length--;
    return length == text_length && memcmp(line, text, length) == 0;
}

static bool line_starts_with(const char *line, size_t length, const char *text) {
    size_t text_length = strlen(text);
    return length >= text_length && memcmp(line, text, text_length) == 0;
}

bool lisp_library_source(const char *document, size_t length, const char *section, LispBuffer *source) {
    char heading[32];
    snprintf(heading, sizeof heading, "### %s ", section);
    bool in_section = false;
    bool in_code = false;
    bool found_code = false;
    size_t position = 0;
    while (position < length) {
        const char *line = document + position;
        const char *newline = memchr(line, '\n', length - position);
        size_t line_length = newline ? (size_t)(newline - line) : length - position;
        position += line_length + (newline ? 1 : 0);

        bool keep = false;
        if (in_code) {
            if (line_is(line, line_length, "```")) {
                in_code = false;
            } else {
                keep = true;
            }
        } else if (line_starts_with(line, line_length, "## ") || line_starts_with(line, line_length, "### ")) {
            in_section = line_starts_with(line, line_length, heading);
        } else if (in_section && line_is(line, line_length, "```lisp")) {
            in_code = true;
            found_code = true;
        }
        if (keep) lisp_buffer_append(source, line, line_length);
        lisp_buffer_append_byte(source, '\n');
    }
    return found_code;
}

/* Reads the forms of a library written in the language. */
static Value *library_forms(LispState *state, const LispLibrary *library, Value *name) {
    const char *path = lisp_as_string(state->library_path)->data;
    LispBuffer document;
    lisp_buffer_init(&document);
    LispBuffer source;
    lisp_buffer_init(&source);
    Value *forms = nullptr;
    LispReadError read_error;
    LispFileSystem disk = lisp_file_system_disk();
    if (!disk.read_file(disk.context, path, &document)) {
        lisp_error(state, "import-error", LISP_LIST(name, lisp_keyword_cstr("cannot-open")),
                   "cannot open the standard library %s (set NOVELO_STDLIB)", path);
    } else if (!lisp_library_source(document.data ? document.data : "", document.length, library->section,
                                    &source)) {
        lisp_error(state, "import-error", LISP_LIST(name, lisp_keyword_cstr("cannot-open")),
                   "no code for %s in section %s of %s", library->name, library->section, path);
    } else if (!lisp_read_file(source.data ? source.data : "", source.length, path, &forms, &read_error)) {
        Value *line = lisp_integer(read_error.position.line);
        Value *column = lisp_integer(read_error.position.column);
        lisp_error(state, "import-error",
                   LISP_LIST(name, lisp_keyword_cstr(lisp_read_error_name(read_error.code)), line, column),
                   "%s:%u:%u: %s", path, (unsigned)read_error.position.line, (unsigned)read_error.position.column,
                   read_error.message);
        lisp_release(line);
        lisp_release(column);
        forms = nullptr;
    }
    lisp_buffer_free(&source);
    lisp_buffer_free(&document);
    return forms;
}

static void bind_native(LispState *state, const LispLibrary *library, Value *target) {
    for (size_t index = 0; index < library->function_count; index++) {
        const LispNativeFunction *entry = &library->functions[index];
        Value *function = lisp_primitive(entry->name, entry->minimum_arguments, entry->maximum_arguments,
                                         entry->function);
        lisp_context_define(state, target, lisp_symbol_cstr(entry->name), function, false);
        lisp_release(function);
    }
}

Value *lisp_import(LispState *state, Value *name, Value *prefix, Value *context) {
    const LispLibrary *library = find_library(name);
    if (!library) {
        return lisp_error(state, "import-error", LISP_LIST(name, lisp_keyword_cstr("unknown-library")),
                          "no library named %s", lisp_as_name(name)->text);
    }
    Value *target = prefix ? lisp_context_child(state, context, prefix) : context;
    if (was_imported(state, name, target)) return LISP_VOID;

    if (!library->section) {
        mark_imported(state, name, target);
        bind_native(state, library, target);
        return LISP_VOID;
    }
    /* Marked once the source was read, like include, so an import that could
     * not find the document can be retried in the REPL. */
    Value *forms = library_forms(state, library, name);
    if (!forms) return nullptr;
    mark_imported(state, name, target);
    Value *result = LISP_VOID;
    for (const Value *rest = forms; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
        Value *value = lisp_process_top_level(state, lisp_first(rest), target);
        if (!value) {
            result = nullptr;
            break;
        }
        lisp_release(value);
    }
    lisp_release(forms);
    return result;
}
