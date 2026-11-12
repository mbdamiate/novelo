/* core §10 — printing for write! and display!. */
#pragma once

#include "buffer.h"
#include "value.h"

typedef enum LispPrintMode {
    LISP_PRINT_WRITE,   /* reads back (§10), used by write! and the REPL */
    LISP_PRINT_DISPLAY, /* a top-level String comes out raw; the rest as write! */
} LispPrintMode;

/* Appends the printed form of the value to the buffer. */
void lisp_print(LispBuffer *output, const Value *value, LispPrintMode mode);
