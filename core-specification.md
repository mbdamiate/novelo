# Core Specification — Novelo

Nov 10, 2026 · @Matheus Damiate

## 1. Overview

This specification defines the **minimal core** of Novelo, a useful, immutable Lisp: reader, evaluator, primitives and printing. For the same input, every implementation, in any language, MUST produce the same datums, the same values and the same errors.

A **datum** is a complete expression already converted into a value of the language itself, before it is evaluated. The text `(if (> x 0) x otherwise 0)` becomes an ordinary list of symbols and integers. There is no separate syntax tree: code is data.

The core is a three-stage pipeline:

1. Reader: text to datums (sections 2 to 7).
2. Expansion: macros and derived forms rewritten into the five special forms (section 8).
3. Evaluation: special forms, primitives and printing (sections 8 to 10).

**Libraries.** Nothing beyond the primitives (section 9) is visible to a program until it asks for it. A program declares each library it uses with `import` (8.9), much as a C program declares its headers. The core defines the mechanism and one native library, `io`, which holds the input and output functions.

**Out of scope.** The standard library, with libraries such as `list`, `text` and `map` and functions such as `map`, `filter` and `sort`, has its own specification. The extensions already planned, such as sum types and gradual typing, belong to the full specification. The core only reserves the names they use (section 8.2).

**Name.** The language is called **Novelo**, Portuguese for a ball of yarn: a single thread, unwound from its first element to the rest, as the lists of the language are. Source files use the extension `.nvl`; the extension is a convention, and `include` accepts any path (8.8).

**Normative terms.** MUST indicates a mandatory requirement. MUST NOT indicates a prohibition. MAY indicates implementation freedom that does not affect observable behavior.

**Version:** core 0.10.1, draft.

**Changes since 0.10.0.** No change to the language. Measuring which code of the reference implementation the cases reach showed behaviors with no case, now covered by 38 new ones (12.4): an error inside a map, set or quasiquote being built, expansion errors nested in `if`, `let`, `and`, `or`, `match`, literals and quasiquotes, macros with many arguments or whose transformer fails, the argument types of `/`, `contains?`, `string-at`, `substring`, `empty?`, `dissociate`, `concatenate`, `string-to-number`, `string-to-symbol`, `symbol-to-string`, `raise` and `catch`, `quotient` and `remainder` by `-1`, every type as a map key, keys nested deeper than twelve levels, an `include` path with a NUL, and a `\u` escape cut by the end of the input.

**Changes since 0.9.0.** A new primitive, `float-to-fixed`, writes a Float with a fixed number of decimals, rounded from its exact binary value with ties away from zero (9). It cannot be written in the language, since the arithmetic of Floats rounds at every step. Nothing else changes.

**Changes since 0.8.1.** A new type, Float: a finite IEEE 754 binary64 number, written `3.14`, `-0.5` or `6.02e23` (2, 2.5, 4.2). A Float literal whose value is too large is the read error `float-overflow` (7.4). Integers and Floats never mix: `+`, `-`, `*`, the comparisons and `=` take all Integers or all Floats, and a mix is `:wrong-type` (2.4, 9). The division `/` comes back, for Floats only (9). There is no infinity and no NaN: an operation whose result would be one of them is an error, `:float-overflow`, `:division-by-zero` or `:domain-error` (8.5). New primitives `float?`, `/`, `integer-to-float`, `truncate` and `sqrt`; `string-to-number` also reads Floats (9). Floats print as the shortest decimal that reads back as the same value (10). Tokens such as `1e3`, `.5` and `5.` are still `invalid-number`. A program that runs on 0.8.1 gives the same result, unless it calls `/`, which was unbound, or calls `string-to-number` on a text such as `"3.14"`, which gave `#void`.

**Changes since 0.8.0.** The REPL imports every library of the standard library, whatever their number, instead of a fixed list (8.7). Nothing else changes.

**Changes since 0.7.0.** Two primitives convert between a String and its code points: `string-to-code-points` and `code-points-to-string` (9). A number that is not a Unicode scalar value produces the new error `:invalid-code-point` (8.5, 9). With them, comparing, classifying and changing the case of text can be written in the language, so the standard library does it. Section 8.9 no longer lists the libraries of the standard library, which has its own specification.

**Changes since 0.6.0.** A new derived form, `match`, takes a value apart by patterns: literals, `_`, names, lists with an optional `&rest`, and maps by key (8.3). When no pattern matches and there is no `otherwise`, the result is the new error `:no-match` (8.5). `match` is now the name of a derived form, so it cannot name a function called as `(match …)`. Section 5.3 no longer says that `#{` is reserved, since 0.5.0 gave it to sets.

**Changes since 0.5.0.** `if` accepts several test and result pairs, with an optional `otherwise` at the end: `(if c1 a1 c2 a2 otherwise b)` (8.2). The two forms that existed keep their meaning, and every other shape is still an expansion error. Each result is in tail position (8.4).

**Changes since 0.4.0.** A new type, Set, written `#{…}` (2, 5.2, 5.5), with the read error `duplicate-element` (7.4) and the runtime error `:duplicate-element` (8.4, 8.5). Set literals evaluate their elements like map literals (8.1, 8.4). `equal?`, printing and quasiquote handle sets (2.4.1, 8.3, 10). New primitives `set?`, `adjoin`, `disjoin` and `elements`; `count`, `empty?` and `contains?` accept sets (9). Sets are persistent, like maps (8.6). The REPL also imports the library `set` (8.7).

**Changes since 0.3.0.** Maps are persistent: `associate`, `dissociate` and `get` MUST NOT copy the whole map, and a map of one million keys MUST be built one key at a time (8.6). The REPL also imports the library `map` of the standard library (8.7).

**Changes since 0.2.2.** Errors carry the source position where they happened: a descriptor gains the key `:position` as the error leaves an expression with a known position (8.5). Code generated by macros and derived forms takes the position of the nearest enclosing form that has one (6.3). The message for an uncaught error includes the position (8.7). `raise` keeps the position a descriptor already has (8.5). In the conformance file, an `eval` case MAY fix the position of its uncaught error with `error_position` (12.4).

**Changes since 0.2.1.** The language is named Novelo, and source files use the extension `.nvl`. In the conformance file, the program of an `eval` case is the file `main.nvl` (12.4). Nothing else changes.

**Changes since 0.2.0.** Points the text left open are now fixed: the order of errors inside `\u{…}` (4.3, 7.3), `define-macro` with a transformer that is not a function (8.2), macro names used as values (8.2), the call depth an implementation MUST support and `:stack-overflow` (8.4, 8.5), `read-line!` with invalid UTF-8 and `:io-error` (8.5, 9), when a file or library counts as loaded (8.8, 8.9), the reasons of `:import-error` (8.9) and how `display!` prints strings inside collections (10).

**Changes since 0.1.0.** `import` and libraries (8.9). `display!`, `write!` and `read-line!` moved from the global environment to the native library `io`. The standard library is no longer loaded before the program: a program imports what it uses. The REPL starts with every library imported (8.7).

## 2. Data model

The reader produces values of nine types, and evaluation adds a tenth, Function, for closures and primitives, which the reader never produces. All of them are immutable: no operation of the language changes a value after it is created.

| Type | Written example | Content |
| --- | --- | --- |
| Boolean | `#true`, `#false` | one of two values |
| Void | `#void` | single value, result of side effects |
| Integer | `42`, `-7` | signed 64-bit integer |
| Float | `3.14`, `-0.5`, `6.02e23` | finite IEEE 754 binary64 number (2.5) |
| String | `"hello"` | sequence of Unicode code points; there is no character type (2.5) |
| Symbol | `define`, `&rest` | interned name |
| Keyword | `:name` | interned name, a type distinct from Symbol |
| List | `(a b)`, `()` | proper linked list; `()` is the empty list |
| Map | `{:a 1}` | key-value pairs, unique keys |
| Set | `#{1 2}` | elements, unique under `equal?` |
| Function | no written form | closure created by `lambda`, or a primitive |

### 2.1 Proper lists

Every list is proper: the rest of a non-empty list is always a list. Dotted pairs do not exist. The empty list `()` is a value of type List and MUST NOT be confused with `#false` or with `#void`.

### 2.2 Interning

Two symbols with the same name MUST be the same object, comparable by identity. The same holds for keywords. A symbol and a keyword with the same name are different values. The implementation MAY share the storage of the name between them. The only exception is symbols created by `generate-symbol` (section 9), which are not interned.

### 2.3 Order in maps and sets

List preserves reading order. The iteration order of Map, which `keys` (section 9) exposes, and of Set, which `elements` exposes, is unspecified: the implementation MAY use hash-based persistent structures. For this reason, the conformance tests compare maps and sets by equality, not by printed text.

