/* core §2 — values, reference counting, interning, equal? and hashing.
 *
 * OWNERSHIP RULE
 *   - A function that returns a Value * returns a NEW reference, which the
 *     caller must eventually pass to lisp_release (or hand to a _take function).
 *   - Arguments are BORROWED: the callee retains whatever it keeps.
 *   - Exception: a function whose name ends in _take takes over the caller's
 *     reference to the arguments marked in its comment.
 *   - Accessors marked "borrowed" (lisp_first, lisp_rest, lisp_map_get, …) return a
 *     reference owned by the container; retain it to keep it.
 *
 * The heap is acyclic (values are immutable and recursion goes through global
 * names), so counting references frees everything. Releasing never recurses in
 * C: objects whose count drops to zero go on an explicit work stack.
 *
 * Immortal objects (#true, #false, #void, (), the empty map, small integers and
 * interned symbols and keywords) carry LISP_IMMORTAL; retain and release skip
 * them, and lisp_live_objects() does not count them.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum LispKind : uint8_t {
    LISP_KIND_BOOLEAN,
    LISP_KIND_VOID,
    LISP_KIND_INTEGER,
    LISP_KIND_FLOAT,     /* finite binary64 (§2.5) */
    LISP_KIND_STRING,
    LISP_KIND_SYMBOL,
    LISP_KIND_KEYWORD,
    LISP_KIND_EMPTY,     /* ()            — type List */
    LISP_KIND_PAIR,      /* non-empty list — type List */
    LISP_KIND_MAP,
    LISP_KIND_SET,       /* a LispMap whose values are all #true */
    LISP_KIND_CLOSURE,   /* type Function */
    LISP_KIND_PRIMITIVE, /* type Function */
    LISP_KIND_OPAQUE,    /* internal objects (nodes, environments, contexts) */
} LispKind;

enum {
    LISP_IMMORTAL = 1 << 0,
    LISP_ASCII = 1 << 1,        /* String: every code point is ASCII */
    LISP_HAS_POSITION = 1 << 2, /* has an entry in the position table (§6) */
    LISP_GENERATED = 1 << 3,    /* Symbol: made by generate-symbol, not interned */
};

typedef struct Value {
    uint32_t refs;
    uint8_t kind;
    uint8_t flags;
} Value;

typedef struct LispInteger {
    Value header;
    int64_t value;
} LispInteger;

typedef struct LispFloat {
    Value header;
    double value;   /* always finite */
} LispFloat;

typedef struct LispString {
    Value header;
    size_t bytes;   /* UTF-8 length */
    size_t length;  /* code points */
    char data[];    /* NUL-terminated; may contain U+0000 */
} LispString;

typedef struct LispName { /* Symbol or Keyword (the name has no ':') */
    Value header;
    uint64_t hash;
    size_t bytes;
    char text[];    /* NUL-terminated */
} LispName;

typedef struct LispPair {
    Value header;
    Value *first;
    Value *rest;    /* always a List */
} LispPair;

typedef struct LispMapEntry {
    Value *key;
    Value *value;
    uint64_t hash;
    uint64_t sequence; /* when the key was inserted; replacing a value keeps it */
} LispMapEntry;

/* core §8.6 — a persistent hash array mapped trie (value.c). associate and
 * dissociate copy only the path to the changed slot and share the rest.
 * Iteration follows the insertion sequence, which keeps the reading order of
 * map literals for left-to-right evaluation (§8.4). */
typedef struct LispMap {
    Value header;
    size_t count;
    Value *root;                  /* trie node, or nullptr when empty */
    uint64_t next_sequence;
    const LispMapEntry **ordered; /* the entries by sequence, built on the first iteration */
} LispMap;

typedef struct LispClosure {
    Value header;
    Value *lambda;  /* expanded lambda node */
    Value *environment;     /* captured environment, or nullptr */
    Value *context; /* include context (§8.8), or nullptr */
} LispClosure;

typedef struct LispState LispState;
typedef Value *(*LispPrimitiveFunction)(LispState *state, size_t argument_count, Value *const *arguments);

