# 5. Functions as values

A function in Novelo is a value like a number or a list. You can pass it to another function, return it from one, and keep it in a list or a map. This chapter shows what that buys you. In short, most of the recursion of chapter 4 never has to be written again.

## Passing functions

Chapter 4 wrote a recursion for each job. Many of those jobs have the same shape and differ only in what happens to each element. The library `list` writes each shape once and takes the varying part as a function.

**`map`** applies a function to each element:

```novelo
> (map (lambda (n) (* n n)) '(1 2 3))
(1 4 9)
> (map first '((1 2) (3 4)))
(1 3)
```

**`filter`** keeps the elements for which a predicate holds:

```novelo
> (filter (lambda (n) (> n 2)) '(1 2 3 4))
(3 4)
```

**`fold`** combines the elements into one value, from first to last. It calls `(f accumulator element)` for each element, starting from an initial value. It is the accumulator of chapter 4, packaged:

```novelo
> (fold + 0 '(1 2 3 4))
10
> (fold (lambda (reversed item) (prepend item reversed)) '() '(1 2 3))
(3 2 1)
```

**`sort`** orders a list by a function that says whether one element comes before another. It is stable, so equal elements keep their order:

```novelo
> (sort < '(3 1 2))
(1 2 3)
> (sort precedes? '("pear" "apple" "fig"))
("apple" "fig" "pear")
> (sort (lambda (a b) (< (count a) (count b))) '("pear" "apple" "fig"))
("fig" "pear" "apple")
```

`precedes?`, from the library `text`, orders strings.

**`any?`, `every?` and `span`** ask about the elements, and stop as soon as they know the answer:

```novelo
> (any? (lambda (n) (> n 2)) '(1 2 3))
#true
> (every? (lambda (n) (> n 2)) '(1 2 3))
#false
> (span (lambda (n) (< n 5)) '(1 3 5 2))
((1 3) (5 2))
```

There is no `find`. To get the first element that satisfies a predicate, `filter` and check that the result is not empty.

The function always comes first and the list last, in every function of the standard library.

## Pipelines

These functions combine into a pipeline that reads from the inside out. For example, the sum of the squares of the even numbers from 1 to 10:

```novelo
> (define even? (lambda (n) (= (remainder n 2) 0)))
> (fold + 0 (map (lambda (n) (* n n)) (filter even? (range 1 11))))
220
```

Each step makes a new list and the next step reads it. Nothing is changed in place, so any intermediate list can be named with `let` and checked on its own. That is often the clearest way to write a longer pipeline:

```novelo
> (define even? (lambda (n) (= (remainder n 2) 0)))
> (let ((evens (filter even? (range 1 11)))
>       (squares (map (lambda (n) (* n n)) evens)))
>   (fold + 0 squares))
220
```

## Returning functions

A `lambda` can use the names around it. The function it makes keeps them, even after the function that made it has returned. Such a function is called a *closure*:

```novelo
> (define make-adder
>   (lambda (amount)
>     (lambda (n) (+ n amount))))
> (define add-ten (make-adder 10))
> (add-ten 5)
15
> (map (make-adder 1) '(1 2 3))
(2 3 4)
```

`(make-adder 10)` returns a function that remembers `amount` as 10. Each call of `make-adder` makes a different function with its own `amount`. Since values never change, what a closure remembers never changes either.

## `partial` and composition

Fixing the first arguments of a function is common enough that the library `function` has it. `(partial f a …)` is the function that calls `f` with `a …` before its own arguments:

```novelo
> (define add-one (partial + 1))
> (add-one 41)
42
> (map (partial * 2) '(1 2 3))
(2 4 6)
```

Its definition, in [stdlib §4.3](../standard-library.md#43-function), is three lines long. Functions that combine functions are short to write. Here is composition, the function that applies one function and then another:

```novelo
> (define compose
>   (lambda (outer inner)
>     (lambda (value) (outer (inner value)))))
> ((compose to-string (partial * 2)) 21)
"42"
```

`apply`, seen in chapter 3, calls a function with the elements of a list. It turns a function of many arguments into a function of a list:

```novelo
> (apply max '(3 9 2))
9
```

## Functions that take functions

You can write your own function that takes a function. Here is a `count-if` that counts the elements for which a predicate holds:

```novelo
> (define count-if
>   (lambda (keep? items)
>     (fold (lambda (total item) (if (keep? item) (+ total 1) otherwise total))
>           0 items)))
> (count-if (lambda (word) (> (count word) 3)) '("a" "pear" "fig" "apple"))
2
```

Writing it with `fold` instead of a recursion means it is already in tail position, for a list of any size.

## Exercises

- **`05-pipelines`.** Answer three questions about a list of words, each with a pipeline of `map`, `filter`, `fold` and `sort`.
- **`05-closures`.** Write `make-multiplier`, `compose-all` and `repeat-function`, three functions that return functions.
- **`05-own-map`.** Write `map` and `filter` yourself, with `fold`, and then `group-adjacent`, which groups the equal neighbors of a list.

```bash
tutorial/check.sh 05-pipelines
```

The examples [`05-word-count`](../examples/05-word-count/main.nvl) and [`06-unique`](../examples/06-unique/main.nvl) are built from pipelines like these.

Next: [Immutable data](06-immutable-data.md).
