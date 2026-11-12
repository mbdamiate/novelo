/* core §8.7 — the interpreter state and the top level of a program. */
#pragma once

#include "io.h"
#include "reader.h"
#include "value.h"

#include <stddef.h>

/* A global binding (§8.2). Macros share the table: a name holds either a
 * value or a macro transformer, and the last definition wins. */
typedef struct LispGlobal {
    Value *name;   /* Symbol; nullptr marks an empty slot */
    Value *value;
    bool is_macro;
} LispGlobal;

typedef struct LispGlobals {
    LispGlobal *entries;
    size_t count;
    size_t capacity; /* zero or a power of two */
} LispGlobals;

struct LispState {
    Value *error;          /* pending error descriptor (§8.5), or nullptr */
    LispGlobals globals;
    Value *contexts;       /* Map: full prefix (String) → context (§8.8) */
    Value *imported;       /* Map: (library full-prefix) → #true (§8.9) */
    LispOutput output;
    LispInput input;
    LispFileSystem files;
    Value *working_directory; /* String: absolute; resolves relative paths outside files (§8.8) */
    Value *loading;        /* List of Strings: absolute paths of the files still loading, innermost first */
    Value *included;       /* Map: (path full-prefix) → #true, files loaded per context (§8.8) */
    LispBuffer line;       /* scratch line for read-line! */
    const char *stack_base; /* address near the top of the thread's stack */
    size_t stack_limit;     /* bytes of stack evaluation may use */
};

typedef struct LispStateOptions {
    LispOutput output;
    LispInput input;
    LispFileSystem files;          /* default: the disk */
    const char *working_directory; /* absolute; default: the process's current directory */
    /* Bytes of C stack the interpreter may use, counted from the frame that
     * calls lisp_run_source; deeper non-tail recursion raises :stack-overflow.
     * Zero means a conservative default that fits a default thread. */
    size_t stack_limit;
} LispStateOptions;

[[nodiscard]] LispState *lisp_state_new(const LispStateOptions *options);
void lisp_state_free(LispState *state);

typedef enum LispRunResult {
    LISP_RUN_OK,
    LISP_RUN_READ_ERROR, /* *read_error is filled; nothing ran (§8.7) */
    LISP_RUN_ERROR,      /* an uncaught error; state->error holds the descriptor */
} LispRunResult;

/* Reads the whole source, then processes each top-level form in turn
 * (§8.7). `path` names the file the source came from, relative to the working
 * directory; includes resolve against its directory, and including it again
 * is a cycle. Without a path (the REPL), includes resolve against the working
 * directory. On LISP_RUN_OK, *last_value (if not null) receives the value of
 * the last form, or #void when there are none. */
LispRunResult lisp_run_source(LispState *state, const char *source, size_t length, const char *path,
                              Value **last_value, LispReadError *read_error);

/* Processes one top-level form in a context (nullptr: the empty context).
 * Returns its value (a new reference), or nullptr with an error pending. */
[[nodiscard]] Value *lisp_process_top_level(LispState *state, Value *form, Value *context);

/* Runs function(argument) on a new thread with a large stack (512 MB) and
 * waits for it. Returns false if the thread cannot be created. */
bool lisp_run_on_large_stack(void (*function)(void *argument), void *argument);

/* The stack size lisp_run_on_large_stack provides, minus a safety margin;
 * pass it as LispStateOptions.stack_limit for states used on such a thread. */
size_t lisp_large_stack_limit(void);
