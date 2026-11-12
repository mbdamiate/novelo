/* core §2 — values, refcounting, interning, equal?, hashing. */
#include "test.h"

#include "value.h"

/* Builds the list of the given integers. */
static Value *ints(size_t n, const int64_t *items) {
    LmListBuilder b;
    lm_list_builder_init(&b);
    for (size_t i = 0; i < n; i++) lm_list_append_take(&b, lm_integer(items[i]));
    return lm_list_builder_finish(&b);
}
#define INTS(...) ints(sizeof((int64_t[]){__VA_ARGS__}) / sizeof(int64_t), (int64_t[]){__VA_ARGS__})

static Value *map_of(size_t n, Value *const *pairs) { /* borrows keys and values */
    Value *map = lm_map_new(n);
    for (size_t i = 0; i < n; i++) {
        CHECK(lm_map_insert_take(&map, lm_retain(pairs[2 * i]), lm_retain(pairs[2 * i + 1])));
    }
    return map;
}

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
    Value *big = lm_integer(INT64_MIN);
    CHECK_INT(lm_live_objects(), live + 1);
    CHECK_INT(lm_integer_value(big), INT64_MIN);
    CHECK_STR(lm_type_name(big), "Integer");
    lm_release(big);
    CHECK_INT(lm_live_objects(), live);
}

static void t_refcounting(void) {
    size_t live = lm_live_objects();
    Value *s = lm_string_cstr("hello");
    CHECK_INT(s->refs, 1);
    lm_retain(s);
    CHECK_INT(s->refs, 2);
    Value *list = lm_prepend(s, LM_EMPTY);
    CHECK_INT(s->refs, 3);
    lm_release(s);
    lm_release(s);
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
    lm_release(ascii);
    lm_release(mixed);
    lm_release(with_nul);
}

static void t_interning(void) {
    size_t live = lm_live_objects();
    Value *foo = lm_symbol_cstr("foo");
    CHECK(foo == lm_symbol("foo", 3));
    Value *keyword = lm_keyword_cstr("foo");
    CHECK(keyword == lm_keyword_cstr("foo"));
    CHECK(keyword != foo);
    CHECK(!lm_equal(foo, keyword));
    CHECK_STR(lm_type_name(foo), "Symbol");
    CHECK_STR(lm_type_name(keyword), "Keyword");
    CHECK_STR(lm_as_name(keyword)->text, "foo");
    CHECK_INT(lm_live_objects(), live); /* interned names are immortal */
}

static void t_generated_symbols(void) {
    size_t live = lm_live_objects();
    Value *a = lm_generate_symbol();
    Value *b = lm_generate_symbol();
    CHECK(a != b);
    CHECK(!lm_equal(a, b));
    CHECK(lm_equal(a, a));
    CHECK(a != lm_symbol(lm_as_name(a)->text, lm_as_name(a)->bytes));
    CHECK_INT(lm_live_objects(), live + 2);
    lm_release(a);
    lm_release(b);
}

static void t_lists(void) {
    Value *list = INTS(1, 2, 3);
    CHECK_INT(lm_list_length(list), 3);
    CHECK_INT(lm_integer_value(lm_first(list)), 1);
    CHECK_INT(lm_integer_value(lm_first(lm_rest(lm_rest(list)))), 3);
    CHECK(lm_rest(lm_rest(lm_rest(list))) == LM_EMPTY);
    CHECK_INT(lm_list_length(LM_EMPTY), 0);
    lm_release(list);

    LmListBuilder b;
    lm_list_builder_init(&b);
    lm_list_append_take(&b, lm_string_cstr("x"));
    lm_list_builder_discard(&b);
}

static void t_releases_long_list(void) {
    size_t live = lm_live_objects();
    Value *list = LM_EMPTY;
    for (int64_t i = 0; i < 1000000; i++) list = lm_prepend_take(lm_integer(i + 5000), list);
    CHECK_INT(lm_list_length(list), 1000000);
    lm_release(list); /* would overflow the C stack if release recursed */
    CHECK_INT(lm_live_objects(), live);
}

