# 10. Modules

A program that grows past a few hundred lines wants more than one file. Novelo has two forms for that: `include` reads another file of yours, and `import` reads a library by name. Both can put the names they bring in under a prefix, so that two files never fight over a name.

## `import`

`(import list)` makes the library `list` available. Its names arrive as they are: `map`, `filter`, `reverse`. A second argument puts them under a prefix instead:

```novelo
> (import list l)
> (l/reverse (list 1 2))
(2 1)
> (import text t)
> (t/join "-" (list "a" "b"))
"a-b"
```

The REPL had already imported every library without a prefix, so `reverse` works there too. In a program, `(import list l)` binds only `l/reverse`, and a plain `reverse` is unbound. Use a prefix when a library's names would clash with your own, or when you want each call to show which library it comes from.

A library is loaded once per prefix, so importing it twice does nothing the second time. An unknown name is an error that stops the program:

```novelo
> (import no-such-library)
1:1: error :import-error: no library named no-such-library no-such-library :unknown-library
```

`import` and `include` are allowed only at the top level of a file.

## `include`

`(include "path")` reads another file and evaluates its forms where the `include` stands, as if they were written there. The path is relative to the file that holds the `include`. With a prefix, the file's definitions get the prefix.

[`programs/10-modules`](programs/10-modules) has two files. `money.nvl` defines functions for amounts in cents:

```novelo
(import list)   ; fold

(define two-digits
  (lambda (number)
    (if (< number 10) (concatenate "0" (to-string number))
        otherwise (to-string number))))

(define format-cents
  (lambda (cents)
    (concatenate "$" (to-string (quotient cents 100)) "." (two-digits (remainder cents 100)))))

(define total
  (lambda (amounts)
    (fold + 0 amounts)))
```

`main.nvl` includes it with the prefix `money`:

```novelo
(import io)
(import list)               ; map
(import text t)            ; t/join, t/pad-left
(include "money.nvl" money)

(define total "not the module's total")

(define prices '(1999 250 5))

(display! (t/join "\n" (map money/format-cents prices)))
(display! "\n")
(display! (t/pad-left 10 " " (money/format-cents (money/total prices))))
(display! "\n")
(display! total)
(display! "\n")
```

```bash
build/novelo tutorial/programs/10-modules/main.nvl
```

```
$19.99
$2.50
$0.05
    $22.54
not the module's total
```

How the names resolve:
- **In `main.nvl`,** the module's functions are `money/format-cents` and `money/total`. The program's own `total` is untouched.
- **Inside `money.nvl`,** the plain names still work. `format-cents` calls `two-digits`, and that finds `money/two-digits`. While a file included with a prefix runs, a name it defined itself resolves to the prefixed one, and any other name resolves as usual.

So a file does not need to know the prefix it will be included with. The rules are in [core §8.8](../core-specification.md#88-including-files).

## Dependencies stay inside the prefix

`money.nvl` imports `list` for `fold`. Since `money.nvl` runs in the context of the prefix `money`, that import binds `money/fold`, `money/map` and the rest, not `map`. That is why `main.nvl` imports `list` itself:

```novelo
> (include "tutorial/programs/10-modules/money.nvl" money)
> (money/format-cents 1999)
"$19.99"
> (money/two-digits 7)
"07"
> (money/map (lambda (n) (* n 2)) (list 1 2))
(2 4)
```

`money/two-digits` is reachable from outside, because there are no private names. A name used only inside a file is private by convention: document which names are meant for others.

In the REPL, a relative path is relative to the directory you started it in, which here is the root of the repository. A file that cannot be read is an error:

```novelo
> (include "no-such-file.nvl")
1:1: error :include-error: cannot open the file "no-such-file.nvl" :cannot-open
```

Like a library, a file is included at most once per prefix. Two files can include the same helper file without loading it twice.

## Exercises

- **`10-statistics`.** Write a small module of statistics functions. The tests include your file with the prefix `stats` and call `stats/mean`, `stats/median` and `stats/describe`, so your functions must call one another by their plain names.

```bash
tutorial/check.sh 10-statistics
```

The example [`08-modules`](../examples/08-modules/main.nvl) splits a program over several files with nested prefixes.

Next: [Macros](11-macros.md).
