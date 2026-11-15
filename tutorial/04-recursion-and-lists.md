# 4. Recursion and lists

Novelo has no `for` and no `while`. It has no assignment either, so a loop counter could not change anyway. Repetition is written as a function that calls itself. This chapter shows how, using lists, the structure that recursion fits best.

## Taking a list apart

A list is either empty, `()`, or a first element followed by the rest of the list. Three primitives work with that shape, and `empty?` tells which case you have:

```novelo
> (define numbers '(3 1 4))
> (first numbers)
3
> (rest numbers)
(1 4)
> (rest '(4))
()
> (prepend 9 numbers)
(9 3 1 4)
> numbers
(3 1 4)
> (empty? '())
#true
```

- `first` and `rest` take a list apart.
- `prepend` builds a new list with one more element in front.

`numbers` is still `(3 1 4)` afterwards: `prepend` made a new list and left the old one alone. Nothing in Novelo changes a value. The new list shares the old one as its rest, so `prepend` costs the same for any length.

`first` and `rest` of an empty list are errors, so check `empty?` before you take a list apart:

```novelo
> (first '())
1:1: error :wrong-type: first expects a non-empty List, got () ()
```

## Recursion

To add up a list:

- the sum of an empty list is 0;
- the sum of any other list is its first element plus the sum of the rest.

That is a function:

```novelo
> (define sum
>   (lambda (numbers)
>     (if (empty? numbers) 0
>         otherwise (+ (first numbers) (sum (rest numbers))))))
> (sum '(3 1 4))
8
```

`sum` calls itself on a shorter list, and the empty list ends the calls. Almost every function over a list has these same two parts: a result for the empty list, and a way to combine the first element with the result for the rest.

A function builds a list the same way, with `prepend`:

```novelo
> (define count-down
>   (lambda (n)
>     (if (= n 0) '()
>         otherwise (prepend n (count-down (- n 1))))))
> (count-down 5)
(5 4 3 2 1)
```

## Tail calls

In `sum`, the addition happens after the recursive call returns, so every call has to wait for the one after it. A list of 100,000 numbers keeps 100,000 calls open at once. The interpreter allows that, but not without limit: somewhere past a million calls, depending on the machine, it stops with `:stack-overflow`.

The way out is to leave nothing to do after the call. Carry the work done so far in a parameter, usually called an *accumulator*:

```novelo
> (define sum-from
>   (lambda (numbers total)
>     (if (empty? numbers) total
>         otherwise (sum-from (rest numbers) (+ total (first numbers))))))
> (sum-from '(3 1 4) 0)
8
> (sum-from (range 0 1000000) 0)
499999500000
```

Here the recursive call is the last thing the function does: it is in **tail position**. Novelo guarantees that a call in tail position replaces the call it came from instead of piling on top of it, so `sum-from` runs in constant memory for a list of any length. That guarantee is what makes recursion a complete replacement for loops.

A call is in tail position when its value becomes the value of the function as it is:
- the last expression of a `lambda`;
- each result of an `if`, a `let` body or a `match` (chapter 7), when the form itself is in tail position;
- the last argument of `and` and `or`.

`(+ (first numbers) (sum …))` is not a tail call, because `+` still has work to do with the result.

An accumulator that builds a list collects the elements in reverse, since `prepend` adds at the front. Either count in the other direction, or reverse the list at the end:

```novelo
> (define up-to
>   (lambda (n numbers)
>     (if (= n 0) numbers
>         otherwise (up-to (- n 1) (prepend n numbers)))))
> (up-to 5 '())
(1 2 3 4 5)
```

A function such as `sum-from` has an extra parameter that its callers should not have to fill in. Wrap it in a function with a cleaner interface:

```novelo
> (define sum-from
>   (lambda (numbers total)
>     (if (empty? numbers) total
>         otherwise (sum-from (rest numbers) (+ total (first numbers))))))
> (define sum (lambda (numbers) (sum-from numbers 0)))
> (sum '(3 1 4))
8
```

## Recursion on numbers

Recursion is not only for lists. Anything that gets smaller on each call works, such as a number on its way to zero. `quotient` and `remainder` by 10 take a number apart digit by digit:

```novelo
> (define count-digits
>   (lambda (n)
>     (if (< n 10) 1
>         otherwise (+ 1 (count-digits (quotient n 10))))))
> (count-digits 2026)
4
```

## The library `list`

Writing every recursion by hand gets old. The library `list` ([stdlib §4.1](../standard-library.md#41-list)) has the common ones, written in Novelo with tail calls, and the REPL imports it for you. A program imports it with `(import list)`. Some of them:

```novelo
> (range 0 5)
(0 1 2 3 4)
> (reverse '(1 2 3))
(3 2 1)
> (append '(1 2) '(3 4))
(1 2 3 4)
> (take 2 '(a b c d))
(a b)
> (drop 2 '(a b c d))
(c d)
> (count '(a b c))
3
> (zip '(1 2 3) '(:a :b :c))
((1 :a) (2 :b) (3 :c))
```

`range` leaves out its end, like `substring`. `count` is a primitive, not part of `list`, and also counts strings, maps and sets.

The most useful functions of `list`, namely `map`, `filter`, `fold` and `sort`, take another function as an argument. They are the subject of the next chapter.

To read the source of a library function, open its section in [`standard-library.md`](../standard-library.md). The libraries are written in Novelo, so their code is a good place to see recursion in practice.

## Exercises

- **`04-digits`.**
  - Add up the digits of a number.
  - Turn a number into the list of its digits.
- **`04-list-functions`.** Write `size`, `largest`, `occurrences` and `last-element` by recursion, without the library `list`.
- **`04-accumulators`.**
  - Running totals: `(1 2 3)` becomes `(1 3 6)`.
  - Interleave two lists.

  Both with tail calls.

```bash
tutorial/check.sh 04-digits
```

The examples [`03-recursion`](../examples/03-recursion/main.nvl) and [`04-lists`](../examples/04-lists/main.nvl) cover the same ground with more commentary.
