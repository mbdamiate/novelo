# Standard Library — Novelo

Nov 10, 2026 · @Matheus Damiate

## 1. Overview

The standard library is a set of functions written in the language itself, on top of the core primitives. It is not part of the core: an implementation conformant to the core does not need to provide it, and the core tests do not depend on it.

The standard library is made of twelve libraries, in the sense of core section 8.9: `list`, `text`, `function`, `map`, `set`, `number`, `result`, `random`, `json`, `csv`, `time` and `pattern`. Nothing is loaded before the program: a file imports the libraries it uses, such as `(import list)`, and only then sees their names. Imported with no prefix, the names are ordinary globals, and a program may redefine them. Imported with a prefix, as in `(import list l)`, they are bound as `l/map` and so on.

The reference definitions in section 4 are normative. The implementation MAY replace any of them with a native version, as long as the observable behavior is identical, including errors and the names each library binds.

**Version:** 0.15.0, draft, on top of core 0.18.0.

**Changes since 0.14.0.** A new library, `pattern`, matches text against regular expressions in a common subset of the syntax of Perl and JavaScript (sections 3 and 4.12).
- `compile-pattern` turns the source of a pattern into a value, and an invalid one into a `:domain-error` with its column.
- `find-match`, `find-all` and `matches?` search a text, and a match gives its positions, its text and the text of its groups.
- `replace-all` replaces each match with what a function makes of it, and `split-pattern` splits a text at the matches.

The search is a Pike machine, written in the language. Its time grows with the length of the text times the size of the pattern, for every pattern. The other libraries are unchanged.

**Changes since 0.13.0.** A new library, `time`, for dates and times of the calendar in UTC, without effects (sections 3 and 4.11). An instant is the Integer of milliseconds that `current-time!` returns.
- `time-to-date` and `date-to-time` convert an instant to a Map of its fields and back.
- `format-time` writes an instant in ISO 8601.
- `read-time` reads the dates and times of RFC 3339 into a result.
- `leap-year?` and `days-in-month` answer for the Gregorian calendar.

The other libraries are unchanged.

**Changes since 0.12.0.** A new library, `csv`, reads and writes comma-separated values as RFC 4180 defines them (sections 3 and 4.10).
- `read-csv` returns `(:ok records)`, a List of records, each a List of Strings, or `(:error message line column)`.
- `write-csv` writes records back as text, quoting only the fields that need it.
- `read-csv-with` and `write-csv-with` take another separator, such as the `;` of spreadsheets that write decimals with a comma.

The other libraries are unchanged.

**Changes since 0.11.1.** A new library, `json`, reads and writes JSON (sections 3 and 4.9).
- `read-json` returns a result: `(:ok value)`, or `(:error message line column)` for a text that is not JSON.
- `write-json` writes a value back as JSON text.

It began as the reader and writer of example 12-json. Two things changed on the way into the library:
- A failure now gives a line as well as a column, because a whole document can span many lines.
- A number is gathered as it is read. The example measured each number by counting the rest of the text, which made reading a document take time that grows with the square of its length; `make bench` showed it.

The other libraries are unchanged.

**Changes since 0.11.0.** No change to the libraries. With the traces of core 0.18.0, the functions of a library that an error leaves appear in its `:trace`, as any function does; a case fixes the calls that an error in the function given to `map` shows (section 5).

**Changes since 0.10.0.** A new library, `random`, draws pseudo-random numbers without effects: a generator is a value, made from a seed by `random-generator`, and `random-integer`, `random-float` and `shuffle` each return what they drew together with the generator for the next draw (sections 3 and 4.8). The same seed always gives the same draws, on every implementation, because the algorithm, MRG32k3a, and the way a seed becomes a generator are part of the definition. A seed that changes at every run comes from `random-seed!` of core 0.17.0. The other libraries are unchanged.

**Changes since 0.9.1.** A new library, `result`, for failures that are values instead of errors: `ok` and `failure` build a result, `ok?` and `failure?` test one, `then` passes the value of a result on, as the step of a `chain` (core 0.14.0), and `map-results` turns a list into a result with a list of values, stopping at the first failure (sections 3 and 4.7). Examples 12-json and 13-expressions, which return the problems of their input as values, each wrote the same `then`, and each a loop that `map-results` replaces. The other libraries are unchanged.

**Changes since 0.9.0.** No change in behavior. The definition of `any?` uses the `if` with several pairs (core 0.6.0) instead of an `if` nested in `otherwise` (section 4.1).

**Changes since 0.8.0.** Two functions in `list` that example 13-expressions, an infix calculator, had to write by hand. `span` splits a list where a test stops holding, as a tokenizer reads a run of digits; `zip` pairs the elements of two lists, as a call pairs parameters with arguments, in the shape `from-entries` takes (sections 3 and 4.1). The other functions are unchanged.

**Changes since 0.7.1.** Three functions that example 12-json, a JSON parser, had to write by hand. In `text`, `characters` turns a text into the list of its characters, the form a parser walks with `match`. In `number`, `digits-to-integer` reads an Integer written in a base from 2 to 36, and `integer-to-digits` writes one, as the `\u` escapes of JSON need in base 16 (sections 3, 4.2 and 4.6). The definitions bind the first item of a `let` to `_` to check an argument, which needs the patterns of `let` of core 0.11.0. The other functions are unchanged.

**Changes since 0.7.0.** No observable change. `join` and `format` gather their pieces and call `concatenate` once, so their time grows with the length of the result; the reference definitions of 0.7.0 copied the text built so far at every part, which took seconds to join a hundred thousand words (section 4.2).

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
- **Helpers.** Since the core has no local recursion, some functions use helpers defined at the top level of their library: `take-step`, `span-step`, `range-step`, `zip-step`, `merge-sorted`, `split-step`, `precedes-step`, `padding`, `repeat-step`, `format-step`, `check-base`, `digit-value`, `digits-step`, `map-results-step`, `letter-code-point?`, `uppercase-code-point`, `lowercase-code-point`, and the names of `random` that start with `random-` and are not in section 3: `random-jump`, `random-matrix-apply`, `random-matrix-product`, `random-dot`, `random-multiply`, `random-next`, `random-modulo`, `random-below`, `random-below-narrow`, `random-below-wide`, `random-positions`, `random-shuffle-step` and `random-gather`, and the names of `json`, `csv`, `time` and `pattern` that start with `json-`, `csv-`, `time-` and `pattern-`, except the functions of `time` in section 3. They are bound by `import` like the other names, but they are not part of the interface: a program should not rely on them, and a native implementation MAY omit them.
- **Dependencies.** `text`, `function`, `map`, `set`, `number` and `result` import `list` (core 8.9); `random` imports nothing; `json` imports `list`, `text`, `number` and `result`; `csv` and `time` import `list`, `text` and `result`; `pattern` imports `list` and `text`. Importing a library therefore also binds the names of the libraries it imports in the same context. The names of all the libraries are different, so they can be imported together without a prefix.
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
| `list` | `span` | `(span keep? xs)` | a List of two lists: the elements at the front of `xs` for which `keep?` is true, and the rest, from the first element for which it is false; `keep?` is not called after that element: `(span (lambda (n) (< n 5)) '(1 3 5 2))` is `((1 3) (5 2))` |
| `list` | `range` | `(range start end)` | the integers from `start` up to before `end`; `()` if `start` is not less than `end` |
| `list` | `zip` | `(zip xs ys)` | List of the pairs `(x y)` of the elements of `xs` and `ys` at the same position, in order; it stops at the end of the shorter list: `(zip '(1 2 3) '(:a :b))` is `((1 :a) (2 :b))` |
| `list` | `any?`, `every?` | `(any? matches? xs)` | `#true` if any, or all, satisfy it; they stop as soon as the answer is decided |
| `list` | `sort` | `(sort less? xs)` | the elements ordered by `less?`, stably |
| `text` | `split` | `(split separator text)` | List of the parts of `text`; an empty separator is an error |
| `text` | `join` | `(join separator parts)` | the strings in `parts` joined by `separator` |
| `text` | `precedes?` | `(precedes? a b)` | `#true` if `a` comes strictly before `b` in code point order: at the first position where they differ, `a` has the smaller code point, or `a` is a proper prefix of `b` |
| `text` | `uppercase`, `lowercase` | `(uppercase text)` | `text` with each letter that has a pair in section 2 changed to the other case |
| `text` | `pad-left`, `pad-right` | `(pad-left width fill text)` | `text` with copies of the one-character String `fill` before it, or after it, until it has `width` characters; a `text` of `width` characters or more comes back unchanged; a `fill` of another length is the error `:error` |
| `text` | `format` | `(format template value …)` | `template` with each `{}`, from left to right, replaced by the `to-string` of the next value, so a String goes in without quotes; there is no escape, and a `{` or `}` not part of a `{}` stays as it is; a number of values different from the number of `{}` is the error `:error` |
| `text` | `characters` | `(characters text)` | List of the characters of `text`, each a String of one code point, in order; `()` for `""` |
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
| `number` | `digits-to-integer` | `(digits-to-integer text base)` | the Integer that `text` writes in `base`, from 2 to 36, with the digits `0`–`9` and then the letters `a`–`z`, in either case, and an optional `-` before them: `(digits-to-integer "ff" 16)` is `255`, `(digits-to-integer "-101" 2)` is `-5`; a text with no digit, or with any character that is not a digit of `base`, including a `+`, a space or a point, gives `#void`, as `string-to-number` does; a value outside the range of Integer is `:integer-overflow`; a `text` that is not a String or a `base` that is not an Integer is `:wrong-type`, and an Integer outside 2 to 36 is `:domain-error`, with `:values` `(base)` |
| `number` | `integer-to-digits` | `(integer-to-digits number base)` | the String that writes the Integer `number` in `base`, from 2 to 36, with lowercase letters, no leading zero, and `-` before a negative number: `(integer-to-digits 255 16)` is `"ff"`, `(integer-to-digits 0 2)` is `"0"`; `digits-to-integer` reads it back as `number`, for every Integer; errors as `digits-to-integer` |
| `result` | `ok` | `(ok value)` | the result `(:ok value)`, a step that worked |
| `result` | `failure` | `(failure detail …)` | the result `(:error detail …)`, a step that failed; the details are the program's, such as a message and where it was found |
| `result` | `ok?`, `failure?` | `(ok? x)` | `ok?` is `#true` for a List of `:ok` and one value, `failure?` for a List that starts with `:error`; any other value, of any type, is neither, and gives `#false` |
| `result` | `then` | `(then result next)` | `(next value)` for `(:ok value)`; a failure, as it is; any other value is `:wrong-type`, with `:values` `(x)`, since it is a mistake in the program rather than a failure. It is the step of a `chain`: `(chain then ((p e) …) body …)` stops at the first failure |
| `result` | `map-results` | `(map-results f xs)` | `(:ok ys)`, with `ys` the values of the results of `f` on each element of `xs`, in order, or the first failure; `f` is not called after it. A value of `f` that is not a result is `:wrong-type`, as in `then` |
| `random` | `random-generator` | `(random-generator seed)` | the generator of the seed, an Integer from 0 to 2^63 − 1, such as the one `random-seed!` returns (core section 9): the List `(:random-generator a b c d e f)` of section 4.8. A seed that is not an Integer is `:wrong-type`, and a negative one `:domain-error` |
| `random` | `random-integer` | `(random-integer generator low high)` | List of an Integer from `low` up to before `high`, as `range` counts, each equally likely, and the next generator: `(random-integer g 1 7)` throws a die. `low` not less than `high` is `:domain-error`, with `:values` `(low high)`; a `low` or `high` that is not an Integer is `:wrong-type`, and a range whose size `high` − `low` is outside the range of Integer is `:integer-overflow` |
| `random` | `random-float` | `(random-float generator)` | List of a Float greater than 0 and less than 1, and the next generator |
| `random` | `shuffle` | `(shuffle generator xs)` | List of the elements of `xs` in an order drawn so that every order is equally likely, and the next generator |
| `json` | `read-json` | `(read-json text)` | `(:ok value)` with the value the JSON `text` holds, objects as Maps with String keys, arrays as Lists and `null` as `#void`; or `(:error message line column)`, a failure with one of the messages of section 4.9 and the position, counted from 1 in characters, where the text stops being JSON. A `text` that is not a String is `:wrong-type` |
| `json` | `write-json` | `(write-json value)` | the JSON text of `value`, built from `#void`, Booleans, numbers, Strings, Lists and Maps with String keys, with one space after each comma and colon: `(write-json {"a" (list 1 #void)})` is `{"a": [1, null]}`; any other value is `:error`, with that value, or that key, as `:values` |
| `csv` | `read-csv`, `read-csv-with` | `(read-csv text)`, `(read-csv-with separator text)` | `(:ok records)`, the records of the CSV `text`, each a List of its fields as Strings, with an empty line as `()`; or `(:error message line column)`, a failure with one of the messages of section 4.10. The separator is `","`, or the one-character String `separator`, which may not be a double quote or a line break (`:domain-error`); a `text` or a `separator` that is not a String is `:wrong-type` |
| `csv` | `write-csv`, `write-csv-with` | `(write-csv records)`, `(write-csv-with separator records)` | the text of the records, each followed by a line feed, with a field in double quotes only when it holds the separator, a double quote or a line break, and a record of one empty field as `""`; a field that is not a String is `:wrong-type`, and the separator as in `read-csv-with` |
| `time` | `time-to-date` | `(time-to-date instant)` | the date in UTC of an instant, an Integer of milliseconds since 1970-01-01T00:00:00Z: the Map `{:year :month :day :hour :minute :second :millisecond :weekday}`, with `:weekday` from 1, Monday, to 7, Sunday; any Integer, also before 1970 |
| `time` | `date-to-time` | `(date-to-time date)` | the instant of a date Map, from its `:year`, `:month` and `:day`, and its `:hour`, `:minute`, `:second` and `:millisecond`, which are 0 when absent; a missing year, month or day, or a field out of its range, is `:domain-error`, a field that is not an Integer `:wrong-type` |
| `time` | `format-time` | `(format-time instant)` | the instant in ISO 8601, `"2026-11-15T14:30:05.120Z"`; a year outside 0 to 9999 is `:domain-error` |
| `time` | `read-time` | `(read-time text)` | `(:ok instant)` for a date, `2026-11-15`, or a date and time of RFC 3339, `2026-11-15T14:30:05.120-03:00`, with the seconds, the fraction and the zone optional, and UTC without a zone; or `(:error message column)` with a message of section 4.11 |
| `time` | `leap-year?`, `days-in-month` | `(leap-year? year)`, `(days-in-month year month)` | whether the year has a February 29, and the days of the month, 28 to 31; a month outside 1 to 12 is `:domain-error` |
| `pattern` | `compile-pattern` | `(compile-pattern source)` | the compiled pattern of the String `source`, in the syntax of section 4.12: a value the other functions of `pattern` run; a source that is not a pattern is `:domain-error`, with `:values` `(source column)` |
| `pattern` | `find-match` | `(find-match pattern text)` | the first match of `pattern` in `text`, the leftmost, as the Map `{:start :end :text :groups}`, or `#void` when there is none: `(find-match "(\\d+)-" "ab 12-3")` is `{:start 3 :end 6 :text "12-" :groups ("12")}`. `pattern` is a compiled pattern or a source |
| `pattern` | `find-all` | `(find-all pattern text)` | List of the matches from left to right, each searched for after the one before; after an empty match the search starts one character later |
| `pattern` | `matches?` | `(matches? pattern text)` | `#true` if `pattern` matches the whole of `text` |
| `pattern` | `replace-all` | `(replace-all pattern replace text)` | `text` with each match of `find-all` replaced by `(replace match)`, a String |
| `pattern` | `split-pattern` | `(split-pattern pattern text)` | List of the parts of `text` around the matches of `find-all`, as `split` gives them around a separator |
| `number` | `round` | `(round x)` | the Integer nearest to the Float `x`; exactly halfway, the one farther from zero: `(round 2.5)` is `3`, `(round -2.5)` is `-3`; errors as `floor` |

