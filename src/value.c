/* core §2 — values, reference counting, interning, equal? and hashing. */
#include "value.h"

#include "buffer.h"
#include "utf8.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- singletons ---------------------------------------------------------- */

Value lm_true_object = {.kind = LM_KIND_BOOLEAN, .flags = LM_IMMORTAL};
Value lm_false_object = {.kind = LM_KIND_BOOLEAN, .flags = LM_IMMORTAL};
Value lm_void_object = {.kind = LM_KIND_VOID, .flags = LM_IMMORTAL};
Value lm_empty_object = {.kind = LM_KIND_EMPTY, .flags = LM_IMMORTAL};

static LmMap empty_map = {.header = {.kind = LM_KIND_MAP, .flags = LM_IMMORTAL}};

/* ---- allocation and release ------------------------------------------------ */

static size_t live_objects;
static void (*position_forget_hook)(const Value *node);

size_t lm_live_objects(void) { return live_objects; }

void lm_set_position_forget_hook(void (*hook)(const Value *node)) { position_forget_hook = hook; }

Value *lm_object_new(LmKind kind, size_t size) {
    assert(size >= sizeof(Value));
    Value *v = lm_xmalloc(size);
    *v = (Value){.refs = 1, .kind = kind};
    live_objects++;
    return v;
}

/* Objects whose count reached zero, waiting to be destroyed. */
static Value **work;
static size_t work_length, work_capacity;
static bool draining;

static void work_push(Value *v) {
    if (work_length == work_capacity) {
        work_capacity = work_capacity ? work_capacity * 2 : 256;
        work = lm_xrealloc(work, work_capacity * sizeof *work);
    }
    work[work_length++] = v;
}

/* Releases the children (which may push them on the work stack) and frees v. */
static void destroy(Value *v) {
    switch ((LmKind)v->kind) {
    case LM_KIND_PAIR: {
        LmPair *pair = (LmPair *)v;
        lm_release(pair->first);
        lm_release(pair->rest);
        break;
    }
    case LM_KIND_MAP: {
        LmMap *map = (LmMap *)v;
        for (size_t i = 0; i < map->capacity; i++) {
            if (map->entries[i].key) {
                lm_release(map->entries[i].key);
                lm_release(map->entries[i].value);
            }
        }
        break;
    }
    case LM_KIND_CLOSURE: {
        LmClosure *closure = (LmClosure *)v;
        lm_release(closure->lambda);
        lm_release(closure->env);
        lm_release(closure->context);
        break;
    }
    case LM_KIND_OPAQUE: {
        LmOpaque *opaque = (LmOpaque *)v;
        if (opaque->class->drop) opaque->class->drop(v);
        break;
    }
    default:
        break;
    }
    if ((v->flags & LM_HAS_POSITION) && position_forget_hook) position_forget_hook(v);
    free(v);
    live_objects--;
}

void lm_release(Value *v) {
    if (!v || (v->flags & LM_IMMORTAL)) return;
    assert(v->refs > 0);
    if (--v->refs) return;
    work_push(v);
    if (draining) return; /* an outer lm_release is already emptying the stack */
    draining = true;
    while (work_length) destroy(work[--work_length]);
    draining = false;
}

/* ---- types ----------------------------------------------------------------- */

const char *lm_type_name(const Value *v) {
    switch ((LmKind)v->kind) {
    case LM_KIND_BOOLEAN: return "Boolean";
    case LM_KIND_VOID: return "Void";
    case LM_KIND_INTEGER: return "Integer";
    case LM_KIND_STRING: return "String";
    case LM_KIND_SYMBOL: return "Symbol";
    case LM_KIND_KEYWORD: return "Keyword";
    case LM_KIND_EMPTY:
    case LM_KIND_PAIR: return "List";
    case LM_KIND_MAP: return "Map";
    case LM_KIND_CLOSURE:
    case LM_KIND_PRIMITIVE: return "Function";
    case LM_KIND_OPAQUE: return ((const LmOpaque *)v)->class->name;
    }
    return "?";
}

/* ---- integers -------------------------------------------------------------- */

constexpr int64_t SMALL_MIN = -128;
constexpr int64_t SMALL_MAX = 1023;
static LmInteger small_integers[SMALL_MAX - SMALL_MIN + 1];
static bool small_integers_ready;

