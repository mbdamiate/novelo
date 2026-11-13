/* core §10 — printing for write! and display!. */
#pragma once

#include "buffer.h"
#include "value.h"

typedef enum LispPrintMode {
    LISP_PRINT_WRITE,   /* reads back (§10), used by write! and the REPL */
    LISP_PRINT_DISPLAY, /* a top-level String comes out raw; the rest as write! */
} LispPrintMode;

/* Longest text lisp_format_float writes, with its terminating NUL. */
constexpr size_t LISP_FLOAT_TEXT_SIZE = 40;

/* core §10 — writes the printed form of a finite Float to `text`: the
 * shortest decimal that reads back as `number`. */
void lisp_format_float(double number, char text[static LISP_FLOAT_TEXT_SIZE]);

/* Appends the printed form of the value to the buffer. */
void lisp_print(LispBuffer *output, const Value *value, LispPrintMode mode);