### 2.4 Equality

The language has two equality predicates. Maps and the conformance tests use `equal?`.

| Predicate | Question | `(… 1 1)` | `(… (list 1) (list 1))` |
| --- | --- | --- | --- |
| `equal?` | same type and same content? | `#true` | `#true` |
| `=` | numbers of one type with the same value? | `#true` | error: not numbers |

### 2.4.1 `equal?`

`equal?` requires the same type: values of different types are never equal, so `foo` and `:foo`, or `"1"` and `1`, are different. Within each type:

- **Boolean, Void, Integer:** the same value.
- **Float:** the same numeric value, so `0.0` and `-0.0` are `equal?`. An Integer and a Float are never `equal?`: `(equal? 1 1.0)` is `#false`.
- **String:** the same sequence of code points.
- **Symbol, Keyword:** identity (because of interning).
- **List:** same length and `equal?` elements, in order.
- **Map:** same number of pairs and, for each key of one, an `equal?` key in the other with an `equal?` value, regardless of order.
- **Set:** same number of elements and, for each element of one, an `equal?` element in the other, regardless of order. A Set is never `equal?` to a Map or a List with the same contents.
- **Function:** identity: a function is only `equal?` to itself.

### 2.4.2 `=`

`=` accepts only numbers, with two or more arguments, all Integers or all Floats: `(= a b c)` is true when all of them are equal. An argument that is not a number, or a mix of Integers and Floats, is an error (`:wrong-type`). For Floats, `=` compares numeric values, like `equal?`. Compared to `equal?`, `=` is meant for chained numeric comparisons and catches accidental use with other types.

### 2.5 Text and numbers

**There is no character type.** A character is a String of length 1: `(string-at "abc" 0)` returns `"a"`. String lengths and indices count code points.

**There are two numeric types that never mix.** An Integer is exact; a Float is an IEEE 754 binary64 number, always finite: there is no infinity and no NaN, so every Float is `equal?` to itself and can be a key of a Map or an element of a Set. Arithmetic and comparison take all Integers or all Floats; `(+ 1 2.5)` is `:wrong-type`, and a program converts explicitly with `integer-to-float` and `truncate` (9). Mixing is not allowed because it hides a loss: an Integer above 2^53 has no exact Float. Integer division is done with `quotient` and `remainder`, and `/` divides Floats.

## 3. Lexical syntax

Source text is a sequence of datums separated by "atmosphere": whitespace and comments. The grammar below uses ISO 14977 EBNF; sections 4 and 5 detail each production.

### 3.1 Grammar

```
program       = atmosphere , { datum , atmosphere } ;

atmosphere    = { whitespace | line-comment } ;
whitespace    = U+0020 | U+0009 | U+000A | U+000D | U+000C ;
line-comment  = ";" , { any code point except U+000A } ;

datum         = list | map | set | prefixed | string | hash-literal | keyword | token ;

list          = "(" , atmosphere , { datum , atmosphere } , ")" ;
map           = "{" , atmosphere , { datum , atmosphere } , "}" ;  (* even count *)
set           = "#{" , atmosphere , { datum , atmosphere } , "}" ;  (* no repeats *)
prefixed      = ( "'" | "`" | ",@" | "," ) , atmosphere , datum ;

hash-literal  = ( "#true" | "#false" | "#void" ) ;  (* followed by a delimiter *)
keyword       = ":" , constituent , { constituent } ;
token         = constituent , { constituent } ;      (* integer or symbol: 4.6 *)

string        = '"' , { string-char | escape } , '"' ;
string-char   = any code point except '"' and "\" ;
escape        = "\n" | "\t" | '\"' | "\\" ;

delimiter     = whitespace | "(" | ")" | "{" | "}" | reserved
              | '"' | ";" | "'" | "`" | "," | end of input ;
reserved      = "[" | "]" ;                           (* always an error: 5.3 *)
constituent   = any code point that is not a delimiter ;
```

### 3.2 Encoding

The input MUST be valid UTF-8. An invalid byte sequence MUST produce the `invalid-utf8` error at the position of the first invalid byte. U+FEFF (BOM) has no special treatment: it is a constituent like any other.

### 3.3 Whitespace

Only the five ASCII code points of the `whitespace` production are whitespace. Other Unicode spaces, such as U+00A0, are constituents and may be part of symbols. The comma is NOT whitespace: it is the `unquote` prefix, so `a,b` is two datums, `a` and `(unquote b)`.

### 3.4 Comments

`;` comments until the end of the line. It is the only comment form in the core: `#|` and `#;` have no meaning and produce `unknown-dispatch` (5.5). This leaves room to add block or datum comments in the future without breaking existing programs.

### 3.5 Delimiters

A token ends at the first delimiter. `#` and `:` are NOT delimiters: `a#b` and `a:b` are valid symbols. At the **start** of a datum, however, a `#` begins a `hash-literal`, and a `:` begins a `keyword`.

## 4. Literals

A token becomes a number, a symbol or an error according to the classification in section 4.6. Strings, keywords and `#` literals have their own syntax.

### 4.1 Integers

```
integer = [ "-" ] , digit , { digit } ;
```

The valid range is −2^63 to 2^63 − 1. A literal outside it MUST produce the `integer-overflow` error at the position of the start of the token, including `-9223372036854775809`. Leading zeros are allowed: `007` is `7`. There is no `+` sign.

### 4.2 Floats

```
float    = [ "-" ] , digit , { digit } , "." , digit , { digit } , [ exponent ] ;
exponent = "e" , [ "-" ] , digit , { digit } ;
```

A Float literal has digits on both sides of the point; the optional exponent is a lowercase `e` followed by a decimal power of ten, which may be negative. As with integers, there is no `+` sign. `3.14`, `-0.5`, `007.5`, `6.02e23` and `1.0e-9` are Floats. `1e3`, `.5`, `5.`, `1.e3`, `1.5e`, `1.5E3` and `1.5e+3` are numeric under the rule in section 4.6 but do not match `float`, so they produce `invalid-number`.

The value is the binary64 number nearest to the decimal value of the literal, with ties to the one whose last bit is even. A literal whose value rounds to a number too large for binary64 MUST produce the `float-overflow` error at the position of the start of the token. A value too small rounds, to a subnormal number or to zero, without error. `-0.0` is negative zero.

### 4.3 Strings

A string runs from `"` to the next unescaped `"`. Line breaks and any Unicode character may be written directly inside it. The valid escapes are:

| Escape | Result |
| --- | --- |
| `\n` | U+000A |
| `\t` | U+0009 |
| `\r` | U+000D |
| `\"` | double quote |
| `\\` | backslash |
| `\u{H…}` | the code point with the hexadecimal value `H…` |

In `\u{H…}`, the escape runs from the backslash to the first `}`. Between the braces there MUST be from 1 to 6 hexadecimal digits, upper or lower case, so `\u{41}`, `\u{000041}` and `\u{1f600}` are valid. The value MUST be a Unicode scalar value: at most U+10FFFF and outside the surrogates U+D800 to U+DFFF. A `\u` not followed by `{`, a character other than a hexadecimal digit before the `}`, no digits, more than 6 digits or a value that is not a scalar value MUST produce `invalid-escape`.

Any other character after `\` MUST produce `invalid-escape`. Every `invalid-escape` is reported at the position of the backslash. End of input before the closing `"`, including in the middle of an escape, produces `unterminated-string`, at the position of the opening quote.

**Order inside `\u{…}`.** The escape is checked as it is read, one character at a time. The first character that cannot continue it, a character other than `{` after `\u`, a character other than a hexadecimal digit or `}` after the `{`, or a seventh digit, produces `invalid-escape` at once, even if the input then ends before the closing `"`. The value is checked when the `}` is read. So `"\u{zz` and `"\u{1234567` produce `invalid-escape`, while `"\u{D800` and `"\u{41` produce `unterminated-string`: the input ends while the escape could still be valid.

### 4.4 Booleans and void

`#true`, `#false` and `#void` are read exactly like this, followed by a delimiter. Short forms such as `#t` and `#f` are NOT accepted and produce `unknown-dispatch`.

### 4.5 Keywords

A keyword is `:` followed by one or more constituents, which form the name: `:name`, `:a:b` and `:1` are valid. `:` alone MUST produce `empty-keyword`.

### 4.6 Token classification

A token is **numeric** if, after removing a leading `+` or `-`, it starts with a digit or with `.` followed by a digit. Classification follows this order:

1. Matches `integer`: Integer.
2. Matches `float`: Float.
3. Is numeric but matches neither: `invalid-number` error.
4. Is exactly `.`: `dotted-pair` error.
5. Any other case: Symbol.

Rule 3 exists so that typos in numbers do not silently become symbols, and to reserve the remaining numeric syntax for later extensions.

