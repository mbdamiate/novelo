/* core §2 — values, refcounting, interning, equal?, hashing. */
#include "test.h"

#include "value.h"

/* Builds the list of the given integers. */
static Value *integer_list(size_t count, const int64_t *items) {
    LmListBuilder builder;
    lm_list_builder_init(&builder);
    for (size_t index = 0; index < count; index++) {
        lm_list_append_take(&builder, lm_integer(items[index]));
    }
    return lm_list_builder_finish(&builder);
}
#define INTEGERS(...)                                                        \
    integer_list(sizeof((int64_t[]){__VA_ARGS__}) / sizeof(int64_t),         \
                 (int64_t[]){__VA_ARGS__})

/* Builds a map from alternating keys and values (borrowed). */
static Value *map_of(size_t pair_count, Value *const *keys_and_values) {
    Value *map = lm_map_new(pair_count);
    for (size_t index = 0; index < pair_count; index++) {
        CHECK(lm_map_insert_take(&map, lm_retain(keys_and_values[2 * index]),
                                 lm_retain(keys_and_values[2 * index + 1])));
    }
    return map;
}

static void release_all(size_t count, Value *const *values) {
    for (size_t index = 0; index < count; index++) lm_release(values[index]);
}
#define RELEASE_ALL(...) \
    release_all(sizeof((Value *[]){__VA_ARGS__}) / sizeof(Value *), (Value *[]){__VA_ARGS__})

static void t_singletons(void) {
    CHECK(LM_TRUE->flags & LM_IMMORTAL);
    CHECK(lm_bool(true) == LM_TRUE);
    CHECK(lm_bool(false) == LM_FALSE);
    CHECK(LM_EMPTY != LM_FALSE && LM_EMPTY != LM_VOID);
    CHECK_STR(lm_type_name(LM_TRUE), "Boolean");
    CHECK_STR(lm_type_name(LM_VOID), "Void");
    CHECK_STR(lm_type_name(LM_EMPTY), "List");
    lm_release(LM_TRUE); /* no effect */
    CHECK(lm_retain(LM_EMPTY) == LM_EMPTY);
}

static void t_integers(void) {
    size_t live = lm_live_objects();
    Value *small = lm_integer(7);
    CHECK(small == lm_integer(7)); /* cached and immortal */
    CHECK_INT(lm_live_objects(), live);
    Value *large = lm_integer(INT64_MIN);
    CHECK_INT(lm_live_objects(), live + 1);
    CHECK_INT(lm_integer_value(large), INT64_MIN);
    CHECK_STR(lm_type_name(large), "Integer");
    lm_release(large);
    CHECK_INT(lm_live_objects(), live);
}

static void t_reference_counting(void) {
    size_t live = lm_live_objects();
    Value *string = lm_string_cstr("hello");
    CHECK_INT(string->refs, 1);
    lm_retain(string);
    CHECK_INT(string->refs, 2);
    Value *list = lm_prepend(string, LM_EMPTY);
    CHECK_INT(string->refs, 3);
    lm_release(string);
    lm_release(string);
    CHECK_INT(lm_live_objects(), live + 2);
    lm_release(list); /* frees the pair and then the string */
    CHECK_INT(lm_live_objects(), live);
}

