/* core §2 — values, reference counting, interning, equal? and hashing.
 *
 * OWNERSHIP RULE
 *   - A function that returns a Value * returns a NEW reference, which the
 *     caller must eventually pass to lm_release (or hand to a _take function).
 *   - Arguments are BORROWED: the callee retains whatever it keeps.
 *   - Exception: a function whose name ends in _take takes over the caller's
 *     reference to the arguments marked in its comment.
 *   - Accessors marked "borrowed" (lm_first, lm_rest, lm_map_get, …) return a
 *     reference owned by the container; retain it to keep it.
 *
 * The heap is acyclic (values are immutable and recursion goes through global
 * names), so counting references frees everything. Releasing never recurses in
 * C: objects whose count drops to zero go on an explicit work stack.
 *
 * Immortal objects (#true, #false, #void, (), the empty map, small integers and
 * interned symbols and keywords) carry LM_IMMORTAL; retain and release skip
 * them, and lm_live_objects() does not count them.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum LmKind : uint8_t {
    LM_KIND_BOOLEAN,
    LM_KIND_VOID,
    LM_KIND_INTEGER,
    LM_KIND_STRING,
    LM_KIND_SYMBOL,
    LM_KIND_KEYWORD,
    LM_KIND_EMPTY,     /* ()            — type List */
    LM_KIND_PAIR,      /* non-empty list — type List */
    LM_KIND_MAP,
    LM_KIND_CLOSURE,   /* type Function */
    LM_KIND_PRIMITIVE, /* type Function */
    LM_KIND_OPAQUE,    /* internal objects (nodes, environments, contexts) */
} LmKind;

enum {
    LM_IMMORTAL = 1 << 0,
    LM_ASCII = 1 << 1,        /* String: every code point is ASCII */
    LM_HAS_POSITION = 1 << 2, /* has an entry in the position table (§6) */
    LM_GENERATED = 1 << 3,    /* Symbol: made by generate-symbol, not interned */
};

typedef struct Value {
    uint32_t refs;
    uint8_t kind;
    uint8_t flags;
} Value;

typedef struct LmInteger {
    Value header;
    int64_t value;
} LmInteger;

typedef struct LmString {
    Value header;
    size_t bytes;   /* UTF-8 length */
    size_t length;  /* code points */
    char data[];    /* NUL-terminated; may contain U+0000 */
} LmString;

typedef struct LmName { /* Symbol or Keyword (the name has no ':') */
    Value header;
    uint64_t hash;
    size_t bytes;
    char text[];    /* NUL-terminated */
} LmName;

typedef struct LmPair {
    Value header;
    Value *first;
    Value *rest;    /* always a List */
} LmPair;

typedef struct LmMapEntry {
    Value *key;     /* nullptr marks an empty slot */
    Value *value;
    uint64_t hash;
} LmMapEntry;

typedef struct LmMap {
    Value header;
    size_t count;
    size_t capacity; /* zero or a power of two, at least twice count */
    LmMapEntry entries[];
} LmMap;

typedef struct LmClosure {
    Value header;
    Value *lambda;  /* expanded lambda node */
    Value *env;     /* captured environment, or nullptr */
    Value *context; /* include context (§8.8), or nullptr */
} LmClosure;

typedef struct LmState LmState;
typedef Value *(*LmPrimitiveFn)(LmState *state, size_t argc, Value *const *argv);

typedef struct LmPrimitive {
    Value header;
    const char *name;
    int min_args;
    int max_args;   /* -1: no maximum */
    LmPrimitiveFn fn;
} LmPrimitive;

/* Internal objects counted like values. drop() releases the children (with
 * lm_release, which defers to the work stack) and frees any memory the object
 * owns besides itself; lm_release frees the object afterwards. */
typedef struct LmClass {
    const char *name;
    void (*drop)(Value *self);
} LmClass;

typedef struct LmOpaque {
    Value header;
    const LmClass *class;
} LmOpaque;

/* ---- singletons ---------------------------------------------------------- */

extern Value lm_true_object, lm_false_object, lm_void_object, lm_empty_object;
#define LM_TRUE (&lm_true_object)
#define LM_FALSE (&lm_false_object)
#define LM_VOID (&lm_void_object)
#define LM_EMPTY (&lm_empty_object)

static inline Value *lm_bool(bool b) { return b ? LM_TRUE : LM_FALSE; }

/* ---- reference counting --------------------------------------------------- */

static inline Value *lm_retain(Value *v) {
    if (v && !(v->flags & LM_IMMORTAL)) v->refs++;
    return v;
}

void lm_release(Value *v);

/* Allocates a counted object of the given size (refs = 1, flags = 0). */
[[nodiscard]] Value *lm_object_new(LmKind kind, size_t size);

/* Number of live objects that are not immortal (leak detection). */
size_t lm_live_objects(void);

