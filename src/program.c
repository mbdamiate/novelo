/* core §8.7 — the interpreter state and the top level. */
#include "program.h"

#include "error.h"
#include "eval.h"
#include "expander.h"
#include "include.h"
#include "library.h"
#include "primitives.h"

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

constexpr size_t DEFAULT_STACK_LIMIT = 4u << 20;     /* fits a default 8 MB thread */
constexpr size_t LARGE_STACK_SIZE = 512u << 20;
constexpr size_t LARGE_STACK_MARGIN = 16u << 20;

/* ---- state -------------------------------------------------------------------------- */

LispState *lisp_state_new(const LispStateOptions *options) {
    LispState *state = lisp_xcalloc(1, sizeof(LispState));
    state->output = options->output;
    state->input = options->input.read_line ? options->input : lisp_input_empty();
    state->stack_limit = options->stack_limit ? options->stack_limit : DEFAULT_STACK_LIMIT;
    state->files = options->files.read_file ? options->files : lisp_file_system_disk();
    char current_directory[4096];
    const char *directory = options->working_directory;
    if (!directory) directory = getcwd(current_directory, sizeof current_directory) ? current_directory : "/";
    state->working_directory = lisp_string_cstr(directory);
    state->loading = LISP_EMPTY;
    state->included = lisp_map_new(0);
    state->contexts = lisp_map_new(0);
    state->imported = lisp_map_new(0);
    lisp_buffer_init(&state->line);
    lisp_install_primitives(state);
    return state;
}

void lisp_state_free(LispState *state) {
    if (!state) return;
    lisp_release(state->error);
    lisp_globals_free(&state->globals);
    lisp_release(state->working_directory);
    lisp_release(state->loading);
    lisp_release(state->included);
    lisp_release(state->contexts);
    lisp_release(state->imported);
    lisp_buffer_free(&state->line);
    free(state);
}

/* The stack is measured from the highest frame that entered the interpreter. */
static void note_stack_base(LispState *state) {
    const char *frame = __builtin_frame_address(0);
    if (!state->stack_base || frame > state->stack_base) state->stack_base = frame;
}

/* ---- top level (§8.7) ------------------------------------------------------------------ */

static Value *process(LispState *state, Value *form, Value *context);

/* (define name value) and (define-macro name transformer). */
static Value *process_definition(LispState *state, Value *form, Value *context, bool is_macro) {
    if (lisp_list_length(form) != 3 || lisp_first(lisp_rest(form))->kind != LISP_KIND_SYMBOL) {
        return lisp_expansion_error(state, form, is_macro ? "define-macro must be (define-macro name transformer)"
                                                          : "define must be (define name value)");
    }
    Value *name = lisp_first(lisp_rest(form));
    Value *node = lisp_expand(state, lisp_first(lisp_rest(lisp_rest(form))), context);
    if (!node) return nullptr;
    Value *value = lisp_eval(state, node, nullptr, context);
    lisp_release(node);
    if (!value) return nullptr;
    if (is_macro && !lisp_is_function(value)) {
        lisp_release(value);
        return lisp_error(state, "wrong-type", LISP_LIST(name), "define-macro needs a function as transformer");
    }
    lisp_context_define(state, context, name, value, is_macro);
    lisp_release(value);
    return LISP_VOID;
}

/* (import name) or (import name prefix) (§8.9). */
static Value *process_import(LispState *state, Value *form, Value *context) {
    size_t length = lisp_list_length(form);
    Value *name = length >= 2 ? lisp_first(lisp_rest(form)) : nullptr;
    Value *prefix = length == 3 ? lisp_first(lisp_rest(lisp_rest(form))) : nullptr;
    bool valid = (length == 2 || length == 3) && name->kind == LISP_KIND_SYMBOL
                 && (!prefix || prefix->kind == LISP_KIND_SYMBOL);
    if (!valid) return lisp_expansion_error(state, form, "import must be (import name) or (import name prefix)");
    return lisp_import(state, name, prefix, context);
}

