/* core §8.8 — including files: contexts, prefixes, paths, load once, cycles. */
#include "include.h"

#include "buffer.h"
#include "error.h"
#include "eval.h"
#include "expander.h"
#include "reader.h"

#include <string.h>

static void drop_context(Value *self) {
    LispContext *context = (LispContext *)self;
    lisp_release(context->parent);
    lisp_release(context->prefix);
    lisp_release(context->full_prefix);
    lisp_release(context->names);
}

static const LispClass context_class = {"Context", drop_context};

static const LispContext *as_context(const Value *context) { return (const LispContext *)context; }

Value *lisp_context_full_prefix(const Value *context) {
    return context ? as_context(context)->full_prefix : nullptr;
}

Value *lisp_context_child(LispState *state, Value *context, Value *prefix) {
    LispBuffer full;
    lisp_buffer_init(&full);
    if (context) {
        const LispString *parent_prefix = lisp_as_string(as_context(context)->full_prefix);
        lisp_buffer_append(&full, parent_prefix->data, parent_prefix->bytes);
        lisp_buffer_append_byte(&full, '/');
    }
    lisp_buffer_append(&full, lisp_as_name(prefix)->text, lisp_as_name(prefix)->bytes);
    Value *full_prefix = lisp_string(full.data, full.length);
    lisp_buffer_free(&full);

    /* Two contexts with the same full prefix are the same context (§8.8). */
    Value *existing = lisp_map_get(state->contexts, full_prefix);
    if (existing) {
        lisp_release(full_prefix);
        return existing;
    }
    LispContext *child = (LispContext *)lisp_opaque_new(&context_class, sizeof(LispContext));
    child->parent = lisp_retain(context);
    child->prefix = lisp_retain(prefix);
    child->full_prefix = full_prefix;
    child->names = lisp_map_new(0);
    Value *registry = lisp_map_associate(state->contexts, full_prefix, &child->base.header);
    lisp_release(state->contexts);
    state->contexts = registry;
    lisp_release(&child->base.header); /* the registry holds it now */
    return &child->base.header;
}

Value *lisp_context_resolve(const Value *context, Value *symbol) {
    if (!context) return symbol;
    Value *global = lisp_map_get(as_context(context)->names, symbol);
    return global ? global : symbol;
}

/* The Symbol prefix/name. */
static Value *prefixed_name(const Value *prefix, const Value *name) {
    LispBuffer text;
    lisp_buffer_init(&text);
    lisp_buffer_append(&text, lisp_as_name(prefix)->text, lisp_as_name(prefix)->bytes);
    lisp_buffer_append_byte(&text, '/');
    lisp_buffer_append(&text, lisp_as_name(name)->text, lisp_as_name(name)->bytes);
    Value *symbol = lisp_symbol(text.data, text.length);
    lisp_buffer_free(&text);
    return symbol;
}

/* The Symbol F/name, where F is the full prefix of the context. */
static Value *full_name(const Value *context, const Value *name) {
    const LispString *prefix = lisp_as_string(as_context(context)->full_prefix);
    LispBuffer text;
    lisp_buffer_init(&text);
    lisp_buffer_append(&text, prefix->data, prefix->bytes);
    lisp_buffer_append_byte(&text, '/');
    lisp_buffer_append(&text, lisp_as_name(name)->text, lisp_as_name(name)->bytes);
    Value *symbol = lisp_symbol(text.data, text.length);
    lisp_buffer_free(&text);
    return symbol;
}

void lisp_context_define(LispState *state, Value *context, Value *name, Value *value, bool is_macro) {
    while (context) {
        LispContext *current = (LispContext *)context;
        /* Record the name in this context, standing for the global F/name. */
        Value *global = full_name(context, name);
        Value *names = lisp_map_associate(current->names, name, global);
        lisp_release(global);
        lisp_release(current->names);
        current->names = names;
        /* Then define prefix/name in the context before the prefix. */
        name = prefixed_name(current->prefix, name);
        context = current->parent;
    }
    lisp_global_define(state, name, value, is_macro);
}

/* ---- paths ---------------------------------------------------------------------------- */

Value *lisp_path_resolve(const Value *directory, const char *path) {
    LispBuffer joined;
    lisp_buffer_init(&joined);
    if (path[0] != '/') {
        const LispString *base = lisp_as_string(directory);
        lisp_buffer_append(&joined, base->data, base->bytes);
        lisp_buffer_append_byte(&joined, '/');
    }
    lisp_buffer_append_string(&joined, path);

    /* Walk the segments, dropping "" and "." and letting ".." remove the
     * previous one (never above the root). */
    LispBuffer normalized;
    lisp_buffer_init(&normalized);
    const char *cursor = joined.data;
    while (*cursor) {
        while (*cursor == '/') cursor++;
        const char *end = cursor;
        while (*end && *end != '/') end++;
        size_t length = (size_t)(end - cursor);
        if (length == 2 && cursor[0] == '.' && cursor[1] == '.') {
            while (normalized.length > 0 && normalized.data[normalized.length - 1] != '/') normalized.length--;
            if (normalized.length > 0) normalized.length--; /* the '/' before the segment */
            if (normalized.data) normalized.data[normalized.length] = '\0';
        } else if (length > 0 && !(length == 1 && cursor[0] == '.')) {
            lisp_buffer_append_byte(&normalized, '/');
            lisp_buffer_append(&normalized, cursor, length);
        }
        cursor = end;
    }
    if (normalized.length == 0) lisp_buffer_append_byte(&normalized, '/');
    Value *result = lisp_string(normalized.data, normalized.length);
    lisp_buffer_free(&normalized);
    lisp_buffer_free(&joined);
    return result;
}

