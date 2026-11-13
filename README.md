# Novelo

An interpreter, written in C23, for **Novelo**: a small, immutable Lisp. It implements the core (`core-specification.md`, v0.11.0) and the standard library (`standard-library.md`, v0.7.1).

*Novelo* is Portuguese for a ball of yarn: a single thread, unwound from its first element to the rest, as Novelo's lists are. Source files end in `.nvl`.

Every case of both conformance suites passes:

| Suite | Cases |
| --- | --- |
| core | 517 |
| stdlib | 98 |

Those runs report no leaks, and they also pass under AddressSanitizer and UndefinedBehaviorSanitizer.

```lisp
(import io)
(import list)
(import text t)

(define square (lambda (x) (* x x)))
(display! (t/join ", " (map to-string (map square (range 1 6)))))
; prints 1, 4, 9, 16, 25
```

## Building

It needs a C23 compiler, plus `make`, a POSIX libc and pthreads. There are no other dependencies. It is tested with Apple clang 21 on macOS and with gcc 14 on Linux (glibc), where every test also passes under ASan, UBSan and LeakSanitizer. With gcc, pass the compiler to make:

```bash
make CC=gcc test
```

```bash
make            # builds build/novelo
make test       # unit tests + both conformance suites + the examples + the command line
make asan       # the same, rebuilt in build/asan with ASan + UBSan
make coverage   # the same, rebuilt in build/coverage with clang's coverage, then a report
make stress     # the programs of tests/stress at full size, about two minutes
make asan-stress # the same at a hundredth of the size, under ASan + UBSan
make fuzz       # mutated programs under ASan + UBSan, about a minute
make clean
```

## Running

```bash
build/novelo program.nvl   # run a program
build/novelo               # start the REPL
```

| Exit code | Meaning |
| --- | --- |
| 0 | every form was evaluated (or the REPL reached the end of its input) |
| 1 | a read error or an uncaught error |
| 2 | bad arguments, or the program file cannot be read |

Messages go to standard error, after whatever the program had already printed:

```
program.nvl:3:1: read error :odd-map: map with an odd number of datums
program.nvl:7:5: error :unbound-symbol: unbound symbol car car
```

An uncaught error names the innermost form, read from a file, that contains the failing expression (core §8.5). A `catch` handler finds the same position in the descriptor, under `:position`:

```lisp
(catch (lambda () (quotient 1 0))
  (lambda (problem) (get problem :position)))   ; → {:line 1 :column 19 :file "program.nvl"}
```

**Programs** see only the core primitives until they import a library (core §8.9):

```lisp
(import io)            ; write!, display!, read-line!
(import list)          ; map, filter, fold, sort, ...
(import text t)        ; t/split, t/join, t/format, t/pad-left, t/lowercase, ... (and the names of list, as t/map)
(import function)      ; partial
(import map)           ; values, entries, merge, update, map-values, ...
(import set)           ; from-list, union, intersection, difference, subset?
(import number)        ; max, min, abs, floor, ceiling, round
(include "lib/geometry.nvl" geo)   ; geo/area, ...
```

**The REPL** starts with all seven libraries imported. An input may span several lines: while a list, map or string is still open, the REPL keeps reading. Each value except `#void` is printed with the `write!` rules. After an error the REPL reports it and reads the next input. The `> ` prompt appears only when standard input is a terminal, so piping a file through the REPL prints only the values.

**The standard library** is not compiled in. On each `import`, `list`, `text`, `function`, `map`, `set` and `number` are read from the `lisp` code blocks of `standard-library.md` §4.1–4.6, so the code that runs is the normative code. The build fixes the absolute path of that document. To use another copy, set `NOVELO_STDLIB`:

```bash
NOVELO_STDLIB=/usr/local/share/novelo/standard-library.md build/novelo program.nvl
```

## Tests

```bash
make unit
make conformance
make examples
make cli
make coverage
./build/unit-tests -k reader/strings --no-fork      # one test, in-process for a debugger
./build/conformance -v -k include conformance.json  # list the matching cases
```

- **Unit runner (`tests/unit`):**
  - Runs each test in its own process, so a crash counts as a failure.
  - Checks that the count of live objects is the same before and after each test.
- **Conformance runner (`tests/conformance`):**
  - Has its own JSON parser.
  - Compares results with `equal?` and errors by their `:kind`.
  - Matches the exact output.
  - Runs each case with a fresh state on a large stack. A case's `files` sit in an in-memory file system around `/main.nvl`.
  - After each case, no object and no position entry may remain alive.
