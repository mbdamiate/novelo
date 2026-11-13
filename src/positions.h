/* core §6 — source positions, kept in a side table keyed by node identity.
 *
 * Datums stay ordinary values. A node with an entry carries LISP_HAS_POSITION,
 * and freeing it removes the entry (through the hook in value.c), so a later
 * object at the same address never inherits an old position (§6.4).
 */
#pragma once

#include "value.h"

#include <stdint.h>

/* Line and column start at 1; the column counts code points (§6.1). `file`
 * names the file the position is in (lisp_source_file_register), or is 0 when
 * the source has no file, as in the REPL. A line of 0 means no position. */
typedef struct LispPosition {
    uint32_t line;
    uint32_t column;
    uint32_t file;
} LispPosition;

/* Records where a non-empty List or Map was read. The node must be counted
 * (not immortal). */
void lisp_position_record(Value *node, LispPosition position);

/* True, with the position, if the node was read and is still alive. */
bool lisp_position_lookup(const Value *node, LispPosition *position);

/* Number of entries (for leak checks in tests). */
size_t lisp_position_count(void);

/* core §8.5 — the names of the files positions come from, kept for the
 * :file of error positions. Registering a name again returns the same id;
 * ids start at 1. */
uint32_t lisp_source_file_register(const char *name);

/* The name of a registered file, or nullptr for 0. */
const char *lisp_source_file_name(uint32_t file);