typedef struct LispPrimitive {
    Value header;
    const char *name;
    int minimum_arguments;
    int maximum_arguments;   /* -1: no maximum */
    LispPrimitiveFunction function;
} LispPrimitive;

/* Internal objects counted like values. drop() releases the children (with
 * lisp_release, which defers to the work stack) and frees any memory the object
 * owns besides itself; lisp_release frees the object afterwards. */
typedef struct LispClass {
    const char *name;
    void (*drop)(Value *self);
} LispClass;

typedef struct LispOpaque {
    Value header;
    const LispClass *class;
} LispOpaque;

/* ---- singletons ---------------------------------------------------------- */

extern Value lisp_true_object, lisp_false_object, lisp_void_object, lisp_empty_object;
#define LISP_TRUE (&lisp_true_object)
#define LISP_FALSE (&lisp_false_object)
#define LISP_VOID (&lisp_void_object)
#define LISP_EMPTY (&lisp_empty_object)

static inline Value *lisp_bool(bool condition) { return condition ? LISP_TRUE : LISP_FALSE; }

/* ---- reference counting --------------------------------------------------- */

static inline Value *lisp_retain(Value *value) {
    if (value && !(value->flags & LISP_IMMORTAL)) value->refs++;
    return value;
}

void lisp_release(Value *value);

/* Allocates a counted object of the given size (refs = 1, flags = 0). */
[[nodiscard]] Value *lisp_object_new(LispKind kind, size_t size);

/* Number of live objects that are not immortal (leak detection). */
size_t lisp_live_objects(void);

/* Called when an object with LISP_HAS_POSITION is freed (§6.4). */
void lisp_set_position_forget_hook(void (*hook)(const Value *node));

/* Frees the interning table and the integer cache. No value may be used after. */
void lisp_values_shutdown(void);

/* ---- types --------------------------------------------------------------- */

static inline bool lisp_is_list(const Value *value) {
    return value->kind == LISP_KIND_EMPTY || value->kind == LISP_KIND_PAIR;
}
static inline bool lisp_is_function(const Value *value) {
    return value->kind == LISP_KIND_CLOSURE || value->kind == LISP_KIND_PRIMITIVE;
}

/* Type name of §2: "Boolean", "Void", "Integer", …, "Function". */
const char *lisp_type_name(const Value *value);

/* ---- integers ------------------------------------------------------------- */

[[nodiscard]] Value *lisp_integer(int64_t value);
static inline int64_t lisp_integer_value(const Value *integer) { return ((const LispInteger *)integer)->value; }

/* ---- floats (core §2.5) ------------------------------------------------------ */

/* `value` must be finite: callers check for overflow first (§8.5). */
[[nodiscard]] Value *lisp_float(double value);
static inline double lisp_float_value(const Value *number) { return ((const LispFloat *)number)->value; }

/* ---- strings (valid UTF-8 only) ------------------------------------------- */

[[nodiscard]] Value *lisp_string(const char *utf8, size_t bytes);
[[nodiscard]] Value *lisp_string_cstr(const char *utf8);
static inline const LispString *lisp_as_string(const Value *string) { return (const LispString *)string; }

/* Byte offset of code point `index` (0..length); O(1) for ASCII strings. */
size_t lisp_string_offset(const Value *string, size_t index);

/* ---- symbols and keywords -------------------------------------------------- */

/* Interned: the same name always gives the same immortal object. */
[[nodiscard]] Value *lisp_symbol(const char *name, size_t bytes);
[[nodiscard]] Value *lisp_symbol_cstr(const char *name);
[[nodiscard]] Value *lisp_keyword(const char *name, size_t bytes);
[[nodiscard]] Value *lisp_keyword_cstr(const char *name);

/* A fresh, counted, non-interned symbol (generate-symbol, §9). */
[[nodiscard]] Value *lisp_generate_symbol(void);

static inline const LispName *lisp_as_name(const Value *name) { return (const LispName *)name; }

/* ---- lists ----------------------------------------------------------------- */

