/* core §9 — the primitive table. Every primitive checks its arguments' types
 * before doing any work; the evaluator has already checked the arity. */
#include "primitives.h"

#include "error.h"
#include "eval.h"
#include "printer.h"
#include "reader.h"
#include "utf8.h"

#include <stdckdint.h>
#include <stdlib.h>
#include <string.h>

/* Every primitive is primitive_<name>, so no name collides with a libc
 * function such as remainder or raise. */
#define PRIMITIVE(name) \
    static Value *primitive_##name(LispState *state, size_t argument_count, Value *const *arguments)

/* ---- argument checks ------------------------------------------------------------- */

bool lisp_expect_kind(LispState *state, const char *function, const Value *value, LispKind kind,
                      const char *type_name) {
    if (value->kind == kind) return true;
    lisp_error(state, "wrong-type", LISP_LIST((Value *)value), "%s expects a %s, got a %s", function, type_name,
               lisp_type_name(value));
    return false;
}

static bool expect_integers(LispState *state, const char *function, size_t count, Value *const *values) {
    for (size_t index = 0; index < count; index++) {
        if (!lisp_expect_kind(state, function, values[index], LISP_KIND_INTEGER, "Integer")) return false;
    }
    return true;
}

static bool expect_list(LispState *state, const char *function, const Value *value) {
    if (lisp_is_list(value)) return true;
    lisp_error(state, "wrong-type", LISP_LIST((Value *)value), "%s expects a List, got a %s", function,
               lisp_type_name(value));
    return false;
}

static bool expect_function(LispState *state, const char *function, const Value *value) {
    if (lisp_is_function(value)) return true;
    lisp_error(state, "wrong-type", LISP_LIST((Value *)value), "%s expects a Function, got a %s", function,
               lisp_type_name(value));
    return false;
}

static Value *overflow(LispState *state, const char *function) {
    return lisp_error(state, "integer-overflow", LISP_EMPTY, "%s: result outside 64 bits", function);
}

/* ---- arithmetic -------------------------------------------------------------------- */

PRIMITIVE(add) {
    if (!expect_integers(state, "+", argument_count, arguments)) return nullptr;
    int64_t sum = 0;
    for (size_t index = 0; index < argument_count; index++) {
        if (ckd_add(&sum, sum, lisp_integer_value(arguments[index]))) return overflow(state, "+");
    }
    return lisp_integer(sum);
}

PRIMITIVE(multiply) {
    if (!expect_integers(state, "*", argument_count, arguments)) return nullptr;
    int64_t product = 1;
    for (size_t index = 0; index < argument_count; index++) {
        if (ckd_mul(&product, product, lisp_integer_value(arguments[index]))) return overflow(state, "*");
    }
    return lisp_integer(product);
}

PRIMITIVE(subtract) {
    if (!expect_integers(state, "-", argument_count, arguments)) return nullptr;
    int64_t difference = lisp_integer_value(arguments[0]);
    if (argument_count == 1) { /* (- x) negates */
        if (ckd_sub(&difference, (int64_t)0, difference)) return overflow(state, "-");
        return lisp_integer(difference);
    }
    for (size_t index = 1; index < argument_count; index++) {
        if (ckd_sub(&difference, difference, lisp_integer_value(arguments[index]))) return overflow(state, "-");
    }
    return lisp_integer(difference);
}

/* Truncating toward zero, as C does. */
static Value *divide(LispState *state, const char *function, Value *const *arguments, bool want_remainder) {
    if (!expect_integers(state, function, 2, arguments)) return nullptr;
    int64_t dividend = lisp_integer_value(arguments[0]);
    int64_t divisor = lisp_integer_value(arguments[1]);
    if (divisor == 0) return lisp_error(state, "division-by-zero", LISP_EMPTY, "%s: division by zero", function);
    if (divisor == -1) { /* INT64_MIN / -1 overflows; any remainder by -1 is 0 */
        if (want_remainder) return lisp_integer(0);
        int64_t negated;
        if (ckd_sub(&negated, (int64_t)0, dividend)) return overflow(state, function);
        return lisp_integer(negated);
    }
    return lisp_integer(want_remainder ? dividend % divisor : dividend / divisor);
}

PRIMITIVE(quotient) {
    (void)argument_count;
    return divide(state, "quotient", arguments, false);
}

