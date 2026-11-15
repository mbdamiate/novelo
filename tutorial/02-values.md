# 2. Values

Every Novelo program computes with a handful of value types. None of them can be changed once made. This chapter goes through them, then through the two questions every language has to answer about its values: when are two values equal, and which values count as false.

The full table is [core §2](../core-specification.md#2-data-model).

## Integers

An Integer is a whole number of 64 bits. When a result does not fit, Novelo raises an error instead of wrapping around to a wrong number:

```novelo
> 9223372036854775807
9223372036854775807
> (+ 9223372036854775807 1)
1:1: error :integer-overflow: +: result outside 64 bits
```

## Floats

A Float is a number with a fractional part, an IEEE 754 double, as in most languages. A Float literal needs digits on both sides of the point, so write `1.0` or `6.02e23`, never `1.` or `.5`.

```novelo
> 3.5
3.5
> (+ 0.1 0.2)
0.30000000000000004
> (/ 7.0 2.0)
3.5
> (sqrt 2.0)
1.4142135623730951
```

Integers and Floats never mix. Python would quietly turn `1 + 2.5` into `3.5`; Novelo calls it an error, because the conversion can lose precision (an Integer above 2^53 has no exact Float):

```novelo
> (+ 1 2.5)
1:1: error :wrong-type: + cannot mix Integers and Floats; convert with integer-to-float or truncate 1 2.5
> (+ (integer-to-float 1) 2.5)
3.5
> (truncate 3.9)
3
> (/ 7 2)
1:1: error :wrong-type: / divides Floats; divide Integers with quotient and remainder 7
```

`integer-to-float` and `truncate` convert one way and the other. `float-to-fixed` writes a Float with a fixed number of decimals, which is what you want for money and reports:

```novelo
> (float-to-fixed 3.14159 2)
"3.14"
```

A Float is always finite. There is no infinity and no NaN: an operation whose result would be one raises an error instead.

## Strings

A String is text. Its length and its positions count characters (Unicode code points), not bytes. There is no separate character type: a character is a String of length 1.

```novelo
> "café"
"café"
> (count "café")
4
> (string-at "café" 3)
"é"
> (substring "hello" 1 3)
"el"
> (concatenate "Nov" "e" "lo")
"Novelo"
```

`substring` takes the start and the end, and leaves the end out, so `(substring "hello" 1 3)` takes positions 1 and 2.

The escapes are `\n`, `\t`, `\r`, `\"`, `\\` and `\u{…}` with a code point in hexadecimal:

```novelo
> "caf\u{e9}"
"café"
```

Converting between text and numbers:

```novelo
> (to-string 42)
"42"
> (string-to-number "42")
42
> (string-to-number "4.5")
4.5
> (string-to-number "forty-two")
```

That last line printed nothing. The REPL prints nothing for `#void`, which is what `string-to-number` returns for a text that is not a number. More on `#void` below.

## Booleans and `#void`

`#true` and `#false` are the Booleans. `#void` is a value of its own that means "nothing here": `display!` returns it, an `if` without an `otherwise` returns it when its test fails, and functions such as `string-to-number` return it when there is no answer. `void?` tests for it:

```novelo
> (void? (string-to-number "forty-two"))
#true
```

## Keywords

A keyword is a name that stands for itself, written with a colon: `:red`, `:not-found`, `:name`. Use keywords where another language would use an enum, a constant string or an atom:

```novelo
> :red
:red
> (keyword? :red)
#true
```

Error kinds such as `:division-by-zero` are keywords, and so are the usual keys of maps (chapter 6).

## Lists

A List is a sequence of values of any types. `list` builds one from its arguments, evaluated:

```novelo
> (list 1 (+ 1 1) "three" :four)
(1 2 "three" :four)
> (list)
()
```

`()` is the empty list. Lists are the most common structure in a Lisp, and chapter 4 is all about working with them.

## Symbols and `quote`

Look again at how a list prints: `(1 2 "three" :four)`. That is also how a call is written. In a Lisp, code is written as lists. `(+ 1 2)` is a list of three elements, the symbol `+` and two numbers, and evaluating it calls the function named by the symbol.

`quote`, or the shorter `'`, stops evaluation and gives you the code itself as a value:

```novelo
> (quote (+ 1 2))
(+ 1 2)
> '(+ 1 2)
(+ 1 2)
> (first '(+ 1 2))
+
> (symbol? (first '(+ 1 2)))
#true
```

A symbol is a name, like `+`, `count` or `hello`. Evaluated, it means the value the name is bound to, and evaluating a name with nothing bound to it is an error, as chapter 1 showed. Quoted, it is just the name. You will use quoted lists for constant data, `'(1 2 3)`. Writing code that writes code is the subject of chapter 11.

## Maps and Sets

Two more collections, written with braces, complete the picture. Chapter 6 covers them:

```novelo
> {:name "Ada" :born 1815}
{:name "Ada" :born 1815}
> #{1 2 3}
#{1 2 3}
```

## Asking for a type

Each type has a predicate, a function that answers `#true` or `#false`. By convention, a predicate's name ends in `?`:

```novelo
> (integer? 1)
#true
> (integer? 1.0)
#false
> (string? "1")
#true
> (list? '())
#true
```

The others are `boolean?`, `void?`, `float?`, `symbol?`, `keyword?`, `map?`, `set?` and `function?`.

## Equality

There are two equality functions.

- **`equal?`** works on any two values. It is `#true` when they have the same type and the same contents, so it compares lists and strings element by element. It is the one you will use most.
- **`=`** compares numbers only, and catches by mistake a comparison of something that is not a number.

```novelo
> (equal? (list 1 2) (list 1 2))
#true
> (equal? "a" "a")
#true
> (equal? 1 1.0)
#false
> (= 1 1)
#true
> (= "a" "a")
1:1: error :wrong-type: = expects a number, got a String "a"
```

`(equal? 1 1.0)` is `#false` because an Integer and a Float are different types. `(= 1 1.0)` is an error, for the same reason `(+ 1 1.0)` is.

## What counts as false

This is short: **only `#false` is false.** `0`, `""`, `()` and `#void` all count as true:

```novelo
> (if 0 "true" otherwise "false")
"true"
> (if '() "true" otherwise "false")
"true"
> (if #void "true" otherwise "false")
"true"
> (if #false "true" otherwise "false")
"false"
```

`if` gets a proper introduction in the next chapter. Here it only shows which branch runs. To ask whether a list or a string is empty, ask directly with `empty?`. To ask whether something is `#void`, use `void?`. `not` turns `#false` into `#true` and every other value into `#false`.

```novelo
> (empty? "")
#true
> (not 0)
#false
```

## Exercises

- **`02-values`.** Build four values: an average as a Float, initials taken from a name, a list of keywords, and a price as text.

  ```bash
  tutorial/check.sh 02-values
  ```

The example [`11-statistics`](../examples/11-statistics/main.nvl) mixes Integers and Floats with care.

Next: [Names and functions](03-names-and-functions.md).