There is no `find`. To get the first element that satisfies a predicate, take `first` of the `filter` result after checking that it is not empty; to only ask whether such an element exists, use `any?`. To combine from last to first, `fold` the `reverse` of the list.

## 4. Reference definitions

The source of each library is the concatenation, in order, of the `lisp` code blocks in its section (4.1 for `list`, 4.2 for `text`, 4.3 for `function`, 4.4 for `map`, 4.5 for `set`, 4.6 for `number`, 4.7 for `result`, 4.8 for `random`, 4.9 for `json`, 4.10 for `csv`, 4.11 for `time`, 4.12 for `pattern`). The definitions use only the core and the libraries they import. Parameter names avoid those of the primitives, such as `rest` and `count`, because a parameter with the same name would shadow the primitive inside the function. The order of the definitions does not matter: global names are resolved at call time.

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

(define span
  (lambda (keep? xs)
    (span-step keep? xs ())))

(define span-step
  (lambda (keep? xs acc)
    (if (and (not (empty? xs)) (keep? (first xs))) (span-step keep? (rest xs) (prepend (first xs) acc))
        otherwise (list (reverse acc) xs))))

(define range
  (lambda (start end)
    (range-step start end ())))

(define range-step
  (lambda (start n acc)
    (if (<= n start) acc
        otherwise (range-step start (- n 1) (prepend (- n 1) acc)))))

(define zip
  (lambda (xs ys)
    (reverse (zip-step xs ys ()))))

(define zip-step
  (lambda (xs ys acc)
    (if (or (empty? xs) (empty? ys)) acc
        otherwise (zip-step (rest xs) (rest ys) (prepend (list (first xs) (first ys)) acc)))))
```

`span` returns both parts at once, for a `let` pattern to name them (core 8.3): `(let (((digits after) (span digit? characters))) …)`. `zip` stops at the shorter list, so `(zip names (range 0 (count names)))` numbers the names; a program that needs both lists to have the same length checks their `count` first.

#### Search and sorting

```lisp
(define any?
  (lambda (matches? xs)
    (if (empty? xs) #false
        (matches? (first xs)) #true
        otherwise (any? matches? (rest xs)))))

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
        (empty? (rest parts)) (first parts)
        otherwise (apply concatenate
                         (reverse (fold (lambda (acc part) (prepend part (prepend separator acc)))
                                        (list (first parts)) (rest parts)))))))

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

`join` gathers the parts and separators in a list and calls `concatenate` once, so its time grows with the length of the result; concatenating step by step would copy the text built so far at every part. A single part comes back as it is.

The functions that look at characters convert the text to its code points once (core section 9), instead of reading it one `string-at` at a time. `characters` gives a program the same list, with each code point back as a String of one character, which `match` compares with literals such as `"{"`.

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

(define characters
  (lambda (text)
    (map (lambda (code-point) (code-points-to-string (list code-point))) (string-to-code-points text))))

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

`format` splits the template at each `{}` with `split`, so a template with n of them has n + 1 parts, and puts the values between the parts, gathering the pieces for one `concatenate` as `join` does. `split` reads `{}` from left to right without overlap, so `"{{}}"` is `"{"`, a value and `"}"`.

```lisp
(define format
  (lambda (template &rest arguments)
    (let ((parts (split "{}" template)))
      (if (= (count parts) (+ (count arguments) 1))
          (apply concatenate (reverse (format-step (rest parts) arguments (list (first parts)))))
          otherwise (error "format: the number of values differs from the number of {}"
                           template (count arguments))))))

(define format-step
  (lambda (parts arguments acc)
    (if (empty? parts) acc
        otherwise (format-step (rest parts) (rest arguments)
                               (prepend (first parts) (prepend (to-string (first arguments)) acc))))))
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

`digits-to-integer` and `integer-to-digits` check the base with `check-base`, bound to `_` in a `let` (core 8.3) only for its error. `digit-value` gives the value of a digit's code point, and 36, a digit of no base, for any other character.

`digits-to-integer` checks every digit before adding any, so a text with a character that is not a digit is `#void` even when its digits would overflow. It accumulates the value as a negative number, whose range reaches one further than the positive one, so the smallest Integer reads without overflow, and changes its sign at the end, where a positive value too large overflows.

```lisp
(define check-base
  (lambda (name base)
    (if (not (integer? base))
        (raise {:kind :wrong-type :message (concatenate name ": the base must be an Integer") :values (list base)})
        (or (< base 2) (> base 36))
        (raise {:kind :domain-error :message (concatenate name ": the base must be from 2 to 36") :values (list base)})
        otherwise base)))

(define digit-value
  (lambda (code-point)
    (if (<= 48 code-point 57) (- code-point 48)
        (<= 97 code-point 122) (- code-point 87)
        (<= 65 code-point 90) (- code-point 55)
        otherwise 36)))

(define digits-to-integer
  (lambda (text base)
    (let ((code-points (string-to-code-points text))
          (_ (check-base "digits-to-integer" base))
          (negative? (and (not (empty? code-points)) (= (first code-points) 45)))
          (digits (map digit-value (if negative? (rest code-points) otherwise code-points))))
      (if (or (empty? digits) (any? (lambda (digit) (>= digit base)) digits)) #void
          otherwise (let ((magnitude (fold (lambda (total digit) (- (* total base) digit)) 0 digits)))
                      (if negative? magnitude otherwise (- magnitude)))))))
```

`integer-to-digits` works on the number made negative, for the same reason: `quotient` and `remainder` truncate toward zero (core section 9), so the remainder of a negative number is zero or negative, and its opposite is the last digit. `digits-step` gathers the digits from the last, so the list ends up in reading order.

```lisp
(define integer-to-digits
  (lambda (number base)
    (if (not (integer? number))
        (raise {:kind :wrong-type :message "integer-to-digits: expected an Integer" :values (list number)})
        otherwise (let ((_ (check-base "integer-to-digits" base))
                        (digits (digits-step (if (> number 0) (- number) otherwise number) base ())))
                    (code-points-to-string (if (< number 0) (prepend 45 digits) otherwise digits))))))

(define digits-step
  (lambda (negative base code-points)
    (let ((digit (- (remainder negative base)))
          (more (quotient negative base))
          (gathered (prepend (if (< digit 10) (+ 48 digit) otherwise (+ 87 digit)) code-points)))
      (if (= more 0) gathered otherwise (digits-step more base gathered)))))
```

### 4.7 `result`

A result says how a step went, as a value: `(:ok value)` when it worked, `(:error detail …)` when it failed. A failure that a program expects, such as a text that does not parse, can then be what a function returns, visible in its result, instead of an error it raises. The details of a failure are up to the program; the library only reads the first element of a result.

```lisp
(import list)

(define ok (lambda (value) (list :ok value)))

(define failure (lambda (&rest details) (prepend :error details)))

(define ok?
  (lambda (value)
    (match value
      (:ok _) #true
      otherwise #false)))

(define failure?
  (lambda (value)
    (match value
      (:error &rest _) #true
      otherwise #false)))
```

`then` is the step of a `chain`: each binding of `(chain then ((pattern step) …) body …)` goes on with the value of an `(:ok …)`, and the first failure is the value of the whole. A value that is neither is not passed on as a failure: it is a mistake, such as a step that forgot its `ok`, and stops where it is found.

```lisp
(define then
  (lambda (result next)
    (match result
      (:ok value) (next value)
      (:error &rest _) result
      otherwise (raise {:kind :wrong-type :message "then: expected a result" :values (list result)}))))
```

