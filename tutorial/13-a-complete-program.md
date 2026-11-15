# 13. A complete program

This chapter puts the tutorial together in one command-line tool. It reads a timesheet, a CSV file of hours worked per project. It prints the hours per project and per weekday, with bars, and reports the rows it could not read. The finished program is [`programs/13-timesheet/main.nvl`](programs/13-timesheet/main.nvl), about 150 lines. Here we build it in five steps, the order in which you would write it.

```bash
build/novelo tutorial/programs/13-timesheet/main.nvl tutorial/programs/13-timesheet/timesheet.csv
```

```
by project
novelo             9.75 ####################
reading, papers    3.50 #######
email              1.50 ###

by weekday
Mon                4.50 #########
Tue                5.50 ###########
Wed                2.75 ######
Fri                2.00 ####

total             14.75 ##############################
skipped line 8: not a number of hours: lots
skipped line 10: not a date: 2026-11-31
```

The input, [`timesheet.csv`](programs/13-timesheet/timesheet.csv), has a header and one row per entry. Two rows are wrong on purpose:

```
date,project,hours
2026-11-09,novelo,3.5
2026-11-09,email,1
2026-11-10,novelo,4
2026-11-10,"reading, papers",1.5
2026-11-11,novelo,2.25
2026-11-11,email,0.5
2026-11-12,novelo,lots
2026-11-13,"reading, papers",2
2026-11-31,email,1
```

## The plan

Following chapter 9, the program is a pure function from its inputs to its output, with a thin edge around it:

1. **arguments** → options, a result;
2. **the file's text** → rows, each a result;
3. **rows** → a summary, a map of totals;
4. **summary and problems** → the report, a String;
5. **the edge**: read the arguments and the file, call the rest, print, and choose the exit code.

Each of the first four steps can be written and tried in the REPL before the next one exists.

## Step 1: the arguments

The command takes a file name, optionally preceded by `--project NAME`. With so few cases, one `match` on the list of arguments is the whole parser:

```novelo
(define parse-arguments
  (lambda (arguments)
    (match arguments
      (file) (ok {:file file :project #void})
      ("--project" project file) (ok {:file file :project project})
      otherwise (failure "expected a file, and optionally --project NAME before it"))))
```

The result is a map of options or a failure, never an error, because wrong arguments are something to expect. `#void` stands for "no project chosen".

## Step 2: the rows

`read-csv` turns the text into records, Lists of Strings. Each record must then become a row. There are two problems to handle:

- **Integers and Floats.** `string-to-number` reads `"2"` as an Integer and `"1.5"` as a Float, and those cannot be added together (chapter 2). `parse-hours` converts the Integers, so that every number of hours is a Float.
- **Calendar checks.** `read-time` from the library `time` reads the date and rejects impossible dates such as November 31. `time-to-date` then gives the weekday.

```novelo
(define parse-hours
  (lambda (text)
    (let ((number (string-to-number text)))
      (if (integer? number) (ok (integer-to-float number))
          (float? number) (ok number)
          otherwise (failure (format "not a number of hours: {}" text))))))

(define parse-row
  (lambda (record)
    (match record
      (date-text project hours-text)
      (chain then ((instant (parse-date date-text))
                   (hours (parse-hours hours-text)))
        (ok {:project project :hours hours :weekday (get (time-to-date instant) :weekday)}))
      otherwise (failure (format "expected 3 fields, found {}" (count record))))))
```

`match` checks the shape of the record, and `chain` runs the two checks that can fail, stopping at the first failure (chapter 8). `parse-rows` then pairs each record with its line number using `zip` and `range`, so that a failure can say where it was.

## Step 3: the summary

One `fold` over the good rows builds three totals at once:
- hours per project;
- hours per weekday;
- the grand total.

```novelo
(define add-hours
  (lambda (totals key hours)
    (associate totals key (+ hours (get totals key 0.0)))))

(define summarize
  (lambda (rows)
    (fold (lambda (summary row)
            (let (({:project project :hours hours :weekday weekday} row))
              {:projects (add-hours (get summary :projects) project hours)
               :weekdays (add-hours (get summary :weekdays) weekday hours)
               :total (+ (get summary :total) hours)}))
          {:projects {} :weekdays {} :total 0.0}
          rows)))
```

This is the counting fold of chapter 6 three times over. The `let` takes the row apart with a map pattern (chapter 7), and each step returns a new summary.

## Step 4: the report

The report is a String built from the summary. Each line is `hours-line`:

```novelo
(define bar
  (lambda (hours)
    (join "" (map (lambda (_) "#") (range 0 (round (* hours 2.0)))))))

(define hours-line
  (lambda (label hours)
    (format "{} {} {}\n" (pad-right 16 " " label) (pad-left 6 " " (float-to-fixed hours 2)) (bar hours))))
```

`report` sorts the projects by their hours with `sort` and lists the weekdays in calendar order. Then `timesheet` connects steps 2 to 4: from the file's text and the options, it returns `(:ok report)`, or the failure of `read-csv` for a text that is not CSV.

```novelo
(define timesheet
  (lambda (options text)
    (chain then ((records (read-csv text)))
      (let ((results (parse-rows records))
            (rows (map second (filter ok? results)))
            (wanted (let ((project (get options :project)))
                      (if (void? project) rows
                          otherwise (filter (lambda (row) (equal? (get row :project) project)) rows))))
            (problems (map second (filter failure? results))))
        (ok (report (summarize wanted) problems))))))
```

Everything up to here can be called from the REPL or from tests with a String in place of a file.

## Step 5: the edge

The last form of the file is the only one that touches the world. Each way out has its exit code:

```novelo
(match (parse-arguments (program-arguments!))
  (:error message) (fail! (concatenate message "\n" usage) 2)
  (:ok options)
  (match (read-file! (get options :file))
    (:error reason path &rest _) (fail! (format "cannot read {}: {}" path reason) 1)
    (:ok text)
    (match (timesheet options text)
      (:ok output) (display! output)
      (:error message line column) (fail! (format "{}:{}:{}: {}" (get options :file) line column message) 1))))
```

`fail!` writes to standard error and calls `exit!`. Try the ways out:

```bash
build/novelo tutorial/programs/13-timesheet/main.nvl --project novelo tutorial/programs/13-timesheet/timesheet.csv
```

```bash
build/novelo tutorial/programs/13-timesheet/main.nvl
```

```bash
build/novelo tutorial/programs/13-timesheet/main.nvl missing.csv
```

## What the program used

| Idea | Chapter |
| --- | --- |
| `match` on the arguments and on records | 7 |
| results for expected failures, and `chain` | 8 |
| `fold` into maps, persistent updates | 5, 6 |
| `sort`, `map`, `filter`, `zip`, `range` | 4, 5 |
| `csv`, `time`, `text`, `number` | 12 |
| effects only at the edge, exit codes | 9 |

## Exercises

- **`13-hours`.** Extend `parse-hours` to accept hours and minutes, `"1:30"`, as well as plain numbers.

```bash
tutorial/check.sh 13-hours
```

Some ideas for going further on your own, each one small:
- a `--from DATE` option that leaves out earlier rows;
- the report as JSON with `write-json` when `--json` is given;
- a second file, merged with the first.

For each one, the pure part can be checked the way the exercises check it, and the edge stays as it is.

## Where to go next

You have now seen the whole language. To go on:
- **[`examples/`](../examples).** Each example adds one idea, and most of them list the friction they ran into, which shows the language's rough edges honestly.
- **[`standard-library.md`](../standard-library.md).** The source of every library function, in Novelo.
- **[`core-specification.md`](../core-specification.md).** The exact rules, when you need them.
