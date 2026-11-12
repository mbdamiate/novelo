# Novelo

An interpreter, written in C23, for **Novelo**: a small, immutable Lisp. It implements the core (`core-specification.md`, v0.2.2) and the standard library (`standard-library.md`, v0.2.1).

*Novelo* is Portuguese for a ball of yarn: a single thread, unwound from its first element to the rest, as Novelo's lists are. Source files end in `.nvl`.

Every case of both conformance suites passes:

| Suite | Cases |
| --- | --- |
| core | 253 |
| stdlib | 29 |

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

It needs a C23 compiler, such as clang 18+ or gcc 14+, plus `make`, libc and pthreads. There are no other dependencies.

```bash
make            # builds build/novelo
make test       # unit tests + both conformance suites
make asan       # the same, rebuilt in build/asan with ASan + UBSan
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
error :unbound-symbol: unbound symbol car car
```

**Programs** see only the core primitives until they import a library (core §8.9):

```lisp
(import io)            ; write!, display!, read-line!
(import list)          ; map, filter, fold, sort, ...
(import text t)        ; t/split, t/join (and the names of list, as t/map, ...)
(import function)      ; partial
(include "lib/geometry.nvl" geo)   ; geo/area, ...
```

**The REPL** starts with all four libraries imported. An input may span several lines: while a list, map or string is still open, the REPL keeps reading. Each value except `#void` is printed with the `write!` rules. After an error the REPL reports it and reads the next input. The `> ` prompt appears only when standard input is a terminal, so piping a file through the REPL prints only the values.

**The standard library** is not compiled in. On each `import`, `list`, `text` and `function` are read from the `lisp` code blocks of `standard-library.md` §4.1–4.3, so the code that runs is the normative code. The build fixes the absolute path of that document. To use another copy, set `NOVELO_STDLIB`:

```bash
NOVELO_STDLIB=/usr/local/share/novelo/standard-library.md build/novelo program.nvl
```

## Tests

```bash
make unit
make conformance
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

## Source map

| File | Spec | Contents |
| --- | --- | --- |
| `src/value.h/.c` | core §2 | objects, reference counting, immortal values, interning, `equal?`, hashing, ordered maps |
| `src/buffer.h/.c` | | growable byte buffer, allocation helpers |
| `src/utf8.h/.c` | core §3.2 | strict UTF-8 decoding and encoding |
| `src/reader.h/.c` | core §3–7 | tokens, literals, escapes, collections, nesting limit, read errors in §7.3 order |
| `src/positions.h/.c` | core §6 | side table: node address → line and column |
| `src/printer.h/.c` | core §10 | `write!` and `display!` text |
| `src/error.h/.c` | core §8.5 | the pending error `{:kind :message :values}` |
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
4. It also reduces the derived forms (`let`, `sequence`, `and`, `or`, `quasiquote`) and macro calls to a few node kinds.
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

**Positions** live in a side table keyed by node address. Freeing a node removes its entry, so a reused address never inherits a stale position (core §6.4).

## Implementation-defined behavior

Core 0.2.1 settled the points that versions up to 0.2.0 left open, such as `display!` of strings inside collections, `:stack-overflow` and `:io-error`. They are now in the specification and in the conformance cases. What the specification still leaves to the implementation, this interpreter does as follows:

- **Call depth.** The specification requires at least 10,000 nested non-tail calls. Here the limit is the 512 MB stack of the CLI and the conformance runner, minus a 16 MB margin, which allows well over 100,000 nested calls. Beyond it, `:stack-overflow` is raised.
- **Libraries that cannot be loaded.** When the standard library document cannot be read, or has no code for the library, `import` gives `:import-error` with `:values` `(name :cannot-open)`. A read error in the document gives `(name code line column)`, with the line and column in the document.
