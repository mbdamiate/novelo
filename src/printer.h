/* core §10 — printing for write! and display!. */
#pragma once

#include "buffer.h"
#include "value.h"

typedef enum LmPrintMode {
    LM_PRINT_WRITE,   /* reads back (§10), used by write! and the REPL */
    LM_PRINT_DISPLAY, /* a top-level String comes out raw; the rest as write! */
} LmPrintMode;

/* Appends the printed form of the value to the buffer. */
void lm_print(LmBuffer *output, const Value *value, LmPrintMode mode);