| Token | Result |
| --- | --- |
| `42`, `-7`, `007` | Integer |
| `3.14`, `-0.5`, `1.0e-9` | Float |
| `-`, `-x`, `...`, `&rest`, `empty?`, `λ` | Symbol |
| `1e3`, `1+`, `5.`, `.5`, `+5`, `-.5`, `1.e3`, `1.5E3`, `12abc` | `invalid-number` error |
| `.` | `dotted-pair` error |

## 5. Collections and shorthands

Three collections have their own delimiters, four prefixes expand into two-element lists, and square brackets are reserved.

### 5.1 Collection delimiters

| Collection | Opens | Closes |
| --- | --- | --- |
| List | `(` | `)` |
| Map | `{` | `}` |
| Set | `#{` | `}` |

Three errors apply to all of them:

- End of input before the closer: `unclosed-delimiter`, at the position of the opener.
- A closer of another kind, as in `(1 2}`: `mismatched-delimiter`, at the position of the wrong closer.
- A closer with no matching opener, such as `)` at top level: `unexpected-delimiter`, at its position.

### 5.2 Maps and sets

A map contains an even number of datums, read as key, value, key, value. An odd number MUST produce `odd-map`. Two keys equal under `equal?` (section 2.4) MUST produce `duplicate-key`. Both errors are reported at the position of the `{`, because atoms have no position of their own (section 6). The message MAY include the index of the repeated pair.

A set opens with `#{`, with no atmosphere between `#` and `{`, and holds any number of datums, its elements. Two elements equal under `equal?` MUST produce `duplicate-element`, at the position of the `#`. `#{}` is the empty set. A set closes with `}`, so `#{1 2)` is a `mismatched-delimiter` at the `)`.

### 5.3 Reserved characters

`[` and `]` neither open nor close any collection in the core. Outside strings and comments, each one MUST produce `reserved-character` at its own position. This reserves the syntax for future collections, such as vectors.

### 5.4 Prefixes

| Written | Read as |
| --- | --- |
| `'x` | `(quote x)` |
| `` `x `` | `(quasiquote x)` |
| `,x` | `(unquote x)` |
| `,@x` | `(unquote-splicing x)` |

The expansion is purely syntactic. The reader does NOT check whether an `unquote` is inside a `quasiquote`; that is the job of the `quasiquote` expansion (section 8.3). A prefix requires a datum after it, with optional atmosphere in between. If end of input comes instead, the error is `unexpected-eof`. If a closer comes, as in `(a ')`, the error is `unexpected-delimiter`.

### 5.5 `#` dispatch

A `#` at the start of a datum can only begin one of these forms:

| Form | Meaning |
| --- | --- |
| `#true`, `#false` | Boolean |
| `#void` | Void |
| `#{` | Set (5.2) |

Any other continuation, including `#` alone, MUST produce `unknown-dispatch` at the position of the `#`. This reserves room for future extensions without breaking existing code.

### 5.6 Nesting limit

At most 1000 collections or prefixes may be open at the same time; a `#{` counts as one opener, at the position of the `#`. The 1001st opener MUST produce `nesting-too-deep` at its position. The limit protects recursive implementations against stack overflow and makes behavior identical across languages.

## 6. Source positions

Positions live in a **side table** that maps the identity of each compound node to the position of its opener. Datums remain ordinary values, with no position field.

### 6.1 Coordinates

A position is the pair (line, column), both starting at 1. The line advances after each U+000A, and the column resets to 1. The column counts code points, not bytes: in `(λ ]`, the `]` is at column 4. Tab and U+000D count as one column each. Errors use the same coordinates.

### 6.2 What is recorded

- Every non-empty List, including those generated by prefixes, with the position of the `(` or of the prefix character.
- Every Map, with the position of the `{`. The empty map MAY have no position, if the implementation does not allocate it as a distinct object.
- Every Set, with the position of the `#`. The empty set MAY have no position, as the empty map.

Example: in `(define x↵  {:a 1})`, the list is at 1:1 and the map at 2:3.

### 6.3 Known limitations

**Atoms have no position of their own.** Symbols and keywords are interned, so all occurrences of `x` are the same object. Numbers, booleans and `()` have no identity. The position of an atom is deduced from the collection that contains it and its index within it.

**Macro-generated code has no position.** Lists created during expansion are not in the table. The expander MAY copy the position of the original form to the expansion. For the positions of errors (8.5), code without a position of its own, generated by a macro or by a derived form, takes the position of the nearest enclosing form that has one, which is usually the macro call itself.

### 6.4 Validity of entries

An entry always gives the position where its node was read. It MUST NOT give a position for a node that was not read, or the position of another node.

## 7. Read errors

The reader stops at the first error, without trying to recover. Each error has a fixed code and a position, which are normative, and a free-form message.

### 7.1 Interface

The read operation takes the complete source text and returns one of two things: the sequence of top-level datums, with the position table, or the first error. The implementation MAY offer incremental reading, one datum at a time, as long as the results are identical.

### 7.2 Error structure

- **Code:** one of the identifiers in the catalog below.
- **Position:** line and column, as in section 6.1.
- **Message:** free text for humans. It is not compared in tests.

### 7.3 Detection order

The error reported is the first one detected in a left-to-right read. The `odd-map` and `duplicate-key` checks happen when the map is closed, after all of its contents have been read, and so does the `duplicate-element` check of a set. Therefore, in `{:a 1 :a "x}`, the error is `unterminated-string`. Inner errors come before outer ones: in `(a (b`, the error is `unclosed-delimiter` at the second `(`. Inside a string, an escape is checked as it is read (4.3), so an invalid escape comes before the end of an unterminated string.

### 7.4 Catalog

| Code | When it occurs | Reported position | Section |
| --- | --- | --- | --- |
| `invalid-utf8` | invalid UTF-8 byte sequence | first invalid byte | 3.2 |
| `unexpected-eof` | end of input right after a prefix | end of input | 5.4 |
| `unclosed-delimiter` | end of input before the closer | opener | 5.1 |
| `mismatched-delimiter` | closer of another kind | wrong closer | 5.1 |
| `unexpected-delimiter` | closer without opener, or after a prefix | the closer | 5.1 |
| `reserved-character` | `[` or `]` outside a string or comment | the character | 5.3 |
| `unterminated-string` | end of input before the closing `"` | opening quote | 4.3 |
| `invalid-escape` | unknown escape | backslash | 4.3 |
| `integer-overflow` | integer outside 64 bits | start of token | 4.1 |
| `float-overflow` | Float too large for binary64 | start of token | 4.2 |
| `invalid-number` | numeric token that is neither an integer nor a float | start of token | 4.2, 4.6 |
| `dotted-pair` | token `.` | the `.` | 4.6 |
| `empty-keyword` | `:` with no name | the `:` | 4.5 |
| `unknown-dispatch` | `#` with no valid continuation | the `#` | 5.5 |
| `odd-map` | map with an odd number of datums | the `{` | 5.2 |
| `duplicate-key` | repeated key in a map | the `{` | 5.2 |
| `duplicate-element` | repeated element in a set | the `#` | 5.2 |
| `nesting-too-deep` | the 1001st opener | that opener | 5.6 |

## 8. Evaluation

The evaluator knows five special forms. Everything else is function application or a derived form, rewritten into the special forms before evaluation.

### 8.1 How each datum is evaluated

| Datum | Result |
| --- | --- |
| Integer, Float, String, Boolean, Void, Keyword | the value itself |
| Symbol | the value bound to the name: first in the local environment, from the innermost `lambda` outward, then in the global one (8.2, lexical scope); unbound is an error |
| `()` | the empty list itself |
| Map | a new map with evaluated keys and values, left to right (8.4) |
| Set | a new set with evaluated elements, left to right (8.4) |
| Non-empty List | special form, macro or application, depending on the first element |

In an **application** `(f a b …)`, the operator is evaluated first and the arguments next, left to right. If the operator is not a function, or if the number of arguments does not fit, the result is an error.

### 8.2 Special forms

| Form | Syntax | Meaning |
| --- | --- | --- |
| `quote` | `(quote datum)` | returns the datum without evaluating it |
| `if` | `(if c a otherwise b)`, `(if c a)`, or several pairs `(if c1 a1 c2 a2 … otherwise b)` | evaluates `c`; if it is not `#false`, evaluates `a`; otherwise evaluates `b`, or returns `#void` if there is no `otherwise` |
| `lambda` | `(lambda (parameters …) body …)` | creates a closure; the value of a call is that of the last body expression |
| `define` | `(define name value)` | binds a global name and returns `#void`; top level only |
| `define-macro` | `(define-macro name (lambda (parameters …) body …))` | binds a name to a code transformer; top level only |

**`if`.** After `if` come one or more pairs of a test and a result, optionally followed by `otherwise` and exactly one expression. `otherwise` is a marker, not a variable: it may appear only once, right before the last expression. Every other shape is an expansion error: no pair, a test without a result, `otherwise` anywhere else, or none or more than one expression after it. So `(if c a b)` is an error, not a two-armed `if`.

With more than one pair, `if` is rewritten into nested `if`s, each pair becoming the condition and result of one of them:

```lisp
(if c1 a1 c2 a2 more …)           ; ≡ (if c1 a1 otherwise (if c2 a2 more …))
(if c1 a1 c2 a2 otherwise b)      ; ≡ (if c1 a1 otherwise (if c2 a2 otherwise b))
```

The tests are evaluated in order, and the first one that is not `#false` selects its result; the tests after it are not evaluated. When no test holds, the value is that of the expression after `otherwise`, or `#void` without one. A chain of cases is written one pair per line:

```lisp
(if (< n 0) "negative"
    (= n 0) "zero"
    otherwise "positive")
```

**`lambda`.** Parameters are distinct symbols. `&rest`, if present, comes before the last parameter, which receives the extra arguments as a list. The body has one or more expressions. Every function is anonymous: recursion uses a global name, defined with `define`.

**Scope is lexical.** A closure keeps the local environment in which its `lambda` was evaluated, and its body runs in that environment extended with the parameters; the environment of the caller is never consulted. A symbol resolves to the nearest binding in the text: the parameters of the innermost `lambda` that contains it, then those of each enclosing `lambda`, outward, and finally the global name, looked up at the moment of evaluation. Because `let` is rewritten into `lambda` (8.3), its names follow the same rule.

**`define`.** Global functions are written as `(define sum (lambda (x y) (+ x y)))`. Redefining a name replaces the global binding; values remain immutable. A global function calls itself through the global name, resolved at call time.

**`define-macro`.** The transformer receives the argument datums unevaluated and returns a datum, which is expanded again until no macro remains. Macros are non-hygienic; `generate-symbol` (section 9) avoids name capture. A macro MUST be defined before its first use, in text order. The transformer expression is evaluated when the `define-macro` is processed; if its value is not a Function, the result is `:wrong-type` and the name is not bound.

**Macro names have no value.** A global name holds either a value or a macro, and the last definition wins. A name bound to a macro is a macro only at the head of a form being expanded; evaluated as a variable, it behaves as an unbound name and produces `:unbound-symbol`. Since global lookup happens at evaluation time (8.4), this also applies to a function written before the macro was defined: in `(define f (lambda () m))` followed by `(define-macro m …)`, the call `(f)` produces `:unbound-symbol`.

**Reserved names.** A `(declare …)` form at the start of a body and a `(declare-type …)` form at top level MUST be ignored. They are reserved for a future typing extension, so annotated code runs unchanged on the core.

### 8.3 Derived forms

Every implementation MUST provide `let`, `sequence`, `and`, `or`, `quasiquote` and `match`. They are defined by rewriting into the special forms; the implementation MAY implement them differently, as long as the behavior is the same.

**`let`** has the form `(let ((name value) …) body …)`. Bindings are sequential: each value sees the previous names, and a repeated name shadows the previous one. The rewrite is:

```lisp
(let () body …)                   ; ≡ ((lambda () body …))
(let ((x v) more …) body …)       ; ≡ ((lambda (x) (let (more …) body …)) v)
```

**`sequence`** evaluates one or more expressions, in order, and returns the value of the last one. It is used to run several effects where only one expression fits, such as in an `if` branch.

```lisp
(sequence e …)                    ; ≡ ((lambda () e …))
```

**`and`** and **`or`** evaluate left to right and stop as soon as the result is decided. `and` returns `#false` at the first false argument, or the value of the last one if all are true. `or` returns the first true value, or `#false` if there is none. With no arguments, `(and)` is `#true` and `(or)` is `#false`.

```lisp
(and e)                           ; ≡ e
(and e more …)                    ; ≡ (if e (and more …) otherwise #false)
(or e)                            ; ≡ e
(or e more …)                     ; ≡ (let ((t e)) (if t t otherwise (or more …)))
```

In the `or` rewrite, `t` is a symbol created by `generate-symbol`, so that it does not capture names from user code.

**`quasiquote`** builds a datum like `quote`, but evaluates the marked parts. `(unquote e)` is replaced by the value of `e`. `(unquote-splicing e)` must evaluate to a list, whose elements are inserted in its place; it is only valid inside lists. Inside a quasiquoted map or set, keys, values and elements may be unquoted like the elements of a list; `unquote-splicing` is only valid inside lists. A `quasiquote` inside another raises the level, and only the markers of the outermost level are evaluated, as in R7RS. `unquote` outside `quasiquote` is an expansion error.

```lisp
(let ((x 1) (xs (list 2 3)))
  `(a ,x ,@xs))                   ; → (a 1 2 3)
