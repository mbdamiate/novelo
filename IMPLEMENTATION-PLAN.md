# Implementation plan: Novelo in C23

## Goal
An interpreter for the Novelo core (v0.2.2) and standard library (v0.2.1), written in C23. Its only inputs are the specification and the conformance suites:

- `core-specification.md`
- `standard-library.md`
- `conformance.json`
- `stdlib-conformance.json`

The code aims for clarity and fidelity to the spec. Performance only needs to be good enough. Each file cites the spec sections it implements, e.g. `/* core §8.3 */`.

## Decisions
- **Language:**
  - C23 (`-std=c23`).
  - The code uses `<stdckdint.h>`, `nullptr`, `constexpr`, `bool` and `[[nodiscard]]`.
- **Dependencies:** none beyond libc and pthreads. There is no third-party code, not even in the tests.
- **Build:** `make` with any C23 compiler, for example clang 18+ or gcc 14+. Warnings are errors (`-Wall -Wextra -Wpedantic -Werror`).
- **Memory:** reference counting. The heap is acyclic because values are immutable, and recursion goes through global names resolved at call time. Counting references is therefore enough to free everything, and no tracing collector is needed.
- **Errors:** propagated as return values. A function that fails returns `nullptr` (or `false`) and leaves the error pending in the interpreter state. There is no `setjmp`/`longjmp`, because a jump would skip the releases and leak.
- **Tests:**
  - A unit test runner, written in C.
  - A runner for both conformance suites, also in C, with its own minimal JSON parser.

## Layout
```
Makefile            all, test, unit, conformance, asan, clean
README.md           building and running, file → section map, memory and error model
src/
  value.h/.c        §2  objects, refcount, singletons, interning, equal?, hash, type names
  buffer.h/.c           growable byte buffer
  utf8.h/.c         §3.2 decoding, with the position of the first invalid byte
  reader.h/.c       §3–7 tokens, classification, escapes, collections, nesting limit, errors
  positions.h/.c    §6  side table node → (line, column)
  printer.h/.c      §10 write! and display!
  error.h/.c        §8.5 pending error {:kind :message :values}
  node.h            expanded nodes: Quote, If, Lambda, Apply, MapLit, Ref, Quasi (+ templates)
  expander.c        §8.2–8.3 special forms, derived forms, macros, declare
  eval.h/.c         §8.1, 8.2, 8.4 lexical environments, tail loop, application
  primitives.c      §9  the primitive table
  include.c         §8.8–8.9 contexts, prefixes, load once, cycles; include and import
  library.h/.c      §8.9 library registry: native (io) and written in the language (list, text, function)
  library_io.c      §8.9, §9 the native library io: write!, display!, read-line!
  io.h/.c               output, stdin and file system interfaces
  program.h/.c      §8.7 interpreter state, top level
  repl.c            §8.7 REPL
  main.c                CLI
tests/
  unit/test.h test.c    unit test framework and runner
  unit/*_test.c         one file per module
  conformance/json.h json.c conformance.c
```

## Values and memory (`value.h`)
- **Representation:** every value is a `Value *` pointing to an object whose header is `{ uint32_t refs; uint8_t type; uint8_t flags; }`.
- **Immortal objects:**
  - Immortal objects are `#true`, `#false`, `#void`, `()` and the interned symbols and keywords.
  - They carry an `IMMORTAL` flag, and retain and release skip them.
- **Types:**
  - **Integer:** a boxed `int64_t`, with an immortal cache for small values.
  - **String:**
    - Stored as UTF-8 bytes plus the code point count.
    - An `ASCII` flag makes indexing by code point O(1). Otherwise indexing scans the string.
  - **Symbol and Keyword:**
    - Interned in a hash table.
    - Symbols made by `generate-symbol` are not interned, and they are counted like any other object.
  - **Pair:** `{first, rest}`, so prepending is O(1).
  - **Map:**
    - A compact hash table: entries in insertion order plus an open-addressing index, with a structural hash and keys compared by `equal?`.
    - Insertion order keeps the reading order of map literals, which §8.4 evaluates left to right; `keys` and printing follow it too.
    - `associate` and `dissociate` copy the table.
  - **Closure:** `{lambda node (counted), environment, include context}`.
  - **Primitive:** `{name, minimum and maximum arity, function pointer}`.
