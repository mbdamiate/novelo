/* core §8.2–8.3 — expansion: special forms, derived forms, macros, declare.
 *
 * Every lambda opens a scope, and a symbol bound by an enclosing lambda
 * becomes a LOCAL node with its lexical address (depth, index); any other
 * symbol becomes a GLOBAL node, resolved at run time (§8.2). The names of the
 * special and derived forms are recognized wherever they head a form; a macro
 * name is recognized only when no enclosing lambda binds it.
 */
#include "expander.h"

#include "buffer.h"
#include "error.h"
#include "eval.h"
#include "include.h"
#include "node.h"
#include "primitives.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- symbols -------------------------------------------------------------------- */

const LispSymbols *lisp_symbols(void) {
    static LispSymbols symbols;
    static bool ready;
    if (!ready) {
        symbols = (LispSymbols){
            .quote = lisp_symbol_cstr("quote"),
            .if_ = lisp_symbol_cstr("if"),
            .otherwise = lisp_symbol_cstr("otherwise"),
            .lambda = lisp_symbol_cstr("lambda"),
            .rest_marker = lisp_symbol_cstr("&rest"),
            .define = lisp_symbol_cstr("define"),
            .define_macro = lisp_symbol_cstr("define-macro"),
            .let = lisp_symbol_cstr("let"),
            .sequence = lisp_symbol_cstr("sequence"),
            .and_ = lisp_symbol_cstr("and"),
            .or_ = lisp_symbol_cstr("or"),
            .quasiquote = lisp_symbol_cstr("quasiquote"),
            .unquote = lisp_symbol_cstr("unquote"),
            .unquote_splicing = lisp_symbol_cstr("unquote-splicing"),
            .include = lisp_symbol_cstr("include"),
            .import = lisp_symbol_cstr("import"),
            .declare = lisp_symbol_cstr("declare"),
            .declare_type = lisp_symbol_cstr("declare-type"),
            .match = lisp_symbol_cstr("match"),
            .try_ = lisp_symbol_cstr("try"),
            .wildcard = lisp_symbol_cstr("_"),
        };
        ready = true;
    }
    return &symbols;
}

/* ---- nodes ---------------------------------------------------------------------- */

static void drop_node(Value *self) {
    LispNode *node = (LispNode *)self;
    lisp_release(node->value);
    for (size_t index = 0; index < node->child_count; index++) lisp_release(node->children[index]);
    free(node->children);
    free(node->splices);
}

const LispClass lisp_node_class = {"Node", drop_node};

Value *lisp_node_new(LispNodeKind kind, size_t child_count) {
    LispNode *node = (LispNode *)lisp_opaque_new(&lisp_node_class, sizeof(LispNode));
    node->kind = kind;
    node->child_count = child_count;
    node->children = child_count ? lisp_xcalloc(child_count, sizeof(Value *)) : nullptr;
    return &node->base.header;
}

static Value *constant_node(Value *datum) {
    Value *node = lisp_node_new(LISP_NODE_CONSTANT, 0);
    ((LispNode *)node)->value = lisp_retain(datum);
    return node;
}

static LispNode *mutable_node(Value *node) { return (LispNode *)node; }

/* ---- scopes ----------------------------------------------------------------------- */

typedef struct Scope {
    const struct Scope *parent;
    Value *const *names;
    size_t count;
} Scope;

static bool find_local(const Scope *scope, const Value *symbol, size_t *depth, size_t *index) {
    for (size_t level = 0; scope; scope = scope->parent, level++) {
        for (size_t position = 0; position < scope->count; position++) {
            if (scope->names[position] == symbol) {
                *depth = level;
                *index = position;
                return true;
            }
        }
    }
    return false;
}

static bool is_locally_bound(const Scope *scope, const Value *symbol) {
    size_t depth, index;
    return find_local(scope, symbol, &depth, &index);
}

/* ---- helpers -------------------------------------------------------------------------- */

Value *lisp_expansion_error(LispState *state, Value *form, const char *message) {
    return lisp_error(state, "expansion-error", LISP_LIST(form), "%s", message);
}

static Value *element(const Value *list, size_t position) {
    for (size_t step = 0; step < position; step++) list = lisp_rest(list);
    return lisp_first(list);
}

static bool is_form_named(const Value *form, const Value *name) {
    return form->kind == LISP_KIND_PAIR && lisp_first(form) == name;
}

static Value *expand(LispState *state, Value *form, const Scope *scope, Value *context);

/* Expands each element of a list into children[offset …]. */
static bool expand_into(LispState *state, Value *node, size_t offset, const Value *forms,
                        const Scope *scope, Value *context) {
    LispNode *target = mutable_node(node);
    for (size_t index = offset; forms->kind == LISP_KIND_PAIR; forms = lisp_rest(forms), index++) {
        target->children[index] = expand(state, lisp_first(forms), scope, context);
        if (!target->children[index]) return false;
    }
    return true;
}

/* A body: one or more expressions; the last one is in tail position. */
static Value *expand_body(LispState *state, Value *forms, const Scope *scope, Value *context, Value *whole) {
    size_t count = lisp_list_length(forms);
    if (count == 0) return lisp_expansion_error(state, whole, "a body needs at least one expression");
    if (count == 1) return expand(state, lisp_first(forms), scope, context);
    Value *node = lisp_node_new(LISP_NODE_SEQUENCE, count);
    if (!expand_into(state, node, 0, forms, scope, context)) {
        lisp_release(node);
        return nullptr;
    }
    return node;
}

/* A lambda node over an already validated parameter scope. Leading
 * (declare …) forms of the body are ignored (§8.2). */
