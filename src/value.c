/* core §2 — values, reference counting, interning, equal? and hashing. */
#include "value.h"

#include "buffer.h"
#include "utf8.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- singletons ---------------------------------------------------------- */

Value lisp_true_object = {.kind = LISP_KIND_BOOLEAN, .flags = LISP_IMMORTAL};
Value lisp_false_object = {.kind = LISP_KIND_BOOLEAN, .flags = LISP_IMMORTAL};
Value lisp_void_object = {.kind = LISP_KIND_VOID, .flags = LISP_IMMORTAL};
Value lisp_empty_object = {.kind = LISP_KIND_EMPTY, .flags = LISP_IMMORTAL};

static LispMap empty_map = {.header = {.kind = LISP_KIND_MAP, .flags = LISP_IMMORTAL}};
static LispMap empty_set = {.header = {.kind = LISP_KIND_SET, .flags = LISP_IMMORTAL}};

/* ---- allocation and release ------------------------------------------------ */

static size_t live_objects;
static void (*position_forget_hook)(const Value *node);

size_t lisp_live_objects(void) { return live_objects; }

void lisp_set_position_forget_hook(void (*hook)(const Value *node)) { position_forget_hook = hook; }

Value *lisp_object_new(LispKind kind, size_t size) {
    assert(size >= sizeof(Value));
    Value *object = lisp_xmalloc(size);
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
        pending_objects = lisp_xrealloc(pending_objects, pending_capacity * sizeof *pending_objects);
    }
    pending_objects[pending_count++] = object;
}

/* Releases the children (which may push them as pending) and frees the object. */
static void destroy(Value *object) {
    switch ((LispKind)object->kind) {
    case LISP_KIND_PAIR: {
        LispPair *pair = (LispPair *)object;
        lisp_release(pair->first);
        lisp_release(pair->rest);
        break;
    }
    case LISP_KIND_MAP:
    case LISP_KIND_SET: {
        LispMap *map = (LispMap *)object;
        lisp_release(map->root); /* trie nodes are opaque objects, released through the work stack */
        free(map->ordered);
        break;
    }
    case LISP_KIND_CLOSURE: {
        LispClosure *closure = (LispClosure *)object;
        lisp_release(closure->lambda);
        lisp_release(closure->environment);
        lisp_release(closure->context);
        break;
    }
    case LISP_KIND_OPAQUE: {
        LispOpaque *opaque = (LispOpaque *)object;
        if (opaque->class->drop) opaque->class->drop(object);
        break;
    }
    default:
        break;
    }
    if ((object->flags & LISP_HAS_POSITION) && position_forget_hook) position_forget_hook(object);
    free(object);
    live_objects--;
}

void lisp_release(Value *value) {
    if (!value || (value->flags & LISP_IMMORTAL)) return;
    assert(value->refs > 0);
    if (--value->refs) return;
    push_pending(value);
    if (draining) return; /* an outer lisp_release is already emptying the stack */
    draining = true;
    while (pending_count) destroy(pending_objects[--pending_count]);
    draining = false;
}

/* ---- types ----------------------------------------------------------------- */

const char *lisp_type_name(const Value *value) {
    switch ((LispKind)value->kind) {
    case LISP_KIND_BOOLEAN: return "Boolean";
    case LISP_KIND_VOID: return "Void";
    case LISP_KIND_INTEGER: return "Integer";
    case LISP_KIND_STRING: return "String";
    case LISP_KIND_SYMBOL: return "Symbol";
    case LISP_KIND_KEYWORD: return "Keyword";
    case LISP_KIND_EMPTY:
    case LISP_KIND_PAIR: return "List";
    case LISP_KIND_MAP: return "Map";
    case LISP_KIND_SET: return "Set";
    case LISP_KIND_CLOSURE:
    case LISP_KIND_PRIMITIVE: return "Function";
    case LISP_KIND_OPAQUE: return ((const LispOpaque *)value)->class->name;
    }
    return "?";
}

/* ---- integers -------------------------------------------------------------- */

constexpr int64_t SMALL_MINIMUM = -128;
constexpr int64_t SMALL_MAXIMUM = 1023;
static LispInteger small_integers[SMALL_MAXIMUM - SMALL_MINIMUM + 1];
static bool small_integers_ready;

