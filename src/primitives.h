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

/* Shared argument checks: each sets :wrong-type and returns false. */
bool lisp_expect_kind(LispState *state, const char *function, const Value *value, LispKind kind,
                      const char *type_name);