```

**`match`** chooses a result by the shape of a value. It has the form `(match subject pattern result …)`, with one or more pairs of a pattern and a result, optionally followed by `otherwise` and one expression, like `if` (8.2). `subject` is evaluated once. The patterns are then tried in order, and the first one that matches the value selects its result, which is evaluated with the names the pattern binds; the patterns after it are not tried. When none matches, the value is that of the expression after `otherwise`; without `otherwise`, the result is the error `:no-match`, whose `:values` are `(value)`.

| Pattern | Matches | Binds |
| --- | --- | --- |
| `_` | any value | nothing |
| a symbol `x` | any value | `x` to the value |
| an Integer, Float, String, Keyword, `#true`, `#false`, `#void` or `()` | a value `equal?` to it | nothing |
| `(quote datum)`, written `'datum` | a value `equal?` to the datum | nothing |
| a list `(p …)` | a List with exactly one element per pattern, each element matching its pattern | the names of its patterns |
| a list `(p … &rest r)` | a List with at least one element per pattern before `&rest`, matching them, whose remaining elements, as a List, match `r` | the names of its patterns |
| a map `{key p …}` | a Map that has every key of the pattern, each with a value that matches its pattern; other keys are allowed | the names of its patterns |

Patterns nest, so `{:position {:line line}}` binds `line` to the line of a position inside a map. A quote is the only way to match a symbol, a list or a set as a whole: `'end` matches the symbol `end`, and `'(1 2)` the list `(1 2)`. The keys of a map pattern are taken literally, as in a quoted map; they are not patterns.

```lisp
(match stack
  (right left &rest below) (prepend (+ left right) below)
  otherwise (error "not enough values" stack))
```

The names bound by a pattern are visible only in its result, as with `let`, and shadow outer names there. Each result is in tail position when the `match` is (8.4). The tests a pattern makes use the behavior of the primitives `equal?`, `list?`, `empty?`, `first`, `rest`, `map?`, `contains?` and `get`, not the current values of those global names: redefining `first` does not change `match`.

These are expansion errors: no pattern and result pair; a pattern without a result; `otherwise` anywhere but right before the last expression, or followed by none or more than one expression; the same name bound twice in one pattern; `otherwise` or `&rest` used as a pattern; `&rest` not followed by exactly one pattern; a symbol as a key of a map pattern; and a set pattern `#{…}`, since sets have no order to match by. A quoted set, `'#{1 2}`, matches an equal set.

### 8.4 General rules

**Falsity.** Only `#false` is false. `()`, `0`, `""` and `#void` are true; the empty list is tested with `empty?`.

**Map literals** evaluate keys and values, left to right: `{:sum (+ 1 2)}` is a map with the value `3` at key `:sum`. For the literal datum, use `quote`. Keys that only repeat after evaluation, as in `{a 1 b 2}` with `a` and `b` equal, MUST produce an error.

**Set literals** evaluate their elements, left to right: `#{(+ 1 2) x}` is a set with `3` and the value of `x`. For the literal datum, use `quote`. Elements that only repeat after evaluation, as in `#{a b}` with `a` and `b` equal, MUST produce `:duplicate-element`.

**Tail calls are guaranteed.** A call in tail position MUST NOT use more space than the call it replaces, so that tail recursion runs in constant memory. It is the only looping mechanism. The tail positions are the last expression of a `lambda` body and every result of an `if` that is itself in tail position, including the expression after `otherwise`. Through rewriting, the following inherit the position of the form that contains them: the body of a `let`, the last expression of a `sequence`, the last argument of an `and` or an `or`, and each result of a `match`.

```lisp
(define numbers-from
  (lambda (n acc)
    (if (= n 0) acc
        otherwise (numbers-from (- n 1) (prepend n acc)))))

(define numbers-up-to
  (lambda (limit) (numbers-from limit ())))

(numbers-up-to 3)   ; → (1 2 3), in constant memory for any limit
```

