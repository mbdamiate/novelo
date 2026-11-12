/* core §8.1, §8.2, §8.4 — evaluation.
 *
 * lisp_eval is a loop. Evaluating an if branch, the last expression of a
 * sequence or the body of a called closure replaces the current node and
 * environment instead of recursing, and the old environment is released, so
 * calls in tail position run in constant memory (§8.4). Everything else
 * (operands, tests, non-final expressions) recurses on the C stack, which
 * lisp_check_stack guards.
 */
#include "eval.h"

#include "error.h"
#include "include.h"
#include "node.h"

#include <stdlib.h>
#include <string.h>

/* ---- stack guard -------------------------------------------------------------- */

bool lisp_check_stack(LispState *state) {
    const char *frame = __builtin_frame_address(0);
    if (state->stack_base && frame < state->stack_base
        && (size_t)(state->stack_base - frame) > state->stack_limit) {
        lisp_error(state, "stack-overflow", LISP_EMPTY, "stack overflow: recursion too deep");
        return false;
    }
    return true;
}

/* ---- globals ------------------------------------------------------------------- */

static size_t global_slot(const LispGlobals *globals, const Value *name) {
    uint64_t bits = (uint64_t)(uintptr_t)name;
    bits ^= bits >> 33;
    bits *= 0xFF51AFD7ED558CCDULL;
    bits ^= bits >> 33;
    size_t mask = globals->capacity - 1;
    size_t slot = (size_t)bits & mask;
    while (globals->entries[slot].name && globals->entries[slot].name != name) slot = (slot + 1) & mask;
    return slot;
}

LispGlobal *lisp_global_find(LispState *state, const Value *name) {
    LispGlobals *globals = &state->globals;
    if (globals->count == 0) return nullptr;
    LispGlobal *global = &globals->entries[global_slot(globals, name)];
    return global->name ? global : nullptr;
}

static void grow_globals(LispGlobals *globals) {
    LispGlobals bigger = {.capacity = globals->capacity ? globals->capacity * 2 : 256};
    bigger.entries = lisp_xcalloc(bigger.capacity, sizeof(LispGlobal));
    for (size_t slot = 0; slot < globals->capacity; slot++) {
        if (globals->entries[slot].name) {
            bigger.entries[global_slot(&bigger, globals->entries[slot].name)] = globals->entries[slot];
            bigger.count++;
        }
    }
    free(globals->entries);
    *globals = bigger;
}

void lisp_global_define(LispState *state, Value *name, Value *value, bool is_macro) {
    LispGlobals *globals = &state->globals;
    if (2 * (globals->count + 1) > globals->capacity) grow_globals(globals);
    LispGlobal *global = &globals->entries[global_slot(globals, name)];
    if (global->name) {
        lisp_retain(value);
        lisp_release(global->value);
    } else {
        global->name = lisp_retain(name);
        lisp_retain(value);
        globals->count++;
    }
    global->value = value;
    global->is_macro = is_macro;
}

void lisp_globals_free(LispGlobals *globals) {
    for (size_t slot = 0; slot < globals->capacity; slot++) {
        if (globals->entries[slot].name) {
            lisp_release(globals->entries[slot].name);
            lisp_release(globals->entries[slot].value);
        }
    }
    free(globals->entries);
    *globals = (LispGlobals){};
}

/* §8.1: a symbol that is not local names a global, looked up at the moment of
 * evaluation; a name bound to a macro is not a value. */
static Value *global_value(LispState *state, Value *symbol, Value *context) {
    LispGlobal *global = lisp_global_find(state, lisp_context_resolve(context, symbol));
    if (!global || global->is_macro) {
        return lisp_error(state, "unbound-symbol", LISP_LIST(symbol), "unbound symbol %s",
                          lisp_as_name(symbol)->text);
    }
    return lisp_retain(global->value);
}

/* ---- environments ------------------------------------------------------------- */

static void drop_environment(Value *self) {
    LispEnvironment *environment = (LispEnvironment *)self;
    lisp_release(environment->parent);
    for (size_t index = 0; index < environment->count; index++) lisp_release(environment->values[index]);
}

static const LispClass environment_class = {"Environment", drop_environment};

static Value *local_value(const Value *environment, size_t depth, size_t index) {
    for (; depth > 0; depth--) environment = ((const LispEnvironment *)environment)->parent;
    return lisp_retain(((const LispEnvironment *)environment)->values[index]);
}

/* ---- application ----------------------------------------------------------------- */

static bool arity_fits(size_t count, size_t required, bool has_rest, int maximum) {
    if (count < required) return false;
    if (has_rest || maximum < 0) return true;
    return count <= (size_t)maximum;
}

