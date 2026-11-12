/* core §3–7 — the reader.
 *
 * The bytes are first decoded into code points, up to the first invalid
 * UTF-8 byte, if any. The reader then works on that valid prefix. Errors are
 * reported in the order a left-to-right read meets them (§7.3): if the reader
 * reaches the end of the valid prefix — succeeding, or failing only because
 * input ended — the invalid byte is what it actually met, and the error
 * becomes invalid-utf8 at that byte (§3.2).
 */
#include "reader.h"

#include "buffer.h"
#include "utf8.h"

#include <stdarg.h>
#include <stdckdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr int MAXIMUM_NESTING = 1000; /* §5.6 */
constexpr uint32_t END_OF_INPUT = UINT32_MAX;

static const char *const error_names[] = {
    [LISP_READ_INVALID_UTF8] = "invalid-utf8",
    [LISP_READ_UNEXPECTED_EOF] = "unexpected-eof",
    [LISP_READ_UNCLOSED_DELIMITER] = "unclosed-delimiter",
    [LISP_READ_MISMATCHED_DELIMITER] = "mismatched-delimiter",
    [LISP_READ_UNEXPECTED_DELIMITER] = "unexpected-delimiter",
    [LISP_READ_RESERVED_CHARACTER] = "reserved-character",
    [LISP_READ_UNTERMINATED_STRING] = "unterminated-string",
    [LISP_READ_INVALID_ESCAPE] = "invalid-escape",
    [LISP_READ_INTEGER_OVERFLOW] = "integer-overflow",
    [LISP_READ_INVALID_NUMBER] = "invalid-number",
    [LISP_READ_DOTTED_PAIR] = "dotted-pair",
    [LISP_READ_EMPTY_KEYWORD] = "empty-keyword",
    [LISP_READ_UNKNOWN_DISPATCH] = "unknown-dispatch",
    [LISP_READ_ODD_MAP] = "odd-map",
    [LISP_READ_DUPLICATE_KEY] = "duplicate-key",
    [LISP_READ_NESTING_TOO_DEEP] = "nesting-too-deep",
};

const char *lisp_read_error_name(LispReadErrorCode code) { return error_names[code]; }

bool lisp_read_error_is_incomplete(LispReadErrorCode code) {
    return code == LISP_READ_UNCLOSED_DELIMITER || code == LISP_READ_UNTERMINATED_STRING
           || code == LISP_READ_UNEXPECTED_EOF;
}

/* ---- integers (§4.1, §4.6) --------------------------------------------------- */

static bool is_digit(char character) { return character >= '0' && character <= '9'; }

LispIntegerParse lisp_parse_integer(const char *text, size_t length, int64_t *value) {
    /* Numeric: after an optional sign, a digit, or "." followed by a digit. */
    size_t unsigned_start = (length > 0 && (text[0] == '+' || text[0] == '-')) ? 1 : 0;
    const char *body = text + unsigned_start;
    size_t body_length = length - unsigned_start;
    bool numeric = body_length > 0
                   && (is_digit(body[0])
                       || (body[0] == '.' && body_length > 1 && is_digit(body[1])));
    if (!numeric) return LISP_INTEGER_NOT_NUMERIC;

    /* integer = [ "-" ] , digit , { digit } — no "+" sign. */
    if (text[0] == '+') return LISP_INTEGER_INVALID;
    for (size_t position = 0; position < body_length; position++) {
        if (!is_digit(body[position])) return LISP_INTEGER_INVALID;
    }

    /* Accumulate as a negative number, which reaches INT64_MIN. */
    int64_t negative = 0;
    for (size_t position = 0; position < body_length; position++) {
        if (ckd_mul(&negative, negative, 10) || ckd_sub(&negative, negative, body[position] - '0')) {
            return LISP_INTEGER_OVERFLOW;
        }
    }
    if (text[0] == '-') {
        *value = negative;
    } else if (ckd_sub(value, (int64_t)0, negative)) {
        return LISP_INTEGER_OVERFLOW;
    }
    return LISP_INTEGER_OK;
}

