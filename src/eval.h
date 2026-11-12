/* core §8.1, §8.2, §8.4 — evaluation, environments, application, globals. */
#pragma once

#include "program.h"
#include "value.h"

/* Evaluates an expanded node in a local environment (nullptr: none) and an
 * include context (nullptr: the empty context). Returns a new reference, or
 * nullptr with an error pending. Calls in tail position reuse the loop, so
 * tail recursion runs in constant memory (§8.4). */
[[nodiscard]] Value *lisp_eval(LispState *state, Value *node, Value *environment, Value *context);

/* Calls a function with arguments (borrowed). */
[[nodiscard]] Value *lisp_apply(LispState *state, Value *function, size_t argument_count,
                                Value *const *arguments);

/* ---- environments ---------------------------------------------------------- */

typedef struct LispEnvironment {
    LispOpaque base;
    Value *parent;   /* enclosing environment, or nullptr */
    size_t count;
    Value *values[]; /* parameters, the &rest list last */
} LispEnvironment;

/* ---- globals (§8.2) ---------------------------------------------------------- */

/* The binding of a global name, or nullptr. Borrowed. */
LispGlobal *lisp_global_find(LispState *state, const Value *name);

/* Binds a global name to a value or a macro transformer (retained), replacing
 * any previous binding. */
void lisp_global_define(LispState *state, Value *name, Value *value, bool is_macro);

void lisp_globals_free(LispGlobals *globals);

/* True while the C stack has room for another nested evaluation; otherwise
 * sets :stack-overflow and returns false. */
bool lisp_check_stack(LispState *state);