static void t_releases_deep_nesting(void) {
    size_t live = lm_live_objects();
    Value *nested = LM_EMPTY;
    for (int i = 0; i < 1000000; i++) nested = lm_prepend_take(nested, LM_EMPTY);
    Value *copy = LM_EMPTY;
    for (int i = 0; i < 1000000; i++) copy = lm_prepend_take(copy, LM_EMPTY);
    CHECK(lm_equal(nested, copy)); /* equal? does not recurse either */
    lm_release(nested);
    lm_release(copy);
    CHECK_INT(lm_live_objects(), live);
}

static void t_equal(void) {
    Value *s1 = lm_string_cstr("1"), *s2 = lm_string_cstr("1");
    Value *one = lm_integer(1);
    CHECK(lm_equal(s1, s2));
    CHECK(!lm_equal(s1, one));
    CHECK(lm_equal(lm_integer(5), lm_integer(5)));
    Value *b1 = lm_integer(1LL << 40), *b2 = lm_integer(1LL << 40);
    CHECK(lm_equal(b1, b2));
    CHECK(!lm_equal(LM_TRUE, LM_FALSE));
    CHECK(!lm_equal(LM_EMPTY, LM_FALSE));
    CHECK(!lm_equal(LM_EMPTY, LM_VOID));
    Value *l1 = INTS(1, 2), *l2 = INTS(1, 2), *l3 = INTS(1, 2, 3), *l4 = INTS(2, 1);
    CHECK(lm_equal(l1, l2));
    CHECK(!lm_equal(l1, l3));
    CHECK(!lm_equal(l3, l1));
    CHECK(!lm_equal(l1, l4));
    CHECK(!lm_equal(l1, LM_EMPTY));
    Value *nested1 = lm_prepend(l1, LM_EMPTY), *nested2 = lm_prepend(l2, LM_EMPTY);
    CHECK(lm_equal(nested1, nested2));
    Value *a = lm_string_cstr("\xC3\xA9"), *e = lm_string_cstr("e");
    CHECK(!lm_equal(a, e));
    Value *all[] = {s1, s2, b1, b2, l1, l2, l3, l4, nested1, nested2, a, e};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) lm_release(all[i]);
}

static Value *dummy_primitive(LmState *, size_t, Value *const *) { return LM_VOID; }

static void t_functions(void) {
    Value *p = lm_primitive("dummy", 0, 0, dummy_primitive);
    Value *q = lm_primitive("dummy", 0, 0, dummy_primitive);
    CHECK(lm_equal(p, p));
    CHECK(!lm_equal(p, q));
    CHECK(lm_is_function(p));
    CHECK_STR(lm_type_name(p), "Function");
    Value *closure = lm_closure(p, nullptr, nullptr);
    CHECK_STR(lm_type_name(closure), "Function");
    CHECK_INT(p->refs, 2);
    lm_release(closure);
    CHECK_INT(p->refs, 1);
    lm_release(p);
    lm_release(q);
}

static int drops;
static void count_drop(Value *) { drops++; }
static const LmClass counted_class = {"Counted", count_drop};

static void t_opaque(void) {
    drops = 0;
    Value *o = lm_opaque_new(&counted_class, sizeof(LmOpaque));
    CHECK_STR(lm_type_name(o), "Counted");
    Value *list = lm_prepend(o, LM_EMPTY);
    lm_release(o);
    CHECK_INT(drops, 0);
    lm_release(list);
    CHECK_INT(drops, 1);
}