static Value *make_lambda(LispState *state, const Scope *parameters, size_t required, bool has_rest,
                          Value *body_forms, Value *context, Value *whole) {
    while (body_forms->kind == LISP_KIND_PAIR && is_form_named(lisp_first(body_forms), lisp_symbols()->declare)) {
        body_forms = lisp_rest(body_forms);
    }
    Value *body = expand_body(state, body_forms, parameters, context, whole);
    if (!body) return nullptr;
    Value *node = lisp_node_new(LISP_NODE_LAMBDA, 1);
    LispNode *lambda = mutable_node(node);
    lambda->children[0] = body;
    lambda->parameter_count = required;
    lambda->has_rest = has_rest;
    return node;
}

/* ---- special forms (§8.2) ---------------------------------------------------------------- */

static Value *expand_quote(LispState *state, Value *form) {
    if (lisp_list_length(form) != 2) return lisp_expansion_error(state, form, "quote takes exactly one datum");
    return constant_node(element(form, 1));
}

/* Builds the IF node of the pairs left in `arguments`, `pairs` of them,
 * followed by `otherwise` and one expression when has_otherwise:
 * (if c1 a1 c2 a2 …) ≡ (if c1 a1 otherwise (if c2 a2 …)). */
static Value *expand_if_pairs(LispState *state, const Value *arguments, size_t pairs, bool has_otherwise,
                              const Scope *scope, Value *context) {
    Value *node = lisp_node_new(LISP_NODE_IF, 3);
    LispNode *conditional = mutable_node(node);
    conditional->children[0] = expand(state, lisp_first(arguments), scope, context);
    if (!conditional->children[0]) {
        lisp_release(node);
        return nullptr;
    }
    const Value *after_test = lisp_rest(arguments);
    conditional->children[1] = expand(state, lisp_first(after_test), scope, context);
    if (!conditional->children[1]) {
        lisp_release(node);
        return nullptr;
    }
    const Value *more = lisp_rest(after_test);
    if (pairs > 1) {
        conditional->children[2] = expand_if_pairs(state, more, pairs - 1, has_otherwise, scope, context);
    } else if (has_otherwise) {
        conditional->children[2] = expand(state, lisp_first(lisp_rest(more)), scope, context); /* skips otherwise */
    } else {
        return node; /* no otherwise: #void when no test holds */
    }
    if (!conditional->children[2]) {
        lisp_release(node);
        return nullptr;
    }
    return node;
}

/* (if c1 a1 c2 a2 …) or (if c1 a1 c2 a2 … otherwise b), one pair or more.
 * `otherwise` may appear only right before the last expression. */
static Value *expand_if(LispState *state, Value *form, const Scope *scope, Value *context) {
    const Value *arguments = lisp_rest(form);
    size_t count = lisp_list_length(arguments);
    size_t index = 0;
    size_t otherwise_index = SIZE_MAX;
    bool valid = true;
    for (const Value *rest = arguments; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest), index++) {
        if (lisp_first(rest) != lisp_symbols()->otherwise) continue;
        if (otherwise_index != SIZE_MAX || index + 2 != count) valid = false;
        otherwise_index = index;
    }
    bool has_otherwise = otherwise_index != SIZE_MAX;
    size_t paired = has_otherwise ? count - 2 : count; /* the arguments that form pairs */
    if (!valid || paired == 0 || paired % 2 != 0) {
        return lisp_expansion_error(state, form,
                                    "if must be (if c1 a1 c2 a2 …), optionally ending with otherwise and one expression");
    }
    return expand_if_pairs(state, arguments, paired / 2, has_otherwise, scope, context);
}

/* (lambda (parameters …) body …) */
static Value *expand_lambda(LispState *state, Value *form, const Scope *scope, Value *context) {
    if (lisp_list_length(form) < 3) return lisp_expansion_error(state, form, "lambda needs parameters and a body");
    Value *parameter_list = element(form, 1);
    if (!lisp_is_list(parameter_list)) {
        return lisp_expansion_error(state, form, "the parameters of lambda must be a list (there is no named lambda)");
    }

    size_t parameter_total = lisp_list_length(parameter_list);
    Value **names = lisp_xcalloc(parameter_total ? parameter_total : 1, sizeof(Value *));
    size_t name_count = 0;
    bool has_rest = false;
    const char *problem = nullptr;
    for (const Value *rest = parameter_list; rest->kind == LISP_KIND_PAIR && !problem; rest = lisp_rest(rest)) {
        Value *parameter = lisp_first(rest);
        if (parameter == lisp_symbols()->rest_marker) {
            bool exactly_one_after = lisp_rest(rest)->kind == LISP_KIND_PAIR
                                     && lisp_rest(lisp_rest(rest)) == LISP_EMPTY;
            if (has_rest || !exactly_one_after) {
                problem = "&rest must come before exactly one last parameter";
            } else {
                has_rest = true;
            }
            continue;
        }
        if (parameter->kind != LISP_KIND_SYMBOL) {
            problem = "parameters must be symbols";
        } else {
            for (size_t earlier = 0; earlier < name_count; earlier++) {
                if (names[earlier] == parameter) problem = "repeated parameter";
            }
            names[name_count++] = parameter;
        }
    }
    Value *node = nullptr;
    if (problem) {
        lisp_expansion_error(state, form, problem);
    } else {
        Scope parameters = {.parent = scope, .names = names, .count = name_count};
        size_t required = has_rest ? name_count - 1 : name_count;
        node = make_lambda(state, &parameters, required, has_rest, lisp_rest(lisp_rest(form)), context, form);
    }
    free(names);
    return node;
}

/* ---- derived forms (§8.3) ------------------------------------------------------------------ */