PRIMITIVE(remainder) {
    (void)argument_count;
    return divide(state, "remainder", arguments, true);
}

/* ---- comparison ------------------------------------------------------------------------ */

typedef enum Comparison { LESS, GREATER, LESS_OR_EQUAL, GREATER_OR_EQUAL, EQUAL } Comparison;

static Value *compare_chain(LispState *state, const char *function, Comparison comparison, size_t argument_count,
                            Value *const *arguments) {
    if (!expect_integers(state, function, argument_count, arguments)) return nullptr;
    for (size_t index = 0; index + 1 < argument_count; index++) {
        int64_t left = lisp_integer_value(arguments[index]);
        int64_t right = lisp_integer_value(arguments[index + 1]);
        bool holds;
        switch (comparison) {
        case LESS: holds = left < right; break;
        case GREATER: holds = left > right; break;
        case LESS_OR_EQUAL: holds = left <= right; break;
        case GREATER_OR_EQUAL: holds = left >= right; break;
        case EQUAL: holds = left == right; break;
        }
        if (!holds) return LISP_FALSE;
    }
    return LISP_TRUE;
}

PRIMITIVE(less) { return compare_chain(state, "<", LESS, argument_count, arguments); }
PRIMITIVE(greater) { return compare_chain(state, ">", GREATER, argument_count, arguments); }
PRIMITIVE(less_or_equal) { return compare_chain(state, "<=", LESS_OR_EQUAL, argument_count, arguments); }
PRIMITIVE(greater_or_equal) { return compare_chain(state, ">=", GREATER_OR_EQUAL, argument_count, arguments); }
PRIMITIVE(numeric_equal) { return compare_chain(state, "=", EQUAL, argument_count, arguments); }

PRIMITIVE(equal) {
    (void)state;
    (void)argument_count;
    return lisp_bool(lisp_equal(arguments[0], arguments[1]));
}

/* ---- type predicates --------------------------------------------------------------------- */

#define KIND_PREDICATE(function_name, test)  \
    PRIMITIVE(function_name) {               \
        (void)state;                         \
        (void)argument_count;                \
        const Value *value = arguments[0];   \
        return lisp_bool(test);              \
    }

KIND_PREDICATE(is_boolean, value->kind == LISP_KIND_BOOLEAN)
KIND_PREDICATE(is_void, value->kind == LISP_KIND_VOID)
KIND_PREDICATE(is_integer, value->kind == LISP_KIND_INTEGER)
KIND_PREDICATE(is_string, value->kind == LISP_KIND_STRING)
KIND_PREDICATE(is_symbol, value->kind == LISP_KIND_SYMBOL)
KIND_PREDICATE(is_keyword, value->kind == LISP_KIND_KEYWORD)
KIND_PREDICATE(is_list, lisp_is_list(value))
KIND_PREDICATE(is_map, value->kind == LISP_KIND_MAP)
KIND_PREDICATE(is_function, lisp_is_function(value))

/* ---- lists -------------------------------------------------------------------------------- */

PRIMITIVE(list) {
    (void)state;
    return lisp_list_of(argument_count, arguments);
}

PRIMITIVE(prepend) {
    (void)argument_count;
    if (!expect_list(state, "prepend", arguments[1])) return nullptr;
    return lisp_prepend(arguments[0], arguments[1]);
}

static bool expect_non_empty_list(LispState *state, const char *function, const Value *value) {
    if (value->kind == LISP_KIND_PAIR) return true;
    lisp_error(state, "wrong-type", LISP_LIST((Value *)value), "%s expects a non-empty List, got %s", function,
               value == LISP_EMPTY ? "()" : lisp_type_name(value));
    return false;
}

PRIMITIVE(first) {
    (void)argument_count;
    if (!expect_non_empty_list(state, "first", arguments[0])) return nullptr;
    return lisp_retain(lisp_first(arguments[0]));
}

PRIMITIVE(rest) {
    (void)argument_count;
    if (!expect_non_empty_list(state, "rest", arguments[0])) return nullptr;
    return lisp_retain(lisp_rest(arguments[0]));
}

/* ---- sizes and maps ---------------------------------------------------------------------- */

