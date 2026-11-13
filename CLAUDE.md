# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project state

This repository specifies **Novelo**, an immutable Lisp (core v0.8.1, standard library v0.5.0); the name means "ball of yarn" in Portuguese, and source files use `.nvl`, and implements an interpreter for it in **C23**. The interpreter was built in six phases, all done; the language is now being extended one feature at a time (core 0.3.0 added source positions to runtime errors, core 0.4.0 persistent maps and the stdlib library `map`, core 0.5.0 the type Set `#{…}` and the library `set`, core 0.6.0 the `if` with several pairs, core 0.7.0 `match`, core 0.8.0 `string-to-code-points`/`code-points-to-string`, stdlib 0.5.0 text comparison, case and classification over ASCII + Latin-1 and the library `number`): all 357 core and 64 stdlib cases and the unit tests pass, also under `make asan`. `README.md` covers building, running, the source map, the design and the behavior the spec leaves to the implementation. Core 0.2.1 settled the points 0.2.0 left open; when a new gap appears, decide it with the user, write it into the spec and add conformance cases rather than only documenting the choice.

| File | Role |
| --- | --- |
| `core-specification.md` | Normative core: reader (§2–7), positions (§6), read errors (§7), evaluation (§8), primitives (§9), printing (§10), conformance (§12) |
| `standard-library.md` | Normative stdlib: the libraries `list`, `text`, `function`, `map`, `set`, `number`, whose sources are the `lisp` code blocks of §4.1–4.6 |
| `conformance.json` | 357 core cases (`read`, `read-error`, `read-positions`, `eval`, with an optional `error_position`); programs import at most `io` |
| `stdlib-conformance.json` | 64 stdlib `eval` cases; programs import `list`, `text`, `function`, `map`, `set` or `number` |
| `README.md` | Building, running, source map, design, implementation-defined behavior |
| `examples/` | Commented example programs, numbered by complexity, each adding one idea: one directory per example, `NN-name/main.nvl` with an optional `input.txt`, the expected `output.txt`, and any files it includes. The friction they show picks the next extension |

The specs and the conformance files are the only source of truth. When the prose and a conformance case disagree, or the prose is silent, the case wins. Core §12.4 lists behaviors that only the tests fix, such as `first`/`rest` of `()` → `:wrong-type`.

## Commands

```bash
make               # build build/novelo (C23, -Wall -Wextra -Wpedantic -Werror)
make test          # unit tests + both conformance suites + examples; must pass with zero leaks
make unit          # unit tests only
make conformance   # both conformance suites
make examples      # run examples/*/main.nvl with build/novelo and compare with output.txt
make asan          # rebuild in build/asan with ASan + UBSan and run `make test` there
./build/unit-tests -k reader/strings --no-fork   # one unit test, in-process for a debugger
./build/conformance -v -k read-error conformance.json
```

Both test runners take `-k TEXT` to filter tests or cases by name or id (substring match), and `-v` to list each one. The unit runner forks one process per test; pass `--no-fork` to debug a single test. To add a unit suite, export a `TestSuite` from `tests/unit/<module>_test.c` and list it by hand in `tests/unit/test.c`. Make decides what to rebuild from timestamps, so after reverting a file within the same second, run `make clean` first.

## Code style

- Variable, parameter and field names are full words (`index`, `code_point`, `builder`, `environment`), never abbreviations (`i`, `cp`, `b`, `env`). The user asked for this.
- Comments, messages and docs are in English.

