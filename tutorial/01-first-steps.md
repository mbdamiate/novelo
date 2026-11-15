# 1. First steps

Novelo is a small Lisp. Its programs are made of values that never change and functions that build new values from old ones. This chapter shows how to talk to the interpreter and how to read Lisp code, which looks odd for about a day and then stops looking odd.

## The REPL

Start the REPL with `build/novelo` and no file. It reads what you type, evaluates it, prints the result, and waits for more: a *read–eval–print loop*. End it with Ctrl-D.

```novelo
> 42
42
> "hello"
"hello"
> (+ 1 2)
3
```

A number or a string evaluates to itself. The REPL prints results the way you would type them, so a string keeps its quotes.

## Prefix notation

In most languages, an operator sits between its operands: `1 + 2`. In a Lisp, every operation is written as a list in parentheses, with the operation first and the operands after it: `(+ 1 2)`. There are no precedence rules to remember, because the parentheses say everything:

```novelo
> (* (+ 1 2) 4)
12
> (+ 1 (* 2 4))
9
```

`(* (+ 1 2) 4)` is `(1 + 2) * 4`, and `(+ 1 (* 2 4))` is `1 + 2 * 4`. Read from the inside out: the inner list is evaluated first, and its value takes its place.

The same notation serves every function, not only arithmetic. A call is always `(function argument …)`, so `(+ 1 2)` calls the function `+` with two arguments, just as `f(1, 2)` would in another language. Many functions take any number of arguments:

```novelo
> (+ 1 2 3 4)
10
> (+)
0
> (- 10)
-10
> (< 1 2 3)
#true
> (< 1 3 2)
#false
```

`(< 1 2 3)` asks whether the numbers increase from left to right. `#true` and `#false` are the two Booleans.

Integer division has two functions, one for each part of the answer:

```novelo
> (quotient 17 5)
3
> (remainder 17 5)
2
```

`/` exists too, but only for Floats, which chapter 2 covers.

A form can span several lines. The REPL waits until every parenthesis is closed:

```novelo
> (+ 1
>    2)
3
```

## Errors

When something goes wrong, the REPL prints an error and carries on:

```novelo
> (+ 1 (quotient 1 0))
1:6: error :division-by-zero: quotient: division by zero
> undefined-name
error :unbound-symbol: unbound symbol undefined-name undefined-name
```

The message has four parts:

- **Where:** `1:6` is line 1, column 6 of what you typed, the start of `(quotient 1 0)`. The error is reported at the innermost expression that failed.
- **`error`.**
- **The kind:** `:division-by-zero`. Programs can catch errors by kind, as chapter 8 shows.
- **A message and the values involved.**

In a program file, the position also names the file.

## Comments

A semicolon starts a comment that runs to the end of the line:

```novelo
> (+ 1 2) ; three
3
```

## A first program

A program is a file of forms, evaluated from top to bottom. Here is [`programs/01-hello/main.nvl`](programs/01-hello/main.nvl):

```novelo
(import io)

(display! "Hello, world!\n")

(display! "Novelo\tmeans \"ball of yarn\".\n")
(write! "Novelo\tmeans \"ball of yarn\".\n")
(display! "\n")

(display! (+ 40 2))
(display! "\n")
```

Run it:

```bash
build/novelo tutorial/programs/01-hello/main.nvl
```

```
Hello, world!
Novelo	means "ball of yarn".
"Novelo\tmeans \"ball of yarn\".\n"
42
```

Three things to notice.

**`(import io)`.** A program sees only the core of the language until it imports a library. The functions that read and write live in the library `io`. The REPL is the exception: it starts with every library already imported, which is why the sessions above never import anything.

**`display!` and `write!`.** Both print a value.
- `display!` prints text as a reader would want it: the tab is a tab and the quotes are just quotes.
- `write!` prints the value as you would type it in code, with quotes around the string and escapes inside. The REPL prints its results that way, too.
- Neither adds a line break, so the program prints `"\n"` itself.

**The `!`.** The names of functions that touch the world outside the program end in `!`. A function without `!` only computes a value from its arguments. The difference matters a great deal in Novelo, and chapter 9 comes back to it.

## Exercises

Read [Exercises](README.md#exercises) for how they work.

- **`01-expressions`.** Translate four formulas into Novelo. Each one is named with `(define name value)`, which chapter 3 explains; you only replace the value. The file is [`exercises/01-expressions/answer.nvl`](exercises/01-expressions/answer.nvl). Check them with:

  ```bash
  tutorial/check.sh 01-expressions
  ```

The examples [`01-hello`](../examples/01-hello/main.nvl) and [`02-fizzbuzz`](../examples/02-fizzbuzz/main.nvl) use what this chapter covers, and a little of the next two.

Next: [Values](02-values.md).