Value *lm_integer(int64_t value) {
    if (value >= SMALL_MIN && value <= SMALL_MAX) {
        if (!small_integers_ready) {
            for (int64_t i = SMALL_MIN; i <= SMALL_MAX; i++) {
                small_integers[i - SMALL_MIN] = (LmInteger){
                    .header = {.kind = LM_KIND_INTEGER, .flags = LM_IMMORTAL},
                    .value = i,
                };
            }
            small_integers_ready = true;
        }
        return &small_integers[value - SMALL_MIN].header;
    }
    LmInteger *integer = (LmInteger *)lm_object_new(LM_KIND_INTEGER, sizeof(LmInteger));
    integer->value = value;
    return &integer->header;
}

/* ---- strings ----------------------------------------------------------------- */

Value *lm_string(const char *utf8, size_t bytes) {
    LmString *s = (LmString *)lm_object_new(LM_KIND_STRING, sizeof(LmString) + bytes + 1);
    if (bytes) memcpy(s->data, utf8, bytes);
    s->data[bytes] = '\0';
    s->bytes = bytes;
    s->length = lm_utf8_count(utf8, bytes);
    if (s->length == bytes) s->header.flags |= LM_ASCII;
    return &s->header;
}

Value *lm_string_cstr(const char *utf8) { return lm_string(utf8, strlen(utf8)); }

size_t lm_string_offset(const Value *v, size_t index) {
    const LmString *s = lm_as_string(v);
    if (v->flags & LM_ASCII) return index;
    return lm_utf8_offset(s->data, s->bytes, index);
}

/* ---- hashing helpers ------------------------------------------------------------ */

