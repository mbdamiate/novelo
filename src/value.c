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
    Value *object = lm_xmalloc(size);
    *object = (Value){.refs = 1, .kind = kind};
    live_objects++;
    return object;
}

/* Objects whose count reached zero, waiting to be destroyed. */
static Value **pending_objects;
static size_t pending_count, pending_capacity;
static bool draining;

static void push_pending(Value *object) {
    if (pending_count == pending_capacity) {
        pending_capacity = pending_capacity ? pending_capacity * 2 : 256;
        pending_objects = lm_xrealloc(pending_objects, pending_capacity * sizeof *pending_objects);
    }
    pending_objects[pending_count++] = object;
}

/* Releases the children (which may push them as pending) and frees the object. */
static void destroy(Value *object) {
    switch ((LmKind)object->kind) {
    case LM_KIND_PAIR: {
        LmPair *pair = (LmPair *)object;
        lm_release(pair->first);
        lm_release(pair->rest);
        break;
    }
    case LM_KIND_MAP: {
        LmMap *map = (LmMap *)object;
        for (size_t slot = 0; slot < map->capacity; slot++) {
            if (map->entries[slot].key) {
                lm_release(map->entries[slot].key);
                lm_release(map->entries[slot].value);
            }
        }
        break;
    }
    case LM_KIND_CLOSURE: {
        LmClosure *closure = (LmClosure *)object;
        lm_release(closure->lambda);
        lm_release(closure->environment);
        lm_release(closure->context);
        break;
    }
    case LM_KIND_OPAQUE: {
        LmOpaque *opaque = (LmOpaque *)object;
        if (opaque->class->drop) opaque->class->drop(object);
        break;
    }
    default:
        break;
    }
    if ((object->flags & LM_HAS_POSITION) && position_forget_hook) position_forget_hook(object);
    free(object);
    live_objects--;
}

void lm_release(Value *value) {
    if (!value || (value->flags & LM_IMMORTAL)) return;
    assert(value->refs > 0);
    if (--value->refs) return;
    push_pending(value);
    if (draining) return; /* an outer lm_release is already emptying the stack */
    draining = true;
    while (pending_count) destroy(pending_objects[--pending_count]);
    draining = false;
}

/* ---- types ----------------------------------------------------------------- */

const char *lm_type_name(const Value *value) {
    switch ((LmKind)value->kind) {
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
    case LM_KIND_OPAQUE: return ((const LmOpaque *)value)->class->name;
    }
    return "?";
}

/* ---- integers -------------------------------------------------------------- */

constexpr int64_t SMALL_MINIMUM = -128;
constexpr int64_t SMALL_MAXIMUM = 1023;
static LmInteger small_integers[SMALL_MAXIMUM - SMALL_MINIMUM + 1];
static bool small_integers_ready;

Value *lm_integer(int64_t value) {
    if (value >= SMALL_MINIMUM && value <= SMALL_MAXIMUM) {
        if (!small_integers_ready) {
            for (int64_t number = SMALL_MINIMUM; number <= SMALL_MAXIMUM; number++) {
                small_integers[number - SMALL_MINIMUM] = (LmInteger){
                    .header = {.kind = LM_KIND_INTEGER, .flags = LM_IMMORTAL},
                    .value = number,
                };
            }
            small_integers_ready = true;
        }
        return &small_integers[value - SMALL_MINIMUM].header;
    }
    LmInteger *integer = (LmInteger *)lm_object_new(LM_KIND_INTEGER, sizeof(LmInteger));
    integer->value = value;
    return &integer->header;
}

/* ---- strings ----------------------------------------------------------------- */

Value *lm_string(const char *utf8, size_t bytes) {
    LmString *string = (LmString *)lm_object_new(LM_KIND_STRING, sizeof(LmString) + bytes + 1);
    if (bytes) memcpy(string->data, utf8, bytes);
    string->data[bytes] = '\0';
    string->bytes = bytes;
    string->length = lm_utf8_count(utf8, bytes);
    if (string->length == bytes) string->header.flags |= LM_ASCII;
    return &string->header;
}

