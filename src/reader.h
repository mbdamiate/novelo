/* core §3–7 — the reader: text to datums, with positions and read errors. */
#pragma once

#include "buffer.h"
#include "positions.h"
#include "value.h"

#include <stddef.h>
#include <stdint.h>

/* core §7.4 — the error catalog. */
typedef enum LispReadErrorCode {
    LISP_READ_INVALID_UTF8,
    LISP_READ_UNEXPECTED_EOF,
    LISP_READ_UNCLOSED_DELIMITER,
    LISP_READ_MISMATCHED_DELIMITER,
    LISP_READ_UNEXPECTED_DELIMITER,
    LISP_READ_RESERVED_CHARACTER,
    LISP_READ_UNTERMINATED_STRING,
    LISP_READ_INVALID_ESCAPE,
    LISP_READ_INTEGER_OVERFLOW,
    LISP_READ_FLOAT_OVERFLOW,
    LISP_READ_INVALID_NUMBER,
    LISP_READ_DOTTED_PAIR,
    LISP_READ_EMPTY_KEYWORD,
    LISP_READ_UNKNOWN_DISPATCH,
    LISP_READ_ODD_MAP,
    LISP_READ_DUPLICATE_KEY,
    LISP_READ_DUPLICATE_ELEMENT,
    LISP_READ_NESTING_TOO_DEEP,
} LispReadErrorCode;

/* core §7.2 */
typedef struct LispReadError {
    LispReadErrorCode code;
    LispPosition position;
    char message[160]; /* free text, never compared */
} LispReadError;

/* The code as written in the catalog, e.g. "unclosed-delimiter". */
const char *lisp_read_error_name(LispReadErrorCode code);

/* True for the errors caused only by input ending too soon, which a REPL
 * answers by reading another line. */
bool lisp_read_error_is_incomplete(LispReadErrorCode code);

/* core §8.7 — appends "path:line:column: read error :code: message" for
 * stderr; without a path, "line:column: …". */
void lisp_describe_read_error(LispBuffer *output, const char *path, const LispReadError *error);

/* core §7.1 — reads the complete source. On success stores the List of
 * top-level datums in *datums (a new reference) and records the position of
 * every non-empty List and Map. On failure fills *error and returns false. */
[[nodiscard]] bool lisp_read(const char *source, size_t length, Value **datums, LispReadError *error);

/* lisp_read for the source of a file: the recorded positions also name the
 * file (`file_name`, or no file when nullptr), for the positions of errors
 * (§8.5). */
[[nodiscard]] bool lisp_read_file(const char *source, size_t length, const char *file_name, Value **datums,
                                  LispReadError *error);

/* core §4.1, §4.6 — integer syntax, shared with string-to-number (§9). */
typedef enum LispIntegerParse {
    LISP_INTEGER_OK,
    LISP_INTEGER_OVERFLOW,   /* matches `integer` but is outside 64 bits */
    LISP_INTEGER_INVALID,    /* numeric (§4.6) but not an integer */
    LISP_INTEGER_NOT_NUMERIC,
} LispIntegerParse;

LispIntegerParse lisp_parse_integer(const char *text, size_t length, int64_t *value);

/* core §4.2 — float syntax, for a token that is numeric but not an integer,
 * shared with string-to-number (§9). The value is the nearest binary64. */
typedef enum LispFloatParse {
    LISP_FLOAT_OK,
    LISP_FLOAT_OVERFLOW,     /* matches `float` but is too large for binary64 */
    LISP_FLOAT_INVALID,      /* does not match `float` */
} LispFloatParse;

LispFloatParse lisp_parse_float(const char *text, size_t length, double *value);
