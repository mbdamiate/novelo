/* Expanded code (core §8.2–8.3): what the expander produces from a datum and
 * the evaluator runs. Nodes are counted opaque objects, shared by the closures
 * made from them.
 *
 * The derived forms disappear during expansion: let and or become lambda
 * applications (§8.3), and becomes if, and quasiquote becomes the List and
 * Map builders below. sequence keeps a node of its own, which behaves exactly
 * like its rewrite ((lambda () e …)) but needs no closure.
 */
#pragma once

#include "value.h"

typedef enum LispNodeKind {
    LISP_NODE_CONSTANT,   /* value: the datum (quote, self-evaluating atoms, ()) */
    LISP_NODE_LOCAL,      /* depth, index: a parameter of an enclosing lambda */
    LISP_NODE_GLOBAL,     /* value: the Symbol, resolved in the context at run time */
    LISP_NODE_IF,         /* children: test, then, else (nullptr: #void) */
    LISP_NODE_LAMBDA,     /* children: body; parameter_count, has_rest */
    LISP_NODE_APPLY,      /* children: operator, arguments … */
    LISP_NODE_SEQUENCE,   /* children: expressions; the last is in tail position */
    LISP_NODE_MAP,        /* children: key, value, key, value … (map literal, §8.4) */
    LISP_NODE_LIST_BUILD, /* children: parts; splices[i]: part i is ,@ (quasiquote) */
    LISP_NODE_MAP_BUILD,  /* children: key, value … (quasiquote template of a Map) */
} LispNodeKind;

typedef struct LispNode {
    LispOpaque base;
    LispNodeKind kind;
    Value *value;
    size_t depth;
    size_t index;
    Value **children;
    size_t child_count;
    bool *splices;
    size_t parameter_count; /* required parameters, before &rest */
    bool has_rest;
} LispNode;

extern const LispClass lisp_node_class;

static inline const LispNode *lisp_as_node(const Value *value) { return (const LispNode *)value; }

/* A node of the given kind with room for child_count children (all nullptr). */
[[nodiscard]] Value *lisp_node_new(LispNodeKind kind, size_t child_count);
