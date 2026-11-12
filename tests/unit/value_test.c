/* core §2 — values, refcounting, interning, equal?, hashing. */
#include "test.h"

#include "value.h"

/* Builds the list of the given integers. */
static Value *integer_list(size_t count, const int64_t *items) {
    LispListBuilder builder;
    lisp_list_builder_init(&builder);
    for (size_t index = 0; index < count; index++) {
        lisp_list_append_take(&builder, lisp_integer(items[index]));
    }
    return lisp_list_builder_finish(&builder);
}
#define INTEGERS(...)                                                        \
    integer_list(sizeof((int64_t[]){__VA_ARGS__}) / sizeof(int64_t),         \
                 (int64_t[]){__VA_ARGS__})

/* Builds a map from alternating keys and values (borrowed). */
static Value *map_of(size_t pair_count, Value *const *keys_and_values) {
    Value *map = lisp_map_new(pair_count);
    for (size_t index = 0; index < pair_count; index++) {
        CHECK(lisp_map_insert_take(&map, lisp_retain(keys_and_values[2 * index]),
                                 lisp_retain(keys_and_values[2 * index + 1])));
    }
    return map;
}

static void release_all(size_t count, Value *const *values) {
    for (size_t index = 0; index < count; index++) lisp_release(values[index]);
}
#define RELEASE_ALL(...) \
    release_all(sizeof((Value *[]){__VA_ARGS__}) / sizeof(Value *), (Value *[]){__VA_ARGS__})

static void t_singletons(void) {
    CHECK(LISP_TRUE->flags & LISP_IMMORTAL);
    CHECK(lisp_bool(true) == LISP_TRUE);
    CHECK(lisp_bool(false) == LISP_FALSE);
    CHECK(LISP_EMPTY != LISP_FALSE && LISP_EMPTY != LISP_VOID);
    CHECK_STR(lisp_type_name(LISP_TRUE), "Boolean");
    CHECK_STR(lisp_type_name(LISP_VOID), "Void");
    CHECK_STR(lisp_type_name(LISP_EMPTY), "List");
    lisp_release(LISP_TRUE); /* no effect */
    CHECK(lisp_retain(LISP_EMPTY) == LISP_EMPTY);
}

static void t_integers(void) {
    size_t live = lisp_live_objects();
    Value *small = lisp_integer(7);
    CHECK(small == lisp_integer(7)); /* cached and immortal */
    CHECK_INT(lisp_live_objects(), live);
    Value *large = lisp_integer(INT64_MIN);
    CHECK_INT(lisp_live_objects(), live + 1);
    CHECK_INT(lisp_integer_value(large), INT64_MIN);
    CHECK_STR(lisp_type_name(large), "Integer");
    lisp_release(large);
    CHECK_INT(lisp_live_objects(), live);
}

static void t_reference_counting(void) {
    size_t live = lisp_live_objects();
    Value *string = lisp_string_cstr("hello");
    CHECK_INT(string->refs, 1);
    lisp_retain(string);
    CHECK_INT(string->refs, 2);
    Value *list = lisp_prepend(string, LISP_EMPTY);
    CHECK_INT(string->refs, 3);
    lisp_release(string);
    lisp_release(string);
    CHECK_INT(lisp_live_objects(), live + 2);
    lisp_release(list); /* frees the pair and then the string */
    CHECK_INT(lisp_live_objects(), live);
}