- **Ownership:**
  - A function that returns a `Value *` returns a new reference.
  - Arguments are borrowed, unless the function name ends in `_take`.
  - This rule is written at the top of `value.h`.
- **Iterative release:** release uses an explicit work stack. Freeing a list of a million elements, or a deep tree, does not recurse in C.
- **Live counter:** `lisp_live_objects()` counts the objects that are not immortal. Both test runners use it to detect leaks.
- **Integers:**
  - Arithmetic uses `ckd_add`, `ckd_sub` and `ckd_mul`.
  - Overflow is an error, as the spec requires.
  - The reader applies the same check to integer literals.

## Errors (`error.h`)
- **Pending error:**
  - A failing function stores a descriptor `{:kind :message :values}` in the state, and the state owns it.
  - Functions that can fail are marked `[[nodiscard]]`.
- **`catch`:**
  - Calls the thunk.
  - If the thunk fails, `catch` takes the descriptor, clears the pending error and calls the handler with the descriptor.
- **`raise`:** stores the descriptor it is given and fails.
- **Read errors:**
  - A read error is a separate struct, `{code, line, column}`.
  - It never reaches `catch`.

## Pipeline
- **Reader:**
  - Input is bytes. An invalid UTF-8 byte is reported at its line and column, counted in code points.
  - Output is a vector of datums plus the position table.
  - **Position table:**
    - A hash keyed by node address.
    - A node with an entry gets a `HAS_POSITION` flag, and freeing the node removes its entry, so a reused address never inherits an old position.
  - **Error order:** errors are detected in the order the spec fixes (§7.3), including the checks done when a map closes.
- **Expansion:**
  - Reduces each form to the five special forms and builds counted nodes.
  - Enforces the expansion errors and ignores `declare` and `declare-type`.
- **Evaluation:**
  - **Tail calls:** `lisp_eval(node, env, state)` is a loop. A call in tail position replaces the node and environment and releases the old environment, so tail calls run in constant memory (§8.4).
  - **Environments:** counted frames `{parent, n, symbols[], values[]}`.
  - **Lookup:** parameters from the innermost `lambda` outward, then the global name, looked up at call time (§8.2).
  - **Deep non-tail recursion:**
    - Uses the C stack.
    - `main.c` runs the interpreter on a pthread with a 512 MB stack.
    - A stack guard turns exhaustion into a catchable error `:stack-overflow`, instead of a crash. It compares the current frame address with the base recorded when the interpreter is entered, so the limit is in bytes and holds under sanitizers too.
- **Primitives:** the §9 table, one C function per primitive, checking arity and types before doing any work.
- **Program:**
  - The whole file is read before anything runs. Then each top-level form is expanded and evaluated in turn, and a top-level `sequence` is spliced (§8.7).
  - **Libraries (§8.9):**
    - Nothing is loaded before the program. `(import name prefix?)` goes through the same context machinery as `include`, keyed by library name instead of file path.
    - `io` is native: its primitives live in `library_io.c` and are bound only by `import`, never in the global table.
    - `list`, `text` and `function` are read from the `lisp` code blocks of `standard-library.md` §4.1–4.3, so the normative definitions are what runs. The path comes from the build, and `NOVELO_STDLIB` overrides it.
- **IO:**
  - Output goes through a callback.
  - Stdin is a line reader that ends a line only at U+000A, so a CR stays in the line (§9).
  - The file system is an interface with two implementations: the disk, and an in-memory map for tests.
