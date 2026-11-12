/* core §6 — source positions, kept in a side table keyed by node identity.
 *
 * Datums stay ordinary values. A node with an entry carries LISP_HAS_POSITION,
 * and freeing it removes the entry (through the hook in value.c), so a later
 * object at the same address never inherits an old position (§6.4).
 */
#pragma once

#include "value.h"

#include <stdint.h>

/* Line and column start at 1; the column counts code points (§6.1). */
typedef struct LispPosition {
    uint32_t line;
    uint32_t column;
} LispPosition;

/* Records where a non-empty List or Map was read. The node must be counted
 * (not immortal). */
void lisp_position_record(Value *node, LispPosition position);

/* True, with the position, if the node was read and is still alive. */
bool lisp_position_lookup(const Value *node, LispPosition *position);

/* Number of entries (for leak checks in tests). */
size_t lisp_position_count(void);

/* Frees the table; it must be empty. */
void lisp_positions_shutdown(void);
