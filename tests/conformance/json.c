/* Minimal JSON parser for the conformance files. */
#include "json.h"

#include "buffer.h"

#include <stdckdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Parser {
    const char *text;
    size_t length;
    size_t position;
    char *error_message;
    size_t error_message_size;
} Parser;

static bool parse_value(Parser *parser, Json *result);

static bool parse_error(Parser *parser, const char *what) {
    snprintf(parser->error_message, parser->error_message_size, "JSON: %s at byte %zu", what,
             parser->position);
    return false;
}

static void skip_whitespace(Parser *parser) {
    while (parser->position < parser->length) {
        char character = parser->text[parser->position];
        if (character != ' ' && character != '\t' && character != '\n' && character != '\r') return;
        parser->position++;
    }
}

static bool consume(Parser *parser, char expected) {
    skip_whitespace(parser);
    if (parser->position < parser->length && parser->text[parser->position] == expected) {
        parser->position++;
        return true;
    }
    return false;
}

static bool consume_word(Parser *parser, const char *word) {
    size_t word_length = strlen(word);
    if (parser->length - parser->position < word_length
        || memcmp(parser->text + parser->position, word, word_length) != 0) {
        return false;
    }
    parser->position += word_length;
    return true;
}

static bool parse_hex4(Parser *parser, uint32_t *value) {
    if (parser->length - parser->position < 4) return parse_error(parser, "short \\u escape");
    *value = 0;
    for (int digit_index = 0; digit_index < 4; digit_index++) {
        char character = parser->text[parser->position++];
        uint32_t digit;
        if (character >= '0' && character <= '9') digit = (uint32_t)(character - '0');
        else if (character >= 'a' && character <= 'f') digit = (uint32_t)(character - 'a' + 10);
        else if (character >= 'A' && character <= 'F') digit = (uint32_t)(character - 'A' + 10);
        else return parse_error(parser, "bad hex digit");
        *value = *value * 16 + digit;
    }
    return true;
}

static bool parse_string(Parser *parser, char **string, size_t *string_length) {
    if (!consume(parser, '"')) return parse_error(parser, "expected a string");
    LispBuffer buffer;
    lisp_buffer_init(&buffer);
    for (;;) {
        if (parser->position >= parser->length) {
            lisp_buffer_free(&buffer);
            return parse_error(parser, "unterminated string");
        }
        char character = parser->text[parser->position++];
        if (character == '"') break;
        if (character != '\\') {
            lisp_buffer_append_byte(&buffer, character);
            continue;
        }
        if (parser->position >= parser->length) {
            lisp_buffer_free(&buffer);
            return parse_error(parser, "unterminated escape");
        }
        char escaped = parser->text[parser->position++];
        switch (escaped) {
        case '"': lisp_buffer_append_byte(&buffer, '"'); break;
        case '\\': lisp_buffer_append_byte(&buffer, '\\'); break;
        case '/': lisp_buffer_append_byte(&buffer, '/'); break;
        case 'b': lisp_buffer_append_byte(&buffer, '\b'); break;
        case 'f': lisp_buffer_append_byte(&buffer, '\f'); break;
        case 'n': lisp_buffer_append_byte(&buffer, '\n'); break;
        case 'r': lisp_buffer_append_byte(&buffer, '\r'); break;
        case 't': lisp_buffer_append_byte(&buffer, '\t'); break;
        case 'u': {
            uint32_t code_point;
            if (!parse_hex4(parser, &code_point)) {
                lisp_buffer_free(&buffer);
                return false;
            }
            if (code_point >= 0xD800 && code_point <= 0xDBFF) { /* high surrogate */
                uint32_t low;
                if (!consume_word(parser, "\\u") || !parse_hex4(parser, &low)
                    || low < 0xDC00 || low > 0xDFFF) {
                    lisp_buffer_free(&buffer);
                    return parse_error(parser, "unpaired surrogate");
                }
                code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
            } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
                lisp_buffer_free(&buffer);
                return parse_error(parser, "unpaired surrogate");
            }
            lisp_buffer_append_code_point(&buffer, code_point);
            break;
        }
        default:
            lisp_buffer_free(&buffer);
            return parse_error(parser, "bad escape");
        }
    }
    *string = lisp_buffer_take(&buffer, string_length);
    return true;
}