static Value *apply_node(Value *operator_node, size_t argument_count, Value *const *arguments_taken) {
    Value *node = lisp_node_new(LISP_NODE_APPLY, argument_count + 1);
    LispNode *application = mutable_node(node);
    application->children[0] = operator_node;
    for (size_t index = 0; index < argument_count; index++) application->children[index + 1] = arguments_taken[index];
    return node;
}

static Value *expand_match(LispState *state, Value *form, const Scope *scope, Value *context);
static const char *pattern_problem(Value *pattern);

/* (let ((x v) more …) body …) ≡ ((lambda (x) (let (more …) body …)) v)
 * When `more` is empty, the inner let is just the body.
 *
 * core 0.11.0 — any other pattern, `_` included, takes the value apart as
 * match does: (let ((p v) more …) body …) ≡ (match v p (let (more …) body …)),
 * so a value that does not match is :no-match. */
static Value *expand_let_bindings(LispState *state, Value *bindings, Value *body, const Scope *scope,
                                  Value *context, Value *whole) {
    if (bindings == LISP_EMPTY) { /* (let () body …) ≡ ((lambda () body …)) */
        Scope empty_scope = {.parent = scope, .names = nullptr, .count = 0};
        Value *lambda = make_lambda(state, &empty_scope, 0, false, body, context, whole);
        return lambda ? apply_node(lambda, 0, nullptr) : nullptr;
    }
    Value *binding = lisp_first(bindings);
    if (binding->kind != LISP_KIND_PAIR || lisp_list_length(binding) != 2) {
        return lisp_expansion_error(state, whole, "each let binding must be (pattern value)");
    }
    Value *name = lisp_first(binding);
    if (name->kind != LISP_KIND_SYMBOL || name == lisp_symbols()->wildcard) {
        /* checked here, so that an invalid pattern names the let, not its rewrite */
        const char *problem = pattern_problem(name);
        if (problem) return lisp_expansion_error(state, whole, problem);
        const LispSymbols *symbols = lisp_symbols();
        Value *inner_let = lisp_prepend_take(lisp_retain(symbols->let),
                                             lisp_prepend_take(lisp_retain(lisp_rest(bindings)), lisp_retain(body)));
        Value *rewritten = LISP_LIST(symbols->match, element(binding, 1), name, inner_let);
        Value *node = expand_match(state, rewritten, scope, context);
        lisp_release(rewritten);
        lisp_release(inner_let);
        return node;
    }
    Value *value_node = expand(state, element(binding, 1), scope, context);
    if (!value_node) return nullptr;

    Scope inner = {.parent = scope, .names = &name, .count = 1};
    Value *lambda;
    if (lisp_rest(bindings) == LISP_EMPTY) {
        lambda = make_lambda(state, &inner, 1, false, body, context, whole);
    } else {
        Value *inner_body = expand_let_bindings(state, lisp_rest(bindings), body, &inner, context, whole);
        lambda = nullptr;
        if (inner_body) {
            lambda = lisp_node_new(LISP_NODE_LAMBDA, 1);
            mutable_node(lambda)->children[0] = inner_body;
            mutable_node(lambda)->parameter_count = 1;
        }
    }
    if (!lambda) {
        lisp_release(value_node);
        return nullptr;
    }
    return apply_node(lambda, 1, &value_node);
}

static Value *expand_let(LispState *state, Value *form, const Scope *scope, Value *context) {
    if (lisp_list_length(form) < 3 || !lisp_is_list(element(form, 1))) {
        return lisp_expansion_error(state, form, "let must be (let ((pattern value) …) body …)");
    }
    return expand_let_bindings(state, element(form, 1), lisp_rest(lisp_rest(form)), scope, context, form);
}

static Value *expand_sequence(LispState *state, Value *form, const Scope *scope, Value *context) {
    return expand_body(state, lisp_rest(form), scope, context, form);
}

/* (and e more …) ≡ (if e (and more …) otherwise #false) */
static Value *expand_and(LispState *state, Value *arguments, const Scope *scope, Value *context) {
    if (arguments == LISP_EMPTY) return constant_node(LISP_TRUE);
    Value *first = expand(state, lisp_first(arguments), scope, context);
    if (!first || lisp_rest(arguments) == LISP_EMPTY) return first;
    Value *rest = expand_and(state, lisp_rest(arguments), scope, context);
    if (!rest) {
        lisp_release(first);
        return nullptr;
    }
    Value *node = lisp_node_new(LISP_NODE_IF, 3);
    mutable_node(node)->children[0] = first;
    mutable_node(node)->children[1] = rest;
    mutable_node(node)->children[2] = constant_node(LISP_FALSE);
    return node;
}

/* (or e more …) ≡ (let ((t e)) (if t t otherwise (or more …))), t generated */
static Value *expand_or(LispState *state, Value *arguments, const Scope *scope, Value *context) {
    if (arguments == LISP_EMPTY) return constant_node(LISP_FALSE);
    Value *first = expand(state, lisp_first(arguments), scope, context);
    if (!first || lisp_rest(arguments) == LISP_EMPTY) return first;

    Value *temporary = lisp_generate_symbol();
    Scope inner = {.parent = scope, .names = &temporary, .count = 1};
    Value *rest = expand_or(state, lisp_rest(arguments), &inner, context);
    lisp_release(temporary);
    if (!rest) {
        lisp_release(first);
        return nullptr;
    }
    Value *test = lisp_node_new(LISP_NODE_LOCAL, 0);
    Value *then = lisp_node_new(LISP_NODE_LOCAL, 0); /* depth 0, index 0: t */
    Value *conditional = lisp_node_new(LISP_NODE_IF, 3);
    mutable_node(conditional)->children[0] = test;
    mutable_node(conditional)->children[1] = then;
    mutable_node(conditional)->children[2] = rest;
    Value *lambda = lisp_node_new(LISP_NODE_LAMBDA, 1);
    mutable_node(lambda)->children[0] = conditional;
    mutable_node(lambda)->parameter_count = 1;
    return apply_node(lambda, 1, &first);
}

