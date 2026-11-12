/* core §6 */
#include "test.h"

#include "positions.h"

#include <stdlib.h>

static void t_records_and_forgets(void) {
    size_t entries_before = lm_position_count();
    Value *list = lm_prepend_take(lm_integer(1), LM_EMPTY);
    lm_position_record(list, (LmPosition){3, 7});
    LmPosition position;
    CHECK(lm_position_lookup(list, &position));
    CHECK_INT(position.line, 3);
    CHECK_INT(position.column, 7);
    CHECK_INT(lm_position_count(), entries_before + 1);
    lm_release(list);
    CHECK_INT(lm_position_count(), entries_before);
}

static void t_unrecorded_node_has_no_position(void) {
    Value *list = lm_prepend_take(lm_integer(1), LM_EMPTY);
    LmPosition position;
    CHECK(!lm_position_lookup(list, &position));
    lm_release(list);
}

/* §6.4: an object allocated at a freed node's address must not inherit it. */
static void t_reused_address_has_no_position(void) {
    for (int attempt = 0; attempt < 100; attempt++) {
        Value *old_node = lm_prepend_take(lm_integer(attempt), LM_EMPTY);
        lm_position_record(old_node, (LmPosition){1, 1});
        uintptr_t old_address = (uintptr_t)old_node;
        lm_release(old_node);
        Value *new_node = lm_prepend_take(lm_integer(attempt), LM_EMPTY);
        LmPosition position;
        if ((uintptr_t)new_node == old_address) CHECK(!lm_position_lookup(new_node, &position));
        CHECK(!lm_position_lookup(new_node, &position));
        lm_release(new_node);
    }
}

/* Many entries, removed in an interleaved order, exercise the backward shift. */
static void t_many_entries(void) {
    enum { COUNT = 5000 };
    Value **nodes = malloc(COUNT * sizeof *nodes);
    for (int index = 0; index < COUNT; index++) {
        nodes[index] = lm_prepend_take(lm_integer(index), LM_EMPTY);
        lm_position_record(nodes[index], (LmPosition){(uint32_t)index + 1, 1});
    }
    for (int index = 0; index < COUNT; index += 2) lm_release(nodes[index]);
    for (int index = 1; index < COUNT; index += 2) {
        LmPosition position;
        CHECK(lm_position_lookup(nodes[index], &position));
        CHECK_INT(position.line, index + 1);
        lm_release(nodes[index]);
    }
    CHECK_INT(lm_position_count(), 0);
    free(nodes);
}

const TestSuite positions_suite = {"positions", (const TestCase[]){
    {"records and forgets", t_records_and_forgets},
    {"an unrecorded node has no position", t_unrecorded_node_has_no_position},
    {"a reused address has no position", t_reused_address_has_no_position},
    {"many entries with interleaved removal", t_many_entries},
    {},
}};
