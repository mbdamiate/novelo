/* Minimal JSON parser for the conformance files: objects, arrays, strings
 * (with \uXXXX and surrogate pairs), integers, true, false and null. */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum JsonKind {
    JSON_NULL,
    JSON_BOOLEAN,
    JSON_INTEGER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonKind;

typedef struct Json Json;
struct Json {
    JsonKind kind;
    bool boolean;
    int64_t integer;
    char *string;          /* UTF-8, NUL-terminated; may contain NUL */
    size_t string_length;
    size_t count;          /* elements of an array, members of an object */
    Json *items;           /* array elements, or object member values */
    char **keys;           /* object member names */
};

/* Parses the whole text. On failure writes a message and returns false. */
[[nodiscard]] bool json_parse(const char *text, size_t length, Json *result,
                              char *error_message, size_t error_message_size);

void json_free(Json *json);

/* The member with this name, or nullptr. */
const Json *json_member(const Json *object, const char *name);
