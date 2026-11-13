/* core §9 — primitives bound in the global environment. */
#pragma once

#include "program.h"

/* A native function as a library or the global table lists it. */
typedef struct LispNativeFunction {
    const char *name;
    int minimum_arguments;
    int maximum_arguments; /* -1: no maximum */
    LispPrimitiveFunction function;
} LispNativeFunction;

/* Binds every §9 primitive except those of the library io. */
void lisp_install_primitives(LispState *state);

/* §8.3 — a new primitive value for the table entry `name`, or nullptr. The
 * expansion of match calls these values directly, so rebinding the global
 * names does not change it. */
[[nodiscard]] Value *lisp_primitive_value(const char *name);

/* §8.3 — the primitive that raises :no-match with (value). */
[[nodiscard]] Value *lisp_no_match_primitive(void);

/* Shared argument checks: each sets :wrong-type and returns false. */
bool lisp_expect_kind(LispState *state, const char *function, const Value *value, LispKind kind,
                      const char *type_name);