static bool size_of(LispState *state, const char *function, const Value *value, size_t *size) {
    switch ((LispKind)value->kind) {
    case LISP_KIND_EMPTY:
    case LISP_KIND_PAIR: *size = lisp_list_length(value); return true;
    case LISP_KIND_MAP: *size = lisp_map_count(value); return true;
    case LISP_KIND_STRING: *size = lisp_as_string(value)->length; return true;
    default:
        lisp_error(state, "wrong-type", LISP_LIST((Value *)value), "%s expects a List, Map or String, got a %s",
                   function, lisp_type_name(value));
        return false;
    }
}

PRIMITIVE(count) {
    (void)argument_count;
    size_t size;
    if (!size_of(state, "count", arguments[0], &size)) return nullptr;
    return lisp_integer((int64_t)size);
}

PRIMITIVE(is_empty) {
    (void)argument_count;
    /* A List is empty exactly when it is (): answer without counting it, or
     * every traversal that tests empty? at each step would be quadratic. */
    if (arguments[0]->kind == LISP_KIND_PAIR) return LISP_FALSE;
    size_t size;
    if (!size_of(state, "empty?", arguments[0], &size)) return nullptr;
    return lisp_bool(size == 0);
}

PRIMITIVE(get) {
    if (!lisp_expect_kind(state, "get", arguments[0], LISP_KIND_MAP, "Map")) return nullptr;
    Value *value = lisp_map_get(arguments[0], arguments[1]);
    if (value) return lisp_retain(value);
    if (argument_count == 3) return lisp_retain(arguments[2]);
    return lisp_error(state, "missing-key", LISP_LIST(arguments[1]), "get: the key is not in the map");
}

PRIMITIVE(contains) {
    (void)argument_count;
    if (!lisp_expect_kind(state, "contains?", arguments[0], LISP_KIND_MAP, "Map")) return nullptr;
    return lisp_bool(lisp_map_get(arguments[0], arguments[1]) != nullptr);
}

PRIMITIVE(associate) {
    (void)argument_count;
    if (!lisp_expect_kind(state, "associate", arguments[0], LISP_KIND_MAP, "Map")) return nullptr;
    return lisp_map_associate(arguments[0], arguments[1], arguments[2]);
}

PRIMITIVE(dissociate) {
    (void)argument_count;
    if (!lisp_expect_kind(state, "dissociate", arguments[0], LISP_KIND_MAP, "Map")) return nullptr;
    return lisp_map_dissociate(arguments[0], arguments[1]);
}

PRIMITIVE(keys) {
    (void)argument_count;
    if (!lisp_expect_kind(state, "keys", arguments[0], LISP_KIND_MAP, "Map")) return nullptr;
    LispListBuilder builder;
    lisp_list_builder_init(&builder);
    Value *key, *value;
    for (size_t cursor = 0; lisp_map_next(arguments[0], &cursor, &key, &value);) {
        lisp_list_append_take(&builder, lisp_retain(key));
    }
    return lisp_list_builder_finish(&builder);
}

/* ---- strings ------------------------------------------------------------------------------ */

PRIMITIVE(concatenate) {
    for (size_t index = 0; index < argument_count; index++) {
        if (!lisp_expect_kind(state, "concatenate", arguments[index], LISP_KIND_STRING, "String")) return nullptr;
    }
    LispBuffer text;
    lisp_buffer_init(&text);
    for (size_t index = 0; index < argument_count; index++) {
        const LispString *part = lisp_as_string(arguments[index]);
        lisp_buffer_append(&text, part->data, part->bytes);
    }
    Value *result = lisp_string(text.data ? text.data : "", text.length);
    lisp_buffer_free(&text);
    return result;
}

static Value *index_out_of_range(LispState *state, const char *function, Value *index) {
    return lisp_error(state, "index-out-of-range", LISP_LIST(index), "%s: index out of range", function);
}

/* The code points [start, end) of a string; indices already checked. */
static Value *code_point_slice(const Value *string, size_t start, size_t end) {
    size_t start_offset = lisp_string_offset(string, start);
    size_t end_offset = lisp_string_offset(string, end);
    return lisp_string(lisp_as_string(string)->data + start_offset, end_offset - start_offset);
}