/* The directory part of an absolute path, as a new String. */
static Value *directory_of(const Value *path) {
    const LispString *text = lisp_as_string(path);
    size_t length = text->bytes;
    while (length > 0 && text->data[length - 1] != '/') length--;
    if (length > 1) length--; /* keep "/" for files at the root */
    return lisp_string(text->data, length);
}

/* ---- files being loaded ----------------------------------------------------------------- */

void lisp_loading_push(LispState *state, Value *absolute_path) {
    state->loading = lisp_prepend_take(lisp_retain(absolute_path), state->loading);
}

void lisp_loading_pop(LispState *state) {
    Value *rest = lisp_retain(lisp_rest(state->loading));
    lisp_release(state->loading);
    state->loading = rest;
}

static bool is_loading(const LispState *state, const Value *absolute_path) {
    for (const Value *rest = state->loading; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
        if (lisp_equal(lisp_first(rest), absolute_path)) return true;
    }
    return false;
}

/* The key of a file in a context: (path) or (path full-prefix). */
static Value *included_key(Value *absolute_path, Value *context) {
    Value *full_prefix = lisp_context_full_prefix(context);
    return full_prefix ? LISP_LIST(absolute_path, full_prefix) : LISP_LIST(absolute_path);
}

static bool was_included(const LispState *state, Value *absolute_path, Value *context) {
    Value *key = included_key(absolute_path, context);
    bool included = lisp_map_get(state->included, key) != nullptr;
    lisp_release(key);
    return included;
}

/* Marked once the file has been read, so a file that could not be opened or
 * read can be included again later (in the REPL). */
static void mark_included(LispState *state, Value *absolute_path, Value *context) {
    Value *key = included_key(absolute_path, context);
    Value *included = lisp_map_associate(state->included, key, LISP_TRUE);
    lisp_release(state->included);
    state->included = included;
    lisp_release(key);
}

/* ---- include ------------------------------------------------------------------------------ */

static Value *include_error(LispState *state, Value *values, const char *message) {
    return lisp_error(state, "include-error", values, "%s", message);
}

/* Processes the forms of a file, already read, in the target context. */
static Value *load_forms(LispState *state, Value *forms, Value *absolute_path, Value *target) {
    lisp_loading_push(state, absolute_path);
    Value *result = LISP_VOID;
    for (const Value *rest = forms; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
        Value *value = lisp_process_top_level(state, lisp_first(rest), target);
        if (!value) {
            result = nullptr; /* an error in an included form keeps its own :kind */
            break;
        }
        lisp_release(value);
    }
    lisp_loading_pop(state);
    return result;
}

Value *lisp_include(LispState *state, Value *form, Value *context) {
    size_t length = lisp_list_length(form);
    Value *path = length >= 2 ? lisp_first(lisp_rest(form)) : nullptr;
    Value *prefix = length == 3 ? lisp_first(lisp_rest(lisp_rest(form))) : nullptr;
    bool valid = (length == 2 || length == 3) && path->kind == LISP_KIND_STRING
                 && (!prefix || prefix->kind == LISP_KIND_SYMBOL);
    if (!valid) {
        return lisp_expansion_error(state, form, "include must be (include \"path\") or (include \"path\" prefix)");
    }

    const LispString *path_text = lisp_as_string(path);
    if (strlen(path_text->data) != path_text->bytes) { /* a NUL cannot name a file */
        return include_error(state, LISP_LIST(path, lisp_keyword_cstr("cannot-open")), "cannot open the file");
    }
    /* Relative to the directory of the file that contains the include; in
     * the REPL, to the working directory. */
    Value *directory = state->loading->kind == LISP_KIND_PAIR ? directory_of(lisp_first(state->loading))
                                                              : lisp_retain(state->working_directory);
    Value *absolute_path = lisp_path_resolve(directory, path_text->data);
    lisp_release(directory);
    Value *target = prefix ? lisp_context_child(state, context, prefix) : context;

    Value *result = nullptr;
    LispBuffer contents;
    lisp_buffer_init(&contents);
    Value *forms = nullptr;
    LispReadError read_error;
    if (is_loading(state, absolute_path)) {
        include_error(state, LISP_LIST(path, lisp_keyword_cstr("cycle")), "the file is still being loaded");
    } else if (was_included(state, absolute_path, target)) {
        result = LISP_VOID; /* already loaded in this context */
    } else if (!state->files.read_file(state->files.context, lisp_as_string(absolute_path)->data, &contents)) {
        include_error(state, LISP_LIST(path, lisp_keyword_cstr("cannot-open")), "cannot open the file");
    } else if (!lisp_read_file(contents.data ? contents.data : "", contents.length,
                               lisp_as_string(absolute_path)->data, &forms, &read_error)) {
        Value *line = lisp_integer(read_error.position.line);
        Value *column = lisp_integer(read_error.position.column);
        include_error(state, LISP_LIST(path, lisp_keyword_cstr(lisp_read_error_name(read_error.code)), line, column),
                      read_error.message);
        lisp_release(line);
        lisp_release(column);
    } else {
        mark_included(state, absolute_path, target);
        result = load_forms(state, forms, absolute_path, target);
    }
    lisp_release(forms);
    lisp_buffer_free(&contents);
    lisp_release(absolute_path);
    return result;
}