Value *lm_string_cstr(const char *utf8) { return lm_string(utf8, strlen(utf8)); }

size_t lm_string_offset(const Value *string, size_t index) {
    if (string->flags & LM_ASCII) return index;
    const LmString *contents = lm_as_string(string);
    return lm_utf8_offset(contents->data, contents->bytes, index);
}

/* ---- hashing helpers ------------------------------------------------------------ */

static uint64_t mix(uint64_t bits) { /* splitmix64 finalizer */
    bits ^= bits >> 30;
    bits *= 0xBF58476D1CE4E5B9ULL;
    bits ^= bits >> 27;
    bits *= 0x94D049BB133111EBULL;
    bits ^= bits >> 31;
    return bits;
}

static uint64_t hash_bytes(const char *bytes, size_t length) { /* FNV-1a */
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (size_t position = 0; position < length; position++) {
        hash ^= (unsigned char)bytes[position];
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

/* ---- symbols and keywords ---------------------------------------------------------- */

static LmName **interned_names;
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

static void grow_interned_table(void) {
    size_t capacity = interned_capacity ? interned_capacity * 2 : 256;
    LmName **table = lm_xcalloc(capacity, sizeof *table);
    for (size_t old_slot = 0; old_slot < interned_capacity; old_slot++) {
        LmName *name = interned_names[old_slot];
        if (!name) continue;
        size_t slot = name->hash & (capacity - 1);
        while (table[slot]) slot = (slot + 1) & (capacity - 1);
        table[slot] = name;
    }
    free(interned_names);
    interned_names = table;
    interned_capacity = capacity;
}

static Value *intern(LmKind kind, const char *text, size_t bytes) {
    uint64_t hash = mix(hash_bytes(text, bytes) ^ (uint64_t)kind);
    if (2 * (interned_count + 1) > interned_capacity) grow_interned_table();
    size_t mask = interned_capacity - 1;
    size_t slot = hash & mask;
    for (LmName *name; (name = interned_names[slot]); slot = (slot + 1) & mask) {
        if (name->hash == hash && name->header.kind == kind && name->bytes == bytes
            && memcmp(name->text, text, bytes) == 0) {
            return &name->header;
        }
    }
    LmName *name = name_new(kind, text, bytes, hash);
    interned_names[slot] = name;
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
    for (size_t slot = 0; slot < interned_capacity; slot++) free(interned_names[slot]);
    free(interned_names);
    interned_names = nullptr;
    interned_count = interned_capacity = 0;
    free(pending_objects);
    pending_objects = nullptr;
    pending_count = pending_capacity = 0;
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
        builder->last->rest = pair; /* the last pair is still private to the builder */
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

bool lm_map_insert_take(Value **map_reference, Value *key, Value *value) {
    LmMap *map = (LmMap *)*map_reference;
    assert((map->header.flags & LM_IMMORTAL) || map->header.refs == 1);
    if (map->count && lm_map_get(&map->header, key)) {
        lm_release(key);
        lm_release(value);
        return false;
    }
    if (2 * (map->count + 1) > map->capacity) {
        LmMap *bigger = map_alloc(capacity_for(map->count + 1));
        for (size_t slot = 0; slot < map->capacity; slot++) {
            LmMapEntry *entry = &map->entries[slot];
            if (entry->key) map_place(bigger, entry->key, entry->value, entry->hash);
        }
        if (!(map->header.flags & LM_IMMORTAL)) { /* the entries moved, so free only the table */
            free(map);
            live_objects--;
        }
        map = bigger;
        *map_reference = &map->header;
    }
    map_place(map, key, value, lm_hash(key));
    return true;
}

Value *lm_map_get(const Value *map_value, const Value *key) {
    const LmMap *map = (const LmMap *)map_value;
    if (map->count == 0) return nullptr;
    const LmMapEntry *entry = &map->entries[map_slot(map, key, lm_hash(key))];
    return entry->key ? entry->value : nullptr;
}

Value *lm_map_associate(const Value *map_value, Value *key, Value *value) {
    const LmMap *map = (const LmMap *)map_value;
    LmMap *copy = map_alloc(capacity_for(map->count + 1));
    for (size_t slot = 0; slot < map->capacity; slot++) {
        const LmMapEntry *entry = &map->entries[slot];
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

Value *lm_map_dissociate(Value *map_value, const Value *key) {
    if (!lm_map_get(map_value, key)) return lm_retain(map_value);
    const LmMap *map = (const LmMap *)map_value;
    if (map->count == 1) return &empty_map.header;
    LmMap *copy = map_alloc(capacity_for(map->count - 1));
    for (size_t slot = 0; slot < map->capacity; slot++) {
        const LmMapEntry *entry = &map->entries[slot];
        if (entry->key && !lm_equal(entry->key, key)) {
            map_place(copy, lm_retain(entry->key), lm_retain(entry->value), entry->hash);
        }
    }
    return &copy->header;
}

bool lm_map_next(const Value *map_value, size_t *cursor, Value **key, Value **value) {
    const LmMap *map = (const LmMap *)map_value;
    for (size_t slot = *cursor; slot < map->capacity; slot++) {
        if (map->entries[slot].key) {
            *key = map->entries[slot].key;
            *value = map->entries[slot].value;
            *cursor = slot + 1;
            return true;
        }
    }
    *cursor = map->capacity;
    return false;
}

/* ---- functions and opaque objects ------------------------------------------------------- */

Value *lm_closure(Value *lambda, Value *environment, Value *context) {
    LmClosure *closure = (LmClosure *)lm_object_new(LM_KIND_CLOSURE, sizeof(LmClosure));
    closure->lambda = lm_retain(lambda);
    closure->environment = lm_retain(environment);
    closure->context = lm_retain(context);
    return &closure->header;
}

Value *lm_primitive(const char *name, int minimum_arguments, int maximum_arguments, LmPrimitiveFunction function) {
    LmPrimitive *primitive = (LmPrimitive *)lm_object_new(LM_KIND_PRIMITIVE, sizeof(LmPrimitive));
    primitive->name = name;
    primitive->minimum_arguments = minimum_arguments;
    primitive->maximum_arguments = maximum_arguments;
    primitive->function = function;
    return &primitive->header;
}

Value *lm_opaque_new(const LmClass *class, size_t size) {
    assert(size >= sizeof(LmOpaque));
    Value *object = lm_object_new(LM_KIND_OPAQUE, size);
    memset((char *)object + sizeof(Value), 0, size - sizeof(Value));
    ((LmOpaque *)object)->class = class;
    return object;
}

/* ---- equal? (§2.4.1) ------------------------------------------------------------------ */

typedef struct Comparison {
    const Value *left, *right;
} Comparison;

typedef struct ComparisonStack {
    Comparison *items;
    size_t count, capacity;
    Comparison inline_items[32];
} ComparisonStack;

static void push_comparison(ComparisonStack *stack, const Value *left, const Value *right) {
    if (stack->count == stack->capacity) {
        stack->capacity *= 2;
        if (stack->items == stack->inline_items) {
            stack->items = lm_xmalloc(stack->capacity * sizeof(Comparison));
            memcpy(stack->items, stack->inline_items, sizeof stack->inline_items);
        } else {
            stack->items = lm_xrealloc(stack->items, stack->capacity * sizeof(Comparison));
        }
    }
    stack->items[stack->count++] = (Comparison){left, right};
}

/* Iterative over list tails and nesting: an explicit stack of pending comparisons. */
bool lm_equal(const Value *left, const Value *right) {
    ComparisonStack stack;
    stack.items = stack.inline_items;
    stack.count = 0;
    stack.capacity = sizeof stack.inline_items / sizeof stack.inline_items[0];
    push_comparison(&stack, left, right);

    bool equal = true;
    while (equal && stack.count) {
        Comparison comparison = stack.items[--stack.count];
        const Value *first = comparison.left, *second = comparison.right;
        if (first == second) continue;
        if (first->kind != second->kind) {
            equal = false;
            break;
        }
        switch ((LmKind)first->kind) {
        case LM_KIND_INTEGER:
            equal = lm_integer_value(first) == lm_integer_value(second);
            break;
        case LM_KIND_STRING: {
            const LmString *first_string = lm_as_string(first), *second_string = lm_as_string(second);
            equal = first_string->bytes == second_string->bytes
                    && memcmp(first_string->data, second_string->data, first_string->bytes) == 0;
            break;
        }
        case LM_KIND_PAIR:
            /* Rest first, so the first elements are compared before the tail. */
            push_comparison(&stack, lm_rest(first), lm_rest(second));
            push_comparison(&stack, lm_first(first), lm_first(second));
            break;
        case LM_KIND_MAP: {
            if (lm_map_count(first) != lm_map_count(second)) {
                equal = false;
                break;
            }
            Value *key, *value;
            for (size_t cursor = 0; lm_map_next(first, &cursor, &key, &value);) {
                Value *other_value = lm_map_get(second, key);
                if (!other_value) {
                    equal = false;
                    break;
                }
                push_comparison(&stack, value, other_value);
            }
            break;
        }
        default: /* Boolean, Void, (), Symbol, Keyword, Function: identity */
            equal = false;
            break;
        }
    }
    if (stack.items != stack.inline_items) free(stack.items);
    return equal;
}

/* ---- hash -------------------------------------------------------------------------------- */

constexpr int HASH_DEPTH = 8;

/* Below the depth limit, a value hashes to a constant of its type, which keeps
 * the hash consistent with equal? while bounding the recursion. */
static uint64_t hash_at(const Value *value, int depth) {
    switch ((LmKind)value->kind) {
    case LM_KIND_BOOLEAN: return value == LM_TRUE ? 0x1001 : 0x1002;
    case LM_KIND_VOID: return 0x1003;
    case LM_KIND_INTEGER: return mix((uint64_t)lm_integer_value(value) ^ 0x2000);
    case LM_KIND_STRING: {
        const LmString *string = lm_as_string(value);
        return mix(hash_bytes(string->data, string->bytes) ^ 0x3000);
    }
    case LM_KIND_SYMBOL:
    case LM_KIND_KEYWORD: return lm_as_name(value)->hash;
    case LM_KIND_EMPTY:
    case LM_KIND_PAIR: {
        if (depth == 0) return 0x4000;
        uint64_t hash = 0x4001;
        for (; value->kind == LM_KIND_PAIR; value = lm_rest(value)) {
            hash = mix(hash * 31 + hash_at(lm_first(value), depth - 1));
        }
        return hash;
    }
    case LM_KIND_MAP: {
        if (depth == 0) return 0x5000;
        uint64_t hash = 0x5001 + lm_map_count(value);
        Value *key, *entry_value;
        for (size_t cursor = 0; lm_map_next(value, &cursor, &key, &entry_value);) {
            /* Sum: independent of iteration order (§2.3). */
            hash += mix(hash_at(key, depth - 1) ^ (hash_at(entry_value, depth - 1) * 0x9E3779B97F4A7C15ULL));
        }
        return hash;
    }
    case LM_KIND_CLOSURE:
    case LM_KIND_PRIMITIVE:
    case LM_KIND_OPAQUE: return mix((uint64_t)(uintptr_t)value);
    }
    return 0;
}

uint64_t lm_hash(const Value *value) { return hash_at(value, HASH_DEPTH); }
