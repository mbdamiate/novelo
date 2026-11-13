/* core §6 — position side table: open addressing keyed by node address, with
 * backward-shift deletion so removals leave no tombstones. */
#include "positions.h"

#include "buffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct Entry {
    const Value *node; /* nullptr marks an empty slot */
    LispPosition position;
} Entry;

static Entry *entries;
static size_t entry_count, capacity;

static size_t home_slot(const Value *node) {
    uint64_t bits = (uint64_t)(uintptr_t)node;
    bits ^= bits >> 33;
    bits *= 0xFF51AFD7ED558CCDULL;
    bits ^= bits >> 33;
    return (size_t)bits & (capacity - 1);
}

static void forget(const Value *node);

static void grow(void) {
    Entry *old_entries = entries;
    size_t old_capacity = capacity;
    capacity = capacity ? capacity * 2 : 1024;
    entries = lisp_xcalloc(capacity, sizeof *entries);
    for (size_t slot = 0; slot < old_capacity; slot++) {
        if (!old_entries[slot].node) continue;
        size_t target = home_slot(old_entries[slot].node);
        while (entries[target].node) target = (target + 1) & (capacity - 1);
        entries[target] = old_entries[slot];
    }
    free(old_entries);
}

void lisp_position_record(Value *node, LispPosition position) {
    assert(!(node->flags & LISP_IMMORTAL));
    if (capacity == 0) lisp_set_position_forget_hook(forget);
    if (2 * (entry_count + 1) > capacity) grow();
    size_t slot = home_slot(node);
    while (entries[slot].node && entries[slot].node != node) slot = (slot + 1) & (capacity - 1);
    if (!entries[slot].node) entry_count++;
    entries[slot] = (Entry){.node = node, .position = position};
    node->flags |= LISP_HAS_POSITION;
}

/* Slot of the node, or of the empty slot ending its probe sequence. */
static size_t find_slot(const Value *node) {
    size_t slot = home_slot(node);
    while (entries[slot].node && entries[slot].node != node) slot = (slot + 1) & (capacity - 1);
    return slot;
}

bool lisp_position_lookup(const Value *node, LispPosition *position) {
    if (!(node->flags & LISP_HAS_POSITION) || capacity == 0) return false;
    size_t slot = find_slot(node);
    if (!entries[slot].node) return false;
    *position = entries[slot].position;
    return true;
}

static void forget(const Value *node) {
    size_t slot = find_slot(node);
    if (!entries[slot].node) return;
    entries[slot].node = nullptr;
    entry_count--;
    /* Backward shift: move later entries of the same cluster into the hole if
     * the hole lies on their probe path. */
    size_t hole = slot;
    size_t mask = capacity - 1;
    for (size_t next = (hole + 1) & mask; entries[next].node; next = (next + 1) & mask) {
        size_t home = home_slot(entries[next].node);
        bool hole_on_path = ((next - home) & mask) >= ((next - hole) & mask);
        if (hole_on_path) {
            entries[hole] = entries[next];
            entries[next].node = nullptr;
            hole = next;
        }
    }
}

size_t lisp_position_count(void) { return entry_count; }

/* ---- file names ------------------------------------------------------------------ */

/* Plain C strings, not Values, so they never count as live objects; a
 * program names only a handful of files, so a linear search is enough. */
static char **file_names;
static uint32_t file_name_count, file_name_capacity;

uint32_t lisp_source_file_register(const char *name) {
    for (uint32_t index = 0; index < file_name_count; index++) {
        if (strcmp(file_names[index], name) == 0) return index + 1;
    }
    if (file_name_count == file_name_capacity) {
        file_name_capacity = file_name_capacity ? file_name_capacity * 2 : 8;
        char **bigger = lisp_xcalloc(file_name_capacity, sizeof *file_names);
        if (file_name_count) memcpy(bigger, file_names, file_name_count * sizeof *file_names);
        free(file_names);
        file_names = bigger;
    }
    size_t length = strlen(name) + 1;
    file_names[file_name_count] = lisp_xmalloc(length);
    memcpy(file_names[file_name_count], name, length);
    return ++file_name_count;
}

const char *lisp_source_file_name(uint32_t file) {
    return file > 0 && file <= file_name_count ? file_names[file - 1] : nullptr;
}