Value *lisp_integer(int64_t value) {
    if (value >= SMALL_MINIMUM && value <= SMALL_MAXIMUM) {
        if (!small_integers_ready) {
            for (int64_t number = SMALL_MINIMUM; number <= SMALL_MAXIMUM; number++) {
                small_integers[number - SMALL_MINIMUM] = (LispInteger){
                    .header = {.kind = LISP_KIND_INTEGER, .flags = LISP_IMMORTAL},
                    .value = number,
                };
            }
            small_integers_ready = true;
        }
        return &small_integers[value - SMALL_MINIMUM].header;
    }
    LispInteger *integer = (LispInteger *)lisp_object_new(LISP_KIND_INTEGER, sizeof(LispInteger));
    integer->value = value;
    return &integer->header;
}

/* ---- strings ----------------------------------------------------------------- */

Value *lisp_string(const char *utf8, size_t bytes) {
    LispString *string = (LispString *)lisp_object_new(LISP_KIND_STRING, sizeof(LispString) + bytes + 1);
    if (bytes) memcpy(string->data, utf8, bytes);
    string->data[bytes] = '\0';
    string->bytes = bytes;
    string->length = lisp_utf8_count(utf8, bytes);
    if (string->length == bytes) string->header.flags |= LISP_ASCII;
    return &string->header;
}

Value *lisp_string_cstr(const char *utf8) { return lisp_string(utf8, strlen(utf8)); }