**Call depth.** Calls that are not in tail position nest: each one waits for the value of the next. An implementation MUST support at least 10,000 nested non-tail calls. When a chain of non-tail calls goes beyond the depth the implementation supports, evaluation MUST be interrupted with `:stack-overflow`, an ordinary error that `catch` can catch (8.5), never by ending the process. The exact depth at which this happens is up to the implementation; tail calls never count toward it.

**Errors.** `error`, `raise`, integer and Float overflow, unbound symbols and invalid applications interrupt evaluation. An error can be caught with `catch` (8.5). If it is not caught, the program terminates, or the REPL shows the message and reads the next input.

### 8.5 Side effects and errors

**Side effects are implicit.** Any function can perform input and output by calling `display!`, `write!` and `read-line!`, from the library `io` (8.9), once its file imports it. Since evaluation order is defined (8.1), effects happen in a predictable order. There is no mechanism to intercept or redirect these effects; for code that needs that, the recommended style is callbacks, described below.

**The `!` convention.** The name of a function that interacts with the world outside the program ends in `!`. The rule applies to the input and output primitives and, by convention, to every function that directly calls a function ending in `!`. A function that only calls callbacks received as arguments does not need the mark, because whoever passes the callback decides the effect. The core does not check the convention; a future typing extension MAY check it. Errors (`error`, `raise`) and `generate-symbol` are not marked, because they do not interact with the outside world.

```lisp
(define greet!
  (lambda (name)
    (display! (concatenate "Hello, " name "\n"))))
```

**Error descriptor.** Every error carries a descriptor: a Map with `:kind` (Keyword), `:message` (String) and `:values` (List), and, once the error has left an expression with a known position, `:position` (below). `(error message value …)` creates a descriptor with `:kind :error`. The `:kind`s of core errors are `:unbound-symbol`, `:not-a-function`, `:wrong-arguments`, `:wrong-type`, `:index-out-of-range`, `:missing-key`, `:integer-overflow`, `:float-overflow`, `:division-by-zero`, `:domain-error`, `:invalid-code-point` (9), `:duplicate-key`, `:duplicate-element`, `:no-match` (8.3), `:expansion-error`, `:stack-overflow` (8.4), `:io-error` (section 9, `read-line!`), `:include-error` (8.8) and `:import-error` (8.9).

**Positions in errors.** When an error leaves the expansion or the evaluation of an expression that has a source position, and its descriptor has no key `:position` yet, the descriptor gains `:position`: a Map with `:line` and `:column`, Integers with the coordinates of 6.1, and, when the expression was read from a file, `:file`, a String that names that file. The text of `:file` is implementation-defined; it names the program, the included file (8.8) or the source of a library (8.9).

The position of an expression is the position of the innermost List, Map or Set of the position table (6.2) that contains it in the source. An atom has no position of its own (6.3), so it takes the position of the list around it; code generated by a macro or a derived form takes the position of the nearest enclosing form that has one (6.3). Each top-level form is itself an expression, so an error raised while processing `define`, `define-macro`, `include` or `import` takes the position of that form. As a result:

- The position is the innermost one. In `(+ 1 (quotient 1 0))`, the `:division-by-zero` is at `(quotient 1 0)`; in `(define f (lambda () (first ())))`, a call to `f` from anywhere fails at `(first ())`, the expression where the error happened, not at the call.
- `(error message value …)` takes the position of its call.
- `raise` does not change a descriptor that already has `:position`, so re-raising a caught descriptor keeps the original position. A descriptor without `:position`, such as a Map built by the program, takes the position of the `raise` call.
- An error that never leaves an expression with a position has no `:position`; for example, a top-level form that is a lone unbound symbol.
- In the REPL (8.7), positions count from the start of each input, and `:position` has no `:file`.

Positions do not change `:kind`, `:message` or `:values`; code that tests an error by its `:kind` is unaffected.

**Catching.** `(catch body handler)` calls `body`, a function with no arguments. If `body` finishes normally, `catch` returns its value. If an error occurs during `body`, evaluation of `body` is abandoned, and `handler` is called with the descriptor; the result of `handler` becomes the result of `catch`. Catching is exit-only: there is no way to return to the point of the error. An error inside `handler` goes to the next enclosing `catch`.

**Re-raising.** `(raise descriptor)` interrupts evaluation with an existing descriptor, keeping its `:position`, if it has one. An argument that is not a Map with a Keyword `:kind` produces `:wrong-type`.

```lisp
(define catch-kind
  (lambda (kind default thunk)
    (catch thunk
      (lambda (problem)
        (if (equal? (get problem :kind) kind) default
            otherwise (raise problem))))))

(catch-kind :division-by-zero 0 (lambda () (quotient 10 0)))   ; → 0
```

**Callbacks.** When code needs to be tested without a terminal, or stopped halfway, the recommended style is to pass functions as arguments. It is not a language mechanism, just the use of functions as values, in two patterns.

In **injection**, the function receives the functions that perform the effect, instead of calling them directly. A map holding all of them avoids passing many parameters around:

```lisp
(define greet-user
  (lambda (io)
    ((get io :tell) "Name? ")
    ((get io :tell) (concatenate "Hello, " ((get io :ask)) "!\n"))))

(import io)
(greet-user {:ask read-line! :tell display!})                       ; real program: reads stdin
(greet-user {:ask (lambda () "Ana") :tell (lambda (text) text)})   ; test: → "Hello, Ana!\n"
```

In **continuation**, the function receives the rest of the work as an argument, and the caller decides whether to continue. Since tail calls are guaranteed (8.4), long callback chains run in constant memory:

```lisp
(define each-leaf
  (lambda (tree on-leaf after)
    (if (list? tree) (each-child tree on-leaf after)
        otherwise (on-leaf tree after))))

(define each-child
  (lambda (children on-leaf after)
    (if (empty? children) (after)
        otherwise (each-leaf (first children) on-leaf
                    (lambda () (each-child (rest children) on-leaf after))))))

;; Stops at the first leaf greater than 2, without visiting the rest
(each-leaf '(1 (2 (3 4)) 5)
  (lambda (leaf next) (if (> leaf 2) leaf otherwise (next)))
  (lambda () #void))                                               ; → 3
```

### 8.6 Large data

Lists of at least 1,000,000 elements MUST be built, traversed and discarded without error.

Maps are persistent: `associate` and `dissociate` return a new map that shares most of its structure with the original, which stays unchanged. Neither of them, nor `get` and `contains?`, may copy or traverse the whole map: their cost MUST grow at most logarithmically with the number of keys. As a result, a map of at least 1,000,000 keys MUST be built with `associate`, one key at a time, queried, reduced with `dissociate` and discarded without error. `keys`, `equal?` and printing traverse the whole map, so their cost grows with its size. Sets are persistent in the same way: `adjoin`, `disjoin` and `contains?` follow the same rule, and a set of at least 1,000,000 elements MUST be built with `adjoin`, one element at a time. A hash array mapped trie meets these requirements.

Memory management is otherwise left to the implementation and has no observable effect.

### 8.7 Programs and REPL

**Loading a program.** The file is read in full by the reader before any evaluation; a read error prevents any form from being evaluated. Nothing is loaded before the program: a library becomes visible only through `import` (8.9). Then, the top-level forms are processed **one at a time, in order**: each one is expanded and evaluated before the next. Therefore, a macro defined earlier in the file already applies to the following forms. A file included with `include` is read only when that form's turn comes (8.8).

**Top level.** `define`, `define-macro`, `include` and `import` are only valid at top level; elsewhere, they are an expansion error. A `sequence` at top level is treated as if its forms were loose in the file, in the same order, and each of them is also at top level. This applies recursively and lets a macro generate several definitions at once. Outside the top level, `sequence` follows the rewrite in section 8.3.

**Termination.**

| Situation | Output | Exit code |
| --- | --- | --- |
| all forms evaluated | none beyond the program's own | 0 |
| uncaught error | message on standard error, with the position of the error when the descriptor has `:position` | 1 |
| read error | error code, position and message on standard error | 1 |

**REPL.** The REPL repeats four steps:

1. Read an input: a complete datum, which may span several lines. If the line ends with a collection or string still open, the REPL reads one more line instead of reporting an error.
2. Expand and evaluate the input as a top-level form.
3. Print the result with `write!`, followed by a newline. If the result is `#void`, print nothing.
4. On a read error or an uncaught error, show the message, discard the input and go back to step 1.

The REPL starts as if the session began with `(import io)` and then an `import` of each library of the standard library, in the order its specification lists them, in the empty context, so every library the implementation provides is at hand; a library the implementation does not provide is skipped. Step 3 prints with the `write!` rules (section 10) whether or not `write!` is bound.