/* ---- match (§8.3) ------------------------------------------------------------------------------ */

/* match is rewritten into datums and expanded again:
 *
 *   (match s p1 r1 p2 r2 otherwise b)
 *   ≡ (let ((g s))
 *       (if (and tests of p1 on g) (let (bindings of p1) r1)
 *           otherwise (if (and tests of p2 on g) (let (bindings of p2) r2)
 *                         otherwise b)))
 *
 * where g is a generated symbol. Without otherwise, the last branch raises
 * :no-match. The tests call primitive values embedded in the datums, which
 * expand to constants, so rebinding `first` or `get` does not change match. */

typedef struct MatchPrimitives {
    Value *is_list, *is_empty, *not_, *first, *rest, *is_map, *contains, *get, *equal, *no_match;
} MatchPrimitives;

static void match_primitives_init(MatchPrimitives *primitives) {
    *primitives = (MatchPrimitives){
        .is_list = lisp_primitive_value("list?"),
        .is_empty = lisp_primitive_value("empty?"),
        .not_ = lisp_primitive_value("not"),
        .first = lisp_primitive_value("first"),
        .rest = lisp_primitive_value("rest"),
        .is_map = lisp_primitive_value("map?"),
        .contains = lisp_primitive_value("contains?"),
        .get = lisp_primitive_value("get"),
        .equal = lisp_primitive_value("equal?"),
        .no_match = lisp_no_match_primitive(),
    };
}

static void match_primitives_release(MatchPrimitives *primitives) {
    Value *all[] = {primitives->is_list, primitives->is_empty, primitives->not_,   primitives->first,
                    primitives->rest,    primitives->is_map,   primitives->contains, primitives->get,
                    primitives->equal,   primitives->no_match};
    for (size_t index = 0; index < sizeof all / sizeof all[0]; index++) lisp_release(all[index]);
}

/* What walking a pattern collects: the tests, in order, and the bindings
 * (name access), plus the names bound so far, to reject repeats. */
typedef struct MatchWalk {
    const MatchPrimitives *primitives;
    LispListBuilder tests;
    LispListBuilder bindings;
    Value *names; /* List of Symbols */
} MatchWalk;

static void add_test(MatchWalk *walk, Value *test) { lisp_list_append_take(&walk->tests, test); }

/* (equal? 'datum access) */
static void add_literal_test(MatchWalk *walk, Value *datum, Value *access) {
    Value *quoted = LISP_LIST(lisp_symbols()->quote, datum);
    add_test(walk, LISP_LIST(walk->primitives->equal, quoted, access));
    lisp_release(quoted);
}

static const char *walk_pattern(MatchWalk *walk, Value *pattern, Value *access);

/* (p …) or (p … &rest r): one element after another, from `access`. */
static const char *walk_list_pattern(MatchWalk *walk, Value *pattern, Value *access) {
    const MatchPrimitives *primitives = walk->primitives;
    add_test(walk, LISP_LIST(primitives->is_list, access));
    Value *current = lisp_retain(access);
    const char *problem = nullptr;
    const Value *rest = pattern;
    for (; rest->kind == LISP_KIND_PAIR && !problem; rest = lisp_rest(rest)) {
        Value *element_pattern = lisp_first(rest);
        if (element_pattern == lisp_symbols()->rest_marker) {
            bool exactly_one_after = lisp_rest(rest)->kind == LISP_KIND_PAIR && lisp_rest(lisp_rest(rest)) == LISP_EMPTY;
            problem = exactly_one_after ? walk_pattern(walk, lisp_first(lisp_rest(rest)), current)
                                        : "&rest must be followed by exactly one pattern";
            lisp_release(current);
            return problem;
        }
        Value *is_empty = LISP_LIST(primitives->is_empty, current);
        add_test(walk, LISP_LIST(primitives->not_, is_empty));
        lisp_release(is_empty);
        Value *head = LISP_LIST(primitives->first, current);
        problem = walk_pattern(walk, element_pattern, head);
        lisp_release(head);
        Value *next = LISP_LIST(primitives->rest, current);
        lisp_release(current);
        current = next;
    }
    if (!problem) add_test(walk, LISP_LIST(primitives->is_empty, current)); /* no elements left over */
    lisp_release(current);
    return problem;
}

/* {key p …}: each key present, each value matching its pattern. */
static const char *walk_map_pattern(MatchWalk *walk, Value *pattern, Value *access) {
    const MatchPrimitives *primitives = walk->primitives;
    add_test(walk, LISP_LIST(primitives->is_map, access));
    size_t cursor = 0;
    Value *key, *value_pattern;
    while (lisp_map_next(pattern, &cursor, &key, &value_pattern)) {
        if (key->kind == LISP_KIND_SYMBOL) return "the keys of a map pattern must not be symbols";
        Value *quoted = LISP_LIST(lisp_symbols()->quote, key);
        add_test(walk, LISP_LIST(primitives->contains, access, quoted));
        Value *value_access = LISP_LIST(primitives->get, access, quoted);
        lisp_release(quoted);
        const char *problem = walk_pattern(walk, value_pattern, value_access);
        lisp_release(value_access);
        if (problem) return problem;
    }
    return nullptr;
}

/* Adds the tests and bindings that match `pattern` against the value of
 * the datum `access`. Returns the reason the pattern is invalid, or nullptr. */
