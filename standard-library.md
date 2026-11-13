# Standard Library — Novelo

Nov 10, 2026 · @Matheus Damiate

## 1. Overview

The standard library is a set of functions written in the language itself, on top of the core primitives. It is not part of the core: an implementation conformant to the core does not need to provide it, and the core tests do not depend on it.

The standard library is made of six libraries, in the sense of core section 8.9: `list`, `text`, `function`, `map`, `set` and `number`. Nothing is loaded before the program: a file imports the libraries it uses, such as `(import list)`, and only then sees their names. Imported with no prefix, the names are ordinary globals, and a program may redefine them. Imported with a prefix, as in `(import list l)`, they are bound as `l/map` and so on.

The reference definitions in section 4 are normative. The implementation MAY replace any of them with a native version, as long as the observable behavior is identical, including errors and the names each library binds.

**Version:** 0.7.0, draft, on top of core 0.10.0.

**Changes since 0.6.0.** The library `text` lays out text: `pad-left` and `pad-right` fill a text up to a width, and `format` puts values into a template at each `{}` (sections 3 and 4.2). With `float-to-fixed` of core 0.10.0, they print tables and numbers with fixed decimals. The other libraries are unchanged.

**Changes since 0.5.0.** The library `number` follows the type Float of core 0.9.0: `max` and `min` take all Integers or all Floats, and four new functions, `abs`, `floor`, `ceiling` and `round`, are written over `truncate` and `integer-to-float` (sections 3 and 4.6). `floor`, `ceiling` and `round` take a Float and return an Integer, and `round` takes a value exactly halfway to the integer farther from zero. The other libraries are unchanged.

**Changes since 0.4.0.** The library `text` compares, classifies and changes the case of text, over the code point conversions of core 0.8.0: `precedes?`, `uppercase`, `lowercase`, `letter?` and `digit?`. Letters and case cover ASCII and Latin-1 (section 2). A new library, `number`, with `max` and `min` (sections 3 and 4.6). The other libraries are unchanged.

**Changes since 0.3.0.** The library `set` (sections 3 and 4.5), with `from-list`, `union`, `intersection`, `difference` and `subset?`, over the type Set of core 0.5.0. The other libraries are unchanged.

**Changes since 0.2.1.** The library `map` (sections 3 and 4.4), with `values`, `entries`, `from-entries`, `update`, `merge`, `map-values`, `filter-entries` and `fold-entries`. The other libraries are unchanged.

**Changes since 0.2.0.** The language is named Novelo (core 0.2.2); the cases in `stdlib-conformance.json` use `.nvl` file names. The libraries are unchanged.

**Changes since 0.1.0.** The library is split into `list`, `text` and `function`, each imported explicitly with `import`, instead of being loaded before every program.

## 2. General rules

- **Lists only.** The sequence functions of `list` and `text` take and return List. Strings and maps are not sequences here: passing one of them produces whatever error the primitive used internally produces, usually `:wrong-type`. The functions of `map` take and return Map, and those of `set` take and return Set; they traverse a map in the order of `keys` and a set in the order of `elements` (core section 9), so the order in which they call a function is unspecified, like those orders.
- **The function comes first and the collection last**, as in `(map f xs)` and `(map-values f m)`. This makes partial application with `partial` easier. `merge` takes two maps and `update` takes the key, the function and then the map.
- **Evaluation is eager.** Every function traverses the whole list and returns the finished result.
- **Constant memory.** Every function that traverses a list does so by tail recursion, so lists of any size are traversed in constant memory.
- **Helpers.** Since the core has no local recursion, some functions use helpers defined at the top level of their library: `take-step`, `range-step`, `merge-sorted`, `split-step`, `precedes-step`, `padding`, `repeat-step`, `format-step`, `letter-code-point?`, `uppercase-code-point` and `lowercase-code-point`. They are bound by `import` like the other names, but they are not part of the interface: a program should not rely on them, and a native implementation MAY omit them.
- **Dependencies.** `text`, `function`, `map`, `set` and `number` import `list` (core 8.9). Importing any of them therefore also binds the names of `list` in the same context. The names of all the libraries are different, so they can be imported together without a prefix.
- **Letters and case.** `text` knows two ranges of letters: ASCII, `A` to `Z` and `a` to `z`, and Latin-1, U+00C0 to U+00FF without U+00D7 `×` and U+00F7 `÷`, which covers the accented letters of Portuguese and the other languages of Western Europe, such as `á`, `ç`, `ê` and `õ`. `uppercase` and `lowercase` pair `A`–`Z` with `a`–`z` and U+00C0–U+00DE with U+00E0–U+00FE; `ß` (U+00DF) and `ÿ` (U+00FF) have no pair in these ranges and stay as they are. Every other character, in any script, is not a letter and keeps its case. `digit?` knows only `0` to `9`, the digits `string-to-number` reads.
- **Order of text.** `precedes?` orders by code point, not as a dictionary does: every uppercase ASCII letter comes before every lowercase one, so `"Zebra"` precedes `"apple"`, and accented letters come after `z`. To ignore case, compare the `lowercase` of both.