size_t lisp_string_offset(const Value *string, size_t index) {
    if (string->flags & LISP_ASCII) return index;
    const LispString *contents = lisp_as_string(string);
    return lisp_utf8_offset(contents->data, contents->bytes, index);
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

static LispName **interned_names;
static size_t interned_count, interned_capacity;

static LispName *name_new(LispKind kind, const char *text, size_t bytes, uint64_t hash) {
    LispName *name = lisp_xmalloc(sizeof(LispName) + bytes + 1);
    name->header = (Value){.kind = kind, .flags = LISP_IMMORTAL};
    name->hash = hash;
    name->bytes = bytes;
    if (bytes) memcpy(name->text, text, bytes);
    name->text[bytes] = '\0';
    return name;
}

static void grow_interned_table(void) {
    size_t capacity = interned_capacity ? interned_capacity * 2 : 256;
    LispName **table = lisp_xcalloc(capacity, sizeof *table);
    for (size_t old_slot = 0; old_slot < interned_capacity; old_slot++) {
        LispName *name = interned_names[old_slot];
        if (!name) continue;
        size_t slot = name->hash & (capacity - 1);
        while (table[slot]) slot = (slot + 1) & (capacity - 1);
        table[slot] = name;
    }
    free(interned_names);
    interned_names = table;
    interned_capacity = capacity;
}

static Value *intern(LispKind kind, const char *text, size_t bytes) {
    uint64_t hash = mix(hash_bytes(text, bytes) ^ (uint64_t)kind);
    if (2 * (interned_count + 1) > interned_capacity) grow_interned_table();
    size_t mask = interned_capacity - 1;
    size_t slot = hash & mask;
    for (LispName *name; (name = interned_names[slot]); slot = (slot + 1) & mask) {
        if (name->hash == hash && name->header.kind == kind && name->bytes == bytes
            && memcmp(name->text, text, bytes) == 0) {
            return &name->header;
        }
    }
    LispName *name = name_new(kind, text, bytes, hash);
    interned_names[slot] = name;
    interned_count++;
    return &name->header;
}

Value *lisp_symbol(const char *name, size_t bytes) { return intern(LISP_KIND_SYMBOL, name, bytes); }
Value *lisp_symbol_cstr(const char *name) { return lisp_symbol(name, strlen(name)); }
Value *lisp_keyword(const char *name, size_t bytes) { return intern(LISP_KIND_KEYWORD, name, bytes); }
Value *lisp_keyword_cstr(const char *name) { return lisp_keyword(name, strlen(name)); }

Value *lisp_generate_symbol(void) {
    static uint64_t counter;
    char text[48];
    int bytes = snprintf(text, sizeof text, "generated-%llu", (unsigned long long)++counter);
    LispName *name = (LispName *)lisp_object_new(LISP_KIND_SYMBOL, sizeof(LispName) + (size_t)bytes + 1);
    name->header.flags = LISP_GENERATED;
    name->hash = mix((uint64_t)(uintptr_t)name);
    name->bytes = (size_t)bytes;
    memcpy(name->text, text, (size_t)bytes + 1);
    return &name->header;
}

void lisp_values_shutdown(void) {
    for (size_t slot = 0; slot < interned_capacity; slot++) free(interned_names[slot]);
    free(interned_names);
    interned_names = nullptr;
    interned_count = interned_capacity = 0;
    free(pending_objects);
    pending_objects = nullptr;
    pending_count = pending_capacity = 0;
}

/* ---- lists ------------------------------------------------------------------------ */

Value *lisp_prepend_take(Value *first, Value *rest) {
    assert(lisp_is_list(rest));
    LispPair *pair = (LispPair *)lisp_object_new(LISP_KIND_PAIR, sizeof(LispPair));
    pair->first = first;
    pair->rest = rest;
    return &pair->header;
}

Value *lisp_prepend(Value *first, Value *rest) {
    return lisp_prepend_take(lisp_retain(first), lisp_retain(rest));
}

size_t lisp_list_length(const Value *list) {
    size_t length = 0;
    for (; list->kind == LISP_KIND_PAIR; list = lisp_rest(list)) length++;
    return length;
}

void lisp_list_builder_init(LispListBuilder *builder) {
    *builder = (LispListBuilder){.head = LISP_EMPTY, .last = nullptr};
}

void lisp_list_append_take(LispListBuilder *builder, Value *element) {
    Value *pair = lisp_prepend_take(element, LISP_EMPTY);
    if (builder->last) {
        builder->last->rest = pair; /* the last pair is still private to the builder */
    } else {
        builder->head = pair;
    }
    builder->last = (LispPair *)pair;
}

Value *lisp_list_builder_finish(LispListBuilder *builder) {
    Value *head = builder->head;
    lisp_list_builder_init(builder);
    return head;
}

void lisp_list_builder_discard(LispListBuilder *builder) {
    lisp_release(builder->head);
    lisp_list_builder_init(builder);
}

/* ---- maps: a hash array mapped trie (§8.6) ----------------------------------------- */

/* Each node covers 5 bits of the hash, from the lowest up, and stores only
 * the slots it uses: entry_map and child_map say which of the 32 slots hold
 * an entry or a child, and the arrays keep them in slot order. Below the last
 * 64-bit level, keys with fully equal hashes share a collision node, a plain
 * list of entries. A node other than the root always holds two keys or more:
 * dissociate pulls a lone entry back into its parent. */
typedef struct TrieNode {
    LispOpaque base;
    uint32_t entry_map;
    uint32_t child_map;
    uint32_t collision_count; /* nonzero: a collision node, with that many entries */
    LispMapEntry *entries;
    Value **children;
} TrieNode;

constexpr unsigned TRIE_BITS = 5;
constexpr unsigned TRIE_HASH_BITS = 64;

static void drop_trie_node(Value *self) {
    TrieNode *node = (TrieNode *)self;
    size_t entry_count = node->collision_count ? node->collision_count : (size_t)__builtin_popcount(node->entry_map);
    for (size_t index = 0; index < entry_count; index++) {
        lisp_release(node->entries[index].key);
        lisp_release(node->entries[index].value);
    }
    size_t child_count = (size_t)__builtin_popcount(node->child_map);
    for (size_t index = 0; index < child_count; index++) lisp_release(node->children[index]);
    free(node->entries);
    free(node->children);
}

static const LispClass trie_node_class = {"MapNode", drop_trie_node};

static const TrieNode *as_trie(const Value *node) { return (const TrieNode *)node; }

static TrieNode *trie_node_new(size_t entry_count, size_t child_count) {
    TrieNode *node = (TrieNode *)lisp_opaque_new(&trie_node_class, sizeof(TrieNode));
    node->entries = entry_count ? lisp_xmalloc(entry_count * sizeof(LispMapEntry)) : nullptr;
    node->children = child_count ? lisp_xmalloc(child_count * sizeof(Value *)) : nullptr;
    return node;
}

/* Copies an entry, retaining its key and value. */
static LispMapEntry entry_copy(const LispMapEntry *entry) {
    return (LispMapEntry){
        .key = lisp_retain(entry->key),
        .value = lisp_retain(entry->value),
        .hash = entry->hash,
        .sequence = entry->sequence,
    };
}

static uint32_t slot_bit(uint64_t hash, unsigned shift) { return 1u << ((hash >> shift) & 31); }

static size_t slot_index(uint32_t map, uint32_t bit) { return (size_t)__builtin_popcount(map & (bit - 1)); }

static bool same_key(const LispMapEntry *entry, const Value *key, uint64_t hash) {
    return entry->hash == hash && lisp_equal(entry->key, key);
}

static size_t node_entry_count(const TrieNode *node) {
    return node->collision_count ? node->collision_count : (size_t)__builtin_popcount(node->entry_map);
}

static const LispMapEntry *trie_find(const Value *root, const Value *key, uint64_t hash) {
    unsigned shift = 0;
    for (const TrieNode *node = as_trie(root); node; shift += TRIE_BITS) {
        if (node->collision_count) {
            for (size_t index = 0; index < node->collision_count; index++) {
                if (same_key(&node->entries[index], key, hash)) return &node->entries[index];
            }
            return nullptr;
        }
        uint32_t bit = slot_bit(hash, shift);
        if (node->entry_map & bit) {
            const LispMapEntry *entry = &node->entries[slot_index(node->entry_map, bit)];
            return same_key(entry, key, hash) ? entry : nullptr;
        }
        if (!(node->child_map & bit)) return nullptr;
        node = as_trie(node->children[slot_index(node->child_map, bit)]);
    }
    return nullptr;
}

/* A copy of a node in which the slot `bit` holds `entry` (copied), or
 * `child` (taken), or nothing when both are nullptr. */
static Value *node_with_slot(const TrieNode *node, uint32_t bit, const LispMapEntry *entry, Value *child) {
    uint32_t entry_map = (node->entry_map & ~bit) | (entry ? bit : 0);
    uint32_t child_map = (node->child_map & ~bit) | (child ? bit : 0);
    TrieNode *copy = trie_node_new((size_t)__builtin_popcount(entry_map), (size_t)__builtin_popcount(child_map));
    copy->entry_map = entry_map;
    copy->child_map = child_map;
    size_t entry_index = 0, child_index = 0;
    for (unsigned slot = 0; slot < 32; slot++) {
        uint32_t current = 1u << slot;
        if (current == bit) {
            if (entry) copy->entries[entry_index++] = entry_copy(entry);
            if (child) copy->children[child_index++] = child;
        } else if (entry_map & current) {
            copy->entries[entry_index++] = entry_copy(&node->entries[slot_index(node->entry_map, current)]);
        } else if (child_map & current) {
            copy->children[child_index++] = lisp_retain(node->children[slot_index(node->child_map, current)]);
        }
    }
    return &copy->base.header;
}

/* A node holding two entries whose hashes agree below `shift`. */
static Value *pair_node(const LispMapEntry *first, const LispMapEntry *second, unsigned shift) {
    if (shift >= TRIE_HASH_BITS) {
        TrieNode *collision = trie_node_new(2, 0);
        collision->collision_count = 2;
        collision->entries[0] = entry_copy(first);
        collision->entries[1] = entry_copy(second);
        return &collision->base.header;
    }
    uint32_t first_bit = slot_bit(first->hash, shift), second_bit = slot_bit(second->hash, shift);
    if (first_bit == second_bit) {
        TrieNode *node = trie_node_new(0, 1);
        node->child_map = first_bit;
        node->children[0] = pair_node(first, second, shift + TRIE_BITS);
        return &node->base.header;
    }
    TrieNode *node = trie_node_new(2, 0);
    node->entry_map = first_bit | second_bit;
    bool first_is_lower = first_bit < second_bit;
    node->entries[0] = entry_copy(first_is_lower ? first : second);
    node->entries[1] = entry_copy(first_is_lower ? second : first);
    return &node->base.header;
}

/* The trie with the entry (borrowed) in it. A key already present keeps its
 * sequence and gets the new value; *added tells which case happened. */
static Value *trie_associate(const Value *root, unsigned shift, const LispMapEntry *entry, bool *added) {
    if (!root) {
        TrieNode *node = trie_node_new(1, 0);
        node->entry_map = slot_bit(entry->hash, shift);
        node->entries[0] = entry_copy(entry);
        *added = true;
        return &node->base.header;
    }
    const TrieNode *node = as_trie(root);
    if (node->collision_count) {
        size_t count = node->collision_count;
        size_t found = count;
        for (size_t index = 0; index < count; index++) {
            if (same_key(&node->entries[index], entry->key, entry->hash)) found = index;
        }
        *added = found == count;
        TrieNode *copy = trie_node_new(count + (*added ? 1 : 0), 0);
        copy->collision_count = (uint32_t)(count + (*added ? 1 : 0));
        for (size_t index = 0; index < count; index++) copy->entries[index] = entry_copy(&node->entries[index]);
        if (*added) {
            copy->entries[count] = entry_copy(entry);
        } else {
            lisp_release(copy->entries[found].value);
            copy->entries[found].value = lisp_retain(entry->value);
        }
        return &copy->base.header;
    }
    uint32_t bit = slot_bit(entry->hash, shift);
    if (node->entry_map & bit) {
        const LispMapEntry *existing = &node->entries[slot_index(node->entry_map, bit)];
        if (same_key(existing, entry->key, entry->hash)) {
            LispMapEntry replaced = *existing;
            replaced.value = entry->value;
            *added = false;
            return node_with_slot(node, bit, &replaced, nullptr);
        }
        *added = true;
        return node_with_slot(node, bit, nullptr, pair_node(existing, entry, shift + TRIE_BITS));
    }
    if (node->child_map & bit) {
        Value *child = node->children[slot_index(node->child_map, bit)];
        return node_with_slot(node, bit, nullptr, trie_associate(child, shift + TRIE_BITS, entry, added));
    }
    *added = true;
    return node_with_slot(node, bit, entry, nullptr);
}

/* The trie without the key: nullptr when nothing is left. The key must be
 * present. */
static Value *trie_dissociate(const Value *root, unsigned shift, const Value *key, uint64_t hash) {
    const TrieNode *node = as_trie(root);
    if (node->collision_count) {
        size_t count = node->collision_count;
        TrieNode *copy = trie_node_new(count - 1, 0);
        copy->collision_count = (uint32_t)(count - 1);
        size_t kept = 0;
        for (size_t index = 0; index < count; index++) {
            if (!same_key(&node->entries[index], key, hash)) copy->entries[kept++] = entry_copy(&node->entries[index]);
        }
        return &copy->base.header;
    }
    uint32_t bit = slot_bit(hash, shift);
    if (node->entry_map & bit) {
        if (node_entry_count(node) == 1 && node->child_map == 0) return nullptr;
        return node_with_slot(node, bit, nullptr, nullptr);
    }
    Value *child = trie_dissociate(node->children[slot_index(node->child_map, bit)], shift + TRIE_BITS, key, hash);
    const TrieNode *remaining = child ? as_trie(child) : nullptr;
    if (remaining && remaining->child_map == 0 && node_entry_count(remaining) == 1) {
        /* A lone entry moves up into this node. */
        Value *result = node_with_slot(node, bit, &remaining->entries[0], nullptr);
        lisp_release(child);
        return result;
    }
    if (!child && node->entry_map == 0 && node->child_map == bit) return nullptr;
    return node_with_slot(node, bit, nullptr, child);
}

/* A map or set (by kind) over a trie (taken). */
static Value *map_from_trie(LispKind kind, Value *root, size_t count, uint64_t next_sequence) {
    if (count == 0) {
        lisp_release(root);
        return kind == LISP_KIND_SET ? &empty_set.header : &empty_map.header;
    }
    LispMap *map = (LispMap *)lisp_object_new(kind, sizeof(LispMap));
    map->count = count;
    map->root = root;
    map->next_sequence = next_sequence;
    map->ordered = nullptr;
    return &map->header;
}

Value *lisp_map_new(size_t expected) {
    (void)expected;
    return &empty_map.header;
}

bool lisp_map_insert_take(Value **map_reference, Value *key, Value *value) {
    LispMap *map = (LispMap *)*map_reference;
    assert((map->header.flags & LISP_IMMORTAL) || map->header.refs == 1);
    if (lisp_map_get(&map->header, key)) {
        lisp_release(key);
        lisp_release(value);
        return false;
    }
    *map_reference = lisp_map_associate(&map->header, key, value);
    lisp_release(&map->header);
    lisp_release(key);
    lisp_release(value);
    return true;
}

Value *lisp_map_get(const Value *map_value, const Value *key) {
    const LispMap *map = (const LispMap *)map_value;
    if (map->count == 0) return nullptr;
    const LispMapEntry *entry = trie_find(map->root, key, lisp_hash(key));
    return entry ? entry->value : nullptr;
}

Value *lisp_map_associate(const Value *map_value, Value *key, Value *value) {
    const LispMap *map = (const LispMap *)map_value;
    LispMapEntry entry = {.key = key, .value = value, .hash = lisp_hash(key), .sequence = map->next_sequence};
    bool added = false;
    Value *root = trie_associate(map->root, 0, &entry, &added);
    return map_from_trie((LispKind)map->header.kind, root, map->count + (added ? 1 : 0),
                         map->next_sequence + (added ? 1 : 0));
}

Value *lisp_map_dissociate(Value *map_value, const Value *key) {
    const LispMap *map = (const LispMap *)map_value;
    uint64_t hash = lisp_hash(key);
    if (map->count == 0 || !trie_find(map->root, key, hash)) return lisp_retain(map_value);
    Value *root = trie_dissociate(map->root, 0, key, hash);
    return map_from_trie((LispKind)map->header.kind, root, map->count - 1, map->next_sequence);
}

Value *lisp_set_new(void) { return &empty_set.header; }

bool lisp_set_insert_take(Value **set, Value *element) {
    return lisp_map_insert_take(set, element, LISP_TRUE);
}

Value *lisp_set_adjoin(const Value *set, Value *element) {
    if (lisp_set_contains(set, element)) return lisp_retain((Value *)set);
    return lisp_map_associate(set, element, LISP_TRUE);
}

Value *lisp_set_disjoin(Value *set, const Value *element) { return lisp_map_dissociate(set, element); }

/* ---- iteration in insertion order ---- */

static void collect_entries(const Value *root, const LispMapEntry **ordered, size_t *count) {
    const TrieNode *node = as_trie(root);
    size_t entry_count = node_entry_count(node);
    for (size_t index = 0; index < entry_count; index++) ordered[(*count)++] = &node->entries[index];
    size_t child_count = (size_t)__builtin_popcount(node->child_map);
    for (size_t index = 0; index < child_count; index++) collect_entries(node->children[index], ordered, count);
}

static int compare_sequences(const void *left, const void *right) {
    uint64_t first = (*(const LispMapEntry *const *)left)->sequence;
    uint64_t second = (*(const LispMapEntry *const *)right)->sequence;
    return first < second ? -1 : first > second;
}

bool lisp_map_next(const Value *map_value, size_t *cursor, Value **key, Value **value) {
    LispMap *map = (LispMap *)map_value; /* the order is a cache: the map itself does not change */
    if (*cursor >= map->count) return false;
    if (!map->ordered) {
        map->ordered = lisp_xmalloc(map->count * sizeof *map->ordered);
        size_t collected = 0;
        collect_entries(map->root, map->ordered, &collected);
        assert(collected == map->count);
        qsort(map->ordered, map->count, sizeof *map->ordered, compare_sequences);
    }
    *key = map->ordered[*cursor]->key;
    *value = map->ordered[*cursor]->value;
    (*cursor)++;
    return true;
}

/* ---- functions and opaque objects ------------------------------------------------------- */

Value *lisp_closure(Value *lambda, Value *environment, Value *context) {
    LispClosure *closure = (LispClosure *)lisp_object_new(LISP_KIND_CLOSURE, sizeof(LispClosure));
    closure->lambda = lisp_retain(lambda);
    closure->environment = lisp_retain(environment);
    closure->context = lisp_retain(context);
    return &closure->header;
}

Value *lisp_primitive(const char *name, int minimum_arguments, int maximum_arguments, LispPrimitiveFunction function) {
    LispPrimitive *primitive = (LispPrimitive *)lisp_object_new(LISP_KIND_PRIMITIVE, sizeof(LispPrimitive));
    primitive->name = name;
    primitive->minimum_arguments = minimum_arguments;
    primitive->maximum_arguments = maximum_arguments;
    primitive->function = function;
    return &primitive->header;
}

Value *lisp_opaque_new(const LispClass *class, size_t size) {
    assert(size >= sizeof(LispOpaque));
    Value *object = lisp_object_new(LISP_KIND_OPAQUE, size);
    memset((char *)object + sizeof(Value), 0, size - sizeof(Value));
    ((LispOpaque *)object)->class = class;
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
            stack->items = lisp_xmalloc(stack->capacity * sizeof(Comparison));
            memcpy(stack->items, stack->inline_items, sizeof stack->inline_items);
        } else {
            stack->items = lisp_xrealloc(stack->items, stack->capacity * sizeof(Comparison));
        }
    }
    stack->items[stack->count++] = (Comparison){left, right};
}