Redefining a name with `define` replaces the previous binding (8.2). The REPL ends at the end of standard input.

### 8.8 Including files

**Form.** `(include "path")` and `(include "path" prefix)` load the forms of another file. The arguments are not evaluated: the path MUST be a String literal and the prefix, when present, a Symbol. Any other shape, including a third argument, is an expansion error, and so is an `include` outside the top level (8.7). The value of an `include` is `#void`.

**Loading.** An `include` is processed in its turn, like any other top-level form: the forms before it have already been evaluated. The file is read in full by the reader, and then its top-level forms are processed one at a time, in order, as if they stood at top level in place of the `include`, with the differences of prefix below. A macro defined before the `include`, in any file, applies to the included forms, and a macro defined in the included file applies to the forms after the `include`.

**Paths.** A relative path is resolved against the directory of the file that contains the `include`; in the REPL, against the current working directory. An absolute path is used as it is. The separator is `/`. Two paths name the same file when they are equal after being made absolute and having their `.` and `..` segments removed; symbolic links are not resolved.

**Context.** Every top-level form is processed in a context, a sequence of prefixes. The program and the REPL run in the empty context. A library runs in the context of the `import` that loads it (8.9). `(include "path" p)` processes the file in the context of the `include` followed by `p`, and `(include "path")` in the same context as the `include`. The **full prefix** of a context joins its prefixes with `/`: the context `geo`, `u` has the full prefix `geo/u`. Two contexts with the same full prefix are the same context.

**Definitions in a context.** In the empty context, `define` and `define-macro` bind the global name, as in 8.2. In a context that ends with the prefix `p`, `(define x value)` evaluates `value`, records `x` as a name of that context, and then defines `p/x` in the context before `p`, by the same rule; `define-macro` does the same. The global that ends up bound is the full prefix, `/` and the name: in a file included with `geo` that includes another with `u`, `(define clean …)` in the second file binds `geo/u/clean`, records `clean` in the context `geo/u` and records `u/clean` in the context `geo`.

**Resolving names in a context.** A symbol `x` in code from a non-empty context that is not bound locally (8.2) names the global `F/x`, where `F` is the full prefix of the context, if `x` is at that moment a name of the context; otherwise it names the global `x`. The test happens when the symbol is evaluated, like every global lookup, so a function may call another one defined further down in the same file. The head of a form is a macro under the same rule, tested when the form is expanded. Names the file does not define resolve as usual: the primitives, the libraries imported in the empty context and whatever the including program defined are all visible. The rule applies only to symbols evaluated as variables and to macro heads: quoted data, symbols built with `string-to-symbol` and the names of special forms, derived forms, `include` and `import` are never prefixed.

```lisp
;; geo.nvl
(define area (lambda (w h) (* w h)))
(define square-area (lambda (s) (area s s)))   ; area resolves to geo/area

;; main.nvl
(define area :mine)
(include "geo.nvl" geo)
(list area (geo/square-area 3))                ; → (:mine 9)
```

**Macros across files.** The datum a macro returns is expanded in the context of the form that uses it, not in the context where the macro was defined. Macros are not hygienic (8.2), so the symbols of a template resolve in the caller: a macro of `geo.nvl`, included with `geo`, whose template mentions `area` produces a reference to `area`, not to `geo/area`. A macro meant for use outside its file MUST write those names with the full prefix, as `geo/area`, or not refer to them. The transformer itself is an ordinary function of its file, and the names in its body resolve in that file's context.

**Repetition.** A file is loaded at most once per context: an `include` of a file that was already included in the same resulting context is ignored. A file counts as included once it has been opened and read without a read error, before its first form runs. So an `include` that failed with `:cannot-open` or a read error leaves nothing recorded, and in the REPL the same `include` can be tried again once the file is fixed; a file whose forms raised an error does count as included. The same file in another context is loaded again, with its own names, and the effects of its top-level forms, such as `display!`, happen again. An `include` of a file that is still being loaded, either the program itself or a file whose `include` has not finished, is an error in any context; this rules out cycles.

**Errors.** These situations end with `:include-error`. Its `:values` start with the path as written in the `include`:

| Situation | `:values` |
| --- | --- |
| the file cannot be opened | `(path :cannot-open)` |
| the file has a read error (7) | `(path code line column)`, with the code as a Keyword, such as `:odd-map`, and the position in that file |
| the file is still being loaded | `(path :cycle)` |

An `include` is never inside a `catch`, because it is only valid at top level. So an `:include-error` terminates the program with exit code 1 (8.7). In the REPL, the message is shown, and the definitions already made by the file before the error remain. An error raised while evaluating an included form keeps its own `:kind`.

### 8.9 Libraries and `import`

**Form.** `(import name)` and `(import name prefix)` make a library visible. The arguments are not evaluated: `name` and `prefix` MUST be Symbols. Any other shape, including a third argument, is an expansion error, and so is an `import` outside the top level (8.7). The value of an `import` is `#void`.

**Libraries.** A library is a named set of definitions provided by the implementation. Its name is a Symbol, not a path: it does not depend on the directory of the importing file. A library is either **native**, with functions implemented by the host, or **written in the language**, as a sequence of top-level forms. A program cannot tell the two kinds apart. The core defines one native library, `io`; the standard library defines libraries written in the language, such as `list` and `text`, in its own specification. An implementation MAY provide more.

**Loading.** `(import name prefix)` behaves like an `include` (8.8) of the library with the same prefix: it is processed in its turn, its forms run in the context of the `import` followed by `prefix`, and its definitions are recorded and bound by the rules of 8.8. So `(import list)` in the empty context binds `map` as a global, and `(import list l)` binds `l/map` instead, leaving `map` unbound. The functions of a native library are bound as if each one were defined with `define` in that context, in the order the library lists them. A library is loaded at most once per context; a repeated `import` in the same resulting context is ignored. As with files, a library counts as loaded only once its definitions were found and read, so an `import` that failed for that reason can be tried again in the REPL.

**Dependencies.** A library written in the language MAY import other libraries. Its `import` forms run in the library's own context, so the definitions of the dependency become names of that same context: importing `text` in the empty context also binds the globals of `list`, which `text` imports. This is deliberate and mirrors C headers that include other headers.

**Errors.** An `import` that fails ends with `:import-error`, whose `:values` are `(name reason …)`: the name as a Symbol, then a Keyword. An `import` of a name the implementation does not provide has the reason `:unknown-library`. When the implementation provides the library but cannot load it, for example because the file holding its definitions is missing, the reason is any other Keyword the implementation chooses, possibly followed by more values. Like `include`, an `import` is never inside a `catch`, so the error terminates the program with exit code 1 (8.7).

**The library `io`.** It provides `write!`, `display!` and `read-line!`, in that order, with the behavior given in section 9. Without `(import io)`, these names are unbound like any other.

```lisp
(import io)
(import text t)
(display! (t/join ", " (list "a" "b")))   ; prints a, b
```

## 9. Primitives

Primitives are native functions bound in the global environment, except for the input and output functions, which belong to the library `io` (8.9) and are bound only where it is imported. An argument of the wrong type, an index out of range or an invalid number of arguments is an error (8.4).