static const char *walk_pattern(MatchWalk *walk, Value *pattern, Value *access) {
    const LispSymbols *symbols = lisp_symbols();
    switch (pattern->kind) {
    case LISP_KIND_SYMBOL:
        if (pattern == symbols->wildcard) return nullptr;
        if (pattern == symbols->otherwise || pattern == symbols->rest_marker) {
            return "otherwise and &rest are not patterns";
        }
        for (const Value *names = walk->names; names->kind == LISP_KIND_PAIR; names = lisp_rest(names)) {
            if (lisp_first(names) == pattern) return "a name is bound twice in one pattern";
        }
        walk->names = lisp_prepend_take(lisp_retain(pattern), walk->names);
        lisp_list_append_take(&walk->bindings, LISP_LIST(pattern, access));
        return nullptr;
    case LISP_KIND_PAIR:
        if (lisp_first(pattern) == symbols->quote) {
            if (lisp_list_length(pattern) != 2) return "quote takes exactly one datum";
            add_literal_test(walk, element(pattern, 1), access);
            return nullptr;
        }
        return walk_list_pattern(walk, pattern, access);
    case LISP_KIND_MAP:
        return walk_map_pattern(walk, pattern, access);
    case LISP_KIND_SET:
        return "a set has no order to match; quote it to match an equal set";
    default: /* Integer, String, Keyword, Boolean, Void, () */
        add_literal_test(walk, pattern, access);
        return nullptr;
    }
}

/* Why a pattern is invalid, or nullptr. */
static const char *pattern_problem(Value *pattern) {
    MatchPrimitives primitives;
    match_primitives_init(&primitives);
    MatchWalk walk = {.primitives = &primitives, .names = LISP_EMPTY};
    lisp_list_builder_init(&walk.tests);
    lisp_list_builder_init(&walk.bindings);
    const char *problem = walk_pattern(&walk, pattern, lisp_symbols()->wildcard);
    lisp_release(walk.names);
    lisp_list_builder_discard(&walk.tests);
    lisp_list_builder_discard(&walk.bindings);
    match_primitives_release(&primitives);
    return problem;
}

/* One pattern and its result, with `otherwise_branch` for when it fails.
 * Returns the new datum, or nullptr with *problem set. */
static Value *match_clause(const MatchPrimitives *primitives, Value *subject_name, Value *pattern, Value *result,
                           Value *otherwise_branch, const char **problem) {
    const LispSymbols *symbols = lisp_symbols();
    MatchWalk walk = {.primitives = primitives, .names = LISP_EMPTY};
    lisp_list_builder_init(&walk.tests);
    lisp_list_builder_init(&walk.bindings);
    *problem = walk_pattern(&walk, pattern, subject_name);
    lisp_release(walk.names);
    Value *tests = lisp_list_builder_finish(&walk.tests);
    Value *bindings = lisp_list_builder_finish(&walk.bindings);
    Value *clause = nullptr;
    if (!*problem) {
        Value *body = bindings == LISP_EMPTY ? lisp_retain(result) : LISP_LIST(symbols->let, bindings, result);
        if (tests == LISP_EMPTY) {
            clause = body; /* the pattern matches anything */
        } else {
            Value *condition = lisp_prepend(symbols->and_, tests);
            clause = LISP_LIST(symbols->if_, condition, body, symbols->otherwise, otherwise_branch);
            lisp_release(condition);
            lisp_release(body);
        }
    }
    lisp_release(tests);
    lisp_release(bindings);
    return clause;
}

/* (match subject p1 r1 p2 r2 …), optionally ending with otherwise and one
 * expression. `otherwise` may appear only right before the last one. */
static Value *expand_match(LispState *state, Value *form, const Scope *scope, Value *context) {
    size_t length = lisp_list_length(form);
    if (length < 2) return lisp_expansion_error(state, form, "match needs a subject, then pattern and result pairs");
    const Value *arguments = lisp_rest(lisp_rest(form)); /* after the subject */
    size_t count = length - 2;
    size_t index = 0;
    size_t otherwise_index = SIZE_MAX;
    bool valid = true;
    for (const Value *rest = arguments; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest), index++) {
        if (lisp_first(rest) != lisp_symbols()->otherwise) continue;
        if (otherwise_index != SIZE_MAX || index + 2 != count) valid = false;
        otherwise_index = index;
    }
    bool has_otherwise = otherwise_index != SIZE_MAX;
    size_t paired = has_otherwise ? count - 2 : count;
    if (!valid || paired == 0 || paired % 2 != 0) {
        return lisp_expansion_error(
            state, form, "match must be (match subject p1 r1 p2 r2 …), optionally ending with otherwise and one expression");
    }

    size_t pairs = paired / 2;
    Value **patterns = lisp_xcalloc(pairs, sizeof(Value *));
    Value **results = lisp_xcalloc(pairs, sizeof(Value *));
    const Value *rest = arguments;
    for (size_t pair = 0; pair < pairs; pair++) {
        patterns[pair] = lisp_first(rest);
        results[pair] = lisp_first(lisp_rest(rest));
        rest = lisp_rest(lisp_rest(rest));
    }

    MatchPrimitives primitives;
    match_primitives_init(&primitives);
    Value *subject_name = lisp_generate_symbol();
    Value *branch = has_otherwise ? lisp_retain(lisp_first(lisp_rest(rest)))
                                  : LISP_LIST(primitives.no_match, subject_name);
    const char *problem = nullptr;
    for (size_t pair = pairs; pair > 0 && branch; pair--) {
        Value *clause = match_clause(&primitives, subject_name, patterns[pair - 1], results[pair - 1], branch, &problem);
        lisp_release(branch);
        branch = clause;
    }

    Value *node = nullptr;
    if (problem) {
        lisp_expansion_error(state, form, problem);
    } else {
        Value *binding = LISP_LIST(subject_name, element(form, 1));
        Value *bindings = LISP_LIST(binding);
        Value *rewritten = LISP_LIST(lisp_symbols()->let, bindings, branch);
        node = expand(state, rewritten, scope, context);
        lisp_release(rewritten);
        lisp_release(bindings);
        lisp_release(binding);
    }
    lisp_release(branch);
    lisp_release(subject_name);
    match_primitives_release(&primitives);
    free(patterns);
    free(results);
    return node;
}

