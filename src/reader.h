/* core §3–7 — the reader: text to datums, with positions and read errors. */
#pragma once

#include "positions.h"
#include "value.h"

#include <stddef.h>
#include <stdint.h>

/* core §7.4 — the error catalog. */
typedef enum LmReadErrorCode {
    LM_READ_INVALID_UTF8,
    LM_READ_UNEXPECTED_EOF,
    LM_READ_UNCLOSED_DELIMITER,
    LM_READ_MISMATCHED_DELIMITER,
    LM_READ_UNEXPECTED_DELIMITER,
    LM_READ_RESERVED_CHARACTER,
    LM_READ_UNTERMINATED_STRING,
    LM_READ_INVALID_ESCAPE,
    LM_READ_INTEGER_OVERFLOW,
    LM_READ_INVALID_NUMBER,
    LM_READ_DOTTED_PAIR,
    LM_READ_EMPTY_KEYWORD,
    LM_READ_UNKNOWN_DISPATCH,
    LM_READ_ODD_MAP,
    LM_READ_DUPLICATE_KEY,
    LM_READ_NESTING_TOO_DEEP,
} LmReadErrorCode;

/* core §7.2 */
typedef struct LmReadError {
    LmReadErrorCode code;
    LmPosition position;
    char message[160]; /* free text, never compared */
} LmReadError;

/* The code as written in the catalog, e.g. "unclosed-delimiter". */
const char *lm_read_error_name(LmReadErrorCode code);

/* True for the errors caused only by input ending too soon, which a REPL
 * answers by reading another line. */
bool lm_read_error_is_incomplete(LmReadErrorCode code);

/* core §7.1 — reads the complete source. On success stores the List of
 * top-level datums in *datums (a new reference) and records the position of
 * every non-empty List and Map. On failure fills *error and returns false. */
[[nodiscard]] bool lm_read(const char *source, size_t length, Value **datums, LmReadError *error);

/* core §4.1, §4.6 — integer syntax, shared with string-to-number (§9). */
typedef enum LmIntegerParse {
    LM_INTEGER_OK,
    LM_INTEGER_OVERFLOW,   /* matches `integer` but is outside 64 bits */
    LM_INTEGER_INVALID,    /* numeric (§4.6) but not an integer */
    LM_INTEGER_NOT_NUMERIC,
} LmIntegerParse;

LmIntegerParse lm_parse_integer(const char *text, size_t length, int64_t *value);