| Group | Functions | Rules |
| --- | --- | --- |
| Arithmetic | `+`, `*`, `-` | all arguments Integers or all Floats (2.5); a mix, or an argument that is not a number, is `:wrong-type`. `+` and `*` accept zero or more arguments: `(+)` is `0`, `(*)` is `1`. `-` accepts one or more: `(- x)` negates, and `(- 0.0)` is `-0.0`. Integer overflow is `:integer-overflow`. Each Float operation is rounded to the nearest binary64, ties to even, as IEEE 754 requires; a result too large for binary64 is `:float-overflow` |
| Float division | `/` | Floats only, one or more arguments: `(/ a b c)` is `a` divided by `b`, then by `c`, and `(/ x)` is `1.0` divided by `x`. A divisor equal to `0.0` or `-0.0` is `:division-by-zero`; Integers are `:wrong-type`, since they are divided with `quotient` |
| Integer division | `quotient`, `remainder` | Integers only, truncating toward zero; a zero divisor is an error |
| Comparison | `<`, `>`, `<=`, `>=`, `=` | numbers, all Integers or all Floats, two or more arguments, chained: `(< a b c)`. `-0.0` and `0.0` are equal |
| Floats | `integer-to-float`, `truncate`, `sqrt`, `float-to-fixed` | `(integer-to-float n)` takes an Integer and returns the nearest Float, with ties to even, so it is exact for every Integer from −2^53 to 2^53. `(truncate x)` takes a Float and returns the Integer obtained by discarding its fractional part, toward zero: `(truncate -2.7)` is `-2`; a result outside the range of Integer is `:integer-overflow`. `(sqrt x)` takes a Float and returns its square root, correctly rounded as IEEE 754 requires; a Float less than zero is `:domain-error`, and `(sqrt -0.0)` is `-0.0`. `(float-to-fixed x decimals)` takes a Float and an Integer from 0 to 20 and returns a String with `x` written in decimal, without an exponent, with exactly `decimals` digits after the point, and no point when `decimals` is 0: `(float-to-fixed 20.1 2)` is `"20.10"`, and `(float-to-fixed 2.6 0)` is `"3"`. The digits are those of the exact binary value of `x`, rounded to `decimals` places, with ties away from zero, as `round` does in the standard library: `(float-to-fixed 0.125 2)` is `"0.13"`, but `(float-to-fixed 1.005 2)` is `"1.00"`, because the Float nearest to 1.005 is slightly below it. The result starts with `-` when `x` is negative and some written digit is not zero, so `(float-to-fixed -0.001 2)` is `"0.00"`. A `decimals` that is not an Integer, or an `x` that is not a Float, is `:wrong-type`; an Integer outside 0 to 20 is `:domain-error`, with `:values` `(decimals)` |
| Equality | `equal?` | section 2.4 |
| Types | `boolean?`, `void?`, `integer?`, `float?`, `string?`, `symbol?`, `keyword?`, `list?`, `map?`, `set?`, `function?` | one predicate per type; `function?` holds for closures and primitives |
| Lists | `list`, `prepend`, `first`, `rest` | `(prepend x xs)` puts `x` in front of `xs`; `first` and `rest` of `()` are errors |
| Maps and sizes | `count`, `empty?`, `get`, `contains?`, `associate`, `dissociate`, `keys` | `count` and `empty?` work on List, Map, Set and String. `(get m k default)` returns `default` if `k` does not exist; without a default, it is an error. `contains?` tests whether the map has the key, or whether the set has the element. `keys` returns a new list with each key of the map exactly once, in the iteration order of 2.3, and `(keys {})` is `()`; together with `get`, it is how a program traverses a map |
| Strings | `concatenate`, `string-at`, `substring`, `to-string` | indices count code points; `(substring s start end)` excludes `end`; `to-string` returns the text `display!` would print; `concatenate` accepts zero or more strings, and `(concatenate)` returns `""` |
| Number parsing | `string-to-number` | reads the whole text under the rules of section 4 and returns an Integer or a Float; if the text is neither an integer nor a float, returns `#void`; if it is out of range, it is an error (`:integer-overflow` or `:float-overflow`) |
| Code points | `string-to-code-points`, `code-points-to-string` | `(string-to-code-points s)` returns a new List with the code point of each character of `s`, in order, as Integers: `(string-to-code-points "aé")` is `(97 233)`, and `(string-to-code-points "")` is `()`. `(code-points-to-string xs)` returns the String whose code points are the elements of `xs`, in order; `(code-points-to-string ())` is `""`. An element that is not an Integer produces `:wrong-type`; an Integer that is not a Unicode scalar value (3.2), that is, negative, above U+10FFFF or a surrogate from U+D800 to U+DFFF, produces `:invalid-code-point` with `:values` `(n)`. The elements are checked in order, and the first invalid one is reported |
| Symbols | `string-to-symbol`, `symbol-to-string`, `generate-symbol` | `generate-symbol` creates a new symbol, different from all others and not interned, so macros can avoid capture |
| Sets | `adjoin`, `disjoin`, `elements` | `(adjoin s x)` returns the set with `x`, and `(disjoin s x)` the set without `x`; each returns the same set when there is nothing to change. `elements` returns a new list with each element of the set exactly once, in the iteration order of 2.3, and `(elements #{})` is `()`. `get`, `keys`, `associate` and `dissociate` take only maps |
| Functions | `apply` | `(apply f a … list)` calls `f` with `a …` followed by the elements of `list` |
| Logic | `not` | returns `#true` if the argument is `#false`, and `#false` in any other case; `and` and `or` are derived forms (8.3) |
| Input and output (library `io`) | `write!`, `display!`, `read-line!` | only after `(import io)` (8.9). `write!` and `display!` write a value to standard output, in the form of section 10, and return `#void`. `read-line!` reads a line from standard input and returns it without the line terminator, or `#void` at end of input. The only line terminator is U+000A, as in 6.1: a U+000D before it stays in the returned string, and a U+000D alone does not end a line. A last line without U+000A is returned as it is. A line that is not valid UTF-8 (3.2) produces `:io-error` with `:values` `(:invalid-utf8)`; the line is consumed, so the next `read-line!` reads the one after it |
| Errors | `error`, `raise`, `catch` | `(error message value …)` interrupts with `{:kind :error :message message :values (value …)}`; `(raise descriptor)` interrupts with an existing descriptor; `(catch body handler)` catches errors (8.5) |

## 10. Printing

**`write!`** prints in a form that reads back: reading the printed text MUST produce a value `equal?` to the original, with the three exceptions below. The REPL uses `write!`. **`display!`** prints for humans: a String passed directly to it comes out without quotes and without escapes, and everything else comes out as with `write!`. That includes strings inside a List, Map or Set, which keep their quotes and escapes, so the printed collection stays unambiguous: `(display! (list "a b" "c"))` prints `("a b" "c")`, which cannot be confused with the three-element list printed for `(list "a" "b" "c")`. To print the text of several strings, join them first.

| Type | Form printed by `write!` |
| --- | --- |
| Boolean, Void | `#true`, `#false`, `#void` |
| Integer | decimal, with `-` if negative |
| Float | the shortest decimal that reads back as the same Float, laid out as below |
| String | in double quotes, with `\n`, `\t`, `\r`, `\"` and `\\` escaped; every other character from U+0000 to U+001F, and U+007F, comes out as `\u{H…}` with uppercase hexadecimal digits and no leading zeros, such as `\u{0}` and `\u{1B}`; all other characters come out as they are |
| Symbol, Keyword | the name, preceded by `:` for keywords |
| List, Map, Set | delimiters of section 5.1, elements separated by one space; Map and Set order unspecified |
| Function | `#<function>` |

**Floats.** A Float `x` other than zero prints from the shortest sequence of decimal digits d₁…dₙ, with d₁ not zero, and the exponent e such that d₁.d₂…dₙ × 10^e reads back (4.2) as `x`. When several sequences of that length read back as `x`, the one closest to `x` is used, and between two equally close, the one whose last digit is even. Then:

- When −6 ≤ e < 21, the number is written without an exponent: the digits, with the point after the digit of 10⁰, padded with zeros where needed. At least one digit is written on each side of the point: `3.0`, `0.1`, `1234.5`, `100000000000000000000.0`, `0.000001`.
- Otherwise it is written as d₁, a point, the remaining digits or `0` when there are none, `e` and the exponent in decimal, with `-` if negative: `1.0e21`, `1.5e-7`, `5.0e-324`.
- A negative Float starts with `-`. Zero prints as `0.0`, and negative zero as `-0.0`.

Every printed Float has a point with digits on both sides, so it reads back as a Float and never as an Integer. `to-string` and `display!` use the same text.

The exceptions to reading back are:

- **Functions.** The form `#<function>` is deliberately unreadable: the reader rejects it with `unknown-dispatch`, so that it is never read as some other value.
- **Symbols from `generate-symbol`.** They are not interned, so the printed name, when read, produces a different symbol.
- **Symbols from `string-to-symbol` with names the reader does not read as a symbol**, such as `"a b"` or `"42"`. They are printed by name, with no guarantee of reading back.

## 11. Names and conventions

Every abbreviation becomes a full word; what is already a word, such as `if`, `let`, `quote` and `lambda`, stays as it is. To the reader, all these names are ordinary symbols. Only the evaluator gives them meaning.

- Predicates end in `?`: `integer?`, `empty?`.
- Functions that interact with the world outside the program end in `!`: `display!`, `read-line!` (8.5).
- Definition forms start with `define`: `define`, `define-macro`.
- Conversions use `-to-`: `string-to-symbol`, `symbol-to-string`.
- Variadic parameters use the `&rest` marker: `(lambda (x y &rest others) …)`.
- `otherwise` is only special inside an `if`; elsewhere, it is an ordinary symbol.

Correspondences with names from other Lisps, for those coming from them:

| Name | Replaces |
| --- | --- |
| `define` | `def`, `defun` |
| `lambda` | `fn` |
| `define-macro` | `defmacro` |
| `sequence` | `begin`, `progn`, `do` |
| `first`, `rest` | `car`, `cdr` |
| `prepend` | `cons` |
| `associate`, `dissociate` | `assoc`, `dissoc` |
| `concatenate` | `str`, `string-append` |
| `string-at` | `string-ref` |
| `generate-symbol` | `gensym` |
| `display!`, `write!`, `read-line!` | `display`, `write`, `read-line` |

