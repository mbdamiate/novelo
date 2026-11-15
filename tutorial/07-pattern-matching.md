# 7. Pattern matching

Much of a program's work is asking what shape a value has and taking it apart: is this list empty, or does it have a first element? Does this map have a `:position`, and what line is in it? `match` does the asking and the taking apart in one step.

## `match`

`match` takes a value, then pairs of a pattern and a result, and an optional `otherwise`, like `if`. It picks the first pattern that fits the value:

```novelo
> (define describe
>   (lambda (value)
>     (match value
>       0 "zero"
>       (x) (concatenate "one element: " (to-string x))
>       (x y) "two elements"
>       (x &rest more) (concatenate "starts with " (to-string x))
>       otherwise "something else")))
> (describe 0)
"zero"
> (describe '(7))
"one element: 7"
> (describe '(1 2))
"two elements"
> (describe '(1 2 3))
"starts with 1"
> (describe "hi")
"something else"
```

Here is what each pattern means:

| Pattern | Matches | Names it binds |
| --- | --- | --- |
| `0` | a value `equal?` to it; numbers, strings, keywords, `#true`, `#false`, `#void` and `()` work this way | none |
| `x` | any value | `x`, to the value |
| `_` | any value | none |
| `(x y)` | a list of exactly two elements, each matching its own pattern | `x` and `y` |
| `(x &rest more)` | a list of at least one element | `x` to the first element, `more` to the list of the rest |
| `{:key p}` | a map that has the key, with a value matching `p` | the names in `p`; other keys may be present |
| `'datum` | a value `equal?` to the quoted datum, such as a symbol | none |

The names a pattern binds are visible in its result only. When nothing matches and there is no `otherwise`, the result is the error `:no-match`:

```novelo
> (match 5
>   (a b) (+ a b))
1:1: error :no-match: no pattern matches the value 5
```

The full rules are in [core §8.3](../core-specification.md#83-derived-forms).

## Tagged lists

A list that starts with a keyword is a cheap way to represent "one of several kinds of thing", as an enum with data or a tagged union would in other languages. `match` takes it apart by the tag:

```novelo
> (define area
>   (lambda (shape)
>     (match shape
>       (:circle radius) (* 3.0 radius radius)
>       (:rectangle width height) (* width height))))
> (area '(:rectangle 2 3))
6
> (area (list :circle 1.0))
3.0
```

A keyword in a pattern matches itself, and a name binds what is in its place. Each kind of shape has its own fields.

## Matching maps

A map pattern names the keys it needs and ignores the others. Patterns nest:

```novelo
> (match {:name "Ada" :born 1815}
>   {:name name} name)
"Ada"
> (match {:name "Ada"}
>   {:born born} born
>   otherwise "no year")
"no year"
> (match {:position {:line 3 :column 7}}
>   {:position {:line line}} line)
3
```

## Matching symbols and nested lists

A bare symbol in a pattern is a name and binds anything, so to match a symbol, quote it:

```novelo
> (match 'stop
>   'stop :stopped
>   otherwise :running)
:stopped
> (match '(1 (2 3))
>   (a (b c)) (list c b a))
(3 2 1)
> (match '(1 2)
>   (_ second) second)
2
```

**A pattern never compares with a variable.** `(match value limit …)` does not check whether `value` equals `limit`: it binds a new `limit` to `value`. To compare with a value you have in a name, use `if` and `equal?`. Each name can also appear only once in a pattern, so `(x x)` is an expansion error, not a test that two elements are equal.

## Recursion with `match`

A function over a list has two cases, empty or not, and `match` writes them directly:

```novelo
> (define sum
>   (lambda (numbers)
>     (match numbers
>       () 0
>       (head &rest tail) (+ head (sum tail)))))
> (sum '(1 2 3))
6
```

The results of a `match` are in tail position when the `match` is, so a `match` loop with an accumulator runs in constant memory, as in chapter 4.

## Patterns in `let`

A `let` binding can use a pattern instead of a name, to take a value apart as it is named:

```novelo
> (let (((head &rest tail) '(1 2 3)))
>   tail)
(2 3)
> (let (({:x x :y y} {:x 1 :y 2}))
>   (+ x y))
3
```

This is handy for functions that return two values in a list, such as `span` from chapter 5:

```novelo
> (let (((small large) (span (lambda (n) (< n 5)) '(1 3 5 2))))
>   (list :small small :large large))
(:small (1 3) :large (5 2))
```

A `let` pattern has no alternative, so a value that does not fit is the error `:no-match`:

```novelo
> (let (((a b) '(1 2 3)))
>   a)
1:1: error :no-match: no pattern matches the value (1 2 3)
```

## Exercises

- **`07-shapes`.** Compute the area and perimeter of shapes written as tagged lists, and describe them.
- **`07-commands`.** Interpret a list of commands that move a robot on a grid, written as tagged lists and maps.
- **`07-run-length`.** Compress `(a a a b c c)` into `((a 3) (b 1) (c 2))` and back, with `match` doing the work.

```bash
tutorial/check.sh 07-shapes
```

The example [`13-expressions`](../examples/13-expressions/main.nvl) is an evaluator of arithmetic built around `match`, and [`10-calculator`](../examples/10-calculator/main.nvl) matches a stack.

Next: [Errors and results](08-errors-and-results.md).