## 3. Functions

| Library | Function | Usage | Result |
| --- | --- | --- | --- |
| `list` | `reverse` | `(reverse xs)` | the elements in reverse order |
| `list` | `map` | `(map f xs)` | `f` applied to each element |
| `list` | `filter` | `(filter keep? xs)` | the elements for which `keep?` is true |
| `list` | `fold` | `(fold f initial xs)` | combines from first to last, calling `(f accumulator element)` |
| `list` | `append` | `(append xs ys)` | the elements of `xs` followed by those of `ys` |
| `list` | `take`, `drop` | `(take n xs)`, `(drop n xs)` | the first `n` elements, or what remains without them; negative `n` is an error |
| `list` | `range` | `(range start end)` | the integers from `start` up to before `end`; `()` if `start` is not less than `end` |
| `list` | `any?`, `every?` | `(any? matches? xs)` | `#true` if any, or all, satisfy it; they stop as soon as the answer is decided |
| `list` | `sort` | `(sort less? xs)` | the elements ordered by `less?`, stably |
| `text` | `split` | `(split separator text)` | List of the parts of `text`; an empty separator is an error |
| `text` | `join` | `(join separator parts)` | the strings in `parts` joined by `separator` |
| `text` | `precedes?` | `(precedes? a b)` | `#true` if `a` comes strictly before `b` in code point order: at the first position where they differ, `a` has the smaller code point, or `a` is a proper prefix of `b` |
| `text` | `uppercase`, `lowercase` | `(uppercase text)` | `text` with each letter that has a pair in section 2 changed to the other case |
| `text` | `pad-left`, `pad-right` | `(pad-left width fill text)` | `text` with copies of the one-character String `fill` before it, or after it, until it has `width` characters; a `text` of `width` characters or more comes back unchanged; a `fill` of another length is the error `:error` |
| `text` | `format` | `(format template value …)` | `template` with each `{}`, from left to right, replaced by the `to-string` of the next value, so a String goes in without quotes; there is no escape, and a `{` or `}` not part of a `{}` stays as it is; a number of values different from the number of `{}` is the error `:error` |
| `text` | `letter?`, `digit?` | `(letter? text)` | `#true` if `text` is not empty and every one of its characters is a letter, or a digit, in the sense of section 2 |
| `function` | `partial` | `(partial f a …)` | the function that calls `f` with `a …` before the arguments it receives |
| `map` | `values` | `(values m)` | List of the values, in the order of `(keys m)` |
| `map` | `entries` | `(entries m)` | List of the pairs `(key value)`, in the order of `(keys m)` |
| `map` | `from-entries` | `(from-entries pairs)` | the map with each pair `(key value)`; a later pair replaces an earlier one with the same key; an element that is not a List of two elements is an error |
| `map` | `update` | `(update key f m)` | `m` with the value at `key` replaced by `f` applied to it; a missing key is an error, as in `get` |
| `map` | `merge` | `(merge m other)` | the pairs of both maps; for a key in both, the value of `other` |
| `map` | `map-values` | `(map-values f m)` | the same keys, with `f` applied to each value |
| `map` | `filter-entries` | `(filter-entries keep? m)` | the pairs for which `(keep? key value)` is true |
| `map` | `fold-entries` | `(fold-entries f initial m)` | combines the pairs in the order of `(keys m)`, calling `(f accumulator key value)` |
| `set` | `from-list` | `(from-list xs)` | the set of the elements of `xs`; repeated elements count once |
| `set` | `union` | `(union a b)` | the elements in `a` or in `b` |
| `set` | `intersection` | `(intersection a b)` | the elements in both `a` and `b` |
| `set` | `difference` | `(difference a b)` | the elements of `a` that are not in `b` |
| `set` | `subset?` | `(subset? a b)` | `#true` if every element of `a` is in `b` |
| `number` | `max`, `min` | `(max n …)` | the largest, or smallest, of one or more numbers, all Integers or all Floats; with no argument, `:wrong-arguments`; an argument that is not a number, or a mix of Integers and Floats, `:wrong-type`. For a list, `(apply max xs)` |
| `number` | `abs` | `(abs n)` | the absolute value of an Integer or a Float, of the same type; `(abs -0.0)` is `0.0`, and the smallest Integer gives `:integer-overflow` |
| `number` | `floor`, `ceiling` | `(floor x)` | the largest Integer not greater than the Float `x`, or the smallest not less than it: `(floor -2.5)` is `-3`, `(ceiling 2.1)` is `3`; an Integer gives `:wrong-type`, a result outside the range of Integer `:integer-overflow` |
| `number` | `round` | `(round x)` | the Integer nearest to the Float `x`; exactly halfway, the one farther from zero: `(round 2.5)` is `3`, `(round -2.5)` is `-3`; errors as `floor` |