static void t_strings(void) {
    Value *ascii = lm_string_cstr("abc");
    CHECK(ascii->flags & LM_ASCII);
    CHECK_INT(lm_as_string(ascii)->length, 3);
    CHECK_INT(lm_string_offset(ascii, 2), 2);
    Value *mixed = lm_string_cstr("a\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(!(mixed->flags & LM_ASCII));
    CHECK_INT(lm_as_string(mixed)->length, 3);
    CHECK_INT(lm_string_offset(mixed, 2), 3);
    Value *with_nul = lm_string("a\0b", 3);
    CHECK_INT(lm_as_string(with_nul)->length, 3);
    CHECK_STR(lm_type_name(with_nul), "String");
    RELEASE_ALL(ascii, mixed, with_nul);
}

static void t_interning(void) {
    size_t live = lm_live_objects();
    Value *symbol = lm_symbol_cstr("foo");
    CHECK(symbol == lm_symbol("foo", 3));
    Value *keyword = lm_keyword_cstr("foo");
    CHECK(keyword == lm_keyword_cstr("foo"));
    CHECK(keyword != symbol);
    CHECK(!lm_equal(symbol, keyword));
    CHECK_STR(lm_type_name(symbol), "Symbol");
    CHECK_STR(lm_type_name(keyword), "Keyword");
    CHECK_STR(lm_as_name(keyword)->text, "foo");
    CHECK_INT(lm_live_objects(), live); /* interned names are immortal */
}

static void t_generated_symbols(void) {
    size_t live = lm_live_objects();
    Value *first = lm_generate_symbol();
    Value *second = lm_generate_symbol();
    CHECK(first != second);
    CHECK(!lm_equal(first, second));
    CHECK(lm_equal(first, first));
    CHECK(first != lm_symbol(lm_as_name(first)->text, lm_as_name(first)->bytes));
    CHECK_INT(lm_live_objects(), live + 2);
    RELEASE_ALL(first, second);
}

static void t_lists(void) {
    Value *list = INTEGERS(1, 2, 3);
    CHECK_INT(lm_list_length(list), 3);
    CHECK_INT(lm_integer_value(lm_first(list)), 1);
    CHECK_INT(lm_integer_value(lm_first(lm_rest(lm_rest(list)))), 3);
    CHECK(lm_rest(lm_rest(lm_rest(list))) == LM_EMPTY);
    CHECK_INT(lm_list_length(LM_EMPTY), 0);
    lm_release(list);

    LmListBuilder builder;
    lm_list_builder_init(&builder);
    lm_list_append_take(&builder, lm_string_cstr("x"));
    lm_list_builder_discard(&builder);
}

static void t_releases_long_list(void) {
    size_t live = lm_live_objects();
    Value *list = LM_EMPTY;
    for (int64_t number = 0; number < 1000000; number++) {
        list = lm_prepend_take(lm_integer(number + 5000), list);
    }
    CHECK_INT(lm_list_length(list), 1000000);
    lm_release(list); /* would overflow the C stack if release recursed */
    CHECK_INT(lm_live_objects(), live);
}

static void t_releases_deep_nesting(void) {
    size_t live = lm_live_objects();
    Value *nested = LM_EMPTY;
    for (int level = 0; level < 1000000; level++) nested = lm_prepend_take(nested, LM_EMPTY);
    Value *copy = LM_EMPTY;
    for (int level = 0; level < 1000000; level++) copy = lm_prepend_take(copy, LM_EMPTY);
    CHECK(lm_equal(nested, copy)); /* equal? does not recurse either */
    RELEASE_ALL(nested, copy);
    CHECK_INT(lm_live_objects(), live);
}

static void t_equal(void) {
    Value *string_one = lm_string_cstr("1"), *other_string_one = lm_string_cstr("1");
    CHECK(lm_equal(string_one, other_string_one));
    CHECK(!lm_equal(string_one, lm_integer(1)));
    CHECK(lm_equal(lm_integer(5), lm_integer(5)));
    Value *large = lm_integer(1LL << 40), *other_large = lm_integer(1LL << 40);
    CHECK(lm_equal(large, other_large));
    CHECK(!lm_equal(LM_TRUE, LM_FALSE));
    CHECK(!lm_equal(LM_EMPTY, LM_FALSE));
    CHECK(!lm_equal(LM_EMPTY, LM_VOID));
    Value *one_two = INTEGERS(1, 2), *other_one_two = INTEGERS(1, 2);
    Value *one_two_three = INTEGERS(1, 2, 3), *two_one = INTEGERS(2, 1);
    CHECK(lm_equal(one_two, other_one_two));
    CHECK(!lm_equal(one_two, one_two_three));
    CHECK(!lm_equal(one_two_three, one_two));
    CHECK(!lm_equal(one_two, two_one));
    CHECK(!lm_equal(one_two, LM_EMPTY));
    Value *nested = lm_prepend(one_two, LM_EMPTY);
    Value *other_nested = lm_prepend(other_one_two, LM_EMPTY);
    CHECK(lm_equal(nested, other_nested));
    Value *accented = lm_string_cstr("\xC3\xA9"), *plain = lm_string_cstr("e");
    CHECK(!lm_equal(accented, plain));
    RELEASE_ALL(string_one, other_string_one, large, other_large, one_two, other_one_two,
                one_two_three, two_one, nested, other_nested, accented, plain);
}

static Value *dummy_primitive(LmState *, size_t, Value *const *) { return LM_VOID; }

static void t_functions(void) {
    Value *primitive = lm_primitive("dummy", 0, 0, dummy_primitive);
    Value *other_primitive = lm_primitive("dummy", 0, 0, dummy_primitive);
    CHECK(lm_equal(primitive, primitive));
    CHECK(!lm_equal(primitive, other_primitive));
    CHECK(lm_is_function(primitive));
    CHECK_STR(lm_type_name(primitive), "Function");
    Value *closure = lm_closure(primitive, nullptr, nullptr);
    CHECK_STR(lm_type_name(closure), "Function");
    CHECK_INT(primitive->refs, 2);
    lm_release(closure);
    CHECK_INT(primitive->refs, 1);
    RELEASE_ALL(primitive, other_primitive);
}

static int drop_count;
static void count_drop(Value *) { drop_count++; }
static const LmClass counted_class = {"Counted", count_drop};

static void t_opaque(void) {
    drop_count = 0;
    Value *object = lm_opaque_new(&counted_class, sizeof(LmOpaque));
    CHECK_STR(lm_type_name(object), "Counted");
    Value *list = lm_prepend(object, LM_EMPTY);
    lm_release(object);
    CHECK_INT(drop_count, 0);
    lm_release(list);
    CHECK_INT(drop_count, 1);
}

static void t_maps(void) {
    Value *key_a = lm_keyword_cstr("a"), *key_b = lm_keyword_cstr("b");
    Value *one = lm_integer(1), *two = lm_integer(2);
    Value *map = map_of(2, (Value *[]){key_a, one, key_b, two});
    CHECK_INT(lm_map_count(map), 2);
    CHECK(lm_map_get(map, key_a) == one);
    CHECK(lm_map_get(map, lm_keyword_cstr("c")) == nullptr);
    CHECK_STR(lm_type_name(map), "Map");

    /* the builder refuses duplicate keys */
    Value *duplicates = lm_map_new(2);
    CHECK(lm_map_insert_take(&duplicates, lm_string_cstr("k"), lm_integer(1)));
    CHECK(!lm_map_insert_take(&duplicates, lm_string_cstr("k"), lm_integer(2)));
    CHECK_INT(lm_map_count(duplicates), 1);
    lm_release(duplicates);

    /* associate and dissociate are persistent */
    Value *associated = lm_map_associate(map, key_a, two);
    CHECK(lm_map_get(map, key_a) == one);
    CHECK(lm_map_get(associated, key_a) == two);
    CHECK_INT(lm_map_count(associated), 2);
    Value *without_b = lm_map_dissociate(associated, key_b);
    CHECK_INT(lm_map_count(without_b), 1);
    CHECK_INT(lm_map_count(associated), 2);
    Value *unchanged = lm_map_dissociate(without_b, key_b); /* missing key */
    CHECK(unchanged == without_b);
    Value *empty = lm_map_dissociate(without_b, key_a);
    CHECK_INT(lm_map_count(empty), 0);
    CHECK(lm_equal(empty, lm_map_new(0)));

    /* iteration visits each key once */
    int seen = 0;
    Value *key, *value;
    for (size_t cursor = 0; lm_map_next(map, &cursor, &key, &value);) seen++;
    CHECK_INT(seen, 2);

    RELEASE_ALL(map, associated, without_b, unchanged, empty);
}

static void t_map_growth_and_structural_keys(void) {
    Value *map = lm_map_new(0);
    for (int64_t number = 0; number < 2000; number++) {
        CHECK(lm_map_insert_take(&map, INTEGERS(number, number + 1), lm_integer(number)));
    }
    CHECK_INT(lm_map_count(map), 2000);
    Value *key = INTEGERS(1500, 1501);
    Value *found = lm_map_get(map, key);
    CHECK(found && lm_integer_value(found) == 1500);
    RELEASE_ALL(key, map);
}

static void t_map_equality_ignores_order(void) {
    Value *key_a = lm_keyword_cstr("a"), *key_b = lm_keyword_cstr("b"), *key_c = lm_keyword_cstr("c");
    Value *one = lm_integer(1), *two = lm_integer(2);
    Value *a_then_b = map_of(2, (Value *[]){key_a, one, key_b, two});
    Value *b_then_a = map_of(2, (Value *[]){key_b, two, key_a, one});
    Value *other_values = map_of(2, (Value *[]){key_a, one, key_b, one});
    Value *other_keys = map_of(2, (Value *[]){key_a, one, key_c, two});
    CHECK(lm_equal(a_then_b, b_then_a));
    CHECK(lm_hash(a_then_b) == lm_hash(b_then_a));
    CHECK(!lm_equal(a_then_b, other_values));
    CHECK(!lm_equal(a_then_b, other_keys));

    /* maps as keys of maps */
    Value *outer = map_of(1, (Value *[]){a_then_b, one});
    CHECK(lm_map_get(outer, b_then_a) == one);
    RELEASE_ALL(a_then_b, b_then_a, other_values, other_keys, outer);
}

static void t_hash_consistent(void) {
    Value *list = INTEGERS(1, 2, 3), *other_list = INTEGERS(1, 2, 3);
    CHECK(lm_hash(list) == lm_hash(other_list));
    Value *string = lm_string_cstr("x"), *other_string = lm_string_cstr("x");
    CHECK(lm_hash(string) == lm_hash(other_string));
    Value *large = lm_integer(1LL << 50), *other_large = lm_integer(1LL << 50);
    CHECK(lm_hash(large) == lm_hash(other_large));
    RELEASE_ALL(list, other_list, string, other_string, large, other_large);
}

const TestSuite value_suite = {"value", (const TestCase[]){
    {"singletons", t_singletons},
    {"integers", t_integers},
    {"reference counting", t_reference_counting},
    {"strings count code points", t_strings},
    {"symbols and keywords are interned", t_interning},
    {"generated symbols are fresh and counted", t_generated_symbols},
    {"lists", t_lists},
    {"releases a list of a million elements", t_releases_long_list},
    {"releases and compares deep nesting", t_releases_deep_nesting},
    {"equal? per type", t_equal},
    {"functions compare by identity", t_functions},
    {"opaque objects are dropped once", t_opaque},
    {"maps", t_maps},
    {"maps grow and take structural keys", t_map_growth_and_structural_keys},
    {"map equality ignores order", t_map_equality_ignores_order},
    {"hash is consistent with equal?", t_hash_consistent},
    {},
}};
