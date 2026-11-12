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
    [LM_READ_INVALID_UTF8] = "invalid-utf8",
    [LM_READ_UNEXPECTED_EOF] = "unexpected-eof",
    [LM_READ_UNCLOSED_DELIMITER] = "unclosed-delimiter",
    [LM_READ_MISMATCHED_DELIMITER] = "mismatched-delimiter",
    [LM_READ_UNEXPECTED_DELIMITER] = "unexpected-delimiter",
    [LM_READ_RESERVED_CHARACTER] = "reserved-character",
    [LM_READ_UNTERMINATED_STRING] = "unterminated-string",
    [LM_READ_INVALID_ESCAPE] = "invalid-escape",
    [LM_READ_INTEGER_OVERFLOW] = "integer-overflow",
    [LM_READ_INVALID_NUMBER] = "invalid-number",
    [LM_READ_DOTTED_PAIR] = "dotted-pair",
    [LM_READ_EMPTY_KEYWORD] = "empty-keyword",
    [LM_READ_UNKNOWN_DISPATCH] = "unknown-dispatch",
    [LM_READ_ODD_MAP] = "odd-map",
    [LM_READ_DUPLICATE_KEY] = "duplicate-key",
    [LM_READ_NESTING_TOO_DEEP] = "nesting-too-deep",
};

const char *lm_read_error_name(LmReadErrorCode code) { return error_names[code]; }

bool lm_read_error_is_incomplete(LmReadErrorCode code) {
    return code == LM_READ_UNCLOSED_DELIMITER || code == LM_READ_UNTERMINATED_STRING
           || code == LM_READ_UNEXPECTED_EOF;
}

/* ---- integers (§4.1, §4.6) --------------------------------------------------- */

static bool is_digit(char character) { return character >= '0' && character <= '9'; }

LmIntegerParse lm_parse_integer(const char *text, size_t length, int64_t *value) {
    /* Numeric: after an optional sign, a digit, or "." followed by a digit. */
    size_t unsigned_start = (length > 0 && (text[0] == '+' || text[0] == '-')) ? 1 : 0;
    const char *body = text + unsigned_start;
    size_t body_length = length - unsigned_start;
    bool numeric = body_length > 0
                   && (is_digit(body[0])
                       || (body[0] == '.' && body_length > 1 && is_digit(body[1])));
    if (!numeric) return LM_INTEGER_NOT_NUMERIC;

    /* integer = [ "-" ] , digit , { digit } — no "+" sign. */
    if (text[0] == '+') return LM_INTEGER_INVALID;
    for (size_t position = 0; position < body_length; position++) {
        if (!is_digit(body[position])) return LM_INTEGER_INVALID;
    }

    /* Accumulate as a negative number, which reaches INT64_MIN. */
    int64_t negative = 0;
    for (size_t position = 0; position < body_length; position++) {
        if (ckd_mul(&negative, negative, 10) || ckd_sub(&negative, negative, body[position] - '0')) {
            return LM_INTEGER_OVERFLOW;
        }
    }
    if (text[0] == '-') {
        *value = negative;
    } else if (ckd_sub(value, (int64_t)0, negative)) {
        return LM_INTEGER_OVERFLOW;
    }
    return LM_INTEGER_OK;
}

/* ---- reader state ---------------------------------------------------------------- */

