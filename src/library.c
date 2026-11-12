/* core §8.9 — the library registry and import.
 *
 * An import goes through the same context rules as include (§8.8): it binds
 * the library's definitions in the context of the import followed by the
 * prefix, and it happens at most once per library and resulting context.
 */
#include "library.h"

#include "error.h"
#include "include.h"

#include <string.h>

static const LispLibrary *const libraries[] = {
    &lisp_library_io,
};

static const LispLibrary *find_library(const Value *name) {
    for (size_t index = 0; index < sizeof libraries / sizeof libraries[0]; index++) {
        if (strcmp(libraries[index]->name, lisp_as_name(name)->text) == 0) return libraries[index];
    }
    return nullptr;
}

/* Records (name full-prefix) as imported; false if it already was. */
static bool mark_imported(LispState *state, Value *name, Value *context) {
    Value *full_prefix = lisp_context_full_prefix(context);
    Value *key = full_prefix ? LISP_LIST(name, full_prefix) : LISP_LIST(name);
    bool first_time = lisp_map_get(state->imported, key) == nullptr;
    if (first_time) {
        Value *imported = lisp_map_associate(state->imported, key, LISP_TRUE);
        lisp_release(state->imported);
        state->imported = imported;
    }
    lisp_release(key);
    return first_time;
}

Value *lisp_import(LispState *state, Value *name, Value *prefix, Value *context) {
    const LispLibrary *library = find_library(name);
    if (!library) {
        return lisp_error(state, "import-error", LISP_LIST(name, lisp_keyword_cstr("unknown-library")),
                          "no library named %s", lisp_as_name(name)->text);
    }
    Value *target = prefix ? lisp_context_child(state, context, prefix) : context;
    if (!mark_imported(state, name, target)) return LISP_VOID;

    for (size_t index = 0; index < library->function_count; index++) {
        const LispNativeFunction *entry = &library->functions[index];
        Value *function = lisp_primitive(entry->name, entry->minimum_arguments, entry->maximum_arguments,
                                         entry->function);
        lisp_context_define(state, target, lisp_symbol_cstr(entry->name), function, false);
        lisp_release(function);
    }
    return LISP_VOID;
}