static void t_maps(void) {
    Value *ka = lm_keyword_cstr("a"), *kb = lm_keyword_cstr("b");
    Value *one = lm_integer(1), *two = lm_integer(2);
    Value *m = map_of(2, (Value *[]){ka, one, kb, two});
    CHECK_INT(lm_map_count(m), 2);
    CHECK(lm_map_get(m, ka) == one);
    CHECK(lm_map_get(m, lm_keyword_cstr("c")) == nullptr);
    CHECK_STR(lm_type_name(m), "Map");

    /* duplicate keys are refused by the builder */
    Value *dup = lm_map_new(2);
    CHECK(lm_map_insert_take(&dup, lm_string_cstr("k"), lm_integer(1)));
    CHECK(!lm_map_insert_take(&dup, lm_string_cstr("k"), lm_integer(2)));
    CHECK_INT(lm_map_count(dup), 1);
    lm_release(dup);

    /* associate and dissociate are persistent */
    Value *m2 = lm_map_associate(m, ka, two);
    CHECK(lm_map_get(m, ka) == one);
    CHECK(lm_map_get(m2, ka) == two);
    CHECK_INT(lm_map_count(m2), 2);
    Value *m3 = lm_map_dissociate(m2, kb);
    CHECK_INT(lm_map_count(m3), 1);
    CHECK_INT(lm_map_count(m2), 2);
    Value *same = lm_map_dissociate(m3, kb); /* missing key: unchanged */
    CHECK(same == m3);
    Value *empty = lm_map_dissociate(m3, ka);
    CHECK_INT(lm_map_count(empty), 0);
    CHECK(lm_equal(empty, lm_map_new(0)));

    /* iteration visits each key once */
    int seen = 0;
    Value *key, *value;
    for (size_t cursor = 0; lm_map_next(m, &cursor, &key, &value);) seen++;
    CHECK_INT(seen, 2);

    Value *all[] = {m, m2, m3, same, empty};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) lm_release(all[i]);
}

static void t_map_growth_and_structural_keys(void) {
    Value *m = lm_map_new(0);
    for (int64_t i = 0; i < 2000; i++) {
        CHECK(lm_map_insert_take(&m, INTS(i, i + 1), lm_integer(i)));
    }
    CHECK_INT(lm_map_count(m), 2000);
    Value *key = INTS(1500, 1501);
    Value *found = lm_map_get(m, key);
    CHECK(found && lm_integer_value(found) == 1500);
    lm_release(key);
    lm_release(m);
}

static void t_map_equality_ignores_order(void) {
    Value *ka = lm_keyword_cstr("a"), *kb = lm_keyword_cstr("b"), *kc = lm_keyword_cstr("c");
    Value *one = lm_integer(1), *two = lm_integer(2);
    Value *m1 = map_of(2, (Value *[]){ka, one, kb, two});
    Value *m2 = map_of(2, (Value *[]){kb, two, ka, one});
    Value *m3 = map_of(2, (Value *[]){ka, one, kb, one});
    Value *m4 = map_of(2, (Value *[]){ka, one, kc, two});
    CHECK(lm_equal(m1, m2));
    CHECK(lm_hash(m1) == lm_hash(m2));
    CHECK(!lm_equal(m1, m3));
    CHECK(!lm_equal(m1, m4));

    /* maps as keys of maps */
    Value *outer1 = map_of(1, (Value *[]){m1, one});
    Value *found = lm_map_get(outer1, m2);
    CHECK(found == one);
    Value *all[] = {m1, m2, m3, m4, outer1};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) lm_release(all[i]);
}

static void t_hash_consistent(void) {
    Value *a = INTS(1, 2, 3), *b = INTS(1, 2, 3);
    CHECK(lm_hash(a) == lm_hash(b));
    Value *s1 = lm_string_cstr("x"), *s2 = lm_string_cstr("x");
    CHECK(lm_hash(s1) == lm_hash(s2));
    Value *i1 = lm_integer(1LL << 50), *i2 = lm_integer(1LL << 50);
    CHECK(lm_hash(i1) == lm_hash(i2));
    Value *all[] = {a, b, s1, s2, i1, i2};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) lm_release(all[i]);
}

const TestSuite value_suite = {"value", (const TestCase[]){
    {"singletons", t_singletons},
    {"integers", t_integers},
    {"reference counting", t_refcounting},
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
