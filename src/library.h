/* core §8.9 — libraries and import. */
#pragma once

#include "primitives.h"
#include "program.h"

typedef struct LispLibrary {
    const char *name;
    /* Native: the functions, bound in this order. */
    const LispNativeFunction *functions;
    size_t function_count;
} LispLibrary;

/* The library io (library_io.c). */
extern const LispLibrary lisp_library_io;

/* (import name) or (import name prefix) at top level, already checked for
 * shape. Returns #void, or nullptr with :import-error (or an error raised
 * while loading) pending. */
[[nodiscard]] Value *lisp_import(LispState *state, Value *name, Value *prefix, Value *context);