/* A top-level sequence is spliced: each form is itself at top level. */
static Value *process_sequence(LispState *state, Value *form, Value *context) {
    if (lisp_rest(form) == LISP_EMPTY) return lisp_expansion_error(state, form, "sequence needs at least one form");
    Value *last = nullptr;
    for (const Value *rest = lisp_rest(form); rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
        lisp_release(last);
        last = process(state, lisp_first(rest), context);
        if (!last) return nullptr;
    }
    return last;
}

static Value *process(LispState *state, Value *form, Value *context) {
    lisp_retain(form);
    Value *result = nullptr;
    const LispSymbols *symbols = lisp_symbols();
    for (;;) {
        if (form->kind == LISP_KIND_PAIR) {
            Value *head = lisp_first(form);
            if (head == symbols->sequence) {
                result = process_sequence(state, form, context);
                break;
            }
            if (head == symbols->define || head == symbols->define_macro) {
                result = process_definition(state, form, context, head == symbols->define_macro);
                break;
            }
            if (head == symbols->import) {
                result = process_import(state, form, context);
                break;
            }
            if (head == symbols->include) {
                result = lisp_include(state, form, context);
                break;
            }
            if (head == symbols->declare_type) { /* reserved for typing: ignored (§8.2) */
                result = LISP_VOID;
                break;
            }
            /* A macro may produce any of the forms above, so expand it here. */
            Value *expansion;
            if (!lisp_expand_macro_call(state, form, context, &expansion)) break;
            if (expansion) {
                lisp_release(form);
                form = expansion;
                continue;
            }
        }
        Value *node = lisp_expand(state, form, context);
        if (node) {
            result = lisp_eval(state, node, nullptr, context);
            lisp_release(node);
        }
        break;
    }
    lisp_release(form);
    return result;
}

Value *lisp_process_top_level(LispState *state, Value *form, Value *context) {
    note_stack_base(state);
    return process(state, form, context);
}

LispRunResult lisp_run_source(LispState *state, const char *source, size_t length, const char *path,
                              Value **last_value, LispReadError *read_error) {
    note_stack_base(state);
    Value *forms;
    if (!lisp_read(source, length, &forms, read_error)) return LISP_RUN_READ_ERROR;
    /* The program is a file still being loaded while it runs (§8.8, cycles). */
    if (path) {
        Value *absolute = lisp_path_resolve(state->working_directory, path);
        lisp_loading_push(state, absolute);
        lisp_release(absolute);
    }
    Value *last = LISP_VOID;
    for (const Value *rest = forms; rest->kind == LISP_KIND_PAIR; rest = lisp_rest(rest)) {
        lisp_release(last);
        last = process(state, lisp_first(rest), nullptr);
        if (!last) break;
    }
    if (path) lisp_loading_pop(state);
    lisp_release(forms);
    if (!last) return LISP_RUN_ERROR;
    if (last_value) {
        *last_value = last;
    } else {
        lisp_release(last);
    }
    return LISP_RUN_OK;
}

/* ---- large stack ------------------------------------------------------------------------- */

typedef struct ThreadCall {
    void (*function)(void *argument);
    void *argument;
} ThreadCall;

static void *thread_main(void *pointer) {
    ThreadCall *call = pointer;
    call->function(call->argument);
    return nullptr;
}

bool lisp_run_on_large_stack(void (*function)(void *argument), void *argument) {
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return false;
    pthread_attr_setstacksize(&attributes, LARGE_STACK_SIZE);
    ThreadCall call = {function, argument};
    pthread_t thread;
    int failed = pthread_create(&thread, &attributes, thread_main, &call);
    pthread_attr_destroy(&attributes);
    if (failed) return false;
    pthread_join(thread, nullptr);
    return true;
}

size_t lisp_large_stack_limit(void) { return LARGE_STACK_SIZE - LARGE_STACK_MARGIN; }