PRIMITIVE(string_at) {
    (void)argument_count;
    if (!lisp_expect_kind(state, "string-at", arguments[0], LISP_KIND_STRING, "String")
        || !lisp_expect_kind(state, "string-at", arguments[1], LISP_KIND_INTEGER, "Integer")) {
        return nullptr;
    }
    int64_t index = lisp_integer_value(arguments[1]);
    if (index < 0 || (uint64_t)index >= lisp_as_string(arguments[0])->length) {
        return index_out_of_range(state, "string-at", arguments[1]);
    }
    return code_point_slice(arguments[0], (size_t)index, (size_t)index + 1);
}

PRIMITIVE(substring) {
    (void)argument_count;
    if (!lisp_expect_kind(state, "substring", arguments[0], LISP_KIND_STRING, "String")
        || !lisp_expect_kind(state, "substring", arguments[1], LISP_KIND_INTEGER, "Integer")
        || !lisp_expect_kind(state, "substring", arguments[2], LISP_KIND_INTEGER, "Integer")) {
        return nullptr;
    }
    int64_t start = lisp_integer_value(arguments[1]);
    int64_t end = lisp_integer_value(arguments[2]);
    uint64_t length = lisp_as_string(arguments[0])->length;
    if (start < 0 || (uint64_t)start > length) return index_out_of_range(state, "substring", arguments[1]);
    if (end < start || (uint64_t)end > length) return index_out_of_range(state, "substring", arguments[2]);
    return code_point_slice(arguments[0], (size_t)start, (size_t)end);
}

/* The text display! would print (§9). */
PRIMITIVE(to_string) {
    (void)state;
    (void)argument_count;
    LispBuffer text;
    lisp_buffer_init(&text);
    lisp_print(&text, arguments[0], LISP_PRINT_DISPLAY);
    Value *result = lisp_string(text.data ? text.data : "", text.length);
    lisp_buffer_free(&text);
    return result;
}

/* The whole text under the rules of §4: an Integer, #void if it is not one,
 * or :integer-overflow if it is out of range. */
PRIMITIVE(string_to_number) {
    (void)argument_count;
    if (!lisp_expect_kind(state, "string-to-number", arguments[0], LISP_KIND_STRING, "String")) return nullptr;
    const LispString *text = lisp_as_string(arguments[0]);
    int64_t value;
    switch (lisp_parse_integer(text->data, text->bytes, &value)) {
    case LISP_INTEGER_OK: return lisp_integer(value);
    case LISP_INTEGER_OVERFLOW: return overflow(state, "string-to-number");
    case LISP_INTEGER_INVALID:
    case LISP_INTEGER_NOT_NUMERIC: break;
    }
    return LISP_VOID;
}

PRIMITIVE(string_to_symbol) {
    (void)argument_count;
    if (!lisp_expect_kind(state, "string-to-symbol", arguments[0], LISP_KIND_STRING, "String")) return nullptr;
    return lisp_symbol(lisp_as_string(arguments[0])->data, lisp_as_string(arguments[0])->bytes);
}

PRIMITIVE(symbol_to_string) {
    (void)argument_count;
    if (!lisp_expect_kind(state, "symbol-to-string", arguments[0], LISP_KIND_SYMBOL, "Symbol")) return nullptr;
    return lisp_string(lisp_as_name(arguments[0])->text, lisp_as_name(arguments[0])->bytes);
}

PRIMITIVE(generate_symbol) {
    (void)state;
    (void)argument_count;
    (void)arguments;
    return lisp_generate_symbol();
}

/* ---- functions and logic ---------------------------------------------------------------------- */

/* (apply f a … list) */
PRIMITIVE(apply) {
    Value *last = arguments[argument_count - 1];
    if (!expect_list(state, "apply", last)) return nullptr;
    size_t fixed = argument_count - 2;
    size_t total = fixed + lisp_list_length(last);
    Value **all = lisp_xmalloc((total ? total : 1) * sizeof(Value *));
    for (size_t index = 0; index < fixed; index++) all[index] = arguments[index + 1];
    size_t index = fixed;
    for (const Value *rest = last; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) all[index++] = lisp_first(rest);
    Value *result = lisp_apply(state, arguments[0], total, all);
    free(all);
    return result;
}

PRIMITIVE(not) {
    (void)state;
    (void)argument_count;
    return lisp_bool(arguments[0] == LISP_FALSE);
}

/* ---- errors (§8.5) ------------------------------------------------------------------------------ */