- **Command line (`tests/cli.sh`):** runs `build/novelo` in a temporary directory and checks the exit code, standard output and standard error of each case:
  - the exit codes 0, 1 and 2;
  - bad arguments and missing files;
  - read errors and uncaught errors, with and without a position, after the program's own output;
  - descriptors built by `raise` that lack fields;
  - errors in included files;
  - `NOVELO_STDLIB`;
  - the REPL with standard input that is not a terminal.
- **Examples (`examples/`):** numbered by complexity, each adding one idea, starting at `01-hello`. Each example is a directory: `main.nvl` is a complete program, commented, run by `build/novelo` through `tests/examples.sh`. It must exit with 0 and print exactly `output.txt`; `input.txt`, when present, is its standard input. Any other file is part of the example, such as the files `08-modules` includes.

### Stress

`make stress` runs the programs of `tests/stress` with `build/stress`. Each program runs in its own process. The run passes only if:
- the program prints exactly `ok`, which it does only after checking its own results;
- it ends without an uncaught error;
- once its state is freed, no object or position entry is left alive.

The runner prints the time and the peak memory (`ru_maxrss`) of each one. Comments at the top of a program set how it runs:

| Comment | Effect |
| --- | --- |
| `; scale: N` | standard input is the line `N`, the size the program reads |
| `; memory: constant` | it also runs with `10 × N`, and its peak memory may grow by at most 25% plus 16 MB |
| `; include-chain: K` | an in-memory chain of `K` files, each defining a name and including the next |

The programs cover:
- **tail loops of ten million steps**, in constant memory:
  - a plain `if`;
  - `match`;
  - a Float accumulator;
  - mutual recursion;
  - `catch` at every step;
  - calls from `let`, `sequence`, `and`, `or` and `match`;
  - errors re-raised through ten catches;
  - text formatting;
- **volume:**
  - lists of a million elements;
  - maps and sets of half a million, emptied again;
  - `sort`;
  - a text of a million characters;
  - a million closures, and a chain of a million;
- **depth:**
  - a list nested a million levels deep, compared, hashed, printed and freed;
  - `:stack-overflow` caught and repeated, with memory stable;
  - macro expansions a hundred thousand levels deep;
  - the reader's nesting limit;
  - a chain of 500 includes.

`make asan-stress` runs them under ASan + UBSan at a hundredth of the size, at least 1, without the memory check.

### Fuzzing

`make fuzz` builds `build/fuzz/fuzz` with ASan + UBSan and feeds the interpreter mutated programs. Neither Apple clang nor gcc ships libFuzzer, so it is a small mutation fuzzer of its own, with no dependencies, and it is deterministic: an input depends only on `SEED` and its number. `make fuzz ITERATIONS=20000 SEED=1` are the defaults; another seed explores other inputs.

The seeds are every `program` of both conformance files, with its `files` and standard input, the `input` of every read case, and the examples. The tokens to splice come from their code, leaving out comments and strings. The mutations are:
- flipping, inserting and deleting bytes;
- deleting or duplicating a delimiter;
- replacing a token with another of the same input, with a name another seed calls, or with any token of another seed;
- replacing an argument with a value of another type, such as `""`, `'()`, `{:kind :custom}`, `#{}`, `#void` or a lambda, so that calls reach their error paths;
- replacing a number with an edge value, such as `9223372036854775808`, `-0.0` or `5.0e-324`;
- inserting invalid UTF-8, `\u{…}` escapes and bare special forms;
- copying a stretch elsewhere, and wrapping one in a call or a collection.

There are two modes:
- **Reader**, 10 × `ITERATIONS` inputs through `lisp_read` alone, in batches of 500 per process. A read error must be at a line and column inside the input. Every datum read must print as text that reads back `equal?` and prints the same again.
- **Eval**, `ITERATIONS` programs, one process each, on the command line's 512 MB stack with a 32 MB limit for evaluation. Inputs that do not read are drawn again, up to 8 times, so most of them expand and run. A program may finish, fail to read, or raise an uncaught error whose descriptor is a Map with a Keyword `:kind`. A run longer than 2 seconds is counted but not failed, because a mutated bound easily makes a loop that never ends. In the default run, about 22% of the programs finish and 76% end in an uncaught error, a third of them an unbound name.

In both modes, every object and position entry must be freed afterwards. Anything else fails: a signal, a sanitizer report, a broken check. The input is saved as `build/fuzz/crashes/MODE-INDEX.nvl`, with the process's standard error beside it; past 20 failures they are only counted. `build/fuzz/fuzz --only eval:INDEX conformance.json stdlib-conformance.json examples` prints that input and its outcome, running it in the fuzzer's own process, for a debugger.