static Value *wrong_arguments(LispState *state, Value *function, size_t count) {
    const char *name = function->kind == LISP_KIND_PRIMITIVE ? ((LispPrimitive *)function)->name : "function";
    return lisp_error(state, "wrong-arguments", LISP_LIST(function), "%s does not take %zu argument%s", name, count,
                      count == 1 ? "" : "s");
}

/* The environment of a closure call. Takes the arguments. Returns nullptr,
 * with the error set and the arguments released, if the arity does not fit. */
static Value *bind_arguments(LispState *state, Value *closure_value, size_t count, Value **arguments) {
    const LispClosure *closure = (const LispClosure *)closure_value;
    const LispNode *lambda = lisp_as_node(closure->lambda);
    if (!arity_fits(count, lambda->parameter_count, lambda->has_rest, (int)lambda->parameter_count)) {
        for (size_t index = 0; index < count; index++) lisp_release(arguments[index]);
        return wrong_arguments(state, closure_value, count);
    }
    size_t slots = lambda->parameter_count + (lambda->has_rest ? 1 : 0);
    LispEnvironment *environment = (LispEnvironment *)lisp_opaque_new(
        &environment_class, sizeof(LispEnvironment) + slots * sizeof(Value *));
    environment->parent = lisp_retain(closure->environment);
    environment->count = slots;
    for (size_t index = 0; index < lambda->parameter_count; index++) environment->values[index] = arguments[index];
    if (lambda->has_rest) {
        Value *rest = LISP_EMPTY;
        for (size_t index = count; index > lambda->parameter_count; index--) {
            rest = lisp_prepend_take(arguments[index - 1], rest);
        }
        environment->values[lambda->parameter_count] = rest;
    }
    return &environment->base.header;
}

static Value *call_primitive(LispState *state, Value *function, size_t count, Value *const *arguments) {
    const LispPrimitive *primitive = (const LispPrimitive *)function;
    if (!arity_fits(count, (size_t)primitive->minimum_arguments, false, primitive->maximum_arguments)) {
        return wrong_arguments(state, function, count);
    }
    return primitive->function(state, count, arguments);
}

static Value *not_a_function(LispState *state, Value *value) {
    return lisp_error(state, "not-a-function", LISP_LIST(value), "a %s is not a function", lisp_type_name(value));
}

Value *lisp_apply(LispState *state, Value *function, size_t count, Value *const *arguments) {
    if (function->kind == LISP_KIND_PRIMITIVE) return call_primitive(state, function, count, arguments);
    if (function->kind != LISP_KIND_CLOSURE) return not_a_function(state, function);
    Value *inline_arguments[8];
    Value **owned = count <= 8 ? inline_arguments : lisp_xmalloc(count * sizeof(Value *));
    for (size_t index = 0; index < count; index++) owned[index] = lisp_retain(arguments[index]);
    Value *environment = bind_arguments(state, function, count, owned);
    if (owned != inline_arguments) free(owned);
    if (!environment) return nullptr;
    const LispClosure *closure = (const LispClosure *)function;
    Value *result = lisp_eval(state, lisp_as_node(closure->lambda)->children[0], environment, closure->context);
    lisp_release(environment);
    return result;
}

/* ---- maps and quasiquote builders ------------------------------------------------------ */

/* Keys and values in order, left to right (§8.4); keys that repeat only after
 * evaluation are an error. */
static Value *build_map(LispState *state, const LispNode *node, Value *environment, Value *context) {
    Value *map = lisp_map_new(node->child_count / 2);
    for (size_t index = 0; index < node->child_count; index += 2) {
        Value *key = lisp_eval(state, node->children[index], environment, context);
        Value *value = key ? lisp_eval(state, node->children[index + 1], environment, context) : nullptr;
        if (!value) {
            lisp_release(key);
            lisp_release(map);
            return nullptr;
        }
        Value *key_for_error = lisp_retain(key);
        if (!lisp_map_insert_take(&map, key, value)) {
            lisp_release(map);
            return lisp_error(state, "duplicate-key", lisp_prepend_take(key_for_error, LISP_EMPTY),
                              "a key repeats in the map");
        }
        lisp_release(key_for_error);
    }
    return map;
}

