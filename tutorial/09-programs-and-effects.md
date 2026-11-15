# 9. Programs and effects

So far almost everything has been a pure function: values in, a value out, nothing else touched. A real program also reads input, writes output, looks at files and ends with an exit code. In Novelo, all of that goes through the library `io`. This chapter is about how to use it while keeping most of the program pure.

## The library `io`

A program imports `io` like any other library, and the REPL imports it for you. Its functions are listed in [core §9](../core-specification.md#9-primitives). They all end in `!`:

| Function | What it does |
| --- | --- |
| `display!`, `write!` | print a value to standard output |
| `display-to-error!`, `write-to-error!` | the same, to standard error |
| `read-line!` | the next line of standard input, without its line break, or `#void` at the end |
| `read-lines!` | all the remaining lines, as a List |
| `program-arguments!` | the words after the program's file on the command line, as a List of Strings |
| `exit!` | ends the program with an exit code from 0 to 255 |
| `read-file!`, `write-file!`, `append-file!`, `list-directory!`, `file-kind!` | files and directories |
| `environment-variable!`, `current-time!`, `monotonic-time!`, `random-seed!` | the system |

The REPL was started with no arguments, so:

```novelo
> (program-arguments!)
()
> (environment-variable! "NOVELO_TUTORIAL_UNSET")
> (integer? (current-time!))
#true
```

`current-time!` gives milliseconds since 1970, and `monotonic-time!` gives nanoseconds from a fixed point. The second one is for measuring how long something takes.

## Files return results

A missing file is not a bug in the program. It is something to expect, so the file functions return results (chapter 8) instead of raising:

```novelo
> (read-file! "no-such-file")
(:error :not-found "no-such-file")
> (file-kind! "tutorial")
:directory
> (file-kind! "no-such-file")
:none
> (list-directory! "tutorial/programs/01-hello")
(:ok ("main.nvl" "output.txt"))
> (write-file! "no-such-directory/notes.txt" "hi")
(:error :not-found "no-such-directory/notes.txt")
```

The reasons a file operation can fail are `:not-found`, `:permission-denied`, `:not-a-file` and `:not-a-directory`, plus `:invalid-utf8` for a file that is not text. A relative path is resolved against the directory the program was started from.

## Effects at the edge

A function that calls a `!` function is itself an effect, and by convention its name ends in `!` too. The style that works best in Novelo keeps those functions few and at the outside of the program.

1. **The edge** reads everything the program needs: its arguments, its input, its files.
2. **The pure middle** turns those values into the output, as a value: a String to print, an exit code, a map of files to write. Most of the code lives here.
3. **The edge** again writes the output.

The pure middle is easy to test. You can call it in the REPL, or from a test file such as the `check.nvl` of the exercises, without any input, terminal or files. It cannot fail in surprising ways because the disk was full.

[`programs/09-grades/main.nvl`](programs/09-grades/main.nvl) is a small program in this style. It reads "name score" lines from standard input and takes the passing score as an argument:

```bash
build/novelo tutorial/programs/09-grades/main.nvl 55 < tutorial/programs/09-grades/input.txt
```

```
Ada      91   passed
Grace    58   passed
Alan     72   passed
Donald   49   failed
average 67.5
skipped: expected a name and a score: Barbara
skipped: not a score: Edsger 4x
```

Every function in it is pure except the last form of the file, which is the whole edge:

```novelo
(match (passing-score (program-arguments!))
  (:ok passing) (display! (report passing (read-lines!)))
  (:error message) (sequence (display-to-error! (format "grades: {}\n" message))
                             (exit! 2)))
```

Some things to notice about the program:
- **`passing-score`** turns the arguments into a result. A bad argument is a failure value, not an error.
- **`report`** builds the whole output as one String, and the program prints it once.
- **Bad lines** become failures too. They are collected and reported, and the good lines still count.
- **A usage error** is written to standard error and ends with exit code 2, the usual code for a command used the wrong way.

Without `exit!`, a program ends with 0 when it finishes, or with 1 when an error escapes, after printing the error and its trace on standard error.

## Reading input

`read-lines!` reads all of standard input at once, which is the simplest choice for most programs. For input too large to hold in memory, read one line at a time with `read-line!` in a tail-recursive loop, which runs in constant memory:

```novelo
(define count-lines!
  (lambda (total)
    (if (void? (read-line!)) total
        otherwise (count-lines! (+ total 1)))))
```

`count-lines!` ends in `!` because it calls `read-line!`.

## Exercises

These exercises are the pure middles of two programs. The edge is up to you, if you want to try them for real.

- **`09-options`.** Turn a list of command-line arguments into a map of options, or a failure that says what is wrong.
- **`09-receipt`.** Turn lines of a shopping list into a receipt, with bad lines reported at the end.

```bash
tutorial/check.sh 09-options
```

The examples [`14-tool`](../examples/14-tool/main.nvl) (arguments, standard error and exit codes), [`15-files`](../examples/15-files/main.nvl) (files) and [`16-simulation`](../examples/16-simulation/main.nvl) (the clock and random seeds) are built the same way.

Next: [Modules](10-modules.md).