/* ---- try (§8.3, core 0.13.0) ------------------------------------------------------------------- */

/* (try body p1 r1 p2 r2 …) is rewritten into datums and expanded again:
 *
 *   (catch (lambda () body)
 *          (lambda (e) (match e p1 r1 p2 r2 … otherwise (raise e))))
 *
 * with `e` a generated symbol, and `catch` and `raise` the primitive values
 * embedded as constants, so rebinding those names does not change try. */
static Value *expand_try(LispState *state, Value *form, const Scope *scope, Value *context) {
    size_t length = lisp_list_length(form);
    const Value *clauses = length >= 2 ? lisp_rest(lisp_rest(form)) : LISP_EMPTY;
    bool valid = length >= 4 && (length - 2) % 2 == 0;
    for (const Value *rest = clauses; valid && rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
        if (lisp_first(rest) == lisp_symbols()->otherwise) valid = false;
    }
    if (!valid) {
        return lisp_expansion_error(state, form, "try must be (try body p1 r1 p2 r2 …), without otherwise");
    }
    /* checked here, so that an invalid pattern names the try, not its rewrite */
    for (const Value *rest = clauses; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(lisp_rest(rest))) {
        const char *problem = pattern_problem(lisp_first(rest));
        if (problem) return lisp_expansion_error(state, form, problem);
    }

    const LispSymbols *symbols = lisp_symbols();
    Value *catch_primitive = lisp_primitive_value("catch");
    Value *raise_primitive = lisp_primitive_value("raise");
    Value *problem_name = lisp_generate_symbol();

    LispListBuilder match_form;
    lisp_list_builder_init(&match_form);
    lisp_list_append_take(&match_form, lisp_retain(symbols->match));
    lisp_list_append_take(&match_form, lisp_retain(problem_name));
    for (const Value *rest = clauses; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
        lisp_list_append_take(&match_form, lisp_retain(lisp_first(rest)));
    }
    lisp_list_append_take(&match_form, lisp_retain(symbols->otherwise));
    lisp_list_append_take(&match_form, LISP_LIST(raise_primitive, problem_name));
    Value *match = lisp_list_builder_finish(&match_form);

    Value *no_parameters = LISP_EMPTY;
    Value *body = LISP_LIST(symbols->lambda, no_parameters, element(form, 1));
    Value *parameters = LISP_LIST(problem_name);
    Value *handler = LISP_LIST(symbols->lambda, parameters, match);
    Value *rewritten = LISP_LIST(catch_primitive, body, handler);
    Value *node = expand(state, rewritten, scope, context);

    lisp_release(rewritten);
    lisp_release(handler);
    lisp_release(parameters);
    lisp_release(body);
    lisp_release(match);
    lisp_release(problem_name);
    lisp_release(raise_primitive);
    lisp_release(catch_primitive);
    return node;
}

/* ---- quasiquote (§8.3) ----------------------------------------------------------------------- */

static bool is_marker_form(const Value *form, const Value *marker) {
    return is_form_named(form, marker) && lisp_list_length(form) == 2;
}

/* A LIST_BUILD node over parts (taken). When no part is spliced and every
 * part is a constant, the result is folded into a single constant list. */
static Value *list_build(size_t count, Value **parts, bool *splices) {
    bool all_constant = true;
    for (size_t index = 0; index < count; index++) {
        if (splices[index] || lisp_as_node(parts[index])->kind != LISP_NODE_CONSTANT) all_constant = false;
    }
    if (all_constant) {
        LispListBuilder list;
        lisp_list_builder_init(&list);
        for (size_t index = 0; index < count; index++) {
            lisp_list_append_take(&list, lisp_retain(lisp_as_node(parts[index])->value));
            lisp_release(parts[index]);
        }
        free(parts);
        free(splices);
        Value *datum = lisp_list_builder_finish(&list);
        Value *constant = constant_node(datum);
        lisp_release(datum);
        return constant;
    }
    Value *node = lisp_node_new(LISP_NODE_LIST_BUILD, 0);
    LispNode *builder = mutable_node(node);
    builder->children = parts;
    builder->child_count = count;
    builder->splices = splices;
    return node;
}

static Value *quasi(LispState *state, Value *template, int level, const Scope *scope, Value *context);

/* (marker operand) kept as data at a nested level, with operand already
 * quasi-expanded (taken). */
static Value *rebuild_marker(Value *marker, Value *operand_node) {
    Value **parts = lisp_xmalloc(2 * sizeof(Value *));
    bool *splices = lisp_xcalloc(2, sizeof(bool));
    parts[0] = constant_node(marker);
    parts[1] = operand_node;
    return list_build(2, parts, splices);
}

static Value *quasi_list(LispState *state, Value *template, int level, const Scope *scope, Value *context) {
    size_t count = lisp_list_length(template);
    Value **parts = lisp_xcalloc(count, sizeof(Value *));
    bool *splices = lisp_xcalloc(count, sizeof(bool));
    size_t index = 0;
    for (const Value *rest = template; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest), index++) {
        Value *part = lisp_first(rest);
        if (level == 1 && is_marker_form(part, lisp_symbols()->unquote_splicing)) {
            parts[index] = expand(state, element(part, 1), scope, context);
            splices[index] = true;
        } else {
            parts[index] = quasi(state, part, level, scope, context);
        }
        if (!parts[index]) {
            for (size_t earlier = 0; earlier < index; earlier++) lisp_release(parts[earlier]);
            free(parts);
            free(splices);
            return nullptr;
        }
    }
    return list_build(count, parts, splices);
}