typedef struct Reader {
    uint32_t *code_points; /* the valid prefix of the input */
    size_t count;
    bool has_invalid_byte;
    LmPosition invalid_position;

    size_t index; /* next code point */
    LmPosition position;
    bool reached_end;

    int depth; /* open collections and prefixes */
    LmBuffer text;
    LmReadError *error;
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
static bool fail(Reader *reader, LmReadErrorCode code, LmPosition position, const char *format, ...) {
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
    lm_buffer_clear(&reader->text);
    while (!is_delimiter(peek(reader))) {
        lm_buffer_append_code_point(&reader->text, peek(reader));
        advance(reader);
    }
}

/* ---- datums -------------------------------------------------------------------------- */

static bool read_datum(Reader *reader, Value **datum);

static bool enter_nesting(Reader *reader, LmPosition opener) {
    if (reader->depth >= MAXIMUM_NESTING) {
        return fail(reader, LM_READ_NESTING_TOO_DEEP, opener,
                    "more than %d collections or prefixes open", MAXIMUM_NESTING);
    }
    reader->depth++;
    return true;
}

static Value *finish_map(Reader *reader, Value *elements, LmPosition opener) {
    size_t count = lm_list_length(elements);
    if (count % 2 != 0) {
        fail(reader, LM_READ_ODD_MAP, opener, "map with an odd number of datums");
        return nullptr;
    }
    Value *map = lm_map_new(count / 2);
    size_t pair_index = 0;
    for (const Value *rest = elements; rest != LM_EMPTY; rest = lm_rest(lm_rest(rest))) {
        Value *key = lm_retain(lm_first(rest));
        Value *value = lm_retain(lm_first(lm_rest(rest)));
        if (!lm_map_insert_take(&map, key, value)) {
            lm_release(map);
            fail(reader, LM_READ_DUPLICATE_KEY, opener, "repeated key in pair %zu", pair_index);
            return nullptr;
        }
        pair_index++;
    }
    return map;
}

/* §5.1, §5.2 — the opener is the current code point. */
static bool read_collection(Reader *reader, Value **datum) {
    LmPosition opener = reader->position;
    uint32_t opening = peek(reader);
    uint32_t closing = opening == '(' ? ')' : '}';
    if (!enter_nesting(reader, opener)) return false;
    advance(reader);

    LmListBuilder elements;
    lm_list_builder_init(&elements);
    for (;;) {
        skip_atmosphere(reader);
        uint32_t code_point = peek(reader);
        if (code_point == END_OF_INPUT) {
            lm_list_builder_discard(&elements);
            return fail(reader, LM_READ_UNCLOSED_DELIMITER, opener, "'%c' is never closed",
                        (char)opening);
        }
        if (code_point == closing) break;
        if (is_closer(code_point)) {
            lm_list_builder_discard(&elements);
            return fail(reader, LM_READ_MISMATCHED_DELIMITER, reader->position,
                        "'%c' closes '%c'", (char)code_point, (char)opening);
        }
        Value *element;
        if (!read_datum(reader, &element)) {
            lm_list_builder_discard(&elements);
            return false;
        }
        lm_list_append_take(&elements, element);
    }
    advance(reader); /* the closer */
    reader->depth--;

    Value *list = lm_list_builder_finish(&elements);
    if (opening == '(') {
        if (list != LM_EMPTY) lm_position_record(list, opener);
        *datum = list;
        return true;
    }
    Value *map = finish_map(reader, list, opener);
    lm_release(list);
    if (!map) return false;
    if (map->flags & LM_IMMORTAL) { /* the empty map has no position (§6.2) */
        *datum = map;
        return true;
    }
    lm_position_record(map, opener);
    *datum = map;
    return true;
}

/* §5.4 — ' ` , ,@ */
static bool read_prefixed(Reader *reader, Value **datum) {
    LmPosition prefix_position = reader->position;
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
        return fail(reader, LM_READ_UNEXPECTED_EOF, reader->position, "end of input after '%s' prefix", name);
    }
    if (is_closer(code_point)) {
        return fail(reader, LM_READ_UNEXPECTED_DELIMITER, reader->position,
                    "'%c' right after a prefix", (char)code_point);
    }
    Value *operand;
    if (!read_datum(reader, &operand)) return false;
    reader->depth--;

    Value *list = lm_prepend_take(lm_symbol_cstr(name), lm_prepend_take(operand, LM_EMPTY));
    lm_position_record(list, prefix_position);
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
static bool read_unicode_escape(Reader *reader, LmPosition quote, LmPosition backslash) {
    uint32_t code_point = peek(reader);
    if (code_point == END_OF_INPUT) {
        return fail(reader, LM_READ_UNTERMINATED_STRING, quote, "string is never closed");
    }
    if (code_point != '{') {
        return fail(reader, LM_READ_INVALID_ESCAPE, backslash, "\\u must be followed by '{'");
    }
    advance(reader);
    uint32_t value = 0;
    int digit_count = 0;
    for (;;) {
        code_point = peek(reader);
        if (code_point == END_OF_INPUT) {
            return fail(reader, LM_READ_UNTERMINATED_STRING, quote, "string is never closed");
        }
        if (code_point == '}') break;
        int digit = hex_digit_value(code_point);
        if (digit < 0) {
            return fail(reader, LM_READ_INVALID_ESCAPE, backslash, "non-hexadecimal digit in \\u{…}");
        }
        if (++digit_count > 6) {
            return fail(reader, LM_READ_INVALID_ESCAPE, backslash, "more than 6 digits in \\u{…}");
        }
        value = value * 16 + (uint32_t)digit;
        advance(reader);
    }
    advance(reader); /* the '}' */
    if (digit_count == 0) {
        return fail(reader, LM_READ_INVALID_ESCAPE, backslash, "no digits in \\u{}");
    }
    if (!lm_is_scalar(value)) {
        return fail(reader, LM_READ_INVALID_ESCAPE, backslash, "U+%X is not a Unicode scalar value", value);
    }
    lm_buffer_append_code_point(&reader->text, value);
    return true;
}

/* §4.3 */
static bool read_string(Reader *reader, Value **datum) {
    LmPosition quote = reader->position;
    advance(reader);
    lm_buffer_clear(&reader->text);
    for (;;) {
        uint32_t code_point = peek(reader);
        if (code_point == END_OF_INPUT) {
            return fail(reader, LM_READ_UNTERMINATED_STRING, quote, "string is never closed");
        }
        if (code_point == '"') break;
        if (code_point != '\\') {
            lm_buffer_append_code_point(&reader->text, code_point);
            advance(reader);
            continue;
        }

        LmPosition backslash = reader->position;
        advance(reader);
        uint32_t escaped = peek(reader);
        if (escaped == END_OF_INPUT) {
            return fail(reader, LM_READ_UNTERMINATED_STRING, quote, "string is never closed");
        }
        advance(reader);
        switch (escaped) {
        case 'n': lm_buffer_append_byte(&reader->text, '\n'); break;
        case 't': lm_buffer_append_byte(&reader->text, '\t'); break;
        case 'r': lm_buffer_append_byte(&reader->text, '\r'); break;
        case '"': lm_buffer_append_byte(&reader->text, '"'); break;
        case '\\': lm_buffer_append_byte(&reader->text, '\\'); break;
        case 'u':
            if (!read_unicode_escape(reader, quote, backslash)) return false;
            break;
        default:
            return fail(reader, LM_READ_INVALID_ESCAPE, backslash, "unknown escape");
        }
    }
    advance(reader); /* the closing quote */
    *datum = lm_string(reader->text.data, reader->text.length);
    return true;
}

/* §4.4, §5.5 */
static bool read_dispatch(Reader *reader, Value **datum) {
    LmPosition hash = reader->position;
    read_token_text(reader); /* '#' is a constituent, so it starts the token */
    const char *token = reader->text.data;
    if (strcmp(token, "#true") == 0) {
        *datum = LM_TRUE;
    } else if (strcmp(token, "#false") == 0) {
        *datum = LM_FALSE;
    } else if (strcmp(token, "#void") == 0) {
        *datum = LM_VOID;
    } else {
        return fail(reader, LM_READ_UNKNOWN_DISPATCH, hash, "unknown '#' form");
    }
    return true;
}

/* §4.5 */
static bool read_keyword(Reader *reader, Value **datum) {
    LmPosition colon = reader->position;
    read_token_text(reader);
    if (reader->text.length == 1) return fail(reader, LM_READ_EMPTY_KEYWORD, colon, "keyword without a name");
    *datum = lm_keyword(reader->text.data + 1, reader->text.length - 1);
    return true;
}

/* §4.6 */
static bool read_token(Reader *reader, Value **datum) {
    LmPosition start = reader->position;
    read_token_text(reader);
    const char *token = reader->text.data;
    size_t length = reader->text.length;
    int64_t value;
    switch (lm_parse_integer(token, length, &value)) {
    case LM_INTEGER_OK:
        *datum = lm_integer(value);
        return true;
    case LM_INTEGER_OVERFLOW:
        return fail(reader, LM_READ_INTEGER_OVERFLOW, start, "integer outside 64 bits");
    case LM_INTEGER_INVALID:
        return fail(reader, LM_READ_INVALID_NUMBER, start, "invalid number");
    case LM_INTEGER_NOT_NUMERIC:
        break;
    }
    if (strcmp(token, ".") == 0) return fail(reader, LM_READ_DOTTED_PAIR, start, "dotted pairs do not exist");
    *datum = lm_symbol(token, length);
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
        return fail(reader, LM_READ_RESERVED_CHARACTER, reader->position, "'[' and ']' are reserved");
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
    reader->code_points = lm_xmalloc((length + 1) * sizeof(uint32_t));
    LmPosition position = {1, 1};
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point;
        if (!lm_utf8_next(bytes, length, &offset, &code_point)) {
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

bool lm_read(const char *source, size_t length, Value **datums, LmReadError *error) {
    Reader reader = {.position = {1, 1}, .error = error};
    lm_buffer_init(&reader.text);
    decode(&reader, source, length);

    LmListBuilder builder;
    lm_list_builder_init(&builder);
    bool succeeded = true;
    for (;;) {
        skip_atmosphere(&reader);
        uint32_t code_point = peek(&reader);
        if (code_point == END_OF_INPUT) break;
        if (is_closer(code_point)) {
            succeeded = fail(&reader, LM_READ_UNEXPECTED_DELIMITER, reader.position,
                             "'%c' without an opener", (char)code_point);
            break;
        }
        Value *datum;
        if (!read_datum(&reader, &datum)) {
            succeeded = false;
            break;
        }
        lm_list_append_take(&builder, datum);
    }

    if (reader.has_invalid_byte && reader.reached_end) {
        succeeded = fail(&reader, LM_READ_INVALID_UTF8, reader.invalid_position, "invalid UTF-8");
    }
    free(reader.code_points);
    lm_buffer_free(&reader.text);
    if (!succeeded) {
        lm_list_builder_discard(&builder);
        return false;
    }
    *datums = lm_list_builder_finish(&builder);
    return true;
}
