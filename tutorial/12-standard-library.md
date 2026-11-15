# 12. A tour of the standard library

The standard library is twelve libraries, all written in Novelo, whose source is in [`standard-library.md`](../standard-library.md). Chapters 4 to 8 covered `list`, `function`, `map`, `set` and `result`. This chapter visits the other seven with one short task each. The full list of functions is in [stdlib §3](../standard-library.md#3-functions).

Every library returns new values and changes nothing, like the rest of the language. The ones that read text (`json`, `csv`, `time`) return results for text that does not parse, as chapter 8 recommends.

## `text`

Splitting, joining, formatting and case:

```novelo
> (split "," "a,b,,c")
("a" "b" "" "c")
> (join ", " '("a" "b"))
"a, b"
> (format "{} has {} items" "cart" 3)
"cart has 3 items"
> (pad-left 5 "0" "42")
"00042"
> (uppercase "Ação")
"AÇÃO"
> (characters "olá")
("o" "l" "á")
> (precedes? "apple" "banana")
#true
```

`format` puts the `to-string` of each value in place of each `{}`, so strings go in without quotes. `split` splits on one exact separator. To split on "any amount of spaces", use `pattern`, below.

## `number`

```novelo
> (abs -7)
7
> (max 3 9 2)
9
> (floor 2.7)
2
> (round 2.5)
3
> (digits-to-integer "ff" 16)
255
> (integer-to-digits 255 2)
"11111111"
```

`floor`, `ceiling` and `round` take a Float and return an Integer: they are the explicit conversions chapter 2 asked for. `round` rounds halves away from zero.

## `random`

A random generator is a value. Each draw returns the number together with the next generator, and the same seed always gives the same numbers:

```novelo
> (define generator (random-generator 42))
> (let (((roll next) (random-integer generator 1 7))
>       ((second-roll _) (random-integer next 1 7)))
>   (list roll second-roll))
(4 2)
> (first (random-integer (random-generator 42) 1 7))
4
```

To draw many numbers, thread the generator through a fold or a recursion. A program gets a fresh seed from `random-seed!` in the library `io`, once, at its edge, and prints it so a run can be repeated. The example [`16-simulation`](../examples/16-simulation/main.nvl) does exactly that.

## `json`

```novelo
> (read-json "{\"name\": \"Ada\", \"tags\": [\"math\", null]}")
(:ok {"name" "Ada" "tags" ("math" #void)})
> (write-json {"name" "Ada" "born" 1815})
"{\"name\": \"Ada\", \"born\": 1815}"
```

Objects become Maps with String keys, arrays become Lists, and `null` becomes `#void`. A failure gives the message and the line and column where the text stopped being JSON.

## `csv`

```novelo
> (read-csv "name,score\nAda,91\n\"Smith, J\",80\n")
(:ok (("name" "score") ("Ada" "91") ("Smith, J" "80")))
> (write-csv '(("a" "b,c") ("d" "e")))
"a,\"b,c\"\nd,e\n"
```

Each record is a List of Strings. Converting the fields is up to the program. `read-csv-with` and `write-csv-with` take another separator, such as `";"`.

## `time`

`current-time!` gives an instant in milliseconds, and the library `time` turns it into a calendar date and back, in UTC:

```novelo
> (time-to-date 0)
{:year 1970 :month 1 :day 1 :hour 0 :minute 0 :second 0 :millisecond 0 :weekday 4}
> (format-time 1763217005120)
"2025-11-15T14:30:05.120Z"
> (read-time "2026-11-15T14:30:05-03:00")
(:ok 1794763805000)
> (date-to-time {:year 2026 :month 11 :day 15})
1794700800000
> (days-in-month 2024 2)
29
```

The `:weekday` runs from 1, Monday, to 7, Sunday. `read-time` reads RFC 3339 with any offset, and the instant it returns is always UTC.

## `pattern`

Regular expressions, written as Strings. Inside a Novelo string a backslash is itself escaped, so `\d` is written `"\\d"`:

```novelo
> (find-match "(\\d+)-(\\d+)" "call 555-1234 now")
{:start 5 :end 13 :text "555-1234" :groups ("555" "1234")}
> (map (lambda (found) (get found :text)) (find-all "\\d+" "a1b22c333"))
("1" "22" "333")
> (matches? "[a-z]+@[a-z]+\\.com" "ada@example.com")
#true
> (replace-all "\\s+" (lambda (found) " ") "a   b \t c")
"a b c"
> (split-pattern ",\\s*" "a, b,c,   d")
("a" "b" "c" "d")
```

A match is a Map with its position, its text and the text of each group. `replace-all` takes a function of the match, so the replacement can depend on it. A pattern used many times can be compiled once with `compile-pattern`. The syntax is a common subset, without lookaround or backreferences, and it runs in time linear in the text for every pattern ([stdlib §4.12](../standard-library.md#412-pattern)).

## Exercises

- **`12-log-summary`.** Read log lines with `pattern` and `time`. Count the lines of each level, and find the first error.
- **`12-csv-to-json`.** Turn a CSV table with a header row into a JSON array of objects.

```bash
tutorial/check.sh 12-log-summary
```

Each library has an example of its own:
- [`12-json`](../examples/12-json/main.nvl) for `json`;
- [`16-simulation`](../examples/16-simulation/main.nvl) for `random`;
- [`17-csv`](../examples/17-csv/main.nvl) for `csv`;
- [`18-time`](../examples/18-time/main.nvl) for `time`;
- [`19-pattern`](../examples/19-pattern/main.nvl) for `pattern`.

Next: [A complete program](13-a-complete-program.md).