[[nodiscard]] Value *lisp_prepend(Value *first, Value *rest);
/* Takes both references. */
[[nodiscard]] Value *lisp_prepend_take(Value *first, Value *rest);

static inline Value *lisp_first(const Value *pair) { return ((const LispPair *)pair)->first; } /* borrowed */
static inline Value *lisp_rest(const Value *pair) { return ((const LispPair *)pair)->rest; }   /* borrowed */

size_t lisp_list_length(const Value *list);

/* Builds a list front to back by appending to the last pair while it is still
 * private to the builder. */
typedef struct LispListBuilder {
    Value *head;
    LispPair *last;
} LispListBuilder;

void lisp_list_builder_init(LispListBuilder *builder);
void lisp_list_append_take(LispListBuilder *builder, Value *element);
[[nodiscard]] Value *lisp_list_builder_finish(LispListBuilder *builder);
void lisp_list_builder_discard(LispListBuilder *builder);

/* ---- maps ------------------------------------------------------------------ */

/* A new map, which starts as the immortal empty map; `expected` is a hint
 * the persistent representation does not need. */
[[nodiscard]] Value *lisp_map_new(size_t expected);

/* Builder insertion into a map that only the caller holds (refs == 1). Takes
 * key and value. Returns false, and releases both, if the key is already
 * present. When *map is the shared empty map, it is replaced by a new one. */
bool lisp_map_insert_take(Value **map, Value *key, Value *value);

static inline size_t lisp_map_count(const Value *map) { return ((const LispMap *)map)->count; }

Value *lisp_map_get(const Value *map, const Value *key); /* borrowed, or nullptr */

/* Persistent operations: return a new map and leave the argument unchanged.
 * Dissociating a missing key returns the same map (retained). */
[[nodiscard]] Value *lisp_map_associate(const Value *map, Value *key, Value *value);
[[nodiscard]] Value *lisp_map_dissociate(Value *map, const Value *key);

/* ---- sets (§2, §9) ------------------------------------------------------------ */

/* A Set is a LispMap of kind LISP_KIND_SET whose values are all #true, so it
 * shares the trie, its persistence and its order. lisp_map_count gives its
 * size. */

/* The immortal empty set. */
[[nodiscard]] Value *lisp_set_new(void);

/* Builder insertion into a set only the caller holds. Takes the element.
 * Returns false, and releases it, if the element is already present. */
bool lisp_set_insert_take(Value **set, Value *element);

static inline bool lisp_set_contains(const Value *set, const Value *element) {
    return lisp_map_get(set, element) != nullptr;
}

/* Persistent: the set with (or without) the element; the same set, retained,
 * when there is nothing to change. */
[[nodiscard]] Value *lisp_set_adjoin(const Value *set, Value *element);
[[nodiscard]] Value *lisp_set_disjoin(Value *set, const Value *element);

/* Iteration in insertion order: for (size_t cursor = 0;
 * lisp_map_next(map, &cursor, &key, &value);) … (borrowed). The first call
 * sorts the entries once and keeps the order with the map. */
bool lisp_map_next(const Value *map, size_t *cursor, Value **key, Value **value);

/* Iteration over a set, like lisp_map_next. */
static inline bool lisp_set_next(const Value *set, size_t *cursor, Value **element) {
    Value *value;
    return lisp_map_next(set, cursor, element, &value);
}

/* ---- functions ------------------------------------------------------------- */

/* Retains lambda, environment and context (environment and context may be nullptr). */
[[nodiscard]] Value *lisp_closure(Value *lambda, Value *environment, Value *context);
[[nodiscard]] Value *lisp_primitive(const char *name, int minimum_arguments, int maximum_arguments, LispPrimitiveFunction function);

/* Wraps a zeroed opaque object of `size` bytes (≥ sizeof(LispOpaque)). */
[[nodiscard]] Value *lisp_opaque_new(const LispClass *class, size_t size);

/* ---- equality and hashing (§2.4) -------------------------------------------- */

bool lisp_equal(const Value *left, const Value *right);

/* Consistent with lisp_equal: equal values have equal hashes. */
uint64_t lisp_hash(const Value *value);
