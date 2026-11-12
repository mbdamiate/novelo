/* core §6 — source positions, kept in a side table keyed by node identity.
 *
 * Datums stay ordinary values. A node with an entry carries LM_HAS_POSITION,
 * and freeing it removes the entry (through the hook in value.c), so a later
 * object at the same address never inherits an old position (§6.4).
 */
#pragma once

#include "value.h"

#include <stdint.h>

/* Line and column start at 1; the column counts code points (§6.1). */
typedef struct LmPosition {
    uint32_t line;
    uint32_t column;
} LmPosition;

/* Records where a non-empty List or Map was read. The node must be counted
 * (not immortal). */
void lm_position_record(Value *node, LmPosition position);

/* True, with the position, if the node was read and is still alive. */
bool lm_position_lookup(const Value *node, LmPosition *position);

/* Number of entries (for leak checks in tests). */
size_t lm_position_count(void);

/* Frees the table; it must be empty. */
void lm_positions_shutdown(void);