The CLI (`main.c`): `build/novelo file.nvl` runs a file, `build/novelo` alone starts the REPL (`repl.c`), which starts with every library imported and prompts only when stdin is a terminal. Exit codes are 0 for success, 1 for a read error or uncaught error, and 2 for bad arguments or an unreadable program file. Messages go to stderr as `path:line:column: read error :code: message` or `path:line:column: error :kind: message values…` (the position comes from the descriptor's `:position`, and is left out when there is none).

## Constraints that shape the code

- **No dependencies** beyond libc and pthreads, including in the tests. The conformance runner has its own minimal JSON parser.
- **Portable to gcc and glibc.** Tested with Apple clang 21 and with gcc 14 on Linux (`docker run gcc:14`, then `make CC=gcc test`). The Makefile defines `_POSIX_C_SOURCE=200809L` and `_DARWIN_C_SOURCE`, because strict `-std=c23` otherwise hides POSIX functions such as `mkdtemp` on glibc. Static functions must not share a name with a libc function: gcc rejects a `static remainder`, which is why primitives are `primitive_<name>`.
- **C23 idioms:** `nullptr`, `constexpr`, `bool`, `[[nodiscard]]`, and `<stdckdint.h>` (`ckd_add`/`ckd_sub`/`ckd_mul`). Integer overflow is a spec error, both at runtime and in reader literals.
- **Cite the spec in each file**, e.g. `/* core §8.3 */`.
- **Memory is reference counted.** The heap is acyclic: values are immutable and recursion goes through global names resolved at call time. Ownership rule: a function returning `Value *` returns a new reference, and arguments are borrowed unless the function name ends in `_take`. Release must be iterative (explicit work stack), because dropping a 1M-element list must not recurse in C. `lisp_live_objects()` counts non-immortal objects, and both runners check that it returns to its baseline after each test or case.
- **Errors are return values.** Never use `setjmp`/`longjmp`, because a jump would skip releases. A failing call returns `nullptr`/`false` and leaves a pending `{:kind :message :values}` descriptor in the interpreter state. Read errors are a separate `{code, line, column}` struct and never reach `catch`.
- **Maps are a persistent HAMT** (value.c, core §8.6): `LispMap` holds a root trie node (an opaque object, class `MapNode`), the count and the next insertion sequence. Never copy a whole map in `associate`/`dissociate`: `map-001` builds a million keys one at a time and would hang. Iteration (`lisp_map_next`) sorts entries by sequence on first use and caches the order in the map, so the expander still sees map literals in reading order (§8.4). A Set is the same `LispMap` with kind `LISP_KIND_SET` and every value `#true` (`lisp_set_*` in value.h); the reader turns `#{` into one, and set literals expand to `LISP_NODE_SET`.
- **Position table:** a side table keyed by node address. Freeing a node must remove its entry, so a reused address never inherits a stale position (core §6.4). `LispPosition` also carries a `file` id (positions.c keeps the names as plain C strings, so they never count as live objects); `lisp_read_file` records it.
- **Positions in errors (core §8.5):** every `LispNode` has a `position`. `expand` stamps it from the form, or from `state->expansion_position` (the nearest enclosing form) for nodes built by derived forms, quasiquote and macros. On failure, `lisp_eval`, `expand` and the top-level `process` call `lisp_error_locate`, which adds `:position` only if the descriptor has none, so the innermost location wins and `raise` keeps an existing one.

## Pipeline architecture

1. **Reader** (`reader`, `utf8`, `positions`) turns bytes into a vector of datums plus the position table. It must detect errors in the order core §7.3 fixes. Invalid UTF-8 is reported at its line and column, counted in code points.
2. **Expander** (`expander.c`, `node.h`) resolves locals to lexical addresses (depth, index) and reduces datums to the five special forms (`quote`, `if`, `lambda`, `define`, `define-macro`) and builds counted nodes. It also rewrites the derived forms `let`, `sequence`, `and`, `or`, `quasiquote` and `match`, runs macros (non-hygienic, defined before first use), and ignores `declare`/`declare-type`. `if` takes one or more test/result pairs, optionally ending with `otherwise` and one expression (core 0.6.0); `expand_if_pairs` turns several pairs into nested IF nodes. `(if c a b)` is still an expansion error. `expand_match` validates the patterns and rewrites `match` into datums (`let` of a generated symbol, `if`/`and` tests, `let` of the bindings) that it expands again; the tests are primitive values from `lisp_primitive_value` embedded as constants, and the fallback without `otherwise` calls `lisp_no_match_primitive`.
3. **Evaluator** (`eval.c`) is a loop. A call in tail position replaces the node and environment, which guarantees constant-memory tail calls (core §8.4). Lookup goes through lexical frames, then the global table at call time. Deep non-tail recursion uses the C stack. `lisp_run_on_large_stack` (program.c) runs the interpreter on a pthread with a 512 MB stack; the conformance runner uses it for every eval case. `lisp_check_stack` compares the frame address with the base recorded when the interpreter is entered, and raises the catchable error `:stack-overflow` before the stack runs out.
4. **Program** (`program.c`) reads the whole file before running anything, then expands and evaluates each top-level form in turn, splicing a top-level `sequence`. Nothing is loaded before the program: libraries arrive only through `import` (core §8.9).
5. **Include and import** (`include.c`, core §8.8–8.9) handle contexts, prefixes, load-once and cycle detection. `import` uses the same context rules as `include`, keyed by library name. `io` is native (`library_io.c`, never in the global table); `list`, `text`, `function`, `map`, `set` and `number` are read on import from the `lisp` blocks of the `### 4.1`–`4.6` sections of `standard-library.md` (`lisp_library_source`; other lines become blank, so positions match the document). The Makefile bakes in the document's absolute path for `library.o`; `NOVELO_STDLIB` or `LispStateOptions.library_path` overrides it. The document is always read from disk, never through the state's file system. File system access goes through an interface (`io.h`), with a disk implementation and an in-memory one; the conformance runner puts the program at `/main.nvl` in memory, with the `files` around it. The state tracks the files still loading (cycles, and the directory relative paths resolve against) and the files already loaded per context; a file is marked loaded only after it was read, so a failed include can be retried in the REPL.

## Conformance runner semantics

- Compare results with `equal?` on datums, never by printed text. An error result matches by its `:kind` only.
- `eval` cases must match the exact stdout in `output`. They also check either `value` (the last form's value) or `error` (the `:kind` of the uncaught error). `input` is fed as stdin. The stdin line reader splits only on U+000A, so a CR stays in the line.
- With `files`, the program is `main.nvl` and paths are relative to it. A path missing from `files` cannot be opened.
- Each case uses a fresh interpreter state, and the live-object count must be zero after it is destroyed.
- Constant-memory check, done by hand: a 10M-iteration tail loop must keep a stable RSS (`/usr/bin/time -l` on macOS).