## 12. Conformance

An implementation is conformant when it passes all the cases below. Results are compared by `equal?` (section 2.4) between datums, not by printed text, so no print format is required in these tests. The cases MUST also live in a shared data file, such as JSON, used by the tests of every implementation. In the tables, `↵` represents a line break. The tables below show the reader cases. The conformance file contains these plus the evaluation and primitive cases (12.4).

### 12.1 Valid reads

"≡ B" means the input produces the same sequence of datums as the text B.

| Input | Expected result |
| --- | --- |
| `007` | ≡ `7` |
| `9223372036854775807` | maximum Integer |
| `-9223372036854775808` | minimum Integer |
| `42 -7` | Integer, Integer |
| `3.14 -0.5 6.02e23` | Float, Float, Float |
| `2.50` | ≡ `2.5` |
| `1.0e-400` | ≡ `0.0` |
| `-x`, `-`, `...`, `&rest`, `λ` | Symbol |
| `:1`, `:a:b` | Keyword |
| `#true #false #void` | Boolean, Boolean, Void |
| `' x` | ≡ `(quote x)` |
| `` `(a ,b ,@c) `` | ≡ `(quasiquote (a (unquote b) (unquote-splicing c)))` |
| `a,b` | ≡ `a (unquote b)` (two datums) |
| `(a ; comment↵ b)` | ≡ `(a b)` |
| `(1 (2) ())` | List |
| `{:b 2 :a 1}` | ≡ `{:a 1 :b 2}` |
| `#{2 1 #{}}` | ≡ `#{#{} 1 2}` |
| `"a↵b"` | ≡ `"a\nb"` |
| `"\r\u{41}\u{000042}\u{1f600}\u{1F600}\u{0}"` | ≡ the string U+000D, `A`, `B`, two `😀`, U+0000 |
| `"😀 é"` | String |
| U+FEFF followed by `(a)` | Symbol, List |
| empty, or `   ; just a comment ` | zero datums |

### 12.2 Errors

| Input | Code | Position |
| --- | --- | --- |
| `(1 2` | `unclosed-delimiter` | 1:1 |
| `(a (b` | `unclosed-delimiter` | 1:4 |
| `(a↵  (b` | `unclosed-delimiter` | 2:3 |
| `)` | `unexpected-delimiter` | 1:1 |
| `(a ')` | `unexpected-delimiter` | 1:5 |
| `(1 2}` | `mismatched-delimiter` | 1:5 |
| `(λ ]` | `reserved-character` | 1:4 |
| `[1 2]` | `reserved-character` | 1:1 |
| `'` | `unexpected-eof` | 1:2 |
| `#;` | `unknown-dispatch` | 1:1 |
| `#\|` | `unknown-dispatch` | 1:1 |
| `#(1)` | `unknown-dispatch` | 1:1 |
| `# {1}` | `unknown-dispatch` | 1:1 |
| `"abc` | `unterminated-string` | 1:1 |
| `{:a 1 :a "x}` | `unterminated-string` | 1:10 |
| `"\q"` | `invalid-escape` | 1:2 |
| `"\u{D800}"` | `invalid-escape` | 1:2 |
| `"\u{110000}"` | `invalid-escape` | 1:2 |
| `"\u{}"` | `invalid-escape` | 1:2 |
| `"\u{1234567}"` | `invalid-escape` | 1:2 |
| `"\u41"` | `invalid-escape` | 1:2 |
| `"a \u{4G}"` | `invalid-escape` | 1:4 |
| `"\u{41` | `unterminated-string` | 1:1 |
| `"\u{zz` | `invalid-escape` | 1:2 |
| `"\u{1234567` | `invalid-escape` | 1:2 |
| `"\u{D800` | `unterminated-string` | 1:1 |
| `9223372036854775808` | `integer-overflow` | 1:1 |
| `-9223372036854775809` | `integer-overflow` | 1:1 |
| `3.14e999` | `float-overflow` | 1:1 |
| `-1.0e309` | `float-overflow` | 1:1 |
| `1.e3` | `invalid-number` | 1:1 |
| `(x 1e3)` | `invalid-number` | 1:4 |
| `(x 1+)` | `invalid-number` | 1:4 |
| `+5` | `invalid-number` | 1:1 |
| `(a . b)` | `dotted-pair` | 1:4 |
| `:` | `empty-keyword` | 1:1 |
| `#t` | `unknown-dispatch` | 1:1 |
| `#<function>` | `unknown-dispatch` | 1:1 |
| `{:a 1 :b}` | `odd-map` | 1:1 |
| `{:a 1 :a 2}` | `duplicate-key` | 1:1 |
| `#{1 (1) 1}` | `duplicate-element` | 1:1 |
| `#{1 2)` | `mismatched-delimiter` | 1:6 |
| `(#{1` | `unclosed-delimiter` | 1:2 |
| 1001 times `(` | `nesting-too-deep` | 1:1001 |
| the byte FF | `invalid-utf8` | 1:1 |
| `a` followed by a lone C3 byte | `invalid-utf8` | 1:2 |

### 12.3 Additional checks

- **Interning:** in `(foo foo :foo)`, the two `foo` are the same object, and `:foo` is different from `foo`.
- **Positions:** in `(define x↵  {:a 1})`, the list is at 1:1 and the map at 2:3. In `(a 'b)`, the list `(quote b)` is at 1:4. In `(x↵ #{1})`, the set is at 2:2.
- **Nesting limit:** 1000 times `(` followed by 1000 times `)` read without error.

### 12.4 Conformance file

The file `conformance.json` has 499 cases, each with an `id`, the section it tests and a kind:

| Kind | Cases | What it checks |
| --- | --- | --- |
| `read` | 28 | `input` produces the same sequence of datums as `equivalent`, or datums with the types in `types` |
| `read-error` | 55 | reading `input`, or the bytes of `input_hex`, fails with the code `error` at `position` |
| `read-positions` | 3 | the node indicated by each `path` is in the position table with the expected `position` |
| `eval` | 413 | `program` runs as a file, with `input` (or the bytes of `input_hex`) on standard input; it imports at most the library `io`; the output is exactly `output`, and the program ends with the value `value` or with an uncaught error of the `:kind` given in `error`; with `error_position`, the descriptor of that error has a `:position` whose `:line` and `:column` are the given pair, or, when `error_position` is `null`, no `:position` |

An `eval` case that tests `include` (8.8) also has `files`, an object that maps each relative path, with `/` as separator, to the text of that file. The program is the file `main.nvl`, and the paths are relative to its directory. A path missing from `files` cannot be opened.

The `eval` cases cover the special and derived forms, including `if` with several pairs and `match`, tail calls with one million iterations, a traversal of a list of one million elements with `empty?`, `first` and `rest` (8.6), a map of one million keys built with `associate`, persistence and `dissociate` on large maps, keys whose hashes may collide (8.6), sets: reading, literals, equality, printing, quasiquote, the primitives and a set of one million elements, 10,000 nested non-tail calls and `:stack-overflow`, equality, arithmetic and overflows, primitives, `read-line!` with invalid UTF-8, the code point conversions, including a String of one million code points, Floats: arithmetic, comparisons, every mix of Integers and Floats, the errors that keep them finite, the conversions, `sqrt`, Floats as keys and in patterns, and how they print, `float-to-fixed`: rounding from the exact binary value, ties away from zero, carries, signs, the extremes and the errors, printing (including `display!` of strings inside collections), `catch` and `raise`, the positions in errors (8.5), both callback patterns, `include` with and without prefixes, and `import` (8.9). Some cases check that what was removed from the core does not exist: `same?`, named `lambda`, the names without `!`, the standard library functions without `import`, and the `io` functions without `import`. The library has its own cases, in the file `stdlib-conformance.json`.

**Behaviors fixed by the tests.** While writing the cases, these points needed an answer that the text above does not give yet:

- `first` and `rest` of `()` produce `:wrong-type`.
- Repeated parameters in a `lambda` produce `:expansion-error`.
- `dissociate` of a missing key returns the map unchanged.
- A `raise` with a valid descriptor and a custom `:kind`, such as `:custom`, terminates the program with that `:kind`.

Version 0.2.1 settled the remaining points in the text (see its list of changes), and the cases cover them. Version 0.3.0 added the positions in errors, version 0.4.0 the persistent maps, version 0.5.0 the type Set and version 0.6.0 the `if` with several pairs, version 0.7.0 `match`, version 0.8.0 the code point conversions and version 0.9.0 the type Float and version 0.10.0 `float-to-fixed`; their cases cover them too. Version 0.10.1 added cases for the behaviors that a measurement of the implementation's coverage found untested.
