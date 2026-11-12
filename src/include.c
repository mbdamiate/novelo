/* core §8.8 — include contexts. The include form itself arrives in phase 4. */
#include "include.h"

#include "buffer.h"
#include "error.h"
#include "eval.h"

#include <string.h>

static void drop_context(Value *self) {
    LispContext *context = (LispContext *)self;
    lisp_release(context->parent);
    lisp_release(context->prefix);
    lisp_release(context->full_prefix);
    lisp_release(context->names);
}

static const LispClass context_class = {"Context", drop_context};

static const LispContext *as_context(const Value *context) { return (const LispContext *)context; }

Value *lisp_context_full_prefix(const Value *context) {
    return context ? as_context(context)->full_prefix : nullptr;
}

Value *lisp_context_child(LispState *state, Value *context, Value *prefix) {
    LispBuffer full;
    lisp_buffer_init(&full);
    if (context) {
        const LispString *parent_prefix = lisp_as_string(as_context(context)->full_prefix);
        lisp_buffer_append(&full, parent_prefix->data, parent_prefix->bytes);
        lisp_buffer_append_byte(&full, '/');
    }
    lisp_buffer_append(&full, lisp_as_name(prefix)->text, lisp_as_name(prefix)->bytes);
    Value *full_prefix = lisp_string(full.data, full.length);
    lisp_buffer_free(&full);

    /* Two contexts with the same full prefix are the same context (§8.8). */
    Value *existing = lisp_map_get(state->contexts, full_prefix);
    if (existing) {
        lisp_release(full_prefix);
        return existing;
    }
    LispContext *child = (LispContext *)lisp_opaque_new(&context_class, sizeof(LispContext));
    child->parent = lisp_retain(context);
    child->prefix = lisp_retain(prefix);
    child->full_prefix = full_prefix;
    child->names = lisp_map_new(0);
    Value *registry = lisp_map_associate(state->contexts, full_prefix, &child->base.header);
    lisp_release(state->contexts);
    state->contexts = registry;
    lisp_release(&child->base.header); /* the registry holds it now */
    return &child->base.header;
}

Value *lisp_context_resolve(const Value *context, Value *symbol) {
    if (!context) return symbol;
    Value *global = lisp_map_get(as_context(context)->names, symbol);
    return global ? global : symbol;
}

/* The Symbol prefix/name. */
static Value *prefixed_name(const Value *prefix, const Value *name) {
    LispBuffer text;
    lisp_buffer_init(&text);
    lisp_buffer_append(&text, lisp_as_name(prefix)->text, lisp_as_name(prefix)->bytes);
    lisp_buffer_append_byte(&text, '/');
    lisp_buffer_append(&text, lisp_as_name(name)->text, lisp_as_name(name)->bytes);
    Value *symbol = lisp_symbol(text.data, text.length);
    lisp_buffer_free(&text);
    return symbol;
}

/* The Symbol F/name, where F is the full prefix of the context. */
static Value *full_name(const Value *context, const Value *name) {
    const LispString *prefix = lisp_as_string(as_context(context)->full_prefix);
    LispBuffer text;
    lisp_buffer_init(&text);
    lisp_buffer_append(&text, prefix->data, prefix->bytes);
    lisp_buffer_append_byte(&text, '/');
    lisp_buffer_append(&text, lisp_as_name(name)->text, lisp_as_name(name)->bytes);
    Value *symbol = lisp_symbol(text.data, text.length);
    lisp_buffer_free(&text);
    return symbol;
}

void lisp_context_define(LispState *state, Value *context, Value *name, Value *value, bool is_macro) {
    while (context) {
        LispContext *current = (LispContext *)context;
        /* Record the name in this context, standing for the global F/name. */
        Value *global = full_name(context, name);
        Value *names = lisp_map_associate(current->names, name, global);
        lisp_release(global);
        lisp_release(current->names);
        current->names = names;
        /* Then define prefix/name in the context before the prefix. */
        name = prefixed_name(current->prefix, name);
        context = current->parent;
    }
    lisp_global_define(state, name, value, is_macro);
}

Value *lisp_include(LispState *state, Value *form, Value *) {
    return lisp_error(state, "include-error", LISP_LIST(form), "include is not implemented yet (phase 4)");
}
