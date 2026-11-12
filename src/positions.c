/* core §6 — position side table: open addressing keyed by node address, with
 * backward-shift deletion so removals leave no tombstones. */
#include "positions.h"

#include "buffer.h"

#include <assert.h>
#include <stdlib.h>

typedef struct Entry {
    const Value *node; /* nullptr marks an empty slot */
    LmPosition position;
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
    entries = lm_xcalloc(capacity, sizeof *entries);
    for (size_t slot = 0; slot < old_capacity; slot++) {
        if (!old_entries[slot].node) continue;
        size_t target = home_slot(old_entries[slot].node);
        while (entries[target].node) target = (target + 1) & (capacity - 1);
        entries[target] = old_entries[slot];
    }
    free(old_entries);
}

void lm_position_record(Value *node, LmPosition position) {
    assert(!(node->flags & LM_IMMORTAL));
    if (capacity == 0) lm_set_position_forget_hook(forget);
    if (2 * (entry_count + 1) > capacity) grow();
    size_t slot = home_slot(node);
    while (entries[slot].node && entries[slot].node != node) slot = (slot + 1) & (capacity - 1);
    if (!entries[slot].node) entry_count++;
    entries[slot] = (Entry){.node = node, .position = position};
    node->flags |= LM_HAS_POSITION;
}

/* Slot of the node, or of the empty slot ending its probe sequence. */
static size_t find_slot(const Value *node) {
    size_t slot = home_slot(node);
    while (entries[slot].node && entries[slot].node != node) slot = (slot + 1) & (capacity - 1);
    return slot;
}

bool lm_position_lookup(const Value *node, LmPosition *position) {
    if (!(node->flags & LM_HAS_POSITION) || capacity == 0) return false;
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

size_t lm_position_count(void) { return entry_count; }

void lm_positions_shutdown(void) {
    assert(entry_count == 0);
    free(entries);
    entries = nullptr;
    entry_count = capacity = 0;
}