/* ---- reader state ---------------------------------------------------------------- */

typedef struct Reader {
    uint32_t *code_points; /* the valid prefix of the input */
    size_t count;
    bool has_invalid_byte;
    LispPosition invalid_position;

    size_t index; /* next code point */
    LispPosition position;
    bool reached_end;

    int depth; /* open collections and prefixes */
    LispBuffer text;
    LispReadError *error;
} Reader;

static uint32_t peek(Reader *reader) {
    if (reader->index >= reader->count) {
        reader->reached_end = true;
        return END_OF_INPUT;
    }
    return reader->code_points[reader->index];
}

static void advance(Reader *reader) {
    uint32_t code_point = reader->code_points[reader->index++];
    if (code_point == '\n') {
        reader->position.line++;
        reader->position.column = 1;
    } else {
        reader->position.column++;
    }
}

[[gnu::format(printf, 4, 5)]]
static bool fail(Reader *reader, LispReadErrorCode code, LispPosition position, const char *format, ...) {
    reader->error->code = code;
    reader->error->position = position;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(reader->error->message, sizeof reader->error->message, format, arguments);
    va_end(arguments);
    return false;
}

/* ---- character classes (§3.1, §3.3, §3.5) ------------------------------------------ */

static bool is_whitespace(uint32_t code_point) {
    return code_point == ' ' || code_point == '\t' || code_point == '\n' || code_point == '\r'
           || code_point == '\f';
}

static bool is_delimiter(uint32_t code_point) {
    if (code_point == END_OF_INPUT || is_whitespace(code_point)) return true;
    switch (code_point) {
    case '(': case ')': case '{': case '}': case '[': case ']':
    case '"': case ';': case '\'': case '`': case ',':
        return true;
    default:
        return false;
    }
}

static bool is_closer(uint32_t code_point) { return code_point == ')' || code_point == '}'; }

static void skip_atmosphere(Reader *reader) {
    for (;;) {
        uint32_t code_point = peek(reader);
        if (is_whitespace(code_point)) {
            advance(reader);
        } else if (code_point == ';') {
            while (peek(reader) != '\n' && peek(reader) != END_OF_INPUT) advance(reader);
        } else {
            return;
        }
    }
}

/* Reads constituents up to the next delimiter into reader->text (UTF-8). */
static void read_token_text(Reader *reader) {
    lisp_buffer_clear(&reader->text);
    while (!is_delimiter(peek(reader))) {
        lisp_buffer_append_code_point(&reader->text, peek(reader));
        advance(reader);
    }
}

/* ---- datums -------------------------------------------------------------------------- */

static bool read_datum(Reader *reader, Value **datum);

static bool enter_nesting(Reader *reader, LispPosition opener) {
    if (reader->depth >= MAXIMUM_NESTING) {
        return fail(reader, LISP_READ_NESTING_TOO_DEEP, opener,
                    "more than %d collections or prefixes open", MAXIMUM_NESTING);
    }
    reader->depth++;
    return true;
}

static Value *finish_map(Reader *reader, Value *elements, LispPosition opener) {
    size_t count = lisp_list_length(elements);
    if (count % 2 != 0) {
        fail(reader, LISP_READ_ODD_MAP, opener, "map with an odd number of datums");
        return nullptr;
    }
    Value *map = lisp_map_new(count / 2);
    size_t pair_index = 0;
    for (const Value *rest = elements; rest != LISP_EMPTY; rest = lisp_rest(lisp_rest(rest))) {
        Value *key = lisp_retain(lisp_first(rest));
        Value *value = lisp_retain(lisp_first(lisp_rest(rest)));
        if (!lisp_map_insert_take(&map, key, value)) {
            lisp_release(map);
            fail(reader, LISP_READ_DUPLICATE_KEY, opener, "repeated key in pair %zu", pair_index);
            return nullptr;
        }
        pair_index++;
    }
    return map;
}

