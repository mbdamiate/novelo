# Learning Novelo

This tutorial teaches Novelo to a programmer who already knows another language, such as Python, JavaScript or C, but has never written Lisp. Each chapter introduces one idea, shows it in REPL sessions you can type yourself, and ends with exercises that a script checks for you.

The two specifications, [`core-specification.md`](../core-specification.md) and [`standard-library.md`](../standard-library.md), are the reference: they define the language precisely, and the chapters link to them where you may want the full rules. The tutorial is the path through them. The [`examples/`](../examples) are complete commented programs, and each chapter names the ones that use its ideas.

## Chapters

1. [First steps](01-first-steps.md): the REPL, prefix notation, a first program.
2. [Values](02-values.md): numbers, text, keywords, lists, and what counts as false.
3. [Names and functions](03-names-and-functions.md): `define`, `lambda`, `let`, `if`, `and`, `or`.
4. [Recursion and lists](04-recursion-and-lists.md): loops without loops, and the library `list`.

## Before you start

Build the interpreter from the root of the repository:

```bash
make
```

That gives `build/novelo`. Run it with no argument to get the REPL, and with a file to run a program:

```bash
build/novelo
```

```bash
build/novelo tutorial/programs/01-hello/main.nvl
```

Every command in this tutorial runs from the root of the repository.

## How the sessions are written

A REPL session is shown with `>` before what you type, and the REPL's answer below it:

```novelo
> (+ 1 2)
3
```

Type only what follows `> `. Each session starts from a fresh REPL, so names defined in one session are not there in the next unless the session defines them again. Every session in these chapters runs as part of `make test`, so what you see is what the interpreter prints.

## Exercises

Each exercise is a directory in [`exercises/`](exercises), named after its chapter: `exercises/03-time-units` belongs to chapter 3. In it:

- `answer.nvl` is the file you edit. It starts as a stub: the names the exercise asks for, defined with a placeholder that fails.
- `check.nvl` holds the tests. Read it to see what is expected, but you do not need to change it.
- `solution.nvl` is a solution. Try the exercise before you open it.

Check your answer with:

```bash
tutorial/check.sh 03-time-units
```

It prints each failing test, with what it expected and what it got, or the error it raised, and then how many tests passed. With `seconds-in-hours` multiplying by 60 only once:

```
FAIL (seconds-in-hours 2)
  expected: 7200
  got:      120
4 of 5 tests passed
```

To start an exercise over, restore its stub with `git checkout tutorial/exercises/NAME/answer.nvl`.
