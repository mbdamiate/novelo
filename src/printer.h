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

/* The most decimals lisp_format_fixed accepts (core §9, float-to-fixed). */
constexpr int LISP_FIXED_MAXIMUM_DECIMALS = 20;

/* Longest text lisp_format_fixed writes, with its terminating NUL: a sign,
 * a carry into a new leading digit, the 309 digits before the point of the
 * largest Float, the point, and the 1074 digits after it of the smallest. */
constexpr size_t LISP_FIXED_TEXT_SIZE = 1400;

/* core §9 — writes a finite Float to `text` in decimal, without an exponent,
 * with exactly `decimals` digits after the point (from 0 to
 * LISP_FIXED_MAXIMUM_DECIMALS), and no point when `decimals` is 0. The
 * digits come from the exact binary value, rounded with ties away from zero;
 * a `-` is written only when some written digit is not zero. */
void lisp_format_fixed(double number, int decimals, char text[static LISP_FIXED_TEXT_SIZE]);

/* Appends the printed form of the value to the buffer. */
void lisp_print(LispBuffer *output, const Value *value, LispPrintMode mode);