/* §5.1, §5.2 — the opener is the current code point. */
static bool read_collection(Reader *reader, Value **datum) {
    LispPosition opener = reader->position;
    uint32_t opening = peek(reader);
    uint32_t closing = opening == '(' ? ')' : '}';
    if (!enter_nesting(reader, opener)) return false;
    advance(reader);

    LispListBuilder elements;
    lisp_list_builder_init(&elements);
    for (;;) {
        skip_atmosphere(reader);
        uint32_t code_point = peek(reader);
        if (code_point == END_OF_INPUT) {
            lisp_list_builder_discard(&elements);
            return fail(reader, LISP_READ_UNCLOSED_DELIMITER, opener, "'%c' is never closed",
                        (char)opening);
        }
        if (code_point == closing) break;
        if (is_closer(code_point)) {
            lisp_list_builder_discard(&elements);
            return fail(reader, LISP_READ_MISMATCHED_DELIMITER, reader->position,
                        "'%c' closes '%c'", (char)code_point, (char)opening);
        }
        Value *element;
        if (!read_datum(reader, &element)) {
            lisp_list_builder_discard(&elements);
            return false;
        }
        lisp_list_append_take(&elements, element);
    }
    advance(reader); /* the closer */
    reader->depth--;

    Value *list = lisp_list_builder_finish(&elements);
    if (opening == '(') {
        if (list != LISP_EMPTY) lisp_position_record(list, opener);
        *datum = list;
        return true;
    }
    Value *map = finish_map(reader, list, opener);
    lisp_release(list);
    if (!map) return false;
    if (map->flags & LISP_IMMORTAL) { /* the empty map has no position (§6.2) */
        *datum = map;
        return true;
    }
    lisp_position_record(map, opener);
    *datum = map;
    return true;
}

/* §5.4 — ' ` , ,@ */
static bool read_prefixed(Reader *reader, Value **datum) {
    LispPosition prefix_position = reader->position;
    uint32_t prefix = peek(reader);
    if (!enter_nesting(reader, prefix_position)) return false;
    advance(reader);
    const char *name = prefix == '\'' ? "quote" : prefix == '`' ? "quasiquote" : "unquote";
    if (prefix == ',' && peek(reader) == '@') {
        advance(reader);
        name = "unquote-splicing";
    }

    skip_atmosphere(reader);
    uint32_t code_point = peek(reader);
    if (code_point == END_OF_INPUT) {
        return fail(reader, LISP_READ_UNEXPECTED_EOF, reader->position, "end of input after '%s' prefix", name);
    }
    if (is_closer(code_point)) {
        return fail(reader, LISP_READ_UNEXPECTED_DELIMITER, reader->position,
                    "'%c' right after a prefix", (char)code_point);
    }
    Value *operand;
    if (!read_datum(reader, &operand)) return false;
    reader->depth--;

    Value *list = lisp_prepend_take(lisp_symbol_cstr(name), lisp_prepend_take(operand, LISP_EMPTY));
    lisp_position_record(list, prefix_position);
    *datum = list;
    return true;
}

static int hex_digit_value(uint32_t code_point) {
    if (code_point >= '0' && code_point <= '9') return (int)(code_point - '0');
    if (code_point >= 'a' && code_point <= 'f') return (int)(code_point - 'a' + 10);
    if (code_point >= 'A' && code_point <= 'F') return (int)(code_point - 'A' + 10);
    return -1;
}