PRIMITIVE(error) {
    if (!lisp_expect_kind(state, "error", arguments[0], LISP_KIND_STRING, "String")) return nullptr;
    Value *values = lisp_list_of(argument_count - 1, arguments + 1);
    return lisp_error(state, "error", values, "%s", lisp_as_string(arguments[0])->data);
}

PRIMITIVE(raise) {
    (void)argument_count;
    Value *descriptor = arguments[0];
    Value *kind = descriptor->kind == LISP_KIND_MAP ? lisp_map_get(descriptor, lisp_keyword_cstr("kind")) : nullptr;
    if (!kind || kind->kind != LISP_KIND_KEYWORD) {
        return lisp_error(state, "wrong-type", LISP_LIST(descriptor), "raise expects a Map with a Keyword :kind");
    }
    return lisp_error_from_descriptor(state, lisp_retain(descriptor));
}

/* (catch body handler): exit-only; an error inside the handler goes to the
 * next enclosing catch. */
PRIMITIVE(catch) {
    (void)argument_count;
    if (!expect_function(state, "catch", arguments[0]) || !expect_function(state, "catch", arguments[1])) {
        return nullptr;
    }
    Value *result = lisp_apply(state, arguments[0], 0, nullptr);
    if (result) return result;
    Value *descriptor = lisp_take_error(state);
    result = lisp_apply(state, arguments[1], 1, &descriptor);
    lisp_release(descriptor);
    return result;
}

/* ---- the table ------------------------------------------------------------------------------------ */

static const LispNativeFunction primitives[] = {
    {"+", 0, -1, primitive_add},
    {"*", 0, -1, primitive_multiply},
    {"-", 1, -1, primitive_subtract},
    {"quotient", 2, 2, primitive_quotient},
    {"remainder", 2, 2, primitive_remainder},
    {"<", 2, -1, primitive_less},
    {">", 2, -1, primitive_greater},
    {"<=", 2, -1, primitive_less_or_equal},
    {">=", 2, -1, primitive_greater_or_equal},
    {"=", 2, -1, primitive_numeric_equal},
    {"equal?", 2, 2, primitive_equal},
    {"boolean?", 1, 1, primitive_is_boolean},
    {"void?", 1, 1, primitive_is_void},
    {"integer?", 1, 1, primitive_is_integer},
    {"string?", 1, 1, primitive_is_string},
    {"symbol?", 1, 1, primitive_is_symbol},
    {"keyword?", 1, 1, primitive_is_keyword},
    {"list?", 1, 1, primitive_is_list},
    {"map?", 1, 1, primitive_is_map},
    {"function?", 1, 1, primitive_is_function},
    {"list", 0, -1, primitive_list},
    {"prepend", 2, 2, primitive_prepend},
    {"first", 1, 1, primitive_first},
    {"rest", 1, 1, primitive_rest},
    {"count", 1, 1, primitive_count},
    {"empty?", 1, 1, primitive_is_empty},
    {"get", 2, 3, primitive_get},
    {"contains?", 2, 2, primitive_contains},
    {"associate", 3, 3, primitive_associate},
    {"dissociate", 2, 2, primitive_dissociate},
    {"keys", 1, 1, primitive_keys},
    {"concatenate", 0, -1, primitive_concatenate},
    {"string-at", 2, 2, primitive_string_at},
    {"substring", 3, 3, primitive_substring},
    {"to-string", 1, 1, primitive_to_string},
    {"string-to-number", 1, 1, primitive_string_to_number},
    {"string-to-symbol", 1, 1, primitive_string_to_symbol},
    {"symbol-to-string", 1, 1, primitive_symbol_to_string},
    {"generate-symbol", 0, 0, primitive_generate_symbol},
    {"apply", 2, -1, primitive_apply},
    {"not", 1, 1, primitive_not},
    {"error", 1, -1, primitive_error},
    {"raise", 1, 1, primitive_raise},
    {"catch", 2, 2, primitive_catch},
};

void lisp_install_primitives(LispState *state) {
    for (size_t index = 0; index < sizeof primitives / sizeof primitives[0]; index++) {
        const LispNativeFunction *entry = &primitives[index];
        Value *primitive = lisp_primitive(entry->name, entry->minimum_arguments, entry->maximum_arguments,
                                          entry->function);
        lisp_global_define(state, lisp_symbol_cstr(entry->name), primitive, false);
        lisp_release(primitive);
    }
}