static Value *build_list(LispState *state, const LispNode *node, Value *environment, Value *context) {
    LispListBuilder builder;
    lisp_list_builder_init(&builder);
    for (size_t index = 0; index < node->child_count; index++) {
        Value *part = lisp_eval(state, node->children[index], environment, context);
        if (!part) {
            lisp_list_builder_discard(&builder);
            return nullptr;
        }
        if (!node->splices[index]) {
            lisp_list_append_take(&builder, part);
            continue;
        }
        if (!lisp_is_list(part)) {
            lisp_list_builder_discard(&builder);
            return lisp_error(state, "wrong-type", lisp_prepend_take(part, LISP_EMPTY),
                              "unquote-splicing needs a List");
        }
        for (const Value *rest = part; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
            lisp_list_append_take(&builder, lisp_retain(lisp_first(rest)));
        }
        lisp_release(part);
    }
    return lisp_list_builder_finish(&builder);
}

/* ---- the loop ---------------------------------------------------------------------------- */

/* Replaces *slot with a retained replacement and releases the old value. The
 * replacement is retained first, since it may be owned by the old value. */
static void replace(Value **slot, Value *replacement) {
    lisp_retain(replacement);
    lisp_release(*slot);
    *slot = replacement;
}

Value *lisp_eval(LispState *state, Value *node, Value *environment, Value *context) {
    if (!lisp_check_stack(state)) {
        lisp_error_locate(state, lisp_as_node(node)->position);
        return nullptr;
    }
    lisp_retain(node);
    lisp_retain(environment);
    lisp_retain(context);
    Value *result = nullptr;

    for (;;) {
        const LispNode *current = lisp_as_node(node);
        switch (current->kind) {
        case LISP_NODE_CONSTANT:
            result = lisp_retain(current->value);
            goto done;
        case LISP_NODE_LOCAL:
            result = local_value(environment, current->depth, current->index);
            goto done;
        case LISP_NODE_GLOBAL:
            result = global_value(state, current->value, context);
            goto done;
        case LISP_NODE_LAMBDA:
            result = lisp_closure(node, environment, context);
            goto done;
        case LISP_NODE_MAP:
        case LISP_NODE_MAP_BUILD:
            result = build_map(state, current, environment, context);
            goto done;
        case LISP_NODE_LIST_BUILD:
            result = build_list(state, current, environment, context);
            goto done;

        case LISP_NODE_IF: {
            Value *test = lisp_eval(state, current->children[0], environment, context);
            if (!test) goto done;
            bool true_branch = test != LISP_FALSE; /* only #false is false (§8.4) */
            lisp_release(test);
            Value *branch = true_branch ? current->children[1] : current->children[2];
            if (!branch) {
                result = LISP_VOID;
                goto done;
            }
            replace(&node, branch);
            continue;
        }

        case LISP_NODE_SEQUENCE: {
            for (size_t index = 0; index + 1 < current->child_count; index++) {
                Value *value = lisp_eval(state, current->children[index], environment, context);
                if (!value) goto done;
                lisp_release(value);
            }
            replace(&node, current->children[current->child_count - 1]);
            continue;
        }

        case LISP_NODE_APPLY: {
            /* §8.1: the operator first, then the arguments, left to right. */
            size_t count = current->child_count - 1;
            Value *function = lisp_eval(state, current->children[0], environment, context);
            if (!function) goto done;
            Value *inline_arguments[8];
            Value **arguments = count <= 8 ? inline_arguments : lisp_xmalloc(count * sizeof(Value *));
            size_t evaluated = 0;
            for (; evaluated < count; evaluated++) {
                arguments[evaluated] = lisp_eval(state, current->children[evaluated + 1], environment, context);
                if (!arguments[evaluated]) break;
            }

            if (evaluated < count || function->kind != LISP_KIND_CLOSURE) {
                if (evaluated == count) {
                    result = function->kind == LISP_KIND_PRIMITIVE
                                 ? call_primitive(state, function, count, arguments)
                                 : not_a_function(state, function);
                }
                for (size_t index = 0; index < evaluated; index++) lisp_release(arguments[index]);
                if (arguments != inline_arguments) free(arguments);
                lisp_release(function);
                goto done;
            }

            /* A closure: continue the loop with its body (tail call). */
            Value *frame = bind_arguments(state, function, count, arguments);
            if (arguments != inline_arguments) free(arguments);
            if (!frame) {
                lisp_release(function);
                goto done;
            }
            const LispClosure *closure = (const LispClosure *)function;
            replace(&node, lisp_as_node(closure->lambda)->children[0]);
            replace(&context, closure->context);
            lisp_release(environment);
            environment = frame;
            lisp_release(function);
            continue;
        }
        }
    }

done:
    /* §8.5: the innermost node with a position names where the error left. */
    if (!result) lisp_error_locate(state, lisp_as_node(node)->position);
    lisp_release(node);
    lisp_release(environment);
    lisp_release(context);
    return result;
}