static Value *quasi_map(LispState *state, Value *template, int level, const Scope *scope, Value *context) {
    size_t count = lisp_map_count(template);
    if (count == 0) return constant_node(template);
    Value *node = lisp_node_new(LISP_NODE_MAP_BUILD, 2 * count);
    LispNode *builder = mutable_node(node);
    Value *key, *value;
    size_t index = 0;
    for (size_t cursor = 0; lisp_map_next(template, &cursor, &key, &value); index += 2) {
        if (is_marker_form(key, lisp_symbols()->unquote_splicing)
            || is_marker_form(value, lisp_symbols()->unquote_splicing)) {
            lisp_release(node);
            return lisp_expansion_error(state, template, "unquote-splicing is only valid inside lists");
        }
        builder->children[index] = quasi(state, key, level, scope, context);
        if (builder->children[index]) builder->children[index + 1] = quasi(state, value, level, scope, context);
        if (!builder->children[index] || !builder->children[index + 1]) {
            lisp_release(node);
            return nullptr;
        }
    }
    return node;
}

/* The elements of a quasiquoted set, each quasi-expanded; splicing is only
 * valid inside lists (§8.3). */
static Value *quasi_set(LispState *state, Value *template, int level, const Scope *scope, Value *context) {
    size_t count = lisp_map_count(template);
    if (count == 0) return constant_node(template);
    Value *node = lisp_node_new(LISP_NODE_SET, count);
    LispNode *builder = mutable_node(node);
    Value *element;
    size_t index = 0;
    for (size_t cursor = 0; lisp_set_next(template, &cursor, &element); index++) {
        if (is_marker_form(element, lisp_symbols()->unquote_splicing)) {
            lisp_release(node);
            return lisp_expansion_error(state, template, "unquote-splicing is only valid inside lists");
        }
        builder->children[index] = quasi(state, element, level, scope, context);
        if (!builder->children[index]) {
            lisp_release(node);
            return nullptr;
        }
    }
    return node;
}

/* Only the markers of the outermost level are evaluated (R7RS levels):
 * quasiquote raises the level, unquote and unquote-splicing lower it. */
static Value *quasi(LispState *state, Value *template, int level, const Scope *scope, Value *context) {
    const LispSymbols *symbols = lisp_symbols();
    if (template->kind == LISP_KIND_MAP) return quasi_map(state, template, level, scope, context);
    if (template->kind == LISP_KIND_SET) return quasi_set(state, template, level, scope, context);
    if (template->kind != LISP_KIND_PAIR) return constant_node(template);

    Value *head = lisp_first(template);
    bool is_marker = head == symbols->unquote || head == symbols->unquote_splicing || head == symbols->quasiquote;
    if (!is_marker) return quasi_list(state, template, level, scope, context);
    if (lisp_list_length(template) != 2) {
        return lisp_expansion_error(state, template, "unquote, unquote-splicing and quasiquote take one argument");
    }
    Value *operand = element(template, 1);
    if (head == symbols->quasiquote) {
        Value *inner = quasi(state, operand, level + 1, scope, context);
        return inner ? rebuild_marker(head, inner) : nullptr;
    }
    if (level == 1) {
        if (head == symbols->unquote_splicing) {
            return lisp_expansion_error(state, template, "unquote-splicing is only valid inside lists");
        }
        return expand(state, operand, scope, context);
    }
    Value *inner = quasi(state, operand, level - 1, scope, context);
    return inner ? rebuild_marker(head, inner) : nullptr;
}

/* ---- macros ----------------------------------------------------------------------------------- */

/* The macro transformer a head symbol names in the context, or nullptr. */
static Value *macro_for(LispState *state, const Value *head, const Scope *scope, Value *context) {
    if (head->kind != LISP_KIND_SYMBOL || is_locally_bound(scope, head)) return nullptr;
    LispGlobal *global = lisp_global_find(state, lisp_context_resolve(context, (Value *)head));
    return global && global->is_macro ? global->value : nullptr;
}

/* Calls the transformer with the unevaluated argument datums (§8.2). */
static Value *call_macro(LispState *state, Value *transformer, Value *form) {
    size_t count = lisp_list_length(form) - 1;
    Value *inline_arguments[8];
    Value **arguments = count <= 8 ? inline_arguments : lisp_xmalloc(count * sizeof(Value *));
    size_t index = 0;
    for (const Value *rest = lisp_rest(form); rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
        arguments[index++] = lisp_first(rest);
    }
    Value *expansion = lisp_apply(state, transformer, count, arguments);
    if (arguments != inline_arguments) free(arguments);
    return expansion;
}

bool lisp_expand_macro_call(LispState *state, Value *form, Value *context, Value **expansion) {
    *expansion = nullptr;
    if (form->kind != LISP_KIND_PAIR) return true;
    Value *transformer = macro_for(state, lisp_first(form), nullptr, context);
    if (!transformer) return true;
    *expansion = call_macro(state, transformer, form);
    return *expansion != nullptr;
}

/* ---- dispatch ------------------------------------------------------------------------------------ */

static Value *expand_application(LispState *state, Value *form, const Scope *scope, Value *context) {
    Value *node = lisp_node_new(LISP_NODE_APPLY, lisp_list_length(form));
    if (!expand_into(state, node, 0, form, scope, context)) {
        lisp_release(node);
        return nullptr;
    }
    return node;
}