There is no `find`. To get the first element that satisfies a predicate, take `first` of the `filter` result after checking that it is not empty; to only ask whether such an element exists, use `any?`. To combine from last to first, `fold` the `reverse` of the list.

## 4. Reference definitions

The source of each library is the concatenation, in order, of the `lisp` code blocks in its section (4.1 for `list`, 4.2 for `text`, 4.3 for `function`, 4.4 for `map`, 4.5 for `set`, 4.6 for `number`). The definitions use only the core and the libraries they import. Parameter names avoid those of the primitives, such as `rest` and `count`, because a parameter with the same name would shadow the primitive inside the function. The order of the definitions does not matter: global names are resolved at call time.

### 4.1 `list`

#### Reduction and transformation

```lisp
(define fold
  (lambda (f acc xs)
    (if (empty? xs) acc
        otherwise (fold f (f acc (first xs)) (rest xs)))))

(define reverse
  (lambda (xs)
    (fold (lambda (acc x) (prepend x acc)) () xs)))

(define map
  (lambda (f xs)
    (reverse (fold (lambda (acc x) (prepend (f x) acc)) () xs))))

(define filter
  (lambda (keep? xs)
    (reverse (fold (lambda (acc x) (if (keep? x) (prepend x acc) otherwise acc))
                   () xs))))
```

#### Lists

```lisp
(define append
  (lambda (xs ys)
    (fold (lambda (r x) (prepend x r)) ys (reverse xs))))

(define take
  (lambda (wanted xs)
    (if (< wanted 0) (error "take: negative count" wanted)
        otherwise (reverse (take-step wanted xs ())))))

(define take-step
  (lambda (n xs acc)
    (if (or (= n 0) (empty? xs)) acc
        otherwise (take-step (- n 1) (rest xs) (prepend (first xs) acc)))))

(define drop
  (lambda (wanted xs)
    (if (< wanted 0) (error "drop: negative count" wanted)
        otherwise (if (or (= wanted 0) (empty? xs)) xs
                      otherwise (drop (- wanted 1) (rest xs))))))

(define range
  (lambda (start end)
    (range-step start end ())))

(define range-step
  (lambda (start n acc)
    (if (<= n start) acc
        otherwise (range-step start (- n 1) (prepend (- n 1) acc)))))
```

#### Search and sorting

```lisp
(define any?
  (lambda (matches? xs)
    (if (empty? xs) #false
        otherwise (if (matches? (first xs)) #true
                      otherwise (any? matches? (rest xs))))))

(define every?
  (lambda (matches? xs)
    (not (any? (lambda (x) (not (matches? x))) xs))))

(define sort
  (lambda (less? xs)
    (let ((size (count xs))
          (half (quotient size 2)))
      (if (< size 2) xs
          otherwise (merge-sorted less?
                                  (sort less? (take half xs))
                                  (sort less? (drop half xs))
                                  ())))))

(define merge-sorted
  (lambda (less? a b acc)
    (if (empty? a) (fold (lambda (r x) (prepend x r)) b acc)
        otherwise (if (empty? b) (fold (lambda (r x) (prepend x r)) a acc)
                      otherwise (if (less? (first b) (first a))
                                    (merge-sorted less? a (rest b) (prepend (first b) acc))
                                    otherwise (merge-sorted less? (rest a) b (prepend (first a) acc)))))))
```

### 4.2 `text`

```lisp
(import list)

(define join
  (lambda (separator parts)
    (if (empty? parts) ""
        otherwise (fold (lambda (acc part) (concatenate acc separator part))
                        (first parts) (rest parts)))))

(define split
  (lambda (separator text)
    (if (empty? separator) (error "split: empty separator")
        otherwise (reverse (split-step separator (count separator) text (count text)
                                       0 0 ())))))

(define split-step
  (lambda (separator width text limit start position acc)
    (if (> (+ position width) limit)
        (prepend (substring text start limit) acc)
        otherwise
        (if (equal? (substring text position (+ position width)) separator)
            (split-step separator width text limit (+ position width) (+ position width)
                        (prepend (substring text start position) acc))
            otherwise (split-step separator width text limit start (+ position 1) acc)))))
```