/* §4.3 — the \u{H…} escape; the reader is just after the 'u'. */
static bool read_unicode_escape(Reader *reader, LispPosition quote, LispPosition backslash) {
    uint32_t code_point = peek(reader);
    if (code_point == END_OF_INPUT) {
        return fail(reader, LISP_READ_UNTERMINATED_STRING, quote, "string is never closed");
    }
    if (code_point != '{') {
        return fail(reader, LISP_READ_INVALID_ESCAPE, backslash, "\\u must be followed by '{'");
    }
    advance(reader);
    uint32_t value = 0;
    int digit_count = 0;
    for (;;) {
        code_point = peek(reader);
        if (code_point == END_OF_INPUT) {
            return fail(reader, LISP_READ_UNTERMINATED_STRING, quote, "string is never closed");
        }
        if (code_point == '}') break;
        int digit = hex_digit_value(code_point);
        if (digit < 0) {
            return fail(reader, LISP_READ_INVALID_ESCAPE, backslash, "non-hexadecimal digit in \\u{…}");
        }
        if (++digit_count > 6) {
            return fail(reader, LISP_READ_INVALID_ESCAPE, backslash, "more than 6 digits in \\u{…}");
        }
        value = value * 16 + (uint32_t)digit;
        advance(reader);
    }
    advance(reader); /* the '}' */
    if (digit_count == 0) {
        return fail(reader, LISP_READ_INVALID_ESCAPE, backslash, "no digits in \\u{}");
    }
    if (!lisp_is_scalar(value)) {
        return fail(reader, LISP_READ_INVALID_ESCAPE, backslash, "U+%X is not a Unicode scalar value", value);
    }
    lisp_buffer_append_code_point(&reader->text, value);
    return true;
}

/* §4.3 */
static bool read_string(Reader *reader, Value **datum) {
    LispPosition quote = reader->position;
    advance(reader);
    lisp_buffer_clear(&reader->text);
    for (;;) {
        uint32_t code_point = peek(reader);
        if (code_point == END_OF_INPUT) {
            return fail(reader, LISP_READ_UNTERMINATED_STRING, quote, "string is never closed");
        }
        if (code_point == '"') break;
        if (code_point != '\\') {
            lisp_buffer_append_code_point(&reader->text, code_point);
            advance(reader);
            continue;
        }

        LispPosition backslash = reader->position;
        advance(reader);
        uint32_t escaped = peek(reader);
        if (escaped == END_OF_INPUT) {
            return fail(reader, LISP_READ_UNTERMINATED_STRING, quote, "string is never closed");
        }
        advance(reader);
        switch (escaped) {
        case 'n': lisp_buffer_append_byte(&reader->text, '\n'); break;
        case 't': lisp_buffer_append_byte(&reader->text, '\t'); break;
        case 'r': lisp_buffer_append_byte(&reader->text, '\r'); break;
        case '"': lisp_buffer_append_byte(&reader->text, '"'); break;
        case '\\': lisp_buffer_append_byte(&reader->text, '\\'); break;
        case 'u':
            if (!read_unicode_escape(reader, quote, backslash)) return false;
            break;
        default:
            return fail(reader, LISP_READ_INVALID_ESCAPE, backslash, "unknown escape");
        }
    }
    advance(reader); /* the closing quote */
    *datum = lisp_string(reader->text.data, reader->text.length);
    return true;
}

/* §4.4, §5.5 */
static bool read_dispatch(Reader *reader, Value **datum) {
    LispPosition hash = reader->position;
    read_token_text(reader); /* '#' is a constituent, so it starts the token */
    const char *token = reader->text.data;
    if (strcmp(token, "#true") == 0) {
        *datum = LISP_TRUE;
    } else if (strcmp(token, "#false") == 0) {
        *datum = LISP_FALSE;
    } else if (strcmp(token, "#void") == 0) {
        *datum = LISP_VOID;
    } else {
        return fail(reader, LISP_READ_UNKNOWN_DISPATCH, hash, "unknown '#' form");
    }
    return true;
}