static Value *expand_map_literal(LispState *state, Value *map, const Scope *scope, Value *context) {
    size_t count = lisp_map_count(map);
    if (count == 0) return constant_node(map);
    Value *node = lisp_node_new(LISP_NODE_MAP, 2 * count);
    LispNode *literal = mutable_node(node);
    Value *key, *value;
    size_t index = 0;
    for (size_t cursor = 0; lisp_map_next(map, &cursor, &key, &value); index += 2) {
        literal->children[index] = expand(state, key, scope, context);
        if (literal->children[index]) literal->children[index + 1] = expand(state, value, scope, context);
        if (!literal->children[index] || !literal->children[index + 1]) {
            lisp_release(node);
            return nullptr;
        }
    }
    return node;
}

/* §8.4: the elements are evaluated left to right, in reading order. */
static Value *expand_set_literal(LispState *state, Value *set, const Scope *scope, Value *context) {
    size_t count = lisp_map_count(set);
    if (count == 0) return constant_node(set);
    Value *node = lisp_node_new(LISP_NODE_SET, count);
    LispNode *literal = mutable_node(node);
    Value *element;
    size_t index = 0;
    for (size_t cursor = 0; lisp_set_next(set, &cursor, &element); index++) {
        literal->children[index] = expand(state, element, scope, context);
        if (!literal->children[index]) {
            lisp_release(node);
            return nullptr;
        }
    }
    return node;
}

static Value *expand_symbol(Value *symbol, const Scope *scope) {
    size_t depth, index;
    if (find_local(scope, symbol, &depth, &index)) {
        Value *node = lisp_node_new(LISP_NODE_LOCAL, 0);
        mutable_node(node)->depth = depth;
        mutable_node(node)->index = index;
        return node;
    }
    Value *node = lisp_node_new(LISP_NODE_GLOBAL, 0);
    mutable_node(node)->value = lisp_retain(symbol);
    return node;
}

static Value *expand_form(LispState *state, Value *form, const Scope *scope, Value *context) {
    if (!lisp_check_stack(state)) return nullptr;
    if (form->kind == LISP_KIND_SYMBOL) return expand_symbol(form, scope);
    if (form->kind == LISP_KIND_MAP) return expand_map_literal(state, form, scope, context);
    if (form->kind == LISP_KIND_SET) return expand_set_literal(state, form, scope, context);
    if (form->kind != LISP_KIND_PAIR) return constant_node(form);

    const LispSymbols *symbols = lisp_symbols();
    Value *head = lisp_first(form);
    if (head == symbols->quote) return expand_quote(state, form);
    if (head == symbols->if_) return expand_if(state, form, scope, context);
    if (head == symbols->lambda) return expand_lambda(state, form, scope, context);
    if (head == symbols->let) return expand_let(state, form, scope, context);
    if (head == symbols->sequence) return expand_sequence(state, form, scope, context);
    if (head == symbols->and_) return expand_and(state, lisp_rest(form), scope, context);
    if (head == symbols->or_) return expand_or(state, lisp_rest(form), scope, context);
    if (head == symbols->match) return expand_match(state, form, scope, context);
    if (head == symbols->try_) return expand_try(state, form, scope, context);
    if (head == symbols->quasiquote) {
        if (lisp_list_length(form) != 2) return lisp_expansion_error(state, form, "quasiquote takes one argument");
        return quasi(state, element(form, 1), 1, scope, context);
    }
    if (head == symbols->unquote || head == symbols->unquote_splicing) {
        return lisp_expansion_error(state, form, "unquote outside quasiquote");
    }
    if (head == symbols->define || head == symbols->define_macro || head == symbols->include
        || head == symbols->import || head == symbols->declare_type) {
        return lisp_expansion_error(state, form, "only valid at top level");
    }

    Value *transformer = macro_for(state, head, scope, context);
    if (transformer) {
        Value *expansion = call_macro(state, transformer, form);
        if (!expansion) return nullptr;
        Value *node = expand(state, expansion, scope, context);
        lisp_release(expansion);
        return node;
    }
    return expand_application(state, form, scope, context);
}

/* ---- positions (§6.3, §8.5) ------------------------------------------------------------------- */

void lisp_note_position(LispState *state, const Value *form) {
    LispPosition position;
    bool is_collection = form->kind == LISP_KIND_PAIR || form->kind == LISP_KIND_MAP || form->kind == LISP_KIND_SET;
    if (is_collection && lisp_position_lookup(form, &position)) {
        state->expansion_position = position;
    }
}

/* Gives the position to the node and to every node under it that has none
 * yet. Nodes that went through expand already have one, so each node is
 * stamped once; the rest were built by the derived forms and quasiquote. */
static void stamp(Value *value, LispPosition position) {
    LispNode *node = mutable_node(value);
    if (position.line == 0 || node->position.line != 0) return;
    node->position = position;
    for (size_t index = 0; index < node->child_count; index++) {
        if (node->children[index]) stamp(node->children[index], position);
    }
}

/* Expands a form inside the position of the nearest enclosing form that has
 * one, or its own. */
static Value *expand(LispState *state, Value *form, const Scope *scope, Value *context) {
    LispPosition enclosing = state->expansion_position;
    lisp_note_position(state, form);
    Value *node = expand_form(state, form, scope, context);
    if (node) {
        stamp(node, state->expansion_position);
    } else {
        lisp_error_locate(state, state->expansion_position);
    }
    state->expansion_position = enclosing;
    return node;
}

Value *lisp_expand(LispState *state, Value *form, Value *context) {
    return expand(state, form, nullptr, context);
}
