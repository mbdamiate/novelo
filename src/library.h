/* core §8.9 — libraries and import; stdlib §1, §4 — the libraries written in
 * the language. */
#pragma once

#include "buffer.h"
#include "primitives.h"
#include "program.h"

typedef struct LispLibrary {
    const char *name;
    /* Native: the functions, bound in this order. */
    const LispNativeFunction *functions;
    size_t function_count;
    /* Written in the language: the heading of its section in the standard
     * library document (stdlib §4), e.g. "4.1"; nullptr for a native one. */
    const char *section;
} LispLibrary;

/* The library io (library_io.c). */
extern const LispLibrary lisp_library_io;

/* The names of every library, in the order the REPL imports them (core §8.7),
 * ending with nullptr. */
extern const char *const lisp_library_names[];

/* (import name) or (import name prefix) at top level, already checked for
 * shape. Returns #void, or nullptr with :import-error (or an error raised
 * while loading) pending. */
[[nodiscard]] Value *lisp_import(LispState *state, Value *name, Value *prefix, Value *context);

/* The path of the standard library document: $LISP_MIN_STDLIB if set,
 * otherwise the path fixed at build time. */
const char *lisp_default_library_path(void);

/* stdlib §4 — the source of a library written in the language: the `lisp`
 * code blocks of the section whose heading is "### <section> ...", in order.
 * Every other line of the document becomes an empty line, so positions in the
 * source are positions in the document. Returns false if there is no such
 * section or it has no code. */
bool lisp_library_source(const char *document, size_t length, const char *section, LispBuffer *source);