/* Called when an object with LM_HAS_POSITION is freed (§6.4). */
void lm_set_position_forget_hook(void (*hook)(const Value *node));

/* Frees the interning table and the integer cache. No value may be used after. */
void lm_values_shutdown(void);

/* ---- types --------------------------------------------------------------- */

static inline bool lm_is_list(const Value *v) {
    return v->kind == LM_KIND_EMPTY || v->kind == LM_KIND_PAIR;
}
static inline bool lm_is_function(const Value *v) {
    return v->kind == LM_KIND_CLOSURE || v->kind == LM_KIND_PRIMITIVE;
}

/* Type name of §2: "Boolean", "Void", "Integer", …, "Function". */
const char *lm_type_name(const Value *v);

/* ---- integers ------------------------------------------------------------- */

[[nodiscard]] Value *lm_integer(int64_t value);
static inline int64_t lm_integer_value(const Value *v) { return ((const LmInteger *)v)->value; }

/* ---- strings (valid UTF-8 only) ------------------------------------------- */

[[nodiscard]] Value *lm_string(const char *utf8, size_t bytes);
[[nodiscard]] Value *lm_string_cstr(const char *utf8);
static inline const LmString *lm_as_string(const Value *v) { return (const LmString *)v; }

/* Byte offset of code point `index` (0..length); O(1) for ASCII strings. */
size_t lm_string_offset(const Value *s, size_t index);

/* ---- symbols and keywords -------------------------------------------------- */

/* Interned: the same name always gives the same immortal object. */
[[nodiscard]] Value *lm_symbol(const char *name, size_t bytes);
[[nodiscard]] Value *lm_symbol_cstr(const char *name);
[[nodiscard]] Value *lm_keyword(const char *name, size_t bytes);
[[nodiscard]] Value *lm_keyword_cstr(const char *name);

/* A fresh, counted, non-interned symbol (generate-symbol, §9). */
[[nodiscard]] Value *lm_generate_symbol(void);

static inline const LmName *lm_as_name(const Value *v) { return (const LmName *)v; }

/* ---- lists ----------------------------------------------------------------- */

[[nodiscard]] Value *lm_prepend(Value *first, Value *rest);
/* Takes both references. */
[[nodiscard]] Value *lm_prepend_take(Value *first, Value *rest);

static inline Value *lm_first(const Value *pair) { return ((const LmPair *)pair)->first; } /* borrowed */
static inline Value *lm_rest(const Value *pair) { return ((const LmPair *)pair)->rest; }   /* borrowed */

size_t lm_list_length(const Value *list);

/* Builds a list front to back by appending to the last pair while it is still
 * private to the builder. */
typedef struct LmListBuilder {
    Value *head;
    LmPair *last;
} LmListBuilder;

void lm_list_builder_init(LmListBuilder *builder);
void lm_list_append_take(LmListBuilder *builder, Value *element);
[[nodiscard]] Value *lm_list_builder_finish(LmListBuilder *builder);
void lm_list_builder_discard(LmListBuilder *builder);

/* ---- maps ------------------------------------------------------------------ */

/* A new map with room for `expected` keys; the empty map is immortal. */
[[nodiscard]] Value *lm_map_new(size_t expected);

/* Builder insertion into a map that only the caller holds (refs == 1); may
 * move the map. Takes key and value. Returns false, and releases both, if the
 * key is already present. */
bool lm_map_insert_take(Value **map, Value *key, Value *value);

static inline size_t lm_map_count(const Value *map) { return ((const LmMap *)map)->count; }

Value *lm_map_get(const Value *map, const Value *key); /* borrowed, or nullptr */

/* Persistent operations: return a new map and leave the argument unchanged.
 * Dissociating a missing key returns the same map (retained). */
[[nodiscard]] Value *lm_map_associate(const Value *map, Value *key, Value *value);
[[nodiscard]] Value *lm_map_dissociate(Value *map, const Value *key);

/* Iteration: for (size_t i = 0; lm_map_next(map, &i, &k, &v);) … (borrowed). */
bool lm_map_next(const Value *map, size_t *cursor, Value **key, Value **value);

/* ---- functions ------------------------------------------------------------- */

/* Retains lambda, env and context (env and context may be nullptr). */
[[nodiscard]] Value *lm_closure(Value *lambda, Value *env, Value *context);
[[nodiscard]] Value *lm_primitive(const char *name, int min_args, int max_args, LmPrimitiveFn fn);

/* Wraps a zeroed opaque object of `size` bytes (≥ sizeof(LmOpaque)). */
[[nodiscard]] Value *lm_opaque_new(const LmClass *class, size_t size);

/* ---- equality and hashing (§2.4) -------------------------------------------- */

bool lm_equal(const Value *a, const Value *b);

/* Consistent with lm_equal: equal values have equal hashes. */
uint64_t lm_hash(const Value *v);
