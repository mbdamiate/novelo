/* Interfaces for standard output and standard input, so the interpreter can
 * run against the terminal or against memory (tests, conformance). */
#pragma once

#include "buffer.h"

#include <stddef.h>
#include <stdio.h>

typedef struct LispOutput {
    void (*write)(void *context, const char *bytes, size_t length);
    void *context;
} LispOutput;

/* read_line stores the next line, without its U+000A, in *line and returns
 * true; at the end of input it returns false. Only U+000A ends a line, so a
 * U+000D stays in the line, and a last line without U+000A is returned as it
 * is (core §9, read-line!). */
typedef struct LispInput {
    bool (*read_line)(void *context, LispBuffer *line);
    void *context;
} LispInput;

/* Output appended to a buffer. */
LispOutput lisp_output_to_buffer(LispBuffer *buffer);

/* Output written to a stdio stream. */
LispOutput lisp_output_to_stream(FILE *stream);

/* Input read from text in memory; the text must outlive the input. */
typedef struct LispTextInput {
    const char *text;
    size_t length;
    size_t position;
} LispTextInput;

LispInput lisp_input_from_text(LispTextInput *source);

/* Input read from a stdio stream. */
LispInput lisp_input_from_stream(FILE *stream);

/* Input that is always at its end. */
LispInput lisp_input_empty(void);