- **CLI and REPL:**
  - `novelo file.novelo` runs a program, and `novelo` alone starts the REPL. The REPL starts with `io`, `list`, `text` and `function` imported (§8.7); files import explicitly.
  - **Exit codes:**
    - 0 on success.
    - 1 on a read error or an uncaught error, with a message on stderr.
    - 2 on bad arguments.
  - The REPL prints each value with `write!`, skips `#void`, and keeps reading lines while a collection or string is open.

## Unit test runner (`tests/unit/`)
- **Registration:**
  - Each `*_test.c` exports a suite, e.g. `const TestSuite reader_suite = { "reader", (TestCase[]){ {"reads integers", t_ints}, …, {} } };`.
  - `test.c` lists the suites. There is no auto-registration magic.
- **Assertions:**

  | Assertion | Checks |
  | --- | --- |
  | `CHECK(cond)` | the condition holds |
  | `CHECK_INT(a, b)` | two integers are equal |
  | `CHECK_STR(a, b)` | two strings are equal |
  | `CHECK_EQUAL(value, "datum")` | the value is `equal?` to the datum read from the text |
  | `CHECK_READ_ERROR(src, code, line, col)` | reading fails with this code at this position |

  Each failure reports its file and line.
- **Isolation:** each test runs in a child process (`fork`), so a crash counts as a failure. `--no-fork` turns this off for debugging.
- **Leaks:** the live-object count must be the same before and after each test.
- **Options:** `-k TEXT` filters tests by name, and `-v` lists every test.
- **Output:** a summary `N passed, M failed`. The exit code is nonzero when any test fails.
- **Coverage:**
  - values: refcounting, releasing a million elements, `equal?`, map hashing.
  - utf8 and reader: classification, escapes, positions.
  - printer.
  - expander: rewrite rules.
  - eval: lexical scope, tail calls.
  - primitives: overflow, code point indexing.

## Conformance runner (`tests/conformance/`)
- **`json.c`:**
  - Parses objects, arrays, strings (including `\uXXXX` and surrogate pairs), integers, `true`, `false` and `null`.
  - Floating-point numbers are not needed.
- **`conformance.c`:**
  - **Case kinds:**
    - `read`, `read-error` (including `input_hex`) and `read-positions`.
    - `eval`, with `input`, `files` and the exact `output`.
  - **Comparison:**
    - Results are compared with `equal?`, never by printed text.
    - An error result is compared by its `:kind`.
  - **Suites:**
    - Both files run with every library available to `import`; the programs import what they use. `conformance.json` only imports `io`.
  - Each case starts from a fresh state and destroys it at the end. After that the live-object count must be zero.
  - **Options:** `-k` filters cases and `-v` lists every case.

## Phases (each ends with its tests passing)
1. **Foundation:** Makefile, unit test runner, `value`, `buffer` and `utf8`, with unit tests for refcounting, `equal?` and releasing long lists.
2. **Reading:** `reader`, `positions` and `printer`, plus the JSON parser and the conformance runner. Every `read`, `read-error` and `read-positions` case passes.
3. **Evaluation:** `error`, `expander`, `eval`, `primitives`, `program`, plus `import` of the native library `io` (`library`, `library_io`). Every core `eval` case without `files` passes.
4. **Include:** `include.c`. The core `eval` cases with `files` pass.
5. **Standard library, CLI and REPL:** the libraries `list`, `text` and `function` from `standard-library.md`, `main.c` and `repl.c`. All of `stdlib-conformance.json` passes.
6. **Hardening and docs:** both suites pass under `make asan` (AddressSanitizer + UndefinedBehaviorSanitizer). The README is written.

## Verification
Run the unit tests and both suites. Every case must pass with no leaks:

```bash
make test
```

Then run everything again under the sanitizers:

```bash
make asan && make test
```

Finally, check constant memory by hand: a tail-recursive loop of 10 million iterations must keep a stable resident memory size (`/usr/bin/time -l` on macOS, `/usr/bin/time -v` on Linux).
