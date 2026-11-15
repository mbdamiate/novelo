/* core §8.5 — errors as return values.
 *
 * A function that fails returns nullptr (or false) and leaves a descriptor
 * {:kind :message :values} pending in the state, which owns it. There is no
 * longjmp, so every release on the way out still happens.
 */
#pragma once

#include "buffer.h"
#include "positions.h"
#include "value.h"

/* Sets the pending error {:kind kind :message … :values values} and returns
 * nullptr. `kind` is the keyword name without ':' (e.g. "wrong-type");
 * `values` is a List and is taken (pass LISP_EMPTY for none). */
[[gnu::format(printf, 4, 5)]]
Value *lisp_error(LispState *state, const char *kind, Value *values, const char *format, ...);

/* Sets a pending error from an existing descriptor (raise), taking it. */
Value *lisp_error_from_descriptor(LispState *state, Value *descriptor);

/* Hands the pending descriptor to the caller and clears it. */
[[nodiscard]] Value *lisp_take_error(LispState *state);

/* The :kind of the pending error, or nullptr. Borrowed. */
Value *lisp_error_kind(const LispState *state);

/* §8.5 — adds :position {:line :column :file} to the pending descriptor as
 * the error leaves code read at `position`, unless the descriptor already
 * has one or the position is unknown (line 0). */
void lisp_error_locate(LispState *state, LispPosition position);

/* §8.5 — the most frames :trace keeps; the others are counted in
 * :trace-omitted. */
constexpr size_t LISP_TRACE_LIMIT = 32;

/* §8.5 — adds a frame to the end of the pending error's :trace, as the error
 * leaves a call of the function `name` (a Symbol) made at `position` (line
 * 0: by a primitive, with no :position); past LISP_TRACE_LIMIT frames it
 * counts the frame in :trace-omitted instead. Nothing happens when no
 * error is pending or the descriptor's :trace is not a List. */
void lisp_error_add_frame(LispState *state, Value *name, LispPosition position);

/* Appends "kind: message values…" for a descriptor, for messages on stderr. */
void lisp_describe_error(LispBuffer *output, const Value *descriptor);

/* §8.7 — the message for an uncaught error: "file:line:column: error
 * kind: message values…", or "line:column: error …" without a file, or
 * "error …" without a position; then one line per frame of :trace,
 * "  in f, called at file:line:column", and "  ... and N more" for
 * :trace-omitted. */
void lisp_describe_uncaught_error(LispBuffer *output, const Value *descriptor);

/* A new List of the given values (borrowed). */
[[nodiscard]] Value *lisp_list_of(size_t count, Value *const *items);
#define LISP_LIST(...) \
    lisp_list_of(sizeof((Value *[]){__VA_ARGS__}) / sizeof(Value *), (Value *[]){__VA_ARGS__})