What it found:
- In the interpreter, nothing: the default run of 220,000 inputs passes, and so do seeds 2 to 4 at 40,000 programs each.
- Planted bugs show what it catches:
  - A leak on the reader's `:duplicate-element` path failed 6 reader inputs.
  - An extra release of a borrowed String in `substring`'s out-of-range error was caught as a use-after-free, once in the 20,000 programs. The path is rare in the seeds.
- What it does not catch is a wrong value. An off-by-one that let `substring` end one past the text returned `"bc\u{0}"` for `(substring "abc" 1 4)`. It read the NUL terminator, inside the allocation, so nothing failed. Values are what the conformance cases check.

### Coverage

`make coverage` builds everything in `build/coverage` with `-fprofile-instr-generate -fcoverage-mapping`, runs `make test` there, and merges the profile of every process. The unit runner forks once per test, so each process writes its own file. It then prints `llvm-cov report` for `src/` and writes the annotated sources to `build/coverage/html/index.html`. It needs clang with `llvm-cov` and `llvm-profdata`, which come with Xcode and are found through `xcrun`. Elsewhere, put them on the `PATH` or pass `LLVM_COV=…` and `LLVM_PROFDATA=…`. With gcc there is no coverage target.

| | Lines | Branches | Functions |
| --- | --- | --- | --- |
| before the measurement | 95.9% | 89.3% | 98.0% |
| now | 99.5% | 93.5% | 99.7% |

The 22 lines left are unreachable:
- running out of memory (`buffer.c`);
- failing to create the interpreter's thread (`main.c`);
- the `abort()` after the search for the shortest digits, which always ends by 17 digits;
- trailing zeros removed from the shortest digits, which would only appear if the neighbor above carried into a power of ten; no binary64 tried has needed it, and it costs nothing to keep;
- the `return` after a switch that covers every case;
- a REPL input still open at the end that reads without error, which cannot happen because complete inputs are evaluated as they arrive;
- `lisp_primitive_value` with an unknown name, which the expander never asks for;
- closing braces that `llvm-cov` counts as lines.

Most of the branches left are null checks and the other side of `assert`s.

## Source map

| File | Spec | Contents |
| --- | --- | --- |
| `src/value.h/.c` | core §2, §8.6 | objects, reference counting, immortal values, interning, `equal?`, hashing, persistent maps and sets (a hash array mapped trie) |
| `src/buffer.h/.c` | | growable byte buffer, allocation helpers |
| `src/utf8.h/.c` | core §3.2 | strict UTF-8 decoding and encoding |
| `src/reader.h/.c` | core §3–7 | tokens, literals, escapes, collections, nesting limit, read errors in §7.3 order |
| `src/positions.h/.c` | core §6, §8.5 | side table: node address → line, column and file; the names of the files read |
| `src/printer.h/.c` | core §10 | `write!` and `display!` text |
| `src/error.h/.c` | core §8.5, §8.7 | the pending error `{:kind :message :values}`, its `:position`, the messages for stderr |
| `src/node.h`, `src/expander.h/.c` | core §8.2–8.3 | special forms, derived forms, quasiquote, macros, lexical addresses |
| `src/eval.h/.c` | core §8.1, 8.2, 8.4 | environments, the evaluation loop with tail calls, application, globals, stack guard |
| `src/primitives.h/.c` | core §9 | the primitive table |
| `src/program.h/.c` | core §8.7 | interpreter state, top level, running a source, large-stack thread |
| `src/include.h/.c` | core §8.8 | contexts and prefixes, path resolution, load once, cycles |
| `src/library.h/.c` | core §8.9, stdlib §4 | library registry, `import`, reading the libraries from the document |
| `src/library_io.c` | core §8.9, §9 | the native library `io` |
| `src/io.h/.c` | | interfaces for output, input and the file system (disk and memory) |
| `src/repl.h/.c` | core §8.7 | the REPL |
| `src/main.c` | core §8.7 | the command line |

## Design

**The pipeline.**

1. The reader turns the whole source into datums plus a position table.
2. Each top-level form is then expanded and evaluated before the next one, so a macro applies to the forms that follow it.
3. The expander resolves local variables to `(depth, index)` addresses.
4. It also reduces the derived forms (`let`, `sequence`, `and`, `or`, `quasiquote`, `match`) and macro calls to a few node kinds. `match` is rewritten into a `let` of the subject and a chain of `if`s whose tests call primitive values embedded in the code, so redefining `first` or `get` cannot change what a pattern means.
5. Globals are looked up when a call happens, so the order of definitions does not matter.

**Memory: reference counting, no tracing collector.**

