/* core §8.2–8.3 — expansion of datums into nodes. */
#pragma once

#include "program.h"
#include "value.h"

/* The interned symbols the expander and the top level give meaning to. */
typedef struct LispSymbols {
    Value *quote, *if_, *otherwise, *lambda, *rest_marker;
    Value *define, *define_macro, *let, *sequence, *and_, *or_;
    Value *quasiquote, *unquote, *unquote_splicing;
    Value *include, *import, *declare, *declare_type;
    Value *match, *try_, *wildcard;
} LispSymbols;

const LispSymbols *lisp_symbols(void);

/* Expands an expression (not a top-level form) in a context (nullptr: the
 * empty context) into a node. Returns a new node, or nullptr with an
 * :expansion-error (or a macro's error) pending. */
[[nodiscard]] Value *lisp_expand(LispState *state, Value *form, Value *context);

/* §8.5 — if the form is a List or Map with a recorded position, makes it the
 * position of the innermost form being processed (state->expansion_position). */
void lisp_note_position(LispState *state, const Value *form);

/* If form is a macro call in the context, stores its expansion (one step, a
 * new reference) in *expansion; otherwise stores nullptr. Returns false if
 * the macro failed. Used by the top level, which must see the forms a macro
 * produces before expanding them (§8.7). */
[[nodiscard]] bool lisp_expand_macro_call(LispState *state, Value *form, Value *context, Value **expansion);

/* Sets an :expansion-error about form and returns nullptr. */
Value *lisp_expansion_error(LispState *state, Value *form, const char *message);
