/* core §6 */
#include "test.h"

#include "positions.h"

#include <stdlib.h>

static void t_records_and_forgets(void) {
    size_t entries_before = lisp_position_count();
    Value *list = lisp_prepend_take(lisp_integer(1), LISP_EMPTY);
    lisp_position_record(list, (LispPosition){3, 7});
    LispPosition position;
    CHECK(lisp_position_lookup(list, &position));
    CHECK_INT(position.line, 3);
    CHECK_INT(position.column, 7);
    CHECK_INT(lisp_position_count(), entries_before + 1);
    lisp_release(list);
    CHECK_INT(lisp_position_count(), entries_before);
}

static void t_unrecorded_node_has_no_position(void) {
    Value *list = lisp_prepend_take(lisp_integer(1), LISP_EMPTY);
    LispPosition position;
    CHECK(!lisp_position_lookup(list, &position));
    lisp_release(list);
}

/* §6.4: an object allocated at a freed node's address must not inherit it. */
static void t_reused_address_has_no_position(void) {
    for (int attempt = 0; attempt < 100; attempt++) {
        Value *old_node = lisp_prepend_take(lisp_integer(attempt), LISP_EMPTY);
        lisp_position_record(old_node, (LispPosition){1, 1});
        uintptr_t old_address = (uintptr_t)old_node;
        lisp_release(old_node);
        Value *new_node = lisp_prepend_take(lisp_integer(attempt), LISP_EMPTY);
        LispPosition position;
        if ((uintptr_t)new_node == old_address) CHECK(!lisp_position_lookup(new_node, &position));
        CHECK(!lisp_position_lookup(new_node, &position));
        lisp_release(new_node);
    }
}

/* Many entries, removed in an interleaved order, exercise the backward shift. */
static void t_many_entries(void) {
    enum { COUNT = 5000 };
    Value **nodes = malloc(COUNT * sizeof *nodes);
    for (int index = 0; index < COUNT; index++) {
        nodes[index] = lisp_prepend_take(lisp_integer(index), LISP_EMPTY);
        lisp_position_record(nodes[index], (LispPosition){(uint32_t)index + 1, 1});
    }
    for (int index = 0; index < COUNT; index += 2) lisp_release(nodes[index]);
    for (int index = 1; index < COUNT; index += 2) {
        LispPosition position;
        CHECK(lisp_position_lookup(nodes[index], &position));
        CHECK_INT(position.line, index + 1);
        lisp_release(nodes[index]);
    }
    CHECK_INT(lisp_position_count(), 0);
    free(nodes);
}

const TestSuite positions_suite = {"positions", (const TestCase[]){
    {"records and forgets", t_records_and_forgets},
    {"an unrecorded node has no position", t_unrecorded_node_has_no_position},
    {"a reused address has no position", t_reused_address_has_no_position},
    {"many entries with interleaved removal", t_many_entries},
    {},
}};