/* Iterative over list tails and nesting: an explicit stack of pending comparisons. */
bool lisp_equal(const Value *left, const Value *right) {
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
        switch ((LispKind)first->kind) {
        case LISP_KIND_INTEGER:
            equal = lisp_integer_value(first) == lisp_integer_value(second);
            break;
        case LISP_KIND_STRING: {
            const LispString *first_string = lisp_as_string(first), *second_string = lisp_as_string(second);
            equal = first_string->bytes == second_string->bytes
                    && memcmp(first_string->data, second_string->data, first_string->bytes) == 0;
            break;
        }
        case LISP_KIND_PAIR:
            /* Rest first, so the first elements are compared before the tail. */
            push_comparison(&stack, lisp_rest(first), lisp_rest(second));
            push_comparison(&stack, lisp_first(first), lisp_first(second));
            break;
        case LISP_KIND_MAP:
        case LISP_KIND_SET: { /* a set's values are all #true, so the same loop compares elements */
            if (lisp_map_count(first) != lisp_map_count(second)) {
                equal = false;
                break;
            }
            Value *key, *value;
            for (size_t cursor = 0; lisp_map_next(first, &cursor, &key, &value);) {
                Value *other_value = lisp_map_get(second, key);
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
    switch ((LispKind)value->kind) {
    case LISP_KIND_BOOLEAN: return value == LISP_TRUE ? 0x1001 : 0x1002;
    case LISP_KIND_VOID: return 0x1003;
    case LISP_KIND_INTEGER: return mix((uint64_t)lisp_integer_value(value) ^ 0x2000);
    case LISP_KIND_STRING: {
        const LispString *string = lisp_as_string(value);
        return mix(hash_bytes(string->data, string->bytes) ^ 0x3000);
    }
    case LISP_KIND_SYMBOL:
    case LISP_KIND_KEYWORD: return lisp_as_name(value)->hash;
    case LISP_KIND_EMPTY:
    case LISP_KIND_PAIR: {
        if (depth == 0) return 0x4000;
        uint64_t hash = 0x4001;
        for (; value->kind == LISP_KIND_PAIR; value = lisp_rest(value)) {
            hash = mix(hash * 31 + hash_at(lisp_first(value), depth - 1));
        }
        return hash;
    }
    case LISP_KIND_MAP:
    case LISP_KIND_SET: {
        bool is_set = value->kind == LISP_KIND_SET;
        if (depth == 0) return is_set ? 0x6000 : 0x5000;
        uint64_t hash = (is_set ? 0x6001 : 0x5001) + lisp_map_count(value);
        Value *key, *entry_value;
        for (size_t cursor = 0; lisp_map_next(value, &cursor, &key, &entry_value);) {
            /* Sum: independent of iteration order (§2.3). */
            hash += mix(hash_at(key, depth - 1) ^ (hash_at(entry_value, depth - 1) * 0x9E3779B97F4A7C15ULL));
        }
        return hash;
    }
    case LISP_KIND_CLOSURE:
    case LISP_KIND_PRIMITIVE:
    case LISP_KIND_OPAQUE: return mix((uint64_t)(uintptr_t)value);
    }
    return 0;
}

uint64_t lisp_hash(const Value *value) { return hash_at(value, HASH_DEPTH); }
