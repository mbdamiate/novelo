# Profile of the interpreter, November 2026

This report records where the tree-walking interpreter spends its time on the programs of `tests/bench`. The first part is the profile before any optimization, which picked the work of phase 6. The last section, [After phase 6](#after-phase-6), measures the result. `baseline.txt` holds the times after phase 6.

## How it was measured

- Machine: Apple M2, macOS, Apple clang, `-O2 -g`, the `build/novelo` of `make`.
- Each program ran at four times its scale (`calls` at scale 34), long enough for `sample PID 2 1`: two seconds at one sample per millisecond, about 1,500 samples each.
- The tables count the function at the top of the stack. The main thread, which only waits for the interpreter's thread, is left out.
- A second build with `-fno-inline` split the work of `evaluate` into the functions it inlines, and attributed each `lisp_retain` and `lisp_release` to the line that calls it. The numbers in that build are shares of a slower program, so they show where the cost lies, not how large it is.

On Linux, with gcc 14 in Docker on the same machine, `make bench` gives times within 15% of the baseline, faster or slower depending on the program. `perf` needs privileges that a container does not have by default, so the profile comes from macOS alone.

```bash
CFLAGS="-O2 -g -fno-inline -fno-omit-frame-pointer" make build/profile/novelo BUILD=build/profile
echo 200000 | build/novelo tests/bench/match.nvl & sample $! 2 1 -mayDie -file match.txt
```

## Where the time goes

Shares of the interpreter's samples, at `-O2`:

| Program | `evaluate` | `lisp_release` | `malloc`/`free` | map lookups | other |
|---|---:|---:|---:|---:|---:|
| arithmetic | 32% | 32% | 24% | 2% | 10% |
| calls | 37% | 38% | 11% | 2% | 12% |
| closures | 30% | 34% | 24% | 1% | 11% |
| json | 11% | 10% | 6% | 3% | 70% in `count` |
| lists | 34% | 38% | 18% | 2% | 8% |
| maps | 22% | 28% | 29% | 4% | 17%, with `node_with_slot` at 6% and `qsort` at 2% |
| match | 39% | 34% | 13% | 0% | 14%, the primitives `first`, `rest` and `empty?` |
| random | 31% | 30% | 23% | 1% | 15% |
| sort | 34% | 36% | 17% | 1% | 12% |
| text | 36% | 33% | 17% | 7% | 7% |

"Map lookups" counts `lisp_context_resolve`, `trie_find` and `hash_at`. In most programs they come from resolving a global name inside a library.

The time does not go to the primitives or to the data structures. It goes to the bookkeeping of the evaluator. `evaluate` and `lisp_release` take 60–75% of every program except `json`, and allocation takes another 10–30%.

### Evaluating the children of a node costs more than the work itself

Without inlining, `calls` spends:

- 24% in `lisp_release`;
- 20% in `evaluate`;
- 14% in `lisp_retain`;
- 7% in `lisp_check_stack`;
- 5% in `leave_frame`.

Of the calls to `lisp_retain` and `lisp_release`:

- about 60% are the three retains at the entry of `evaluate` (`eval.c:306–308`, the node, the environment and the context) and the three releases at its exit (`eval.c:409–411`);
- the rest are the values themselves.

Every argument and every test of an `if` goes through `lisp_eval`, so each one pays for:

- a full entry into `evaluate`;
- a stack check;
- six reference count updates;
- a frame to leave.

This holds even when the argument is a constant, a local or a global, which is most of them: `(+ total die)` evaluates three leaves this way before it adds two numbers. `lists`, `match` and `sort` show the same split, within two points.

### `lisp_release` is a call each time

`lisp_retain` is `static inline`. `lisp_release` is a function in `value.c`, so each release is a call, even when it only decrements a count that does not reach zero, or when the value is immortal.

### Allocation

`malloc` and `free` take 24% of `arithmetic` and of `closures`, and 29% of `maps`:

- An Integer outside −128 … 1023 is boxed. Every step of an arithmetic loop allocates its results.
- Every call of a closure allocates an environment.
- Every `associate` allocates the path of trie nodes it copies.

All of these are small objects of a few fixed sizes.

### Globals

`lisp_context_resolve` looks up a name in the context's map, which is a trie walk, then in the global hash table. It does both at every evaluation of a global inside a library. That costs 1–2% in most programs, and 7% in `text`, whose library functions call one another in tight loops.

### `json` is quadratic in example 12, not in the interpreter

70% of `json` is `lisp_list_length`, called by `count`. `parse-number` in `examples/12-json/json.nvl` measures the number it read as `(- (count text) (count after))`. That costs a walk over the rest of the document for every number. So the program's time grows with the square of its size: twice the records take three times as long.

The interpreter's `count` is linear on a List, as the spec allows. The fix belongs to the parser: carry the position, or gather the characters of the number as it reads them. Phase 7a, which moves this parser into the library `json`, should fix it.

## What phase 6 should do, in order

Each step is one commit, with `make bench` before and after.

1. **Evaluate leaves without entering `evaluate`.**
   - A constant, a local or a global, as an argument or as the test of an `if`, can be read in place: a retain of the value, and nothing else.
   - This removes the stack check, the six count updates and the frame for most children.
   - It should be the largest gain, on every program.
2. **Borrow instead of retaining on entry.**
   - The caller keeps the node, the environment and the context alive for the whole call.
   - `evaluate` only needs its own reference once a tail call replaces one of them.
   - A flag per slot can say whether `evaluate` owns it, and so whether it must release it.
3. **Make the fast path of `lisp_release` inline.**
   - Do the immortal check and the decrement in the header.
   - Call into `value.c` only when the count reaches zero.
4. **Reuse small objects.**
   - Free lists per size for the sizes that dominate: pairs, boxed Integers, environments, closures and trie nodes.
   - The allocator must still count live objects for the leak checks.
5. **Resolve globals once per node.**
   - Cache in a global node the resolved symbol, or the global's slot, for its context.
   - The cache stays valid because a slot never moves except when the table grows, and growing can bump a generation counter.
   - `define` of a name already defined only changes the value in the slot.
6. **Not yet:**
   - O(1) `count`, which would cost a length in every pair: phase 7a fixes the one quadratic program.
   - The `qsort` that orders a map's entries on its first iteration, at 2% of `maps`.

The first three steps change only `eval.c` and `value.h`. Steps 4 and 5 are larger and should be measured after them, because the share of everything else grows as the first three shrink their part.

## After phase 6

All five steps were kept, each as one commit. Each one gained more than 3% on at least one program. Median seconds of `make bench`:

| Program | Before | Leaves | Borrow | Inline release | Free lists | Global entries | Change |
|---|---:|---:|---:|---:|---:|---:|---:|
| arithmetic | 0.594 | 0.483 | 0.448 | 0.423 | 0.288 | 0.281 | −53% |
| calls | 0.687 | 0.543 | 0.506 | 0.453 | 0.389 | 0.385 | −44% |
| closures | 0.895 | 0.724 | 0.697 | 0.644 | 0.461 | 0.447 | −50% |
| json | 1.279 | 1.184 | 1.163 | 1.140 | 1.028 | 0.979 | −24% |
| lists | 0.813 | 0.637 | 0.601 | 0.551 | 0.447 | 0.433 | −47% |
| maps | 0.791 | 0.680 | 0.664 | 0.616 | 0.499 | 0.478 | −40% |
| match | 1.402 | 1.038 | 0.895 | 0.826 | 0.651 | 0.664 | −53% |
| random | 0.698 | 0.550 | 0.514 | 0.474 | 0.355 | 0.350 | −50% |
| sort | 1.053 | 0.805 | 0.753 | 0.701 | 0.538 | 0.523 | −50% |
| text | 1.417 | 1.135 | 1.085 | 1.006 | 0.793 | 0.690 | −51% |

Peak memory did not change. The free lists keep at most 4,096 blocks of each size, which is under 2 MB in all.

Each step did the following:
- **Leaves:** `lisp_eval` reads a constant, a local or a global in place, without entering `evaluate`.
- **Borrow:** `evaluate` takes its own reference only to a node, environment or context that replaces the one it was given. The branch of an `if`, or the last expression of a `sequence`, stays borrowed while the node it comes from is.
- **Inline release:** `lisp_release` is `static inline`, and enters `value.c` only when a count reaches zero.
- **Free lists:** `lisp_object_new` reuses freed blocks of up to 64 bytes, by size. They are off under AddressSanitizer, so a use after free is still caught there.
- **Global entries:** a global node keeps the entry its name resolved to, for its context and the table's generation.

The profile at `-O2` after the five steps, taken the same way as before:

| Program | `evaluate` | `lisp_eval` | releasing | `malloc`/`free` | other hot spots |
|---|---:|---:|---:|---:|---|
| arithmetic | 35% | 16% | 9% | 0% | `global_value` 13%, `expect_numbers` 8% |
| calls | 31% | 16% | 5% | 0% | `expect_numbers` 14%, `global_value` 9%, `<` 7% |
| closures | 28% | 14% | 13% | 5% | `global_value` 8%, `bind_arguments` 6% |
| lists | 35% | 15% | 9% | 10% | `global_value` 7%, `bind_arguments` 5% |
| maps | 15% | 13% | 4% | 19% | `node_with_slot` 14%, `drop_trie_node` 6%, `qsort` 4% |
| match | 49% | 21% | 7% | 0% | `first`, `bind_arguments`, environments, 3% each |
| random | 33% | 14% | 9% | 4% | `global_value` 7%, `node_with_slot` 6% |
| sort | 35% | 18% | 8% | 1% | `global_value` 9%, `bind_arguments` 6% |
| text | 35% | 16% | 9% | 3% | `global_value` 8% |

"Releasing" counts `lisp_destroy_unreferenced`, `destroy` and `free_object`. At four times its scale, `json` spends 92% in `count`.

### What is left

- **The loop itself.** `evaluate` and `lisp_eval` now take half of most programs. This is the cost of walking a tree of nodes, and the reason for a bytecode compiler, which the roadmap leaves out.
- **`global_value`** still takes 7–13%. That is now the macro check, the retain and the cache comparison, not the lookup. A primitive such as `+`, called by name, could be bound in its node when the name can never be redefined, but the spec allows a program to redefine any global (§8.1), so this would need a rule first.
- **Maps.** `associate` copies trie nodes, each with two arrays of its own: 14% in `node_with_slot` and most of the remaining `malloc` in `maps`. Allocating a node and its arrays as one block would halve the allocations. The `qsort` of the first iteration is 4%.
- **`expect_numbers`** checks the types of arithmetic arguments one at a time. It is 14% of `calls`, a candidate if numbers matter more than they do in the examples.
- **`json`** is still quadratic. Phase 7a fixes it in the library.

None of these is needed by any example now, so phase 6 stops here.