`map-results` calls `f` on the elements in order and gathers the values newest first; `then` calls the rest of the walk in tail position, so it runs in constant memory, and stops at the first failure.

```lisp
(define map-results
  (lambda (f xs)
    (map-results-step f xs ())))

(define map-results-step
  (lambda (f xs values)
    (if (empty? xs) (ok (reverse values))
        otherwise (then (f (first xs))
                        (lambda (value) (map-results-step f (rest xs) (prepend value values)))))))
```

### 4.8 `random`

A generator of pseudo-random numbers that is a value: each draw takes a generator and returns a List of the number drawn and the generator for the next draw, which a `let` pattern takes apart, as in `(let (((die next) (random-integer generator 1 7))) …)`. Nothing changes in place, so the same generator always draws the same number, and a program that keeps its seed can run again exactly as it ran. The seed itself comes from wherever the program wants: a fixed number, an argument, or `random-seed!` of the library `io` (core section 9).

The algorithm is MRG32k3a, by Pierre L'Ecuyer: two linear recurrences of order 3, modulo m1 = 4294967087 and m2 = 4294944443, whose state is six Integers. Its period is about 2^191, and its products stay below 2^53, so it runs on the Integers of the core, which raise `:integer-overflow` instead of wrapping and have no operations on bits. A generator is the List `(:random-generator a b c d e f)`, where `(a b c)` are the last three values of the first recurrence, oldest first, and `(d e f)` those of the second.

Seeds are the streams of L'Ecuyer's RngStreams: the generator of seed `k` is the one reached after `k` × 2^127 draws from the state whose six values are 12345, so different seeds give sequences that do not overlap for 2^127 draws. `random-jump` moves that state by `k` times the jump of 2^127 draws, a matrix for each recurrence, given below with its entries computed beforehand: it squares the matrix once for each binary digit of `k`, and applies it to the state where the digit is 1. The products of numbers below 2^32 would overflow, so `random-multiply` splits the second factor in two halves of 16 bits.

```lisp
(define random-generator
  (lambda (seed)
    (if (not (integer? seed))
        (raise {:kind :wrong-type :message "random-generator: expected an Integer" :values (list seed)})
        (< seed 0)
        (raise {:kind :domain-error :message "random-generator: the seed must not be negative" :values (list seed)})
        otherwise
        (let (((a b c) (random-jump '(2427906178 3580155704 949770784
                                      226153695 1230515664 3580155704
                                      1988835001 986791581 1230515664)
                                    seed 4294967087 '(12345 12345 12345)))
              ((d e f) (random-jump '(1464411153 277697599 1610723613
                                      32183930 1464411153 1022607788
                                      2824425944 32183930 2093834863)
                                    seed 4294944443 '(12345 12345 12345))))
          (list :random-generator a b c d e f)))))

(define random-jump
  (lambda (matrix times modulus state)
    (if (= times 0) state
        otherwise (random-jump (random-matrix-product matrix matrix modulus)
                               (quotient times 2)
                               modulus
                               (if (= (remainder times 2) 1) (random-matrix-apply matrix state modulus)
                                   otherwise state)))))

(define random-matrix-apply
  (lambda (matrix state modulus)
    (let (((a b c d e f g h i) matrix)
          ((x y z) state))
      (list (random-dot a b c x y z modulus)
            (random-dot d e f x y z modulus)
            (random-dot g h i x y z modulus)))))

(define random-matrix-product
  (lambda (left right modulus)
    (let (((a b c d e f g h i) left)
          ((j k l m n o p q r) right))
      (list (random-dot a b c j m p modulus) (random-dot a b c k n q modulus) (random-dot a b c l o r modulus)
            (random-dot d e f j m p modulus) (random-dot d e f k n q modulus) (random-dot d e f l o r modulus)
            (random-dot g h i j m p modulus) (random-dot g h i k n q modulus) (random-dot g h i l o r modulus)))))

(define random-dot
  (lambda (a b c x y z modulus)
    (remainder (+ (random-multiply a x modulus) (random-multiply b y modulus) (random-multiply c z modulus))
               modulus)))

(define random-multiply
  (lambda (a b modulus)
    (remainder (+ (* (remainder (* a (quotient b 65536)) modulus) 65536)
                  (* a (remainder b 65536)))
               modulus)))
```

`random-next` is one step of MRG32k3a: it returns a List of an Integer from 1 to m1 and the next generator, as L'Ecuyer's code computes them. Every draw goes through it, so it is also where a value that is not a generator is found, and produces `:wrong-type`; `shuffle` of a list of fewer than two elements draws nothing, and returns the generator it was given.

```lisp
(define random-next
  (lambda (generator)
    (match generator
      (:random-generator a b c d e f)
      (let ((first-value (random-modulo (- (* 1403580 b) (* 810728 a)) 4294967087))
            (second-value (random-modulo (- (* 527612 f) (* 1370589 d)) 4294944443)))
        (list (if (> first-value second-value) (- first-value second-value)
                  otherwise (+ (- first-value second-value) 4294967087))
              (list :random-generator b c first-value e f second-value)))
      otherwise (raise {:kind :wrong-type :message "random: expected a generator" :values (list generator)}))))

(define random-modulo
  (lambda (number modulus)
    (let ((leftover (remainder number modulus)))
      (if (< leftover 0) (+ leftover modulus) otherwise leftover))))
```

`random-float` divides the step by m1 + 1, as L'Ecuyer's code does, so the Float is greater than 0 and less than 1.

```lisp
(define random-float
  (lambda (generator)
    (let (((value next) (random-next generator)))
      (list (/ (integer-to-float value) 4294967088.0) next))))
```

`random-integer` draws from `low` up to before `high`, as `range` counts, with no bias: every Integer of the range is equally likely. `random-below` draws from 0 up to before `size`. A step less one is a digit from 0 to m1 − 1; for a size up to m1, it is used when it falls below the largest multiple of the size, and its remainder by the size is the number, and otherwise it is thrown away and drawn again. A larger size takes two digits, a high one drawn from 0 to the quotient of the size by m1 and a low one; the pair is kept when it writes a number below the size, which happens at least half of the time.

```lisp
(define random-integer
  (lambda (generator low high)
    (if (not (and (integer? low) (integer? high)))
        (raise {:kind :wrong-type :message "random-integer: expected Integers" :values (list low high)})
        (>= low high)
        (raise {:kind :domain-error :message "random-integer: the range is empty" :values (list low high)})
        otherwise
        (let (((offset next) (random-below generator (- high low))))
          (list (+ low offset) next)))))

(define random-below
  (lambda (generator size)
    (if (> size 4294967087)
        (random-below-wide generator (quotient size 4294967087) (remainder size 4294967087))
        otherwise (random-below-narrow generator size (- 4294967087 (remainder 4294967087 size))))))

(define random-below-narrow
  (lambda (generator size limit)
    (let (((value next) (random-next generator)))
      (if (<= value limit) (list (remainder (- value 1) size) next)
          otherwise (random-below-narrow next size limit)))))

(define random-below-wide
  (lambda (generator high-limit low-limit)
    (let (((high middle) (random-below generator (+ high-limit 1)))
          ((value next) (random-next middle)))
      (if (or (< high high-limit) (< (- value 1) low-limit))
          (list (+ (* high 4294967087) (- value 1)) next)
          otherwise (random-below-wide next high-limit low-limit)))))
```

`shuffle` is the algorithm of Fisher and Yates, from the last position down: at each position `i`, it draws a position from 0 to `i` and exchanges the two elements, so each of the orders of the list is equally likely. The list becomes a Map from positions to elements, where an exchange takes two `associate`s, and back into a List at the end.

```lisp
(define shuffle
  (lambda (generator xs)
    (let ((size (count xs)))
      (random-shuffle-step generator (random-positions xs 0 {}) (- size 1) size))))

(define random-positions
  (lambda (xs index positions)
    (if (empty? xs) positions
        otherwise (random-positions (rest xs) (+ index 1) (associate positions index (first xs))))))

(define random-shuffle-step
  (lambda (generator positions index size)
    (if (< index 1) (list (random-gather positions (- size 1) ()) generator)
        otherwise
        (let (((other next) (random-below generator (+ index 1))))
          (random-shuffle-step next
                               (associate (associate positions index (get positions other))
                                          other (get positions index))
                               (- index 1)
                               size)))))

(define random-gather
  (lambda (positions index gathered)
    (if (< index 0) gathered
        otherwise (random-gather positions (- index 1) (prepend (get positions index) gathered)))))
```

### 4.9 `json`

JSON, as RFC 8259 defines it, read into Novelo values and written back. The types map onto each other directly:
- an object is a Map with String keys;
- an array is a List;
- a string is a String;
- `true` and `false` are the Booleans, and `null` is `#void`;
- a number is an Integer when it has no fraction and no exponent, and a Float otherwise.

`read-json` reads a text that holds exactly one value, with any whitespace (space, tab, line feed, carriage return) around it.
- **Success.** It returns `(:ok value)`, a result of the library `result`. A key that appears twice in an object keeps its last value, as most JSON readers do.
- **Failure.** A text that is not JSON is an answer the reader expects, not an error, so it returns `(:error message line column)`:
  - `message` is one of the Strings below;
  - `line` and `column` give the character where the problem was found, both counted from 1 and in characters (code points);
  - only a line feed starts a new line;
  - a problem at the end of the text is at the position just after its last character.
- **Errors.** A number too large for an Integer or a Float is a failure, `number out of range: …`, not `:integer-overflow` or `:float-overflow`. A `text` that is not a String is `:wrong-type`.

The messages are these:

| Message | The problem |
| --- | --- |
| `expected a value, found the end` | the text ends where a value should start |
| `unexpected c` | a character `c` that cannot start a value |
| `expected a string as the key` | an object key that is not a string |
| `expected :` | no colon after a key |
| `expected , or }` | no comma or brace after a value in an object |
| `expected , or ]` | no comma or bracket after a value in an array |
| `expected a digit` | a number without the digits its syntax requires, such as `-`, `1.` or `1e` |
| `number out of range: n` | a number `n` beyond the range of Integer or Float |
| `invalid escape` | a backslash followed by anything but `" \ / b f n r t u` |
| `invalid \u escape` | `\u` not followed by four hexadecimal digits |
| `unpaired surrogate` | a `\u` escape of a surrogate that is not a high one followed by a low one |
| `control character in a string` | a character below U+0020 inside a string, unescaped |
| `unterminated string` | a string that reaches the end of the text |
| `text after the value` | anything but whitespace after the value |

`write-json` writes a value as JSON text:
- `#void` as `null`, the Booleans as `true` and `false`;
- an Integer or a Float with `to-string`, which gives digits JSON reads, such as `13.5` or `6.0e-5`;
- a String quoted, with `"`, `\` and the control characters escaped;
- a List as an array, `[1, 2]`;
- a Map with String keys as an object, `{"a": 1, "b": 2}`, in the order of `keys`.

A comma or a colon is followed by one space. A value with no JSON form, such as a Keyword, a Map with a key that is not a String, a Set or a function, is a mistake in the program and raises `:error`, whose `:values` holds that value, or that key. `read-json` reads back the text that `write-json` writes as a value `equal?` to the one written.

The library imports `list`, `text`, `number` and `result`. The reader walks the list of the text's characters with `match`, as `characters` gives it. Every reading function takes the characters left and returns a result:
- on success, `(:ok (value more))`, with `more` the characters after the value;
- on failure, `(:error message left)`, where `left` counts the characters not yet read. `read-json` turns that count into a line and a column. Counting the rest of the text costs a walk over it, so it is done only once, on failure.

```lisp
(import list)
(import text)
(import number)
(import result)