- The heap cannot form cycles: values are immutable, and recursion goes through global names.
- Release is iterative, so dropping a list of a million elements does not recurse in C.
- Some values are immortal and are never counted: `#true`, `#false`, `#void`, `()`, the empty map, small integers, and interned symbols and keywords.
- Ownership rule: a function returning a `Value *` returns a new reference. Its arguments are borrowed, unless its name ends in `_take`.

**Errors are return values.** A failing call returns `nullptr` and leaves a descriptor pending in the state. `catch` takes that descriptor. There is no `longjmp`, so every release on the way out still happens. Read errors are a separate struct and never reach `catch`.

**Tail calls and deep recursion.**

- The evaluator is a loop. A call in tail position replaces the current node and environment, so a tail-recursive loop runs in constant memory. A loop of 10 million iterations stays at about 1.8 MB resident.
- Non-tail recursion uses the C stack. The CLI and the conformance runner run on a thread with a 512 MB stack.
- A guard raises the catchable `:stack-overflow` before that stack runs out.

**Maps and sets are persistent** (core §8.6).

- A map is a hash array mapped trie: 32-way nodes with bitmaps, 5 bits of the 64-bit hash per level, and collision nodes below the last level.
- `associate` and `dissociate` copy only the path to the changed slot, at most 13 nodes, and share the rest. A map of a million keys builds in about 1.7 s and 90 MB.
- Trie nodes are internal reference-counted objects, so a large map is released through the same iterative work stack as a long list.
- A Set (`#{…}`) is the same structure with its own type tag and every value `#true`, so it is persistent in the same way and never `equal?` to a map.
- Each entry keeps the sequence number of its insertion. The first iteration over a map sorts the entries by it once and keeps that order with the map.

**Floats** (core §2.5, §10) are boxed binary64 values, like large integers, and never infinite or NaN: every operation checks `isfinite` and raises `:float-overflow` instead.

- Reading checks the grammar of §4.2 first and then calls `strtod`, which rounds correctly on glibc and macOS. The interpreter never calls `setlocale`, so the decimal point is always `.`.
- Printing finds the shortest digits that read back. For each length from 1 to 17, `printf("%.*e")` gives the closest digits of that length, and `strtod` checks whether they read back. Next to a power of two, the closest digits can miss while their neighbor of the same length reads back, so both neighbors are tried too.
- `float-to-fixed` asks `printf("%.*f")` for every digit of the exact value: a Float with k bits after the binary point has exactly k decimal digits after the point, at most 1074. Since nothing is left to round, `printf` cannot round, and the rounding to the requested decimals, ties away from zero, is done on the digit text. glibc and macOS both print the exact digits, but they could break ties differently if asked to round.
- `sqrt` is the only libm function used, because IEEE 754 requires it to be correctly rounded, so every platform prints the same digits.

**Positions** live in a side table keyed by node address. Freeing a node removes its entry, so a reused address never inherits a stale position (core §6.4). Each entry also names its file, as an index into a list of file names that the reader fills.

**Positions in errors.**

- The expander copies the position of each form it expands into the node it builds. Nodes without one of their own, built for derived forms, quasiquote or the output of a macro, take the position of the nearest enclosing form.
- When a node fails, the evaluator adds `:position` to the pending descriptor, unless it already has one. The innermost failing node does this first, so the position is where the error happened.
- Expansion errors and errors of top-level forms such as `include` are located the same way.

## Implementation-defined behavior

Core 0.2.1 settled the points that versions up to 0.2.0 left open, such as `display!` of strings inside collections, `:stack-overflow` and `:io-error`. They are now in the specification and in the conformance cases. What the specification still leaves to the implementation, this interpreter does as follows:

- **Order of maps and sets.** The specification leaves the iteration order of maps and sets unspecified (core §2.3). Here it is the order of insertion: `keys`, `elements`, printing and the functions of `map` and `set` see the keys and elements in the order they were first added, and replacing a value keeps its key's place. Literals are read in the order they are written.
- **`:file` in error positions.** For the program, it is the path as given on the command line. For an included file, it is the absolute path. For a library written in the language, it is the path of the standard library document, and the line is its line in that document. In the REPL there is no `:file`, and lines count from the start of each input.

- **Call depth.** The specification requires at least 10,000 nested non-tail calls. Here the limit is the 512 MB stack of the CLI and the conformance runner, minus a 16 MB margin, which allows well over 100,000 nested calls. Beyond it, `:stack-overflow` is raised.
- **Libraries that cannot be loaded.** When the standard library document cannot be read, or has no code for the library, `import` gives `:import-error` with `:values` `(name :cannot-open)`. A read error in the document gives `(name code line column)`, with the line and column in the document.
