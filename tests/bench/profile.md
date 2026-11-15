# Profile of the interpreter, November 2026

This report records where the tree-walking interpreter spends its time on the programs of `tests/bench`, before any optimization. It picks the work of the next phase. The times are in `baseline.txt`.

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
