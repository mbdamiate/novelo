# 8. Errors and results

Things go wrong in two different ways.

- **A mistake in the program.** It calls `first` on an empty list, or adds a string to a number. Nothing sensible can happen next, so the program stops with an *error*.
- **An expected failure.** A user types "abc" where a number belongs, or a file is missing. The program should handle it like any other case. Novelo's style is to return it as a value, a *result*.

This chapter covers both, and how to tell which one you have.

## Raising an error

`error` stops the evaluation with a message and any values that help explain it:

```novelo
> (error "age must be positive" -3)
1:1: error :error: age must be positive -3
```

Every error carries a map, its *descriptor*, with the keys `:kind`, `:message` and `:values`. Once the error has passed through code with a known place in the source, the descriptor also has `:position`, and once it has left a function, `:trace`. `error` always makes the kind `:error`. The primitives use their own kinds, such as `:division-by-zero`, `:wrong-type` and `:missing-key`. The full list is in [core §8.5](../core-specification.md#85-side-effects-and-errors).

To raise an error of your own kind, build the descriptor yourself and pass it to `raise`:

```novelo
> (raise {:kind :out-of-stock :message "no pens left" :values '("pen")})
1:1: error :out-of-stock: no pens left "pen"
```

## Catching an error

`try` evaluates an expression. If the expression raises an error, `try` matches the descriptor against its patterns, the same patterns as `match`:

```novelo
> (try (quotient 10 0)
>   {:kind :division-by-zero} 0)
0
> (try (error "bad input" 1 2)
>   {:kind :error :message message :values values} (list message values))
("bad input" (1 2))
```

An error that no pattern matches goes on as if the `try` were not there:

```novelo
> (try (first '())
>   {:kind :division-by-zero} 0)
1:6: error :wrong-type: first expects a non-empty List, got () ()
```

A name as the pattern matches every descriptor:

```novelo
> (try (quotient 1 0)
>   problem (get problem :kind))
:division-by-zero
```

`try` is written in terms of the primitive `catch`, which takes two functions: the body to run, and a handler that receives the descriptor.

```novelo
> (catch (lambda () (quotient 10 0))
>        (lambda (problem) (get problem :kind)))
:division-by-zero
```

Catching only goes outward. Once an error is caught, the work that raised it is abandoned, and there is no way to resume it.

## Traces

When an error leaves a function, the message lists the calls it passed through, innermost first:

```novelo
> (define inner (lambda (n) (quotient n 0)))
> (define outer (lambda (n) (+ 1 (inner n))))
> (outer 5)
1:27: error :division-by-zero: quotient: division by zero
  in inner, called at 1:32
  in outer, called at 1:1
```

A call in tail position replaces its caller (chapter 4), so the caller is no longer in progress and does not appear in the trace. Here `parse-age` calls `check-age` as its last act:

```novelo
> (define check-age
>   (lambda (age)
>     (if (< age 0) (error "age must be positive" age)
>         otherwise age)))
> (define parse-age
>   (lambda (text) (check-age (string-to-number text))))
> (parse-age "-3")
3:19: error :error: age must be positive -3
  in check-age, called at 2:18
```

In the REPL, positions count from the start of each input, so `2:18` is in the input that defined `parse-age`. In a program, they count from the start of the file and name it.

## Results

An error is the right answer to a mistake. It is the wrong answer to bad input, which is not a mistake at all: a parser that receives text is supposed to handle text that is not valid. Catching errors for that is clumsy. Nothing in a function's call shows that it may raise, so it is easy to forget the `try`.

The alternative is to make the failure part of the return value. A **result** is either `(:ok value)` or `(:error detail …)`. The library `result` ([stdlib §4.7](../standard-library.md#47-result)) builds them and works with them:

```novelo
> (define safe-divide
>   (lambda (a b)
>     (if (= b 0) (failure "division by zero")
>         otherwise (ok (quotient a b)))))
> (safe-divide 10 2)
(:ok 5)
> (safe-divide 1 0)
(:error "division by zero")
```

The caller cannot use the value without first looking at which case it has, and `match` is the natural way to look:

```novelo
> (define safe-divide
>   (lambda (a b)
>     (if (= b 0) (failure "division by zero")
>         otherwise (ok (quotient a b)))))
> (match (safe-divide 1 0)
>   (:ok value) value
>   (:error message) (concatenate "failed: " message))
"failed: division by zero"
```

The libraries `json`, `csv`, `time` and `io` return results for exactly this kind of failure:

```novelo
> (read-json "[1, 2]")
(:ok (1 2))
> (read-json "[1, 2")
(:error "expected , or ]" 1 6)
```

## Chaining results

A computation of several steps that may each fail would nest one `match` inside another, one per step. `then` passes the value of an `(:ok …)` on to a function, and returns a failure as it is:

```novelo
> (define safe-divide
>   (lambda (a b)
>     (if (= b 0) (failure "division by zero")
>         otherwise (ok (quotient a b)))))
> (then (safe-divide 10 2) (lambda (n) (ok (+ n 1))))
(:ok 6)
> (then (safe-divide 1 0) (lambda (n) (ok (+ n 1))))
(:error "division by zero")
```

`chain` writes a sequence of such steps flat, like a `let`. Each binding runs only if the one before it worked:

```novelo
> (define safe-divide
>   (lambda (a b)
>     (if (= b 0) (failure "division by zero")
>         otherwise (ok (quotient a b)))))
> (chain then ((a (safe-divide 100 5))
>              (b (safe-divide a 2)))
>   (ok (+ a b)))
(:ok 30)
> (chain then ((a (safe-divide 100 0))
>              (b (safe-divide a 2)))
>   (ok (+ a b)))
(:error "division by zero")
```

In the second case, `(safe-divide a 2)` never runs: the first failure is the value of the whole `chain`. If you know `async`/`await`, or Rust's `?` operator, it is the same idea. `chain` takes the step function as its first argument, so `then` is only one choice ([core §8.3](../core-specification.md#83-derived-forms)).

`map-results` applies a function that returns results to each element of a list. It gives the list of values, or the first failure:

```novelo
> (define safe-divide
>   (lambda (a b)
>     (if (= b 0) (failure "division by zero")
>         otherwise (ok (quotient a b)))))
> (map-results (lambda (b) (safe-divide 12 b)) '(1 2 3))
(:ok (12 6 4))
> (map-results (lambda (b) (safe-divide 12 b)) '(1 0 3))
(:error "division by zero")
```

## Which one to use

| Situation | Use |
| --- | --- |
| a bug: a broken assumption, a wrong type, an impossible case | an error, and usually do not catch it |
| a failure the caller should expect and handle: bad input, a missing file | a result |
| an error from code you do not control, which you want to turn into a value | `try` around the call, returning a result |

## Exercises

- **`08-parse-date`.** Read `"2026-11-15"` into a map, returning a result that says what is wrong with a bad date. Use `chain`.
- **`08-safe-calls`.** Turn the errors of a few primitives into results with `try`, and add up a list of texts while reporting the first one that is not a number.

```bash
tutorial/check.sh 08-parse-date
```

The examples [`07-errors`](../examples/07-errors/main.nvl), [`12-json`](../examples/12-json/main.nvl) and [`13-expressions`](../examples/13-expressions/main.nvl) show errors and results in larger programs.