The functions that look at characters convert the text to its code points once (core section 9), instead of reading it one `string-at` at a time.

```lisp
(define precedes?
  (lambda (a b)
    (precedes-step (string-to-code-points a) (string-to-code-points b))))

(define precedes-step
  (lambda (xs ys)
    (if (empty? ys) #false
        (empty? xs) #true
        (< (first xs) (first ys)) #true
        (> (first xs) (first ys)) #false
        otherwise (precedes-step (rest xs) (rest ys)))))

(define letter-code-point?
  (lambda (n)
    (or (<= 65 n 90)
        (<= 97 n 122)
        (and (<= 192 n 255) (not (= n 215)) (not (= n 247))))))

(define uppercase-code-point
  (lambda (n)
    (if (or (<= 97 n 122) (and (<= 224 n 254) (not (= n 247)))) (- n 32)
        otherwise n)))

(define lowercase-code-point
  (lambda (n)
    (if (or (<= 65 n 90) (and (<= 192 n 222) (not (= n 215)))) (+ n 32)
        otherwise n)))

(define uppercase
  (lambda (text)
    (code-points-to-string (map uppercase-code-point (string-to-code-points text)))))

(define lowercase
  (lambda (text)
    (code-points-to-string (map lowercase-code-point (string-to-code-points text)))))

(define letter?
  (lambda (text)
    (let ((code-points (string-to-code-points text)))
      (and (not (empty? code-points)) (every? letter-code-point? code-points)))))

(define digit?
  (lambda (text)
    (let ((code-points (string-to-code-points text)))
      (and (not (empty? code-points)) (every? (lambda (n) (<= 48 n 57)) code-points)))))
```

`pad-left` and `pad-right` count characters, that is code points, as `count` does. `padding` builds the missing characters as a list of code points, which is empty when the text is already wide enough.

```lisp
(define pad-left
  (lambda (width fill text)
    (concatenate (padding "pad-left" width fill text) text)))

(define pad-right
  (lambda (width fill text)
    (concatenate text (padding "pad-right" width fill text))))

(define padding
  (lambda (name width fill text)
    (if (= (count fill) 1)
        (code-points-to-string (repeat-step (first (string-to-code-points fill)) (- width (count text)) ()))
        otherwise (error (concatenate name ": the fill must be one character") fill))))

(define repeat-step
  (lambda (element times acc)
    (if (<= times 0) acc
        otherwise (repeat-step element (- times 1) (prepend element acc)))))
```

`format` splits the template at each `{}` with `split`, so a template with n of them has n + 1 parts, and puts the values between the parts. `split` reads `{}` from left to right without overlap, so `"{{}}"` is `"{"`, a value and `"}"`.

```lisp
(define format
  (lambda (template &rest arguments)
    (let ((parts (split "{}" template)))
      (if (= (count parts) (+ (count arguments) 1))
          (format-step (rest parts) arguments (first parts))
          otherwise (error "format: the number of values differs from the number of {}"
                           template (count arguments))))))

(define format-step
  (lambda (parts arguments acc)
    (if (empty? parts) acc
        otherwise (format-step (rest parts) (rest arguments)
                               (concatenate acc (to-string (first arguments)) (first parts))))))
```

### 4.3 `function`

```lisp
(import list)

(define partial
  (lambda (f &rest fixed)
    (lambda (&rest more) (apply f (append fixed more)))))
```

### 4.4 `map`

Every function goes through `keys`, `get`, `associate` and `dissociate`, which are persistent (core 8.6), so each one costs time proportional to the size of the map times a logarithm, and runs in constant stack, like the functions of `list`.

```lisp
(import list)

(define values
  (lambda (m)
    (map (lambda (key) (get m key)) (keys m))))

(define entries
  (lambda (m)
    (map (lambda (key) (list key (get m key))) (keys m))))

(define from-entries
  (lambda (pairs)
    (fold (lambda (acc pair)
            (if (and (list? pair) (= (count pair) 2))
                (associate acc (first pair) (first (rest pair)))
                otherwise (error "from-entries: each entry must be a list (key value)" pair)))
          {} pairs)))

(define update
  (lambda (key f m)
    (associate m key (f (get m key)))))

(define merge
  (lambda (m other)
    (fold (lambda (acc key) (associate acc key (get other key))) m (keys other))))

(define map-values
  (lambda (f m)
    (fold (lambda (acc key) (associate acc key (f (get m key)))) m (keys m))))

(define filter-entries
  (lambda (keep? m)
    (fold (lambda (acc key) (if (keep? key (get m key)) acc otherwise (dissociate acc key)))
          m (keys m))))

(define fold-entries
  (lambda (f initial m)
    (fold (lambda (acc key) (f acc key (get m key))) initial (keys m))))
```