/* §4.5 */
static bool read_keyword(Reader *reader, Value **datum) {
    LispPosition colon = reader->position;
    read_token_text(reader);
    if (reader->text.length == 1) return fail(reader, LISP_READ_EMPTY_KEYWORD, colon, "keyword without a name");
    *datum = lisp_keyword(reader->text.data + 1, reader->text.length - 1);
    return true;
}

/* §4.6 */
static bool read_token(Reader *reader, Value **datum) {
    LispPosition start = reader->position;
    read_token_text(reader);
    const char *token = reader->text.data;
    size_t length = reader->text.length;
    int64_t value;
    switch (lisp_parse_integer(token, length, &value)) {
    case LISP_INTEGER_OK:
        *datum = lisp_integer(value);
        return true;
    case LISP_INTEGER_OVERFLOW:
        return fail(reader, LISP_READ_INTEGER_OVERFLOW, start, "integer outside 64 bits");
    case LISP_INTEGER_INVALID:
        return fail(reader, LISP_READ_INVALID_NUMBER, start, "invalid number");
    case LISP_INTEGER_NOT_NUMERIC:
        break;
    }
    if (strcmp(token, ".") == 0) return fail(reader, LISP_READ_DOTTED_PAIR, start, "dotted pairs do not exist");
    *datum = lisp_symbol(token, length);
    return true;
}

/* Reads one datum. The caller has skipped atmosphere and checked for end of
 * input and closers. */
static bool read_datum(Reader *reader, Value **datum) {
    switch (peek(reader)) {
    case '(':
    case '{':
        return read_collection(reader, datum);
    case '[':
    case ']':
        return fail(reader, LISP_READ_RESERVED_CHARACTER, reader->position, "'[' and ']' are reserved");
    case '\'':
    case '`':
    case ',':
        return read_prefixed(reader, datum);
    case '"':
        return read_string(reader, datum);
    case '#':
        return read_dispatch(reader, datum);
    case ':':
        return read_keyword(reader, datum);
    default:
        return read_token(reader, datum);
    }
}

/* ---- entry point ------------------------------------------------------------------------ */

static void decode(Reader *reader, const char *source, size_t length) {
    const unsigned char *bytes = (const unsigned char *)source;
    reader->code_points = lisp_xmalloc((length + 1) * sizeof(uint32_t));
    LispPosition position = {1, 1};
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point;
        if (!lisp_utf8_next(bytes, length, &offset, &code_point)) {
            reader->has_invalid_byte = true;
            reader->invalid_position = position;
            return;
        }
        reader->code_points[reader->count++] = code_point;
        if (code_point == '\n') {
            position.line++;
            position.column = 1;
        } else {
            position.column++;
        }
    }
}

bool lisp_read(const char *source, size_t length, Value **datums, LispReadError *error) {
    Reader reader = {.position = {1, 1}, .error = error};
    lisp_buffer_init(&reader.text);
    decode(&reader, source, length);

    LispListBuilder builder;
    lisp_list_builder_init(&builder);
    bool succeeded = true;
    for (;;) {
        skip_atmosphere(&reader);
        uint32_t code_point = peek(&reader);
        if (code_point == END_OF_INPUT) break;
        if (is_closer(code_point)) {
            succeeded = fail(&reader, LISP_READ_UNEXPECTED_DELIMITER, reader.position,
                             "'%c' without an opener", (char)code_point);
            break;
        }
        Value *datum;
        if (!read_datum(&reader, &datum)) {
            succeeded = false;
            break;
        }
        lisp_list_append_take(&builder, datum);
    }

    if (reader.has_invalid_byte && reader.reached_end) {
        succeeded = fail(&reader, LISP_READ_INVALID_UTF8, reader.invalid_position, "invalid UTF-8");
    }
    free(reader.code_points);
    lisp_buffer_free(&reader.text);
    if (!succeeded) {
        lisp_list_builder_discard(&builder);
        return false;
    }
    *datums = lisp_list_builder_finish(&builder);
    return true;
}
