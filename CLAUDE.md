# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project state

This repository specifies **lisp-min**, an immutable Lisp (core + standard library v0.2.0), and implements an interpreter for it in **C23**, following `IMPLEMENTATION-PLAN.md` phase by phase. Each phase ends with its tests passing and one commit. Phases 1 (values, buffer, utf8), 2 (reader, positions, printer, conformance runner) and 3 (expander, evaluator, primitives, `import` of `io`) are done. Until phase 4, the conformance runner skips `eval` cases that use `include`. Until phase 5, `make conformance` leaves out `stdlib-conformance.json`.

| File | Role |
| --- | --- |
| `core-specification.md` | Normative core: reader (§2–7), positions (§6), read errors (§7), evaluation (§8), primitives (§9), printing (§10), conformance (§12) |
| `standard-library.md` | Normative stdlib: the libraries `list`, `text`, `function`, whose sources are the `lisp` code blocks of §4.1–4.3 |
| `conformance.json` | 241 core cases (`read`, `read-error`, `read-positions`, `eval`); programs import at most `io` |
| `stdlib-conformance.json` | 29 stdlib `eval` cases; programs import `list`, `text` or `function` |
| `IMPLEMENTATION-PLAN.md` | Architecture, decisions, file layout, phases, test runners |
| `implementation-notes.md` | Non-normative advice (refcounting, position-table address reuse) |

The specs and the conformance files are the only source of truth. When the prose and a conformance case disagree, or the prose is silent, the case wins. Core §12.4 lists behaviors that only the tests fix, such as `first`/`rest` of `()` → `:wrong-type`.

## Commands

```bash
make               # build (C23, -Wall -Wextra -Wpedantic -Werror)
make test          # unit tests + both conformance suites; must pass with zero leaks
make unit          # unit tests only
make conformance   # both conformance suites
make asan          # rebuild in build/asan with ASan + UBSan and run `make test` there
./build/unit-tests -k reader/strings --no-fork   # one unit test, in-process for a debugger
./build/conformance -v -k read-error conformance.json
```

Both test runners take `-k TEXT` to filter tests or cases by name or id (substring match), and `-v` to list each one. The unit runner forks one process per test; pass `--no-fork` to debug a single test. To add a unit suite, export a `TestSuite` from `tests/unit/<module>_test.c` and list it by hand in `tests/unit/test.c`. Make decides what to rebuild from timestamps, so after reverting a file within the same second, run `make clean` first.

## Code style

- Variable, parameter and field names are full words (`index`, `code_point`, `builder`, `environment`), never abbreviations (`i`, `cp`, `b`, `env`). The user asked for this.
- Comments, messages and docs are in English.

The planned CLI: `lisp-min file.lisp` runs a file, `lisp-min` alone starts the REPL, which starts with every library imported. Exit codes are 0 for success, 1 for a read error or uncaught error, and 2 for bad arguments.

## Constraints that shape the code

- **No dependencies** beyond libc and pthreads, including in the tests. The conformance runner has its own minimal JSON parser.
- **C23 idioms:** `nullptr`, `constexpr`, `bool`, `[[nodiscard]]`, and `<stdckdint.h>` (`ckd_add`/`ckd_sub`/`ckd_mul`). Integer overflow is a spec error, both at runtime and in reader literals.
- **Cite the spec in each file**, e.g. `/* core §8.3 */`.
- **Memory is reference counted.** The heap is acyclic: values are immutable and recursion goes through global names resolved at call time. Ownership rule: a function returning `Value *` returns a new reference, and arguments are borrowed unless the function name ends in `_take`. Release must be iterative (explicit work stack), because dropping a 1M-element list must not recurse in C. `lisp_live_objects()` counts non-immortal objects, and both runners check that it returns to its baseline after each test or case.
- **Errors are return values.** Never use `setjmp`/`longjmp`, because a jump would skip releases. A failing call returns `nullptr`/`false` and leaves a pending `{:kind :message :values}` descriptor in the interpreter state. Read errors are a separate `{code, line, column}` struct and never reach `catch`.
- **Position table:** a side table keyed by node address. Freeing a node must remove its entry, so a reused address never inherits a stale position (core §6.4).

## Pipeline architecture

1. **Reader** (`reader`, `utf8`, `positions`) turns bytes into a vector of datums plus the position table. It must detect errors in the order core §7.3 fixes. Invalid UTF-8 is reported at its line and column, counted in code points.
2. **Expander** (`expander.c`, `node.h`) resolves locals to lexical addresses (depth, index) and reduces datums to the five special forms (`quote`, `if`, `lambda`, `define`, `define-macro`) and builds counted nodes. It also rewrites the derived forms `let`, `sequence`, `and`, `or` and `quasiquote`, runs macros (non-hygienic, defined before first use), and ignores `declare`/`declare-type`. `if` accepts only `(if c a)` or `(if c a otherwise b)`.
3. **Evaluator** (`eval.c`) is a loop. A call in tail position replaces the node and environment, which guarantees constant-memory tail calls (core §8.4). Lookup goes through lexical frames, then the global table at call time. Deep non-tail recursion uses the C stack. `lisp_run_on_large_stack` (program.c) runs the interpreter on a pthread with a 512 MB stack; the conformance runner uses it for every eval case. `lisp_check_stack` compares the frame address with the base recorded when the interpreter is entered, and raises the catchable error `:stack-overflow` before the stack runs out.
4. **Program** (`program.c`) reads the whole file before running anything, then expands and evaluates each top-level form in turn, splicing a top-level `sequence`. Nothing is loaded before the program: libraries arrive only through `import` (core §8.9).
5. **Include and import** (`include.c`, core §8.8–8.9) handle contexts, prefixes, load-once and cycle detection. `import` uses the same context rules as `include`, keyed by library name. `io` is native (`library_io.c`, never in the global table); `list`, `text` and `function` come from the `lisp` blocks of `standard-library.md` §4.1–4.3, located at build time or through `LISP_MIN_STDLIB`. File system access goes through an interface, with a disk implementation and an in-memory one; the conformance `files` cases use the in-memory one.

## Conformance runner semantics

- Compare results with `equal?` on datums, never by printed text. An error result matches by its `:kind` only.
- `eval` cases must match the exact stdout in `output`. They also check either `value` (the last form's value) or `error` (the `:kind` of the uncaught error). `input` is fed as stdin. The stdin line reader splits only on U+000A, so a CR stays in the line.
- With `files`, the program is `main.lisp` and paths are relative to it. A path missing from `files` cannot be opened.
- Each case uses a fresh interpreter state, and the live-object count must be zero after it is destroyed.
- Constant-memory check, done by hand: a 10M-iteration tail loop must keep a stable RSS (`/usr/bin/time -l` on macOS).