### 4.5 `set`

The functions build on `elements`, `adjoin`, `disjoin` and `contains?`, which are persistent (core 8.6).

```lisp
(import list)

(define from-list
  (lambda (xs)
    (fold adjoin #{} xs)))

(define union
  (lambda (a b)
    (fold adjoin a (elements b))))

(define intersection
  (lambda (a b)
    (fold (lambda (acc x) (if (contains? b x) acc otherwise (disjoin acc x)))
          a (elements a))))

(define difference
  (lambda (a b)
    (fold disjoin a (elements b))))

(define subset?
  (lambda (a b)
    (every? (lambda (x) (contains? b x)) (elements a))))
```

### 4.6 `number`

`max` and `min` take their arguments like `+`, so a list goes through `apply`. The first argument is checked explicitly, because with a single argument no comparison would check it; the comparisons check the others, including a mix of Integers and Floats.

```lisp
(import list)

(define max
  (lambda (number &rest more)
    (if (or (integer? number) (float? number)) (fold (lambda (best x) (if (> x best) x otherwise best)) number more)
        otherwise (raise {:kind :wrong-type :message "max: expected a number" :values (list number)}))))

(define min
  (lambda (number &rest more)
    (if (or (integer? number) (float? number)) (fold (lambda (best x) (if (< x best) x otherwise best)) number more)
        otherwise (raise {:kind :wrong-type :message "min: expected a number" :values (list number)}))))
```

`abs` keeps the type of its argument. Adding `0.0` turns `-0.0` into `0.0`, since `-0.0` is not less than zero but its absolute value is positive zero.

```lisp
(define abs
  (lambda (number)
    (if (integer? number) (if (< number 0) (- number) otherwise number)
        (float? number) (if (< number 0.0) (- number) otherwise (+ number 0.0))
        otherwise (raise {:kind :wrong-type :message "abs: expected a number" :values (list number)}))))
```

`floor`, `ceiling` and `round` start from `truncate`, which rejects an Integer and a Float out of range, and correct its result. The Integer `whole` came from the Float `x`, so `(integer-to-float whole)` is exact, and so is the fraction `x - whole`: the two are within a factor of two of each other, or `whole` is zero. `round` therefore compares the exact fraction with `0.5`, so `0.49999999999999994` rounds to `0`, where adding `0.5` and taking the floor would give `1`.

```lisp
(define floor
  (lambda (x)
    (let ((whole (truncate x)))
      (if (< x (integer-to-float whole)) (- whole 1) otherwise whole))))

(define ceiling
  (lambda (x)
    (let ((whole (truncate x)))
      (if (> x (integer-to-float whole)) (+ whole 1) otherwise whole))))

(define round
  (lambda (x)
    (let ((whole (truncate x)))
      (let ((fraction (- x (integer-to-float whole))))
        (if (>= fraction 0.5) (+ whole 1)
            (<= fraction -0.5) (- whole 1)
            otherwise whole)))))
```

## 5. Conformance

The file `stdlib-conformance.json` has 96 cases, in the same format as the core `eval` cases, with one difference: the libraries `list`, `text`, `function`, `map`, `set` and `number` are available to `import`. Each program imports what it uses. An implementation that provides the library MUST pass all of them, in addition to the core cases.

The cases cover every function in section 3, the errors of `take`, `drop` and `split`, the two-list limit of `append`, the early exit of `any?`, the stability of `sort`, a program redefining a library name, a file included with a prefix that imports a library, a traversal of one hundred thousand elements, which only passes if the functions run in constant memory, every function of `map` with its errors, a map of one hundred thousand keys built with `from-entries` and reduced with `filter-entries`, every function of `set`, sets of one hundred thousand elements, the order of `precedes?` and its stable use in `sort`, `uppercase`, `lowercase`, `letter?` and `digit?` at the edges of the ASCII and Latin-1 ranges, `pad-left` and `pad-right` with texts wider than the width, accented fills and fills of the wrong length, `format` with values of every type, braces outside `{}` and too few or too many values, `max` and `min` with Integers, with Floats, with their errors and over one hundred thousand arguments, `abs`, `floor`, `ceiling` and `round` at negative numbers, `-0.0`, halfway values, Floats above 2^52 and the edges of the range of Integer, and `import` itself: names unbound without it, prefixes, the dependencies of `text`, `function`, `map`, `set` and `number` on `list`, and repeated imports.
