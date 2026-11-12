/* core §8.8 — include contexts: prefixes, names of a context and resolution.
 *
 * A context is a sequence of prefixes, identified by its full prefix ("geo/u").
 * The empty context is represented by nullptr. Contexts live in a registry
 * owned by the state, so the pointers stay valid for the state's lifetime.
 */
#pragma once

#include "program.h"
#include "value.h"

typedef struct LispContext {
    LispOpaque base;
    Value *parent;      /* enclosing context, or nullptr for a child of the empty one */
    Value *prefix;      /* Symbol: the last prefix */
    Value *full_prefix; /* String: the prefixes joined by '/' */
    Value *names;       /* Map: name (Symbol) → global it stands for (Symbol) */
} LispContext;

/* The context `context` followed by `prefix`. Borrowed (owned by the registry). */
Value *lisp_context_child(LispState *state, Value *context, Value *prefix);

/* The global a symbol names in a context (§8.8): F/x if x is a name of the
 * context, otherwise x itself. Borrowed. */
Value *lisp_context_resolve(const Value *context, Value *symbol);

/* The full prefix of a context, "" for the empty one. Borrowed String, or
 * nullptr for the empty context. */
Value *lisp_context_full_prefix(const Value *context);

/* define / define-macro in a context (§8.8): records the name in each
 * context on the way out and binds the global with the full prefix. */
void lisp_context_define(LispState *state, Value *context, Value *name, Value *value, bool is_macro);

/* (include "path") or (include "path" prefix) at top level (§8.8). Returns
 * #void, or nullptr with :include-error, :expansion-error or an error of the
 * included forms pending. */
[[nodiscard]] Value *lisp_include(LispState *state, Value *form, Value *context);

/* An absolute path with its "." and ".." segments removed (§8.8): `path` as it
 * is if absolute, otherwise relative to `directory` (an absolute String).
 * Symbolic links are not resolved. Returns a new String. */
[[nodiscard]] Value *lisp_path_resolve(const Value *directory, const char *path);

/* The files still being loaded, for cycle detection. */
void lisp_loading_push(LispState *state, Value *absolute_path);
void lisp_loading_pop(LispState *state);