(define read-json
  (lambda (text)
    (let ((characters-of-text (characters text))
          (result (chain then (((value after) (json-value characters-of-text)))
                    (let ((after (json-skip-space after)))
                      (if (empty? after) (ok value)
                          otherwise (json-fail "text after the value" after))))))
      (match result
        (:error message left)
        (let (((line column) (json-position characters-of-text (- (count characters-of-text) left) 1 1)))
          (failure message line column))
        otherwise result))))

(define json-position
  (lambda (characters-of-text consumed line column)
    (if (= consumed 0) (list line column)
        (equal? (first characters-of-text) "\n") (json-position (rest characters-of-text) (- consumed 1) (+ line 1) 1)
        otherwise (json-position (rest characters-of-text) (- consumed 1) line (+ column 1)))))

(define json-parsed (lambda (value more) (ok (list value more))))

(define json-fail (lambda (message text) (failure message (count text))))

(define json-skip-space
  (lambda (text)
    (match text
      (" " &rest more) (json-skip-space more)
      ("\t" &rest more) (json-skip-space more)
      ("\n" &rest more) (json-skip-space more)
      ("\r" &rest more) (json-skip-space more)
      otherwise text)))
```

A value starts with the character that names its type.

```lisp
(define json-value
  (lambda (text)
    (let ((text (json-skip-space text)))
      (match text
        ("{" &rest more) (json-object (json-skip-space more) {})
        ("[" &rest more) (json-array (json-skip-space more) ())
        ("\"" &rest more) (json-string more ())
        ("t" "r" "u" "e" &rest more) (json-parsed #true more)
        ("f" "a" "l" "s" "e" &rest more) (json-parsed #false more)
        ("n" "u" "l" "l" &rest more) (json-parsed #void more)
        () (json-fail "expected a value, found the end" text)
        otherwise (json-number text)))))

(define json-expect
  (lambda (expected text)
    (let ((text (json-skip-space text)))
      (if (and (not (empty? text)) (equal? (first text) expected)) (ok (rest text))
          otherwise (json-fail (concatenate "expected " expected) text)))))
```

An object is a key, a colon and a value, then a comma and another, or the closing brace; an array is the same without keys. Each step starts where the step before stopped. The elements of an array are gathered newest first and reversed at the end.

```lisp
(define json-object
  (lambda (text object)
    (match text
      ("}" &rest more) (json-parsed object more)
      otherwise
      (chain then (((key after-key) (json-key text))
                   (after-colon (json-expect ":" after-key))
                   ((value after-value) (json-value after-colon)))
        (let ((object (associate object key value))
              (after (json-skip-space after-value)))
          (match after
            ("," &rest more) (json-object (json-skip-space more) object)
            ("}" &rest more) (json-parsed object more)
            otherwise (json-fail "expected , or }" after)))))))

(define json-key
  (lambda (text)
    (match text
      ("\"" &rest more) (json-string more ())
      otherwise (json-fail "expected a string as the key" text))))

(define json-array
  (lambda (text elements)
    (match text
      ("]" &rest more) (json-parsed (reverse elements) more)
      otherwise
      (chain then (((value after-value) (json-value text)))
        (let ((elements (prepend value elements))
              (after (json-skip-space after-value)))
          (match after
            ("," &rest more) (json-array (json-skip-space more) elements)
            ("]" &rest more) (json-parsed (reverse elements) more)
            otherwise (json-fail "expected , or ]" after)))))))
```

A string gathers its characters, newest first, until the closing quote. `\u` is followed by four hexadecimal digits, which name a UTF-16 unit. A character above U+FFFF is written as two units: a high surrogate, from D800 to DBFF, then a low one, from DC00 to DFFF. Together they give the code point. `digits-to-integer` would also take a sign, which JSON does not allow, so a `-` is checked first.

```lisp
(define json-escapes {"\"" "\"" "\\" "\\" "/" "/" "b" "\u{8}" "f" "\u{C}" "n" "\n" "r" "\r" "t" "\t"})

(define json-string
  (lambda (text gathered)
    (match text
      ("\"" &rest more) (json-parsed (apply concatenate (reverse gathered)) more)
      ("\\" "u" a b c d "\\" "u" e f g h &rest more)
      (chain then ((high (json-unit-value (list a b c d) text))
                   (low (json-unit-value (list e f g h) text)))
        (if (and (<= 55296 high 56319) (<= 56320 low 57343))
            (json-string more (prepend (code-points-to-string (list (json-surrogate-pair high low))) gathered))
            otherwise (json-unit high text (drop 6 text) gathered)))
      ("\\" "u" a b c d &rest more)
      (chain then ((unit (json-unit-value (list a b c d) text)))
        (json-unit unit text more gathered))
      ("\\" "u" &rest more) (json-fail "invalid \\u escape" text)
      ("\\" escape &rest more)
      (if (contains? json-escapes escape) (json-string more (prepend (get json-escapes escape) gathered))
          otherwise (json-fail "invalid escape" text))
      ("\\") (json-fail "unterminated string" text)
      (character &rest more)
      (if (< (first (string-to-code-points character)) 32) (json-fail "control character in a string" text)
          otherwise (json-string more (prepend character gathered)))
      otherwise (json-fail "unterminated string" text))))

(define json-unit-value
  (lambda (digits text)
    (let ((value (if (equal? (first digits) "-") #void
                     otherwise (digits-to-integer (apply concatenate digits) 16))))
      (if (void? value) (json-fail "invalid \\u escape" text)
          otherwise (ok value)))))

(define json-surrogate-pair
  (lambda (high low)
    (+ 65536 (* (- high 55296) 1024) (- low 56320))))

(define json-unit
  (lambda (unit text more gathered)
    (if (<= 55296 unit 57343) (json-fail "unpaired surrogate" text)
        otherwise (json-string more (prepend (code-points-to-string (list unit)) gathered)))))
```

A number is an optional minus, then `0` or digits that do not start with `0`, then optionally a point and digits, then optionally `e` or `E`, a sign and digits. Each part takes the characters left and the characters of the number gathered so far, newest first, and returns a result whose value is both after its part. The number's text then goes to `string-to-number`, which reads every text that this syntax lets through, so a number can only fail by its range. Novelo's syntax is not JSON's: it wants a point before the exponent, a lowercase `e` and no `+` sign. So `json-novelo-number` turns `1e3` into `1.0e3`, `2E-1` into `2.0e-1` and `0.5e+1` into `0.5e1`.

```lisp
(define json-number
  (lambda (text)
    (if (not (or (equal? (first text) "-") (digit? (first text))))
        (json-fail (concatenate "unexpected " (first text)) text)
        otherwise
        (let (((sign after-sign) (match text ("-" &rest more) (list '("-") more) otherwise (list () text))))
          (chain then (((integer after-integer) (json-integer-part after-sign sign))
                       ((fraction after-fraction) (json-fraction-part after-integer integer))
                       ((gathered after) (json-exponent-part after-fraction fraction)))
            (json-number-value (apply concatenate (reverse gathered)) text after))))))

(define json-digits
  (lambda (text gathered)
    (if (and (not (empty? text)) (digit? (first text))) (json-digits (rest text) (prepend (first text) gathered))
        otherwise (list gathered text))))

(define json-some-digits
  (lambda (text gathered)
    (if (and (not (empty? text)) (digit? (first text))) (ok (json-digits text gathered))
        otherwise (json-fail "expected a digit" text))))

(define json-integer-part
  (lambda (text gathered)
    (match text
      ("0" &rest more) (ok (list (prepend "0" gathered) more))
      otherwise (json-some-digits text gathered))))

(define json-fraction-part
  (lambda (text gathered)
    (match text
      ("." &rest more) (json-some-digits more (prepend "." gathered))
      otherwise (ok (list gathered text)))))

(define json-exponent-part
  (lambda (text gathered)
    (match text
      ("e" &rest more) (json-exponent-digits more (prepend "e" gathered))
      ("E" &rest more) (json-exponent-digits more (prepend "e" gathered))
      otherwise (ok (list gathered text)))))

(define json-exponent-digits
  (lambda (text gathered)
    (match text
      ("+" &rest more) (json-some-digits more gathered)
      ("-" &rest more) (json-some-digits more (prepend "-" gathered))
      otherwise (json-some-digits text gathered))))

(define json-novelo-number
  (lambda (number-text)
    (match (split "e" number-text)
      (mantissa exponent)
      (concatenate mantissa (if (any? (lambda (character) (equal? character ".")) (characters mantissa)) ""
                                otherwise ".0")
                   "e" exponent)
      otherwise number-text)))

(define json-number-value
  (lambda (number-text text after)
    (try (json-parsed (string-to-number (json-novelo-number number-text)) after)
      {:kind :integer-overflow} (json-fail (concatenate "number out of range: " number-text) text)
      {:kind :float-overflow} (json-fail (concatenate "number out of range: " number-text) text))))
```

Writing escapes `"`, `\` and every control character: with a short escape when there is one, as `\u00XX` otherwise.

```lisp
(define json-short-escapes {"\"" "\\\"" "\\" "\\\\" "\n" "\\n" "\r" "\\r" "\t" "\\t" "\u{8}" "\\b" "\u{C}" "\\f"})

(define json-escape
  (lambda (character)
    (if (contains? json-short-escapes character) (get json-short-escapes character)
        (< (first (string-to-code-points character)) 32)
        (concatenate "\\u" (pad-left 4 "0" (integer-to-digits (first (string-to-code-points character)) 16)))
        otherwise character)))

(define json-quote
  (lambda (text)
    (concatenate "\"" (apply concatenate (map json-escape (characters text))) "\"")))

(define write-json
  (lambda (value)
    (if (void? value) "null"
        (equal? value #true) "true"
        (equal? value #false) "false"
        (or (integer? value) (float? value)) (to-string value)
        (string? value) (json-quote value)
        (list? value) (concatenate "[" (join ", " (map write-json value)) "]")
        (map? value) (concatenate "{" (join ", " (map (lambda (key) (json-member key value)) (keys value))) "}")
        otherwise (raise {:kind :error :message "write-json: the value has no JSON form" :values (list value)}))))

(define json-member
  (lambda (key object)
    (if (string? key) (concatenate (json-quote key) ": " (write-json (get object key)))
        otherwise (raise {:kind :error :message "write-json: an object key must be a String" :values (list key)}))))
```

### 4.10 `csv`

Comma-separated values, as RFC 4180 defines them: a text of records, one per line, each a list of fields. A record is a List of Strings, and a text is a List of records. Fields are always Strings. A program that wants numbers converts them with `string-to-number`.

`read-csv` reads a text with fields separated by commas. `read-csv-with` reads one with another separator, a String of one character other than a double quote, a carriage return or a line feed, such as `";"`, which spreadsheets use where the comma is the decimal mark.

**Records.**
- A record ends at a line feed, or at a carriage return followed by a line feed. The line break after the last record is optional.
- An empty line is the empty record `()`, and an empty text has no records.
- Records may have different numbers of fields. The library does not check them, since only the program knows how many it expects.

**Fields.**
- A field that starts with a double quote is quoted. It ends at the next double quote that is not doubled, and it may hold the separator, line breaks, and `""`, which stands for one double quote. After its closing quote comes the separator, a line break or the end of the text.
- Any other field ends at the separator, a line break or the end of the text, and may not hold a double quote.
- A carriage return not followed by a line feed is an ordinary character of a field.

The result is `(:ok records)`. A text that does not follow these rules is a failure, `(:error message line column)`, with the line and the column counted from 1 in characters, as in `read-json`:

| Message | The problem |
| --- | --- |
| `unterminated quoted field` | a quoted field that reaches the end of the text; the position is that of its opening quote |
| `character after a closing quote` | anything but the separator or a line break after a quoted field, as in `"a"b`; the position is that character's |
| `quote inside an unquoted field` | a double quote inside a field that did not start with one, as in `a"b`; the position is that quote's |

A `text` that is not a String is `:wrong-type`. So is a separator that is not a String. A String that is not a valid separator is `:domain-error`, with `:values` `(separator)`.

`write-csv` and `write-csv-with` write a list of records as text:
- Each record ends with a line feed, the last one too. RFC 4180 asks for a carriage return before it, but the readers accept both, and Unix tools expect the line feed alone.
- A field is quoted only when it holds the separator, a double quote, a carriage return or a line feed. Its double quotes are doubled.
- A record of one empty field is written `""`, so that it is not the empty line of the record `()`.
- A field that is not a String is `:wrong-type`, with the field as `:values`. Numbers go through `to-string` in the program, as everywhere else.

`(read-csv-with separator (write-csv-with separator records))` is `(:ok records)` for every List of Lists of Strings.

A first record that names the fields turns the others into Maps with `zip` and `from-entries`:

```
(let (((:ok (header &rest rows)) (read-csv text)))
  (map (lambda (row) (from-entries (zip header row))) rows))
```

The library imports `list`, `text` and `result`. As in `json`, the reader walks the list of the text's characters, and each step returns a result whose failure counts the characters left; `read-csv-with` turns that count into a line and a column. The fields of a record and the records of a text are gathered newest first, and reversed once each.

```lisp
(import list)
(import text)
(import result)

(define read-csv (lambda (text) (read-csv-with "," text)))

(define write-csv (lambda (records) (write-csv-with "," records)))

(define csv-check-separator
  (lambda (name separator)
    (if (not (string? separator))
        (raise {:kind :wrong-type :message (concatenate name ": the separator must be a String")
                :values (list separator)})
        (or (not (= (count separator) 1)) (equal? separator "\"") (equal? separator "\n") (equal? separator "\r"))
        (raise {:kind :domain-error
                :message (concatenate name ": the separator must be one character, not a quote or a line break")
                :values (list separator)})
        otherwise #true)))

(define read-csv-with
  (lambda (separator text)
    (let ((_ (csv-check-separator "read-csv-with" separator))
          (characters-of-text (characters text)))
      (match (csv-records characters-of-text separator ())
        (:error message left)
        (let (((line column) (csv-position characters-of-text (- (count characters-of-text) left) 1 1)))
          (failure message line column))
        success success))))

(define csv-position
  (lambda (characters-of-text consumed line column)
    (if (= consumed 0) (list line column)
        (equal? (first characters-of-text) "\n") (csv-position (rest characters-of-text) (- consumed 1) (+ line 1) 1)
        otherwise (csv-position (rest characters-of-text) (- consumed 1) line (+ column 1)))))

(define csv-fail (lambda (message text) (failure message (count text))))
```

A record starts at the beginning of a line. An empty line is the record `()`; anything else is read field by field until the line break that ends it.

```lisp
(define csv-records
  (lambda (text separator records)
    (match text
      () (ok (reverse records))
      ("\n" &rest more) (csv-records more separator (prepend () records))
      ("\r" "\n" &rest more) (csv-records more separator (prepend () records))
      otherwise
      (chain then (((record more) (csv-record text separator ())))
        (csv-records more separator (prepend record records))))))

; The fields of one record: after each field comes the separator, which
; starts another, or the end of the record.
(define csv-record
  (lambda (text separator fields)
    (chain then (((field after) (csv-field text separator)))
      (let ((fields (prepend field fields)))
        (match after
          () (ok (list (reverse fields) ()))
          ("\n" &rest more) (ok (list (reverse fields) more))
          ("\r" "\n" &rest more) (ok (list (reverse fields) more))
          (character &rest more)
          (if (equal? character separator) (csv-record more separator fields)
              otherwise (csv-fail "character after a closing quote" after)))))))

(define csv-field
  (lambda (text separator)
    (match text
      ("\"" &rest more) (csv-quoted more () text)
      otherwise (csv-plain text separator ()))))

(define csv-plain
  (lambda (text separator gathered)
    (match text
      () (ok (list (apply concatenate (reverse gathered)) text))
      ("\n" &rest _) (ok (list (apply concatenate (reverse gathered)) text))
      ("\r" "\n" &rest _) (ok (list (apply concatenate (reverse gathered)) text))
      ("\"" &rest _) (csv-fail "quote inside an unquoted field" text)
      (character &rest more)
      (if (equal? character separator) (ok (list (apply concatenate (reverse gathered)) text))
          otherwise (csv-plain more separator (prepend character gathered))))))

; `opening` is the text from the opening quote, where an unterminated field
; is reported.
(define csv-quoted
  (lambda (text gathered opening)
    (match text
      ("\"" "\"" &rest more) (csv-quoted more (prepend "\"" gathered) opening)
      ("\"" &rest more) (ok (list (apply concatenate (reverse gathered)) more))
      () (csv-fail "unterminated quoted field" opening)
      (character &rest more) (csv-quoted more (prepend character gathered) opening))))
```

Writing builds the pieces of the whole text, a line feed after each record, and joins them once.

```lisp
(define write-csv-with
  (lambda (separator records)
    (let ((_ (csv-check-separator "write-csv-with" separator)))
      (apply concatenate (map (lambda (record) (concatenate (csv-line separator record) "\n")) records)))))

(define csv-line
  (lambda (separator record)
    (if (equal? record (list "")) "\"\""
        otherwise (join separator (map (lambda (field) (csv-write-field separator field)) record)))))

(define csv-write-field
  (lambda (separator field)
    (if (not (string? field))
        (raise {:kind :wrong-type :message "write-csv: a field must be a String" :values (list field)})
        (any? (lambda (character)
                (or (equal? character separator) (equal? character "\"") (equal? character "\n")
                    (equal? character "\r")))
              (characters field))
        (concatenate "\"" (join "\"\"" (split "\"" field)) "\"")
        otherwise field)))
```

### 4.11 `time`

Dates and times of the civil calendar in UTC, without effects. An instant is an Integer, the milliseconds since 1970-01-01T00:00:00Z, which is what `current-time!` returns (core section 9). The library converts instants to dates and back, and to text and back. Reading the clock stays at the edge of the program.

**The calendar.**
- The calendar is the Gregorian one, extended back before its adoption in 1582 (the proleptic Gregorian calendar), with a year 0.
- A day has exactly 86,400 seconds. There are no leap seconds, as in Unix time.
- There are no named time zones and no daylight saving time, which need the system's zone database. A fixed offset is a number of milliseconds: `(time-to-date (- instant 10800000))` is the date three hours behind UTC, as in Brasília.

**Dates.** A date is a Map with Keyword keys, which map patterns take apart: `{:year 2026 :month 11 :day 15 :hour 14 :minute 30 :second 5 :millisecond 120 :weekday 7}`. `:weekday` counts from 1, Monday, to 7, Sunday, as ISO 8601 does.

**Functions.**
- `time-to-date` accepts any Integer, including the negative instants before 1970.
- `date-to-time` needs `:year`, `:month` and `:day`. `:hour`, `:minute`, `:second` and `:millisecond` are 0 when absent, and `:weekday` and any other key are ignored.
  - A missing key, or a field out of its range, such as day 31 of a month of 30 or hour 24, is `:domain-error`, with `:values` `(date)` or `(field)`.
  - A field that is not an Integer is `:wrong-type`, and so is a date that is not a Map.
  - An instant beyond the range of Integer is `:integer-overflow`.
- `format-time` writes an instant as `"2026-11-15T14:30:05.120Z"`: always UTC, always with milliseconds, so texts sort as their instants do. An instant whose year is not between 0 and 9999 is `:domain-error`, since ISO 8601 writes those years only by agreement.
- `leap-year?` takes any Integer year. `days-in-month` gives 28 to 31, and a month outside 1 to 12 is `:domain-error`.

**Reading.** `read-time` reads the format of RFC 3339, the part of ISO 8601 that the Internet uses, and a little more:
- A date alone, `2026-11-15`, is midnight UTC.
- A date and time: the date, then `T`, `t` or a space, then hours and minutes, `14:30`. Optionally they are followed by seconds, `:05`, and the seconds by a fraction of one or more digits, `.120`, of which only milliseconds count; the rest is dropped, not rounded.
- Then, optionally, a zone: `Z` or `z` for UTC, or an offset `+03:00` or `-03:00`, the time zone's distance ahead of UTC. A time without a zone is taken as UTC.

The result is `(:ok instant)`, or a failure `(:error message column)`, with the column counted from 1 in characters:

| Message | The problem |
| --- | --- |
| `expected a digit` | a field shorter than its digits, or a point with no digit after it |
| `expected -`, `expected :` | a missing separator inside the date, the time or the offset |
| `month out of range`, `day out of range` | a month outside 1 to 12, or a day outside the month; the position is the field's |
| `hour out of range`, `minute out of range`, `second out of range` | an hour above 23, a minute or a second above 59 |
| `offset out of range` | an offset's hours above 23 or minutes above 59 |
| `text after the time` | anything else after a complete date or time |

A `text` that is not a String is `:wrong-type`.

The conversions are those of Howard Hinnant's `days_from_civil` and `civil_from_days`. They count in eras of 400 years, the period after which the Gregorian calendar repeats, 146,097 days, so they are exact for every year. They start the year on March 1, so that February, with its leap day, is the last month. Their divisions round down, which `quotient` does not do for negative numbers, so `time-floor` and `time-modulo` write them.

```lisp
(import list)
(import text)
(import result)

(define time-floor
  (lambda (n divisor)
    (if (< (remainder n divisor) 0) (- (quotient n divisor) 1)
        otherwise (quotient n divisor))))

(define time-modulo (lambda (n divisor) (- n (* (time-floor n divisor) divisor))))

(define leap-year?
  (lambda (year)
    (and (= (remainder year 4) 0) (or (not (= (remainder year 100) 0)) (= (remainder year 400) 0)))))

(define days-in-month
  (lambda (year month)
    (if (not (<= 1 month 12))
        (raise {:kind :domain-error :message "days-in-month: the month must be from 1 to 12" :values (list month)})
        (= month 2) (if (leap-year? year) 29 otherwise 28)
        (or (= month 4) (= month 6) (= month 9) (= month 11)) 30
        otherwise 31)))

; The days from 1970-01-01 to a date.
(define time-days
  (lambda (year month day)
    (let ((shifted (if (<= month 2) (- year 1) otherwise year))
          (era (time-floor shifted 400))
          (year-of-era (- shifted (* era 400)))
          (day-of-year (+ (quotient (+ (* 153 (if (> month 2) (- month 3) otherwise (+ month 9))) 2) 5) (- day 1)))
          (day-of-era (+ (* year-of-era 365) (quotient year-of-era 4) (- (quotient year-of-era 100)) day-of-year)))
      (+ (* era 146097) day-of-era -719468))))

; The date of a number of days from 1970-01-01, as (year month day).
(define time-civil
  (lambda (days)
    (let ((shifted (+ days 719468))
          (era (time-floor shifted 146097))
          (day-of-era (- shifted (* era 146097)))
          (year-of-era (quotient (- (+ day-of-era (quotient day-of-era 36524)) (quotient day-of-era 1460)
                                    (quotient day-of-era 146096))
                                 365))
          (day-of-year (- day-of-era (+ (* 365 year-of-era) (quotient year-of-era 4) (- (quotient year-of-era 100)))))
          (month-from-march (quotient (+ (* 5 day-of-year) 2) 153))
          (day (+ (- day-of-year (quotient (+ (* 153 month-from-march) 2) 5)) 1))
          (month (if (< month-from-march 10) (+ month-from-march 3) otherwise (- month-from-march 9))))
      (list (+ year-of-era (* era 400) (if (<= month 2) 1 otherwise 0)) month day))))

(define time-to-date
  (lambda (instant)
    (let ((days (time-floor instant 86400000))
          (in-day (- instant (* days 86400000)))
          ((year month day) (time-civil days)))
      {:year year :month month :day day
       :hour (quotient in-day 3600000) :minute (remainder (quotient in-day 60000) 60)
       :second (remainder (quotient in-day 1000) 60) :millisecond (remainder in-day 1000)
       :weekday (+ (time-modulo (+ days 3) 7) 1)})))
```

`date-to-time` checks each field before it computes. 1970-01-01 was a Thursday, day 4 of the week, which is where `:weekday` starts counting above.

```lisp
(define date-to-time
  (lambda (date)
    (if (not (map? date))
        (raise {:kind :wrong-type :message "date-to-time: the date must be a Map" :values (list date)})
        otherwise
        (let ((year (time-field date :year #void))
              (month (time-field date :month #void))
              (day (time-field date :day #void))
              (hour (time-field date :hour 0))
              (minute (time-field date :minute 0))
              (second (time-field date :second 0))
              (millisecond (time-field date :millisecond 0))
              (_ (time-check month 1 12))
              (_ (time-check day 1 (days-in-month year month)))
              (_ (time-check hour 0 23))
              (_ (time-check minute 0 59))
              (_ (time-check second 0 59))
              (_ (time-check millisecond 0 999)))
          (+ (* (time-days year month day) 86400000) (* hour 3600000) (* minute 60000) (* second 1000) millisecond)))))

; A field of the date, or its default; #void as the default makes it required.
(define time-field
  (lambda (date key default)
    (let ((value (get date key default)))
      (if (void? value)
          (raise {:kind :domain-error :message "date-to-time: the date lacks a field" :values (list date)})
          (not (integer? value))
          (raise {:kind :wrong-type :message "date-to-time: a field must be an Integer" :values (list value)})
          otherwise value))))

(define time-check
  (lambda (value low high)
    (if (<= low value high) #true
        otherwise (raise {:kind :domain-error :message "date-to-time: a field is out of range" :values (list value)}))))
```

`format-time` pads each field with zeros to its width.

```lisp
(define format-time
  (lambda (instant)
    (match (time-to-date instant)
      {:year year :month month :day day :hour hour :minute minute :second second :millisecond millisecond}
      (if (not (<= 0 year 9999))
          (raise {:kind :domain-error :message "format-time: the year must be from 0 to 9999" :values (list instant)})
          otherwise
          (format "{}-{}-{}T{}:{}:{}.{}Z" (time-pad 4 year) (time-pad 2 month) (time-pad 2 day)
                  (time-pad 2 hour) (time-pad 2 minute) (time-pad 2 second) (time-pad 3 millisecond))))))

(define time-pad (lambda (width n) (pad-left width "0" (to-string n))))
```

`read-time` walks the characters of the text, as `read-json` does. Each step returns a result whose value is what it read and the characters after it, or whose failure counts the characters left. A field of digits is read by code point, so `09` is 9.

```lisp
(define read-time
  (lambda (text)
    (let ((characters-of-text (characters text)))
      (match (time-read-date characters-of-text)
        (:error message left) (failure message (+ 1 (- (count characters-of-text) left)))
        success success))))

(define time-fail (lambda (message text) (failure message (count text))))

(define time-number
  (lambda (text digits value)
    (if (= digits 0) (ok (list value text))
        (and (not (empty? text)) (digit? (first text)))
        (time-number (rest text) (- digits 1) (+ (* value 10) (- (first (string-to-code-points (first text))) 48)))
        otherwise (time-fail "expected a digit" text))))

; Two digits from low to high; a value outside fails at the field.
(define time-two-digits
  (lambda (text low high message)
    (chain then (((value after) (time-number text 2 0)))
      (if (<= low value high) (ok (list value after))
          otherwise (time-fail message text)))))

(define time-expect
  (lambda (expected text)
    (if (and (not (empty? text)) (equal? (first text) expected)) (ok (rest text))
        otherwise (time-fail (concatenate "expected " expected) text))))

(define time-read-date
  (lambda (text)
    (chain then (((year after-year) (time-number text 4 0))
                 (before-month (time-expect "-" after-year))
                 ((month after-month) (time-two-digits before-month 1 12 "month out of range"))
                 (before-day (time-expect "-" after-month))
                 ((day after-day) (time-number before-day 2 0)))
      (if (not (<= 1 day (days-in-month year month))) (time-fail "day out of range" before-day)
          otherwise (time-read-clock after-day (* (time-days year month day) 86400000))))))

(define time-read-clock
  (lambda (text midnight)
    (match text
      () (ok midnight)
      (separator &rest more)
      (if (not (or (equal? separator "T") (equal? separator "t") (equal? separator " ")))
          (time-fail "text after the time" text)
          otherwise
          (chain then (((hour after-hour) (time-two-digits more 0 23 "hour out of range"))
                       (before-minute (time-expect ":" after-hour))
                       ((minute after-minute) (time-two-digits before-minute 0 59 "minute out of range"))
                       ((second millisecond after-second) (time-read-seconds after-minute))
                       ((offset after-zone) (time-read-zone after-second)))
            (if (empty? after-zone)
                (ok (- (+ midnight (* hour 3600000) (* minute 60000) (* second 1000) millisecond) offset))
                otherwise (time-fail "text after the time" after-zone)))))))

; Seconds, then optionally their fraction, as (second millisecond after).
(define time-read-seconds
  (lambda (text)
    (match text
      (":" &rest more)
      (chain then (((second after-second) (time-two-digits more 0 59 "second out of range"))
                   ((millisecond after-fraction) (time-read-fraction after-second)))
        (ok (list second millisecond after-fraction)))
      otherwise (ok (list 0 0 text)))))

; The digits after the point: the first three are milliseconds, short ones
; scaled up, as .5 is 500; the others are read and dropped.
(define time-read-fraction
  (lambda (text)
    (match text
      ("." &rest more)
      (if (and (not (empty? more)) (digit? (first more))) (time-fraction-digits more 0 0)
          otherwise (time-fail "expected a digit" more))
      otherwise (ok (list 0 text)))))

(define time-fraction-digits
  (lambda (text read value)
    (if (and (not (empty? text)) (digit? (first text)))
        (time-fraction-digits (rest text) (+ read 1)
                              (if (< read 3) (+ (* value 10) (- (first (string-to-code-points (first text))) 48))
                                  otherwise value))
        (= read 1) (ok (list (* value 100) text))
        (= read 2) (ok (list (* value 10) text))
        otherwise (ok (list value text)))))

(define time-read-zone
  (lambda (text)
    (match text
      ("Z" &rest more) (ok (list 0 more))
      ("z" &rest more) (ok (list 0 more))
      ("+" &rest more) (time-read-offset more 1)
      ("-" &rest more) (time-read-offset more -1)
      otherwise (ok (list 0 text)))))

(define time-read-offset
  (lambda (text sign)
    (chain then (((hours after-hours) (time-two-digits text 0 23 "offset out of range"))
                 (before-minutes (time-expect ":" after-hours))
                 ((minutes after-minutes) (time-two-digits before-minutes 0 59 "offset out of range")))
      (ok (list (* sign (+ (* hours 3600000) (* minutes 60000))) after-minutes)))))
```

### 4.12 `pattern`

Text matching with regular expressions. A pattern is written as a String, in a common subset of the syntax of Perl and JavaScript, and `compile-pattern` turns it into a value that the other functions run. The search always takes time proportional to the length of the text times the size of the pattern, whatever the pattern: there is no backtracking, so a pattern such as `(a*)*b` cannot hang a program.

**Syntax.**

| Form | Matches |
| --- | --- |
| a character | itself, unless it is one of `\ . ^ $ \| ? * + ( ) [ ] { }` |
| `.` | any character, also a line break |
| `[abc]`, `[a-z]`, `[^…]` | one character of the class, or with `^` first, one character outside it |
| `\d`, `\w`, `\s` | a digit, `0` to `9`; a word character, a letter of section 2, a digit or `_`; a space, a tab, a line feed, a vertical tab, a form feed or a carriage return |
| `\D`, `\W`, `\S` | a character that `\d`, `\w` or `\s` does not match |
| `\n`, `\t`, `\r` | a line feed, a tab, a carriage return |
| `\` and one of `\ . ^ $ \| ? * + ( ) [ ] { } - /` | that character |
| `^`, `$` | nothing, at the start or at the end of the text |
| `x*`, `x+`, `x?` | `x` any number of times, at least once, or at most once |
| `x{m}`, `x{m,}`, `x{m,n}` | `x` exactly `m` times, at least `m` times, or from `m` to `n` times; `m` and `n` are at most 1000 |
| `(x)` | `x`, as a group whose text the match keeps |
| `x\|y` | `x` or `y`; either may be empty |

Inside a class:
- every character stands for itself except `]`, which closes it, and `\`, which starts the escapes of the table;
- `-` between two characters is a range of code points;
- a `-` that is first or last is itself;
- `\d`, `\w`, `\s` and their negations add what they match.

There are no backreferences, no lookaround, no lazy quantifiers and no flags. To match without regard to case, match the `lowercase` of the text against a pattern in lowercase.

**Which match.** The search finds the matches that start leftmost, and of those, the first in this order of preference:
- `*`, `+`, `?` and `{…}` prefer one more repetition to stopping, so `a+` finds all of `aaa`;
- `|` prefers its left side, so `a|ab` finds `a` in `ab`;
- an earlier part of the pattern has its preference before a later one.

The backtracking matchers of Python and JavaScript find the same match, and Python the same groups. They can differ from it, and from one another, only when a repeated part can match nothing; here, a repetition with no upper limit, `*`, `+` or `{m,}`, never takes a turn that reads no character. A repeated group keeps the text of its last repetition, and a group that took no part in the match has no text.

**Values.**
- A compiled pattern is the List `(:pattern source groups program)`. `groups` counts the groups, numbered by their `(` from left to right, and `program` is the Map of the instructions described below.
- Every function that takes a pattern also takes its source, a String, and compiles it at each call; a pattern used many times is better compiled once.
- A match is the Map `{:start :end :text :groups}`:
  - `:start` and `:end` are the positions of its first character and of the character after it, counted in characters from 0, as `substring` counts;
  - `:text` is the text it matched;
  - `:groups` is a List with the text of each group, in order, or `#void` for a group that took no part in it.

**Errors.** A pattern is part of the program, not data it reads, so a pattern that cannot be compiled is an error, not a failure: `:domain-error`, with `:values` `(source column)`, the column counted from 1 in characters. Its message is `compile-pattern: ` followed by one of:

| Message | The problem | The column |
| --- | --- | --- |
| `nothing to repeat` | `*`, `+`, `?` or `{` with nothing before it, after `(`, `\|`, `^`, `$` or another repetition | the quantifier |
| `invalid repetition` | a `{` not followed by `m}`, `m,}` or `m,n}`, with `m` not above `n` and neither above 1000 | the `{` |
| `missing )`, `missing ]` | a group or a class still open at the end of the pattern | the `(` or the `[` |
| `unexpected )`, `unexpected ]`, `unexpected }` | a `)` outside a group, or a `]` or `}` outside a class | the character |
| `empty class` | `[]` or `[^]` | the `[` |
| `invalid range` | a range whose start is after its end, or with a class such as `\d` at either end | the start of the range |
| `invalid escape` | a `\` before any other character, or at the end of the pattern | the `\` |

A source that is not a String, or a pattern that is neither a String nor a compiled pattern, is `:wrong-type`. So is a text that is not a String, an error from `string-to-code-points`.

The library compiles a pattern in two steps. First a parser reads the source into a tree, then the tree becomes a program for a Pike machine, a list of instructions numbered from 0, kept in a Map:

| Instruction | What it does |
| --- | --- |
| `(:char code-point)`, `(:any)`, `(:class negated items)` | reads one character that it matches, or stops the thread |
| `(:start)`, `(:end)` | goes on only at the start, or at the end, of the text |
| `(:split preferred other)` | goes on at both, `preferred` with priority |
| `(:jump target)` | goes on at `target` |
| `(:save slot)` | records the position in a slot: slot 0 and 1 are the match, slots 2k and 2k + 1 group k |
| `(:match)` | a match |

The parser raises an internal error, which `compile-pattern` gives the source and raises again. Each parsing function takes the characters left and the column of the first of them, and returns what it read with the characters after it and their column.

```lisp
(import list)
(import text)

(define compile-pattern
  (lambda (source)
    (if (not (string? source))
        (raise {:kind :wrong-type :message "compile-pattern: the source must be a String" :values (list source)})
        otherwise
        (try (match (pattern-parse source)
               (tree groups)
               (match (pattern-emit tree {0 (list :save 0)} 1)
                 (program end)
                 (list :pattern source groups
                       (associate (associate program end (list :save 1)) (+ end 1) (list :match)))))
          {:kind :pattern-syntax :message message :values (column)}
          (raise {:kind :domain-error :message (concatenate "compile-pattern: " message)
                  :values (list source column)})))))

(define pattern-fail
  (lambda (message column)
    (raise {:kind :pattern-syntax :message message :values (list column)})))

; The whole source, as (tree groups).
(define pattern-parse
  (lambda (source)
    (match (pattern-alternatives (characters source) 1 0 ())
      (tree () _ groups) (list tree groups)
      (_ (")" &rest _) column _) (pattern-fail "unexpected )" column))))

; Sequences separated by |, up to a ) or the end, as (tree text column groups).
(define pattern-alternatives
  (lambda (text column groups choices)
    (match (pattern-sequence text column groups ())
      (tree ("|" &rest more) after groups-after)
      (pattern-alternatives more (+ after 1) groups-after (prepend tree choices))
      (tree more after groups-after)
      (list (if (empty? choices) tree otherwise (list :alternation (reverse (prepend tree choices))))
            more after groups-after))))

(define pattern-sequence
  (lambda (text column groups items)
    (if (or (empty? text) (equal? (first text) "|") (equal? (first text) ")"))
        (list (list :sequence (reverse items)) text column groups)
        otherwise
        (match (pattern-atom text column groups)
          (atom more after groups-after)
          (match (pattern-quantifier atom more after)
            (item rest-text rest-column) (pattern-sequence rest-text rest-column groups-after (prepend item items)))))))

(define pattern-atom
  (lambda (text column groups)
    (match text
      ("(" &rest more)
      (match (pattern-alternatives more (+ column 1) (+ groups 1) ())
        (tree (")" &rest after) close groups-after) (list (list :group (+ groups 1) tree) after (+ close 1) groups-after)
        otherwise (pattern-fail "missing )" column))
      ("[" &rest more)
      (match more
        ("^" &rest items) (pattern-class items (+ column 2) column groups #true ())
        otherwise (pattern-class more (+ column 1) column groups #false ()))
      ("." &rest more) (list (list :any) more (+ column 1) groups)
      ("^" &rest more) (list (list :start) more (+ column 1) groups)
      ("$" &rest more) (list (list :end) more (+ column 1) groups)
      ("\\" &rest more)
      (let ((item (pattern-escape more column)))
        (list (if (integer? item) (list :char item) otherwise (list :class #false (list item)))
              (rest more) (+ column 2) groups))
      (character &rest more)
      (if (or (equal? character "*") (equal? character "+") (equal? character "?") (equal? character "{"))
          (pattern-fail "nothing to repeat" column)
          (or (equal? character "]") (equal? character "}"))
          (pattern-fail (concatenate "unexpected " character) column)
          otherwise (list (list :char (first (string-to-code-points character))) more (+ column 1) groups)))))

; The character after a \: a code point, or a named class.
(define pattern-escape
  (lambda (text column)
    (match text
      ("d" &rest _) :digit
      ("D" &rest _) :not-digit
      ("w" &rest _) :word
      ("W" &rest _) :not-word
      ("s" &rest _) :space
      ("S" &rest _) :not-space
      ("n" &rest _) 10
      ("t" &rest _) 9
      ("r" &rest _) 13
      (character &rest _)
      (if (contains? #{"\\" "." "^" "$" "|" "?" "*" "+" "(" ")" "[" "]" "{" "}" "-" "/"} character)
          (first (string-to-code-points character))
          otherwise (pattern-fail "invalid escape" column))
      otherwise (pattern-fail "invalid escape" column))))
```

A quantifier wraps the atom before it in `(:repeat atom low high)`, with `high` `#void` when there is no limit. An anchor cannot be repeated.

```lisp
(define pattern-quantifier
  (lambda (atom text column)
    (match text
      ("*" &rest more) (pattern-repeat atom 0 #void more (+ column 1) column)
      ("+" &rest more) (pattern-repeat atom 1 #void more (+ column 1) column)
      ("?" &rest more) (pattern-repeat atom 0 1 more (+ column 1) column)
      ("{" &rest more) (pattern-braces atom more (+ column 1) column)
      otherwise (list atom text column))))

(define pattern-repeat
  (lambda (atom low high text after column)
    (if (or (equal? atom (list :start)) (equal? atom (list :end))) (pattern-fail "nothing to repeat" column)
        otherwise (list (list :repeat atom low high) text after))))

(define pattern-braces
  (lambda (atom text column open)
    (match (pattern-number text 0 0)
      (low 0 _) (pattern-fail "invalid repetition" open)
      (low read ("}" &rest more)) (pattern-bounded atom low low more (+ column read 1) open)
      (low read ("," "}" &rest more)) (pattern-bounded atom low #void more (+ column read 2) open)
      (low read ("," &rest more))
      (match (pattern-number more 0 0)
        (high read-high ("}" &rest after))
        (if (= read-high 0) (pattern-fail "invalid repetition" open)
            otherwise (pattern-bounded atom low high after (+ column read read-high 2) open))
        otherwise (pattern-fail "invalid repetition" open))
      otherwise (pattern-fail "invalid repetition" open))))

(define pattern-bounded
  (lambda (atom low high text after open)
    (if (or (> low 1000) (and (not (void? high)) (or (> high 1000) (> low high))))
        (pattern-fail "invalid repetition" open)
        otherwise (pattern-repeat atom low high text after open))))

; The digits at the front of the text, as (value read after); a value above
; 1000 stays at 1001, which is enough to refuse it.
(define pattern-number
  (lambda (text value read)
    (if (and (not (empty? text)) (digit? (first text)))
        (pattern-number (rest text) (if (> value 1000) 1001 otherwise (- (+ (* value 10) (first (string-to-code-points (first text)))) 48))
                        (+ read 1))
        otherwise (list value read text))))
```

A class gathers its items: a range `(low high)` of code points, or a named class such as `:digit`.

```lisp
(define pattern-class
  (lambda (text column open groups negated items)
    (match text
      () (pattern-fail "missing ]" open)
      ("]" &rest more)
      (if (empty? items) (pattern-fail "empty class" open)
          otherwise (list (list :class negated (reverse items)) more (+ column 1) groups))
      otherwise
      (match (pattern-class-element text column)
        (element after after-column)
        (match after
          ("-" "]" &rest _) (pattern-class after after-column open groups negated (prepend (pattern-item element) items))
          ("-" _ &rest _)
          (match (pattern-class-element (rest after) (+ after-column 1))
            (high more more-column)
            (if (and (integer? element) (integer? high) (<= element high))
                (pattern-class more more-column open groups negated (prepend (list element high) items))
                otherwise (pattern-fail "invalid range" column)))
          otherwise (pattern-class after after-column open groups negated (prepend (pattern-item element) items)))))))

; One character of a class, or an escape, as (element after column).
(define pattern-class-element
  (lambda (text column)
    (match text
      ("\\" &rest more) (list (pattern-escape more column) (rest more) (+ column 2))
      (character &rest more) (list (first (string-to-code-points character)) more (+ column 1)))))

(define pattern-item
  (lambda (element)
    (if (integer? element) (list element element) otherwise element)))
```

The tree becomes instructions, from the first free number on. Each function returns the program and the next free number. A jump forward is written after the instructions it jumps over, once their number is known. `x{m,n}` is `x` written `m` times and then `n − m` optional copies, each inside the one before, so skipping one skips the rest.

```lisp
(define pattern-emit
  (lambda (tree program next)
    (match tree
      (:sequence items) (fold (lambda (state item) (apply pattern-emit item state)) (list program next) items)
      (:group index inner)
      (match (pattern-emit inner (associate program next (list :save (* 2 index))) (+ next 1))
        (after end) (list (associate after end (list :save (+ (* 2 index) 1))) (+ end 1)))
      (:alternation choices) (pattern-emit-choices choices program next)
      (:repeat inner low high) (pattern-emit-repeat inner low high program next)
      otherwise (list (associate program next tree) (+ next 1)))))

(define pattern-emit-choices
  (lambda (choices program next)
    (if (empty? (rest choices)) (pattern-emit (first choices) program next)
        otherwise
        (match (pattern-emit (first choices) program (+ next 1))
          (after-first jump)
          (match (pattern-emit-choices (rest choices) after-first (+ jump 1))
            (after-rest end)
            (list (associate (associate after-rest next (list :split (+ next 1) (+ jump 1))) jump (list :jump end))
                  end))))))

(define pattern-emit-repeat
  (lambda (inner low high program next)
    (if (> low 0)
        (match (pattern-emit inner program next)
          (after end) (pattern-emit-repeat inner (- low 1) (if (void? high) high otherwise (- high 1)) after end))
        (void? high)
        (match (pattern-emit inner program (+ next 1))
          (after jump)
          (list (associate (associate after next (list :split (+ next 1) (+ jump 1))) jump (list :jump next))
                (+ jump 1)))
        (= high 0) (list program next)
        otherwise
        (match (pattern-emit inner program (+ next 1))
          (after more)
          (match (pattern-emit-repeat inner 0 (- high 1) after more)
            (after-rest end) (list (associate after-rest next (list :split (+ next 1) end)) end))))))
```

The machine reads the text once, one character at a time. Before reading, it holds a list of threads, each a pair of an instruction that reads a character and the slots recorded so far. The list is in order of priority, and no two threads are at the same instruction.

`pattern-follow` adds a thread at an instruction. It follows the instructions that read nothing, depth first, the preferred side of a split before the other, and adds the threads they lead to. The set `visited` keeps it from adding a thread twice, or looping in a repetition of something empty. The threads are gathered in reverse order.

`pattern-search` runs the steps. While no thread is alive and no match has been found, a new thread that starts with an instruction reading a character dies at once on any character that instruction does not accept. So the search skips those characters without starting a thread, which makes looking for a pattern that starts with a literal or a class in a long text several times faster.

`pattern-step` reads one character with each thread, in order. A thread at `(:match)` is a match: the threads after it have lower priority and are dropped, while those before it go on, since they may still find a match they prefer. Until a match is found, a new thread starts at each position with the lowest priority, which finds the leftmost match. With `whole`, only the thread at position 0 starts, and a match counts only at the end of the text, as `matches?` needs.

```lisp
(define pattern-follow
  (lambda (program pc slots position size visited threads)
    (if (contains? visited pc) (list visited threads)
        otherwise
        (let ((seen (adjoin visited pc)))
          (match (get program pc)
            (:jump target) (pattern-follow program target slots position size seen threads)
            (:split preferred other)
            (match (pattern-follow program preferred slots position size seen threads)
              (after-seen after-threads) (pattern-follow program other slots position size after-seen after-threads))
            (:save slot) (pattern-follow program (+ pc 1) (associate slots slot position) position size seen threads)
            (:start) (if (= position 0) (pattern-follow program (+ pc 1) slots position size seen threads)
                         otherwise (list seen threads))
            (:end) (if (= position size) (pattern-follow program (+ pc 1) slots position size seen threads)
                       otherwise (list seen threads))
            otherwise (list seen (prepend (list pc slots) threads)))))))

(define pattern-accepts?
  (lambda (instruction code-point)
    (match instruction
      (:char expected) (= code-point expected)
      (:any) #true
      (:class negated items) (not (equal? negated (any? (lambda (item) (pattern-in-item? item code-point)) items)))
      otherwise #false)))

(define pattern-in-item?
  (lambda (item code-point)
    (if (list? item) (<= (first item) code-point (first (rest item)))
        (equal? item :digit) (<= 48 code-point 57)
        (equal? item :word) (pattern-word? code-point)
        (equal? item :space) (pattern-space? code-point)
        (equal? item :not-digit) (not (<= 48 code-point 57))
        (equal? item :not-word) (not (pattern-word? code-point))
        otherwise (not (pattern-space? code-point)))))

(define pattern-word?
  (lambda (code-point)
    (or (letter-code-point? code-point) (<= 48 code-point 57) (= code-point 95))))

(define pattern-space?
  (lambda (code-point)
    (or (= code-point 32) (<= 9 code-point 13))))

; One character, or #void at the end, read by each thread: (visited threads matched).
(define pattern-step
  (lambda (program threads code-point position size whole visited next matched)
    (if (empty? threads) (list visited next matched)
        otherwise
        (match (first threads)
          (pc slots)
          (let ((instruction (get program pc)))
            (if (equal? instruction (list :match))
                (if (or (not whole) (= position size)) (list visited next slots)
                    otherwise (pattern-step program (rest threads) code-point position size whole visited next matched))
                (and (not (void? code-point)) (pattern-accepts? instruction code-point))
                (match (pattern-follow program (+ pc 1) slots (+ position 1) size visited next)
                  (seen more)
                  (pattern-step program (rest threads) code-point position size whole seen more matched))
                otherwise (pattern-step program (rest threads) code-point position size whole visited next matched)))))))

; The slots of the match the threads lead to, from position on, or #void.
(define pattern-search
  (lambda (program leading text position size whole visited reversed matched)
    (if (and (empty? reversed) (void? matched) (not whole) (not (void? leading))
             (not (empty? text)) (not (pattern-accepts? leading (first text))))
        (pattern-search program leading (rest text) (+ position 1) size whole visited reversed matched)
        otherwise
        (match (if (and (void? matched) (or (not whole) (= position 0)))
                   (pattern-follow program 0 {} position size visited reversed)
                   otherwise (list visited reversed))
          (_ all)
          (let ((threads (reverse all)))
            (if (and (empty? threads) (or whole (not (void? matched)))) matched
                (empty? text) (first (rest (rest (pattern-step program threads #void position size whole #{} () matched))))
                otherwise
                (match (pattern-step program threads (first text) position size whole #{} () matched)
                  (seen next found) (pattern-search program leading (rest text) (+ position 1) size whole seen next found))))))))
```

The public functions convert the text to code points once. A match takes its text, and its groups' texts, from the code points where the search started, so a search costs the characters it reads and the match it returns. `find-all` searches again after each match; after an empty match it starts one character later, so it never finds the same empty match twice.

```lisp
(define pattern-of
  (lambda (name pattern)
    (if (string? pattern) (compile-pattern pattern)
        (and (list? pattern) (not (empty? pattern)) (equal? (first pattern) :pattern)) pattern
        otherwise (raise {:kind :wrong-type :message (concatenate name ": not a pattern") :values (list pattern)}))))

(define pattern-slice
  (lambda (code-points from length)
    (code-points-to-string (take length (drop from code-points)))))

; The first match from position on, with code-points the text from there.
(define pattern-find
  (lambda (compiled code-points position size whole)
    (match compiled
      (:pattern _ groups program)
      (let ((leading (match (get program 1)
                       (:char _) (get program 1)
                       (:class _ _) (get program 1)
                       otherwise #void))
            (slots (pattern-search program leading code-points position size whole #{} () #void)))
        (if (void? slots) #void
            otherwise
            (let ((start (get slots 0))
                  (end (get slots 1)))
              {:start start :end end :text (pattern-slice code-points (- start position) (- end start))
               :groups (map (lambda (index)
                              (let ((from (get slots (* 2 index) #void))
                                    (to (get slots (+ (* 2 index) 1) #void)))
                                (if (or (void? from) (void? to)) #void
                                    otherwise (pattern-slice code-points (- from position) (- to from)))))
                            (range 1 (+ groups 1)))}))))))

(define find-match
  (lambda (pattern text)
    (let ((compiled (pattern-of "find-match" pattern))
          (code-points (string-to-code-points text)))
      (pattern-find compiled code-points 0 (count code-points) #false))))

(define matches?
  (lambda (pattern text)
    (let ((compiled (pattern-of "matches?" pattern))
          (code-points (string-to-code-points text)))
      (not (void? (pattern-find compiled code-points 0 (count code-points) #true))))))

(define find-all
  (lambda (pattern text)
    (let ((compiled (pattern-of "find-all" pattern))
          (code-points (string-to-code-points text)))
      (reverse (pattern-all compiled code-points 0 (count code-points) ())))))

(define pattern-all
  (lambda (compiled code-points position size found)
    (let ((match-value (if (> position size) #void
                           otherwise (pattern-find compiled code-points position size #false))))
      (if (void? match-value) found
          otherwise
          (let ((end (get match-value :end))
                (next (if (= end (get match-value :start)) (+ end 1) otherwise end)))
            (pattern-all compiled (drop (- next position) code-points) next size (prepend match-value found)))))))
```

`replace-all` and `split-pattern` take the text between the matches that `find-all` finds, and gather the pieces for one `concatenate`, as `join` does. A replacement that is not a String is the error of `concatenate`.

```lisp
(define replace-all
  (lambda (pattern replace text)
    (let ((compiled (pattern-of "replace-all" pattern))
          (code-points (string-to-code-points text))
          (pieces (pattern-between (reverse (pattern-all compiled code-points 0 (count code-points) ()))
                                   code-points 0 ())))
      (apply concatenate (reverse (pattern-replace-step pieces replace ()))))))

(define pattern-replace-step
  (lambda (pieces replace gathered)
    (match pieces
      (last) (prepend last gathered)
      (before match-value &rest more) (pattern-replace-step more replace (prepend (replace match-value) (prepend before gathered))))))

(define split-pattern
  (lambda (pattern text)
    (let ((compiled (pattern-of "split-pattern" pattern))
          (code-points (string-to-code-points text)))
      (filter string? (pattern-between (reverse (pattern-all compiled code-points 0 (count code-points) ()))
                                       code-points 0 ())))))

; The text before each match, the match, and so on, ending with the text
; after the last match.
(define pattern-between
  (lambda (matches code-points position pieces)
    (if (empty? matches) (reverse (prepend (code-points-to-string code-points) pieces))
        otherwise
        (let ((match-value (first matches))
              (start (get match-value :start))
              (end (get match-value :end)))
          (pattern-between (rest matches) (drop (- end position) code-points) end
                           (prepend match-value
                                    (prepend (pattern-slice code-points 0 (- start position)) pieces)))))))
```

## 5. Conformance

The file `stdlib-conformance.json` has 284 cases, in the same format as the core `eval` cases, with one difference: the libraries `list`, `text`, `function`, `map`, `set`, `number`, `result`, `random`, `json`, `csv`, `time` and `pattern` are available to `import`. Each program imports what it uses. An implementation that provides the library MUST pass all of them, in addition to the core cases.

The cases cover every function in section 3, the errors of `take`, `drop` and `split`, the two-list limit of `append`, the early exit of `any?`, the stability of `sort`, a program redefining a library name, a file included with a prefix that imports a library, a traversal of one hundred thousand elements, which only passes if the functions run in constant memory, every function of `map` with its errors, a map of one hundred thousand keys built with `from-entries` and reduced with `filter-entries`, every function of `set`, sets of one hundred thousand elements, the order of `precedes?` and its stable use in `sort`, `uppercase`, `lowercase`, `letter?` and `digit?` at the edges of the ASCII and Latin-1 ranges, `pad-left` and `pad-right` with texts wider than the width, accented fills and fills of the wrong length, `format` with values of every type, braces outside `{}` and too few or too many values, `join` of a single part that is returned as it is, and `join` of two hundred thousand parts and `format` of ten thousand values, which only pass in reasonable time if they grow linearly, `max` and `min` with Integers, with Floats, with their errors and over one hundred thousand arguments, `abs`, `floor`, `ceiling` and `round` at negative numbers, `-0.0`, halfway values, Floats above 2^52 and the edges of the range of Integer, `characters` of accented letters, of characters outside the BMP and of the empty text, and its use with `match`, `digits-to-integer` and `integer-to-digits` in bases 2, 8, 10, 16 and 36, with uppercase digits, signs, texts that are not numbers, the smallest and largest Integer, overflow, every base error and a round trip in every base, `span` with a test that always or never holds and a test it stops calling, and with a `let` pattern, `zip` of lists of different lengths and as the input of `from-entries`, both over one hundred thousand elements, `ok`, `failure` with no detail and with several, `ok?` and `failure?` over values of every shape, `then` going on, stopping and rejecting what is not a result, as the step of a `chain` with patterns, `map-results` stopping at the first failure without calling `f` again, and over one hundred thousand elements, `random-generator` for the first seeds, the largest one and one from `random-seed!`, the exact draws of `random-float` and of `random-integer` over small, negative, one-element, wide and edge ranges, sixty thousand throws of a die, `shuffle` of short lists, of lists too short to draw from and of twenty thousand elements, and every error of `random`, `read-json` over every kind of value, numbers at the edges of Integer and Float and beyond them, every escape and surrogate case, keys that repeat, whitespace, every failure message with its line and column, also across lines, `write-json` of every type, of escapes and of values without a JSON form, a round trip, and an array of twenty thousand numbers, which only passes in reasonable time if reading grows linearly, `read-csv` with every kind of line break, empty lines and fields, quoted fields holding separators, line breaks and quotes, a lone carriage return, records of different lengths, every failure with its position, other separators and every separator error, `write-csv` quoting only what needs it, its errors, a round trip with two separators, the idiom that turns a header into Maps, and twenty thousand records read in linear time, `time-to-date` at the epoch, before it, at leap days, at the edges of years 0 and 9999 and over a week of weekdays, `date-to-time` with defaults, ignored keys, every range error and overflow, round trips through both, `format-time` and its range, `leap-year?` and `days-in-month` over centuries and negative years, `read-time` of dates, times, fractions, zones and offsets, every failure with its column, the idioms of a fixed offset and of adding days, and every day of four hundred years, `compile-pattern` and the program it gives, every form of the syntax of section 4.12, the classes at the edges of ASCII and Latin-1, the preference of repetitions and alternatives, groups that repeat or take no part, repetitions of something empty, empty matches in `find-all`, `replace-all` and `split-pattern`, positions in characters outside ASCII, a pattern compiled once and used again, every error of a pattern with its column, patterns that would make a backtracking matcher take exponential time, and five thousand matches in a text outside ASCII, which only pass in reasonable time if the search and the slicing grow linearly, the trace of an error in the function given to `map`, and `import` itself: names unbound without it, prefixes, the dependencies of `text`, `function`, `map`, `set`, `number` and `result` on `list` of `json` on four libraries, of `csv` and `time` on three and of `pattern` on two, and repeated imports.
