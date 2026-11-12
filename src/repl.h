/* core §8.7 — the REPL. */
#pragma once

#include "io.h"
#include "program.h"

typedef struct LispReplOptions {
    LispOutput errors;               /* where messages for read and uncaught errors go */
    const char *prompt;              /* written before each input; nullptr for none */
    const char *continuation_prompt; /* written before each further line of an open input */
} LispReplOptions;

/* Imports every library (a library that cannot be loaded is reported and
 * skipped), then reads inputs from state->input until its end, evaluating
 * each datum at top level in the empty context and printing each value other
 * than #void with the write! rules to state->output, followed by a newline. A
 * read error or an uncaught error is reported and the rest of that input is
 * discarded. */
void lisp_repl(LispState *state, const LispReplOptions *options);
