# 3. Names and functions

This chapter covers the forms you will write in every program: naming values, making functions, and choosing between results.

## `define`

`define` gives a global name to a value:

```novelo
> (define tax-rate 0.2)
> tax-rate
0.2
```

`define` itself returns `#void`, so the REPL printed nothing for it. Names can hold almost any character except spaces and parentheses. The usual style is lowercase words joined by `-`, as in `tax-rate`. A name ending in `?` holds a predicate, and one ending in `!` holds a function with effects.

`define` is only allowed at the top level of a file, never inside a function. Defining a name again replaces it. That is handy in the REPL, but it is not a way to keep changing state: values themselves never change.

## `lambda`

`lambda` makes a function. It takes a list of parameters and a body:

```novelo
> (lambda (n) (* n n))
#<function>
> ((lambda (n) (* n n)) 7)
49
```

A function is a value like any other. It prints as `#<function>`, and the second line calls one right where it is made. To use a function more than once, name it with `define`:

```novelo
> (define square (lambda (n) (* n n)))
> (square 7)
49
> (square 1 2)
1:1: error :wrong-arguments: function does not take 2 arguments #<function>
```

There is no separate syntax for defining a function, such as `def` or `function`. `(define square (lambda …))` is how every function in Novelo gets a name. `(define (square n) …)`, a shortcut some Lisps have, is an error here.

A body can hold several expressions. They run in order, and the function returns the value of the last one. There is no `return`.

**Extra arguments.** `&rest` before the last parameter collects any extra arguments into a list. `apply` calls a function with the elements of a list as its arguments:

```novelo
> (define total
>   (lambda (first-number &rest others)
>     (apply + first-number others)))
> (total 1 2 3)
6
> (total 5)
5
```

## `let`

`let` names intermediate values inside an expression, like local variables that are never reassigned:

```novelo
> (let ((width 3)
>       (height 4))
>   (* width height))
12
```

The bindings come in a list of pairs, `((name value) …)`, followed by the body. They are made in order, so each value can use the names before it:

```novelo
> (let ((width 3)
>       (area (* width width)))
>   area)
9
```

The names exist only inside the `let`. A name in an inner `let` hides the same name in an outer one:

```novelo
> (let ((x 1))
>   (let ((x (+ x 1)))
>     x))
2
```

## `if`

`if` takes pairs of a test and a result, and an optional `otherwise` with one last result:

```novelo
> (define sign
>   (lambda (n)
>     (if (< n 0) :negative
>         (= n 0) :zero
>         otherwise :positive)))
> (sign -5)
:negative
> (sign 0)
:zero
```

The tests run in order. The first one that is not `#false` picks its result, and the tests after it are not evaluated. If no test holds and there is no `otherwise`, the value is `#void`.

`otherwise` is required for the last branch. The two-branch `(if test yes no)` of other Lisps is an error in Novelo, to keep the tests and the results apart on the page:

```novelo
> (if (> 2 1) "yes" "no")
1:1: error :expansion-error: if must be (if c1 a1 c2 a2 …), optionally ending with otherwise and one expression (if (> 2 1) "yes" "no")
> (if (> 2 1) "yes" otherwise "no")
"yes"
```

`if` is an expression with a value, like the `? :` of C or the conditional expression of Python, so it can go anywhere a value goes:

```novelo
> (define describe
>   (lambda (n)
>     (concatenate (to-string n) " is "
>                  (if (= (remainder n 2) 0) "even" otherwise "odd"))))
> (describe 7)
"7 is odd"
```

## `and` and `or`

`and` and `or` stop as soon as the answer is known. They return one of their values, not necessarily a Boolean:

```novelo
> (and 1 2 3)
3
> (and 1 #false 3)
#false
> (or #false 2)
2
> (or #false #false)
#false
```

- `and` returns the first `#false`, or the last value when none is false.
- `or` returns the first value that is not `#false`, or `#false`.

They often replace an `if`:

```novelo
> (define teenager?
>   (lambda (age) (and (>= age 13) (<= age 19))))
> (teenager? 15)
#true
```

## `sequence`

Where only one expression fits, such as a branch of an `if`, `sequence` runs several and returns the value of the last one:

```novelo
> (sequence (display! "a\n") (display! "b\n") 3)
a
b
3
```

You will need it mostly for effects. A function that computes a value rarely does.

## Where names come from

When a function body uses a name, Novelo looks for it in this order:

1. the function's own parameters;
2. the `let`s and `lambda`s around it in the text, from the innermost outward;
3. the global names, at the moment the code runs.

Because of the third step, a function sees the current value of a global, even one defined after the function:

```novelo
> (define shipping 10)
> (define with-shipping (lambda (price) (+ price shipping)))
> (with-shipping 1)
11
> (define shipping 20)
> (with-shipping 1)
21
```

This is also how a function can call itself, or another function defined further down the file: the name is looked up when the call happens, not when the function is made. The next chapter depends on it.

## Exercises

- **`03-time-units`.**
  - Write `seconds-in-hours`.
  - Write `format-duration`, which turns 3,723 seconds into `"1h 2m 3s"`. Use `let` and the integer division of chapter 1.
- **`03-temperature`.**
  - Convert Celsius to Fahrenheit with Floats.
  - Classify a temperature with an `if` of several pairs.
- **`03-leap-year`.**
  - Write the leap year rule with `and`, `or` and `not` alone.
  - Use it in a second function.

```bash
tutorial/check.sh 03-time-units
```

The example [`02-fizzbuzz`](../examples/02-fizzbuzz/main.nvl) is a classic `if` with several pairs.

Next: [Recursion and lists](04-recursion-and-lists.md).