static uint64_t mix(uint64_t x) { /* splitmix64 finalizer */
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

static uint64_t hash_bytes(const char *bytes, size_t length) { /* FNV-1a */
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < length; i++) {
        h ^= (unsigned char)bytes[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

/* ---- symbols and keywords ---------------------------------------------------------- */

static LmName **interned;
static size_t interned_count, interned_capacity;

static LmName *name_new(LmKind kind, const char *text, size_t bytes, uint64_t hash) {
    LmName *name = lm_xmalloc(sizeof(LmName) + bytes + 1);
    name->header = (Value){.kind = kind, .flags = LM_IMMORTAL};
    name->hash = hash;
    name->bytes = bytes;
    if (bytes) memcpy(name->text, text, bytes);
    name->text[bytes] = '\0';
    return name;
}

static void interned_grow(void) {
    size_t capacity = interned_capacity ? interned_capacity * 2 : 256;
    LmName **table = lm_xcalloc(capacity, sizeof *table);
    for (size_t i = 0; i < interned_capacity; i++) {
        LmName *name = interned[i];
        if (!name) continue;
        size_t slot = name->hash & (capacity - 1);
        while (table[slot]) slot = (slot + 1) & (capacity - 1);
        table[slot] = name;
    }
    free(interned);
    interned = table;
    interned_capacity = capacity;
}

static Value *intern(LmKind kind, const char *text, size_t bytes) {
    uint64_t hash = mix(hash_bytes(text, bytes) ^ (uint64_t)kind);
    if (2 * (interned_count + 1) > interned_capacity) interned_grow();
    size_t slot = hash & (interned_capacity - 1);
    for (LmName *name; (name = interned[slot]); slot = (slot + 1) & (interned_capacity - 1)) {
        if (name->hash == hash && name->header.kind == kind && name->bytes == bytes
            && memcmp(name->text, text, bytes) == 0) {
            return &name->header;
        }
    }
    LmName *name = name_new(kind, text, bytes, hash);
    interned[slot] = name;
    interned_count++;
    return &name->header;
}

Value *lm_symbol(const char *name, size_t bytes) { return intern(LM_KIND_SYMBOL, name, bytes); }
Value *lm_symbol_cstr(const char *name) { return lm_symbol(name, strlen(name)); }
Value *lm_keyword(const char *name, size_t bytes) { return intern(LM_KIND_KEYWORD, name, bytes); }
Value *lm_keyword_cstr(const char *name) { return lm_keyword(name, strlen(name)); }

Value *lm_generate_symbol(void) {
    static uint64_t counter;
    char text[48];
    int bytes = snprintf(text, sizeof text, "generated-%llu", (unsigned long long)++counter);
    LmName *name = (LmName *)lm_object_new(LM_KIND_SYMBOL, sizeof(LmName) + (size_t)bytes + 1);
    name->header.flags = LM_GENERATED;
    name->hash = mix((uint64_t)(uintptr_t)name);
    name->bytes = (size_t)bytes;
    memcpy(name->text, text, (size_t)bytes + 1);
    return &name->header;
}

void lm_values_shutdown(void) {
    for (size_t i = 0; i < interned_capacity; i++) free(interned[i]);
    free(interned);
    interned = nullptr;
    interned_count = interned_capacity = 0;
    free(work);
    work = nullptr;
    work_length = work_capacity = 0;
}

/* ---- lists ------------------------------------------------------------------------ */

Value *lm_prepend_take(Value *first, Value *rest) {
    assert(lm_is_list(rest));
    LmPair *pair = (LmPair *)lm_object_new(LM_KIND_PAIR, sizeof(LmPair));
    pair->first = first;
    pair->rest = rest;
    return &pair->header;
}

Value *lm_prepend(Value *first, Value *rest) {
    return lm_prepend_take(lm_retain(first), lm_retain(rest));
}

size_t lm_list_length(const Value *list) {
    size_t length = 0;
    for (; list->kind == LM_KIND_PAIR; list = lm_rest(list)) length++;
    return length;
}

void lm_list_builder_init(LmListBuilder *builder) {
    *builder = (LmListBuilder){.head = LM_EMPTY, .last = nullptr};
}

void lm_list_append_take(LmListBuilder *builder, Value *element) {
    Value *pair = lm_prepend_take(element, LM_EMPTY);
    if (builder->last) {
        builder->last->rest = pair; /* the pair is still private to the builder */
    } else {
        builder->head = pair;
    }
    builder->last = (LmPair *)pair;
}

Value *lm_list_builder_finish(LmListBuilder *builder) {
    Value *head = builder->head;
    lm_list_builder_init(builder);
    return head;
}

void lm_list_builder_discard(LmListBuilder *builder) {
    lm_release(builder->head);
    lm_list_builder_init(builder);
}

/* ---- maps -------------------------------------------------------------------------- */

static size_t capacity_for(size_t count) {
    size_t capacity = 4;
    while (capacity < 2 * count) capacity *= 2;
    return capacity;
}

static LmMap *map_alloc(size_t capacity) {
    LmMap *map = (LmMap *)lm_object_new(LM_KIND_MAP, sizeof(LmMap) + capacity * sizeof(LmMapEntry));
    map->count = 0;
    map->capacity = capacity;
    memset(map->entries, 0, capacity * sizeof(LmMapEntry));
    return map;
}

/* Slot holding `key`, or the empty slot where it would go. Requires capacity > count. */
static size_t map_slot(const LmMap *map, const Value *key, uint64_t hash) {
    size_t mask = map->capacity - 1;
    size_t slot = hash & mask;
    for (;;) {
        const LmMapEntry *entry = &map->entries[slot];
        if (!entry->key || (entry->hash == hash && lm_equal(entry->key, key))) return slot;
        slot = (slot + 1) & mask;
    }
}

/* Places an entry known to be absent (used when rebuilding a table). */
static void map_place(LmMap *map, Value *key, Value *value, uint64_t hash) {
    size_t mask = map->capacity - 1;
    size_t slot = hash & mask;
    while (map->entries[slot].key) slot = (slot + 1) & mask;
    map->entries[slot] = (LmMapEntry){.key = key, .value = value, .hash = hash};
    map->count++;
}

Value *lm_map_new(size_t expected) {
    if (expected == 0) return &empty_map.header;
    return &map_alloc(capacity_for(expected))->header;
}

bool lm_map_insert_take(Value **map_ref, Value *key, Value *value) {
    LmMap *map = (LmMap *)*map_ref;
    assert((map->header.flags & LM_IMMORTAL) || map->header.refs == 1);
    uint64_t hash = lm_hash(key);
    if (map->capacity && lm_map_get(&map->header, key)) {
        lm_release(key);
        lm_release(value);
        return false;
    }
    if (2 * (map->count + 1) > map->capacity) {
        LmMap *bigger = map_alloc(capacity_for(map->count + 1));
        for (size_t i = 0; i < map->capacity; i++) {
            LmMapEntry *entry = &map->entries[i];
            if (entry->key) map_place(bigger, entry->key, entry->value, entry->hash);
        }
        if (!(map->header.flags & LM_IMMORTAL)) { /* entries moved, so free only the table */
            free(map);
            live_objects--;
        }
        map = bigger;
        *map_ref = &map->header;
    }
    map_place(map, key, value, hash);
    return true;
}

Value *lm_map_get(const Value *v, const Value *key) {
    const LmMap *map = (const LmMap *)v;
    if (map->count == 0) return nullptr;
    const LmMapEntry *entry = &map->entries[map_slot(map, key, lm_hash(key))];
    return entry->key ? entry->value : nullptr;
}

Value *lm_map_associate(const Value *v, Value *key, Value *value) {
    const LmMap *map = (const LmMap *)v;
    LmMap *copy = map_alloc(capacity_for(map->count + 1));
    for (size_t i = 0; i < map->capacity; i++) {
        const LmMapEntry *entry = &map->entries[i];
        if (entry->key) {
            map_place(copy, lm_retain(entry->key), lm_retain(entry->value), entry->hash);
        }
    }
    uint64_t hash = lm_hash(key);
    LmMapEntry *entry = &copy->entries[map_slot(copy, key, hash)];
    if (entry->key) {
        lm_release(entry->value);
        entry->value = lm_retain(value);
    } else {
        *entry = (LmMapEntry){.key = lm_retain(key), .value = lm_retain(value), .hash = hash};
        copy->count++;
    }
    return &copy->header;
}

Value *lm_map_dissociate(Value *v, const Value *key) {
    if (!lm_map_get(v, key)) return lm_retain(v);
    const LmMap *map = (const LmMap *)v;
    if (map->count == 1) return &empty_map.header;
    LmMap *copy = map_alloc(capacity_for(map->count - 1));
    for (size_t i = 0; i < map->capacity; i++) {
        const LmMapEntry *entry = &map->entries[i];
        if (entry->key && !lm_equal(entry->key, key)) {
            map_place(copy, lm_retain(entry->key), lm_retain(entry->value), entry->hash);
        }
    }
    return &copy->header;
}

bool lm_map_next(const Value *v, size_t *cursor, Value **key, Value **value) {
    const LmMap *map = (const LmMap *)v;
    for (size_t i = *cursor; i < map->capacity; i++) {
        if (map->entries[i].key) {
            *key = map->entries[i].key;
            *value = map->entries[i].value;
            *cursor = i + 1;
            return true;
        }
    }
    *cursor = map->capacity;
    return false;
}

/* ---- functions and opaque objects ------------------------------------------------------- */

Value *lm_closure(Value *lambda, Value *env, Value *context) {
    LmClosure *closure = (LmClosure *)lm_object_new(LM_KIND_CLOSURE, sizeof(LmClosure));
    closure->lambda = lm_retain(lambda);
    closure->env = lm_retain(env);
    closure->context = lm_retain(context);
    return &closure->header;
}

Value *lm_primitive(const char *name, int min_args, int max_args, LmPrimitiveFn fn) {
    LmPrimitive *primitive = (LmPrimitive *)lm_object_new(LM_KIND_PRIMITIVE, sizeof(LmPrimitive));
    primitive->name = name;
    primitive->min_args = min_args;
    primitive->max_args = max_args;
    primitive->fn = fn;
    return &primitive->header;
}

Value *lm_opaque_new(const LmClass *class, size_t size) {
    assert(size >= sizeof(LmOpaque));
    Value *v = lm_object_new(LM_KIND_OPAQUE, size);
    memset((char *)v + sizeof(Value), 0, size - sizeof(Value));
    ((LmOpaque *)v)->class = class;
    return v;
}

/* ---- equal? (§2.4.1) ------------------------------------------------------------------ */

typedef struct Comparison {
    const Value *a, *b;
} Comparison;

/* Iterative over list tails and nesting: an explicit stack of pending pairs. */
bool lm_equal(const Value *a, const Value *b) {
    Comparison inline_stack[32];
    Comparison *stack = inline_stack;
    size_t length = 0, capacity = 32;
    bool result = true;

#define PUSH(x, y)                                                                  \
    do {                                                                            \
        if (length == capacity) {                                                   \
            capacity *= 2;                                                          \
            if (stack == inline_stack) {                                            \
                stack = lm_xmalloc(capacity * sizeof *stack);                       \
                memcpy(stack, inline_stack, sizeof inline_stack);                   \
            } else {                                                                \
                stack = lm_xrealloc(stack, capacity * sizeof *stack);               \
            }                                                                       \
        }                                                                           \
        stack[length++] = (Comparison){(x), (y)};                                   \
    } while (0)

    PUSH(a, b);
    while (result && length) {
        Comparison c = stack[--length];
        if (c.a == c.b) continue;
        if (c.a->kind != c.b->kind) {
            result = false;
            break;
        }
        switch ((LmKind)c.a->kind) {
        case LM_KIND_INTEGER:
            result = lm_integer_value(c.a) == lm_integer_value(c.b);
            break;
        case LM_KIND_STRING: {
            const LmString *x = lm_as_string(c.a), *y = lm_as_string(c.b);
            result = x->bytes == y->bytes && memcmp(x->data, y->data, x->bytes) == 0;
            break;
        }
        case LM_KIND_PAIR:
            /* Rest first, so the first elements are compared before the tail. */
            PUSH(lm_rest(c.a), lm_rest(c.b));
            PUSH(lm_first(c.a), lm_first(c.b));
            break;
        case LM_KIND_MAP: {
            if (lm_map_count(c.a) != lm_map_count(c.b)) {
                result = false;
                break;
            }
            Value *key, *value;
            for (size_t cursor = 0; lm_map_next(c.a, &cursor, &key, &value);) {
                Value *other = lm_map_get(c.b, key);
                if (!other) {
                    result = false;
                    break;
                }
                PUSH(value, other);
            }
            break;
        }
        default: /* Boolean, Void, (), Symbol, Keyword, Function: identity */
            result = false;
            break;
        }
    }
#undef PUSH
    if (stack != inline_stack) free(stack);
    return result;
}

/* ---- hash -------------------------------------------------------------------------------- */

constexpr int HASH_DEPTH = 8;

/* Below the depth limit, a value hashes to a constant of its type, which keeps
 * the hash consistent with equal? while bounding the recursion. */
static uint64_t hash_at(const Value *v, int depth) {
    switch ((LmKind)v->kind) {
    case LM_KIND_BOOLEAN: return v == LM_TRUE ? 0x1001 : 0x1002;
    case LM_KIND_VOID: return 0x1003;
    case LM_KIND_INTEGER: return mix((uint64_t)lm_integer_value(v) ^ 0x2000);
    case LM_KIND_STRING: {
        const LmString *s = lm_as_string(v);
        return mix(hash_bytes(s->data, s->bytes) ^ 0x3000);
    }
    case LM_KIND_SYMBOL:
    case LM_KIND_KEYWORD: return lm_as_name(v)->hash;
    case LM_KIND_EMPTY:
    case LM_KIND_PAIR: {
        if (depth == 0) return 0x4000;
        uint64_t h = 0x4001;
        for (; v->kind == LM_KIND_PAIR; v = lm_rest(v)) {
            h = mix(h * 31 + hash_at(lm_first(v), depth - 1));
        }
        return h;
    }
    case LM_KIND_MAP: {
        if (depth == 0) return 0x5000;
        uint64_t h = 0x5001 + lm_map_count(v);
        Value *key, *value;
        for (size_t cursor = 0; lm_map_next(v, &cursor, &key, &value);) {
            /* Sum: independent of iteration order (§2.3). */
            h += mix(hash_at(key, depth - 1) ^ (hash_at(value, depth - 1) * 0x9E3779B97F4A7C15ULL));
        }
        return h;
    }
    case LM_KIND_CLOSURE:
    case LM_KIND_PRIMITIVE:
    case LM_KIND_OPAQUE: return mix((uint64_t)(uintptr_t)v);
    }
    return 0;
}

uint64_t lm_hash(const Value *v) { return hash_at(v, HASH_DEPTH); }