static bool parse_integer(Parser *parser, Json *result) {
    bool negative = parser->text[parser->position] == '-';
    if (negative) parser->position++;
    size_t start = parser->position;
    int64_t value = 0;
    while (parser->position < parser->length && parser->text[parser->position] >= '0'
           && parser->text[parser->position] <= '9') {
        int digit = parser->text[parser->position++] - '0';
        if (ckd_mul(&value, value, 10) || ckd_add(&value, value, digit)) {
            return parse_error(parser, "integer too large");
        }
    }
    if (parser->position == start) return parse_error(parser, "expected a digit");
    if (parser->position < parser->length
        && strchr(".eE", parser->text[parser->position]) && parser->text[parser->position]) {
        return parse_error(parser, "floating-point numbers are not supported");
    }
    *result = (Json){.kind = JSON_INTEGER, .integer = negative ? -value : value};
    return true;
}

static void push_item(Json *container, size_t *capacity, Json item, char *key) {
    if (container->count == *capacity) {
        *capacity = *capacity ? *capacity * 2 : 8;
        container->items = lisp_xrealloc(container->items, *capacity * sizeof(Json));
        if (container->kind == JSON_OBJECT) {
            container->keys = lisp_xrealloc(container->keys, *capacity * sizeof(char *));
        }
    }
    container->items[container->count] = item;
    if (container->kind == JSON_OBJECT) container->keys[container->count] = key;
    container->count++;
}

static bool parse_array(Parser *parser, Json *result) {
    *result = (Json){.kind = JSON_ARRAY};
    size_t capacity = 0;
    if (consume(parser, ']')) return true;
    do {
        Json item;
        if (!parse_value(parser, &item)) {
            json_free(result);
            return false;
        }
        push_item(result, &capacity, item, nullptr);
    } while (consume(parser, ','));
    if (!consume(parser, ']')) {
        json_free(result);
        return parse_error(parser, "expected ']'");
    }
    return true;
}

static bool parse_object(Parser *parser, Json *result) {
    *result = (Json){.kind = JSON_OBJECT};
    size_t capacity = 0;
    if (consume(parser, '}')) return true;
    do {
        char *key;
        size_t key_length;
        skip_whitespace(parser);
        if (!parse_string(parser, &key, &key_length)) {
            json_free(result);
            return false;
        }
        Json item;
        if (!consume(parser, ':') || !parse_value(parser, &item)) {
            free(key);
            json_free(result);
            return parser->error_message[0] ? false : parse_error(parser, "expected ':'");
        }
        push_item(result, &capacity, item, key);
    } while (consume(parser, ','));
    if (!consume(parser, '}')) {
        json_free(result);
        return parse_error(parser, "expected '}'");
    }
    return true;
}

static bool parse_value(Parser *parser, Json *result) {
    skip_whitespace(parser);
    if (parser->position >= parser->length) return parse_error(parser, "unexpected end");
    char character = parser->text[parser->position];
    switch (character) {
    case '{':
        parser->position++;
        return parse_object(parser, result);
    case '[':
        parser->position++;
        return parse_array(parser, result);
    case '"':
        *result = (Json){.kind = JSON_STRING};
        return parse_string(parser, &result->string, &result->string_length);
    case 't':
        if (!consume_word(parser, "true")) return parse_error(parser, "bad literal");
        *result = (Json){.kind = JSON_BOOLEAN, .boolean = true};
        return true;
    case 'f':
        if (!consume_word(parser, "false")) return parse_error(parser, "bad literal");
        *result = (Json){.kind = JSON_BOOLEAN, .boolean = false};
        return true;
    case 'n':
        if (!consume_word(parser, "null")) return parse_error(parser, "bad literal");
        *result = (Json){.kind = JSON_NULL};
        return true;
    default:
        if (character == '-' || (character >= '0' && character <= '9')) return parse_integer(parser, result);
        return parse_error(parser, "unexpected character");
    }
}

bool json_parse(const char *text, size_t length, Json *result, char *error_message,
                size_t error_message_size) {
    Parser parser = {text, length, 0, error_message, error_message_size};
    error_message[0] = '\0';
    if (!parse_value(&parser, result)) return false;
    skip_whitespace(&parser);
    if (parser.position != length) {
        json_free(result);
        return parse_error(&parser, "trailing text");
    }
    return true;
}

void json_free(Json *json) {
    free(json->string);
    for (size_t index = 0; index < json->count; index++) {
        json_free(&json->items[index]);
        if (json->keys) free(json->keys[index]);
    }
    free(json->items);
    free(json->keys);
    *json = (Json){};
}

const Json *json_member(const Json *object, const char *name) {
    if (object->kind != JSON_OBJECT) return nullptr;
    for (size_t index = 0; index < object->count; index++) {
        if (strcmp(object->keys[index], name) == 0) return &object->items[index];
    }
    return nullptr;
}