static void t_strings(void) {
    Value *ascii = lisp_string_cstr("abc");
    CHECK(ascii->flags & LISP_ASCII);
    CHECK_INT(lisp_as_string(ascii)->length, 3);
    CHECK_INT(lisp_string_offset(ascii, 2), 2);
    Value *mixed = lisp_string_cstr("a\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(!(mixed->flags & LISP_ASCII));
    CHECK_INT(lisp_as_string(mixed)->length, 3);
    CHECK_INT(lisp_string_offset(mixed, 2), 3);
    Value *with_nul = lisp_string("a\0b", 3);
    CHECK_INT(lisp_as_string(with_nul)->length, 3);
    CHECK_STR(lisp_type_name(with_nul), "String");
    RELEASE_ALL(ascii, mixed, with_nul);
}

static void t_interning(void) {
    size_t live = lisp_live_objects();
    Value *symbol = lisp_symbol_cstr("foo");
    CHECK(symbol == lisp_symbol("foo", 3));
    Value *keyword = lisp_keyword_cstr("foo");
    CHECK(keyword == lisp_keyword_cstr("foo"));
    CHECK(keyword != symbol);
    CHECK(!lisp_equal(symbol, keyword));
    CHECK_STR(lisp_type_name(symbol), "Symbol");
    CHECK_STR(lisp_type_name(keyword), "Keyword");
    CHECK_STR(lisp_as_name(keyword)->text, "foo");
    CHECK_INT(lisp_live_objects(), live); /* interned names are immortal */
}

static void t_generated_symbols(void) {
    size_t live = lisp_live_objects();
    Value *first = lisp_generate_symbol();
    Value *second = lisp_generate_symbol();
    CHECK(first != second);
    CHECK(!lisp_equal(first, second));
    CHECK(lisp_equal(first, first));
    CHECK(first != lisp_symbol(lisp_as_name(first)->text, lisp_as_name(first)->bytes));
    CHECK_INT(lisp_live_objects(), live + 2);
    RELEASE_ALL(first, second);
}

static void t_lists(void) {
    Value *list = INTEGERS(1, 2, 3);
    CHECK_INT(lisp_list_length(list), 3);
    CHECK_INT(lisp_integer_value(lisp_first(list)), 1);
    CHECK_INT(lisp_integer_value(lisp_first(lisp_rest(lisp_rest(list)))), 3);
    CHECK(lisp_rest(lisp_rest(lisp_rest(list))) == LISP_EMPTY);
    CHECK_INT(lisp_list_length(LISP_EMPTY), 0);
    lisp_release(list);

    LispListBuilder builder;
    lisp_list_builder_init(&builder);
    lisp_list_append_take(&builder, lisp_string_cstr("x"));
    lisp_list_builder_discard(&builder);
}

static void t_releases_long_list(void) {
    size_t live = lisp_live_objects();
    Value *list = LISP_EMPTY;
    for (int64_t number = 0; number < 1000000; number++) {
        list = lisp_prepend_take(lisp_integer(number + 5000), list);
    }
    CHECK_INT(lisp_list_length(list), 1000000);
    lisp_release(list); /* would overflow the C stack if release recursed */
    CHECK_INT(lisp_live_objects(), live);
}

static void t_releases_deep_nesting(void) {
    size_t live = lisp_live_objects();
    Value *nested = LISP_EMPTY;
    for (int level = 0; level < 1000000; level++) nested = lisp_prepend_take(nested, LISP_EMPTY);
    Value *copy = LISP_EMPTY;
    for (int level = 0; level < 1000000; level++) copy = lisp_prepend_take(copy, LISP_EMPTY);
    CHECK(lisp_equal(nested, copy)); /* equal? does not recurse either */
    RELEASE_ALL(nested, copy);
    CHECK_INT(lisp_live_objects(), live);
}

static void t_equal(void) {
    Value *string_one = lisp_string_cstr("1"), *other_string_one = lisp_string_cstr("1");
    CHECK(lisp_equal(string_one, other_string_one));
    CHECK(!lisp_equal(string_one, lisp_integer(1)));
    CHECK(lisp_equal(lisp_integer(5), lisp_integer(5)));
    Value *large = lisp_integer(1LL << 40), *other_large = lisp_integer(1LL << 40);
    CHECK(lisp_equal(large, other_large));
    CHECK(!lisp_equal(LISP_TRUE, LISP_FALSE));
    CHECK(!lisp_equal(LISP_EMPTY, LISP_FALSE));
    CHECK(!lisp_equal(LISP_EMPTY, LISP_VOID));
    Value *one_two = INTEGERS(1, 2), *other_one_two = INTEGERS(1, 2);
    Value *one_two_three = INTEGERS(1, 2, 3), *two_one = INTEGERS(2, 1);
    CHECK(lisp_equal(one_two, other_one_two));
    CHECK(!lisp_equal(one_two, one_two_three));
    CHECK(!lisp_equal(one_two_three, one_two));
    CHECK(!lisp_equal(one_two, two_one));
    CHECK(!lisp_equal(one_two, LISP_EMPTY));
    Value *nested = lisp_prepend(one_two, LISP_EMPTY);
    Value *other_nested = lisp_prepend(other_one_two, LISP_EMPTY);
    CHECK(lisp_equal(nested, other_nested));
    Value *accented = lisp_string_cstr("\xC3\xA9"), *plain = lisp_string_cstr("e");
    CHECK(!lisp_equal(accented, plain));
    RELEASE_ALL(string_one, other_string_one, large, other_large, one_two, other_one_two,
                one_two_three, two_one, nested, other_nested, accented, plain);
}

static Value *dummy_primitive(LispState *, size_t, Value *const *) { return LISP_VOID; }

static void t_functions(void) {
    Value *primitive = lisp_primitive("dummy", 0, 0, dummy_primitive);
    Value *other_primitive = lisp_primitive("dummy", 0, 0, dummy_primitive);
    CHECK(lisp_equal(primitive, primitive));
    CHECK(!lisp_equal(primitive, other_primitive));
    CHECK(lisp_is_function(primitive));
    CHECK_STR(lisp_type_name(primitive), "Function");
    Value *closure = lisp_closure(primitive, nullptr, nullptr);
    CHECK_STR(lisp_type_name(closure), "Function");
    CHECK_INT(primitive->refs, 2);
    lisp_release(closure);
    CHECK_INT(primitive->refs, 1);
    RELEASE_ALL(primitive, other_primitive);
}

static int drop_count;
static void count_drop(Value *) { drop_count++; }
static const LispClass counted_class = {"Counted", count_drop};

static void t_opaque(void) {
    drop_count = 0;
    Value *object = lisp_opaque_new(&counted_class, sizeof(LispOpaque));
    CHECK_STR(lisp_type_name(object), "Counted");
    Value *list = lisp_prepend(object, LISP_EMPTY);
    lisp_release(object);
    CHECK_INT(drop_count, 0);
    lisp_release(list);
    CHECK_INT(drop_count, 1);
}

static void t_maps(void) {
    Value *key_a = lisp_keyword_cstr("a"), *key_b = lisp_keyword_cstr("b");
    Value *one = lisp_integer(1), *two = lisp_integer(2);
    Value *map = map_of(2, (Value *[]){key_a, one, key_b, two});
    CHECK_INT(lisp_map_count(map), 2);
    CHECK(lisp_map_get(map, key_a) == one);
    CHECK(lisp_map_get(map, lisp_keyword_cstr("c")) == nullptr);
    CHECK_STR(lisp_type_name(map), "Map");

    /* the builder refuses duplicate keys */
    Value *duplicates = lisp_map_new(2);
    CHECK(lisp_map_insert_take(&duplicates, lisp_string_cstr("k"), lisp_integer(1)));
    CHECK(!lisp_map_insert_take(&duplicates, lisp_string_cstr("k"), lisp_integer(2)));
    CHECK_INT(lisp_map_count(duplicates), 1);
    lisp_release(duplicates);

    /* associate and dissociate are persistent */
    Value *associated = lisp_map_associate(map, key_a, two);
    CHECK(lisp_map_get(map, key_a) == one);
    CHECK(lisp_map_get(associated, key_a) == two);
    CHECK_INT(lisp_map_count(associated), 2);
    Value *without_b = lisp_map_dissociate(associated, key_b);
    CHECK_INT(lisp_map_count(without_b), 1);
    CHECK_INT(lisp_map_count(associated), 2);
    Value *unchanged = lisp_map_dissociate(without_b, key_b); /* missing key */
    CHECK(unchanged == without_b);
    Value *empty = lisp_map_dissociate(without_b, key_a);
    CHECK_INT(lisp_map_count(empty), 0);
    CHECK(lisp_equal(empty, lisp_map_new(0)));

    /* iteration visits each key once */
    int seen = 0;
    Value *key, *value;
    for (size_t cursor = 0; lisp_map_next(map, &cursor, &key, &value);) seen++;
    CHECK_INT(seen, 2);

    RELEASE_ALL(map, associated, without_b, unchanged, empty);
}

static void t_map_keeps_insertion_order(void) {
    Value *map = lisp_map_new(0);
    for (int64_t number = 0; number < 100; number++) {
        CHECK(lisp_map_insert_take(&map, lisp_integer(99 - number), lisp_integer(number)));
    }
    int64_t expected_key = 99;
    Value *key, *value;
    for (size_t cursor = 0; lisp_map_next(map, &cursor, &key, &value); expected_key--) {
        CHECK_INT(lisp_integer_value(key), expected_key);
    }
    CHECK_INT(expected_key, -1);

    /* associate keeps the position of an existing key and appends a new one */
    Value *replaced = lisp_map_associate(map, lisp_integer(50), LISP_TRUE);
    Value *appended = lisp_map_associate(map, lisp_integer(500), LISP_TRUE);
    size_t cursor = 49;
    CHECK(lisp_map_next(replaced, &cursor, &key, &value));
    CHECK_INT(lisp_integer_value(key), 50);
    CHECK(value == LISP_TRUE);
    cursor = 100;
    CHECK(lisp_map_next(appended, &cursor, &key, &value));
    CHECK_INT(lisp_integer_value(key), 500);

    /* dissociate keeps the order of the others */
    Value *without = lisp_map_dissociate(map, lisp_integer(99));
    cursor = 0;
    CHECK(lisp_map_next(without, &cursor, &key, &value));
    CHECK_INT(lisp_integer_value(key), 98);
    RELEASE_ALL(map, replaced, appended, without);
}

static void t_map_growth_and_structural_keys(void) {
    Value *map = lisp_map_new(0);
    for (int64_t number = 0; number < 2000; number++) {
        CHECK(lisp_map_insert_take(&map, INTEGERS(number, number + 1), lisp_integer(number)));
    }
    CHECK_INT(lisp_map_count(map), 2000);
    Value *key = INTEGERS(1500, 1501);
    Value *found = lisp_map_get(map, key);
    CHECK(found && lisp_integer_value(found) == 1500);
    RELEASE_ALL(key, map);
}

static void t_map_equality_ignores_order(void) {
    Value *key_a = lisp_keyword_cstr("a"), *key_b = lisp_keyword_cstr("b"), *key_c = lisp_keyword_cstr("c");
    Value *one = lisp_integer(1), *two = lisp_integer(2);
    Value *a_then_b = map_of(2, (Value *[]){key_a, one, key_b, two});
    Value *b_then_a = map_of(2, (Value *[]){key_b, two, key_a, one});
    Value *other_values = map_of(2, (Value *[]){key_a, one, key_b, one});
    Value *other_keys = map_of(2, (Value *[]){key_a, one, key_c, two});
    CHECK(lisp_equal(a_then_b, b_then_a));
    CHECK(lisp_hash(a_then_b) == lisp_hash(b_then_a));
    CHECK(!lisp_equal(a_then_b, other_values));
    CHECK(!lisp_equal(a_then_b, other_keys));

    /* maps as keys of maps */
    Value *outer = map_of(1, (Value *[]){a_then_b, one});
    CHECK(lisp_map_get(outer, b_then_a) == one);
    RELEASE_ALL(a_then_b, b_then_a, other_values, other_keys, outer);
}

static void t_hash_consistent(void) {
    Value *list = INTEGERS(1, 2, 3), *other_list = INTEGERS(1, 2, 3);
    CHECK(lisp_hash(list) == lisp_hash(other_list));
    Value *string = lisp_string_cstr("x"), *other_string = lisp_string_cstr("x");
    CHECK(lisp_hash(string) == lisp_hash(other_string));
    Value *large = lisp_integer(1LL << 50), *other_large = lisp_integer(1LL << 50);
    CHECK(lisp_hash(large) == lisp_hash(other_large));
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
    {"maps keep insertion order", t_map_keeps_insertion_order},
    {"maps grow and take structural keys", t_map_growth_and_structural_keys},
    {"map equality ignores order", t_map_equality_ignores_order},
    {"hash is consistent with equal?", t_hash_consistent},
    {},
}};
