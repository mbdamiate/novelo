#!/bin/sh
# The command line (src/main.c, core §8.7): exit codes 0, 1 and 2 and those
# of exit!, the program's arguments, and the messages on standard error. Each check runs the interpreter in a temporary
# directory and compares the exit code, standard output and standard error.
#
#   tests/cli.sh build/novelo

interpreter=$1
if [ -z "$interpreter" ]; then
    echo "usage: tests/cli.sh path/to/novelo" >&2
    exit 2
fi
case $interpreter in
/*) ;;
*) interpreter=$(pwd)/$interpreter ;;
esac

repository=$(pwd)
directory=$(mktemp -d)
trap 'rm -rf "$directory"' EXIT
cd "$directory" || exit 2
directory=$(pwd -P) # the interpreter resolves included paths without symbolic links

passed=0
failed=0

# check NAME CODE STDOUT STDERR STDIN COMMAND…
# STDOUT and STDERR are exact; a trailing newline is part of them.
check() {
    name=$1 expected_code=$2 expected_output=$3 expected_errors=$4 input=$5
    shift 5
    printf '%s' "$input" | "$@" > output.txt 2> errors.txt
    code=$?
    output=$(cat output.txt; printf x)
    errors=$(cat errors.txt; printf x)
    if [ "$code" = "$expected_code" ] && [ "$output" = "${expected_output}x" ] && [ "$errors" = "${expected_errors}x" ]; then
        passed=$((passed + 1))
        return
    fi
    failed=$((failed + 1))
    echo "FAIL $name" >&2
    echo "  exit code $code, expected $expected_code" >&2
    echo "  stdout: $(cat output.txt)" >&2
    echo "  stderr: $(cat errors.txt)" >&2
}

usage='usage: novelo [file.nvl [argument ...]]
  with a file, runs the program, which receives the arguments after it;
  without one, starts the REPL.
  NOVELO_STDLIB names the standard library document (standard-library.md).
'

# ---- arguments ---------------------------------------------------------------------

check "-h prints the usage" 0 "$usage" "" "" "$interpreter" -h
check "--help prints the usage" 0 "$usage" "" "" "$interpreter" --help
check "an unknown option" 2 "" "$usage" "" "$interpreter" -x
check "an option before the file" 2 "" "$usage" "" "$interpreter" -h a.nvl
check "a missing file, with arguments" 2 "" "novelo: cannot read a.nvl
" "" "$interpreter" a.nvl b.nvl
check "a missing file" 2 "" "novelo: cannot read missing.nvl
" "" "$interpreter" missing.nvl
check "a directory" 2 "" "novelo: cannot read .
" "" "$interpreter" .

# ---- programs ----------------------------------------------------------------------

printf '(import io)\n(display! "hello\\n")\n(+ 1 2)\n' > hello.nvl
check "a program that ends normally" 0 "hello
" "" "" "$interpreter" hello.nvl

: > empty.nvl
check "an empty program" 0 "" "" "" "$interpreter" empty.nvl

printf '(import io)\n(display! (read-line!))\n(display! (to-string (read-line!)))\n' > echo.nvl
check "standard input reaches read-line!" 0 "first#void" "" "first" "$interpreter" echo.nvl

printf '(import io)\n(display! "before")\n(1 2' > read.nvl
check "a read error runs nothing" 1 "" "read.nvl:3:1: read error :unclosed-delimiter: '(' is never closed
" "" "$interpreter" read.nvl

printf '(import io)\n(display! "before\\n")\n(list 1\n  (quotient 1 0))\n' > uncaught.nvl
check "an uncaught error, after the output" 1 "before
" "uncaught.nvl:4:3: error :division-by-zero: quotient: division by zero
" "" "$interpreter" uncaught.nvl

# ---- the program's arguments, standard error and exit! ----------------------------------

printf '(import io)\n(write! (program-arguments!))\n' > arguments.nvl
check "the words after the file are the program's arguments" 0 '("-h" "--help" "-x" "" "two words")' "" "" \
    "$interpreter" arguments.nvl -h --help -x "" "two words"
check "a program with no arguments" 0 "()" "" "" "$interpreter" arguments.nvl

check "an argument that is not UTF-8" 1 "" "arguments.nvl:2:9: error :io-error: program-arguments!: argument 1 is not valid UTF-8 at byte 1 :invalid-utf8 1
" "" "$interpreter" arguments.nvl ok "a$(printf '\377')"

printf '(import io)\n(display! "out ")\n(display-to-error! "error ")\n(write-to-error! "w")\n(display! "more")\n' > streams.nvl
check "display-to-error! and write-to-error! write to standard error" 0 "out more" 'error "w"' "" "$interpreter" streams.nvl

printf '(import io)\n(display! "before\\n")\n(catch (lambda () (exit! 3)) (lambda (problem) 0))\n(display! "after\\n")\n' > exit.nvl
check "exit! ends the program with its code" 3 "before
" "" "" "$interpreter" exit.nvl

printf '(import io)\n(exit! 0)\n(first ())\n' > exit-zero.nvl
check "exit! with 0" 0 "" "" "" "$interpreter" exit-zero.nvl

printf '(import io)\n(exit! 256)\n' > exit-range.nvl
check "exit! with a code out of range" 1 "" "exit-range.nvl:2:1: error :domain-error: exit! expects a code from 0 to 255 256
" "" "$interpreter" exit-range.nvl

# Example 14 stops with code 2 on a usage error; its normal run is checked by
# tests/examples.sh.
tool=$repository/examples/14-tool/main.nvl
tool_usage='usage: main.nvl [-l] [-w] [-c] < text
'
check "example 14: an unknown option" 2 "" "main.nvl: unknown option -x
$tool_usage" "" "$interpreter" "$tool" -l -wx
check "example 14: an argument that is not an option" 2 "" "main.nvl: unexpected argument \"words\"
$tool_usage" "" "$interpreter" "$tool" words
check "example 14: lines only" 0 "     2 lines
" "" "a b
c
" "$interpreter" "$tool" -l

# ---- files: relative paths name files from the current directory -----------------------

mkdir -p tools
printf 'from the current directory' > data.txt
printf 'next to the program' > tools/data.txt
printf '(import io)\n(write! (read-file! "data.txt"))\n(write-file! "written.txt" "w")\n' > tools/reader.nvl
check "a relative path is resolved against the current directory" 0 '(:ok "from the current directory")' "" "" \
    "$interpreter" tools/reader.nvl
check "a file the program wrote" 0 "w" "" "" cat written.txt

if [ "$(id -u)" != 0 ]; then # root reads every file
    printf 'secret' > secret.txt
    chmod 000 secret.txt
    printf '(import io)\n(write! (read-file! "secret.txt"))\n' > secret.nvl
    check "a file without permission to read" 0 '(:error :permission-denied "secret.txt")' "" "" "$interpreter" secret.nvl
    chmod 600 secret.txt
fi

# Example 15 writes its report with -o, and stops on a usage error or a
# report it cannot write.
expenses=$repository/examples/15-files/main.nvl
expenses_usage='usage: main.nvl [-o report.txt] expenses.txt ...
'
printf 'food 10\n' > month.txt
check "example 15: -o writes the report" 0 "report written to report.txt
" "" "" "$interpreter" "$expenses" -o report.txt month.txt
check "example 15: the report written" 0 "month.txt: 1 expense

food            10.00
total           10.00
" "" "" cat report.txt
check "example 15: a report that cannot be written" 1 "" "main.nvl: cannot write tools (:not-a-file)
" "" "$interpreter" "$expenses" -o tools month.txt
check "example 15: no file" 2 "" "main.nvl: no file to read
$expenses_usage" "" "$interpreter" "$expenses"
check "example 15: an unknown option" 2 "" "main.nvl: unknown option -x
$expenses_usage" "" "$interpreter" "$expenses" -x month.txt

printf 'nothing\n' > bare.nvl
check "an error with no position" 1 "" "error :unbound-symbol: unbound symbol nothing nothing
" "" "$interpreter" bare.nvl

printf '(error "custom" 1 "two" (list :three))\n' > values.nvl
check "an error's values, as write! prints them" 1 "" "values.nvl:1:1: error :error: custom 1 \"two\" (:three)
" "" "$interpreter" values.nvl

# A descriptor made by raise may lack a message or values, or carry an
# unusable position: the message shows what there is.
printf '(raise {:kind :custom})\n' > bare-descriptor.nvl
check "a raised descriptor with only a kind" 1 "" "bare-descriptor.nvl:1:1: error :custom
" "" "$interpreter" bare-descriptor.nvl

printf '(raise {:kind :custom :message 5 :values 7 :position {:line "x" :column 1}})\n' > odd-descriptor.nvl
check "a raised descriptor with fields of other types" 1 "" "error :custom
" "" "$interpreter" odd-descriptor.nvl

printf '(raise {:kind :custom :message "m" :values (list 1) :position {:line 9 :column 2 :file 5}})\n' > position.nvl
check "a raised position without a file name" 1 "" "9:2: error :custom: m 1
" "" "$interpreter" position.nvl

mkdir -p library
printf '(include "library/part.nvl")\n' > includes.nvl
printf '(define x 1)\n  (first 5)\n' > library/part.nvl
check "an error in an included file names that file" 1 "" "$directory/library/part.nvl:2:3: error :wrong-type: first expects a non-empty List, got Integer 5
" "" "$interpreter" includes.nvl

printf '(import list)\n' > import.nvl
check "NOVELO_STDLIB names the document" 1 "" "import.nvl:1:1: error :import-error: cannot open the standard library /no/such.md (set NOVELO_STDLIB) list :cannot-open
" "" env NOVELO_STDLIB=/no/such.md "$interpreter" import.nvl

# ---- the REPL, with standard input not a terminal ----------------------------------

check "the REPL prints values, without prompts" 0 "3
\"x\"
" "" "(+ 1 2)
(define y 1)
\"x\"
" "$interpreter"

check "the REPL reports errors and goes on" 0 "2
" "1:1: error :unbound-symbol: unbound symbol car car
1:1: read error :unexpected-delimiter: ')' without an opener
" "(car)
)
(+ 1 1)
" "$interpreter"

check "the REPL imports every library" 0 "(2 4)
" "" "(map (lambda (x) (* 2 x)) (list 1 2))
" "$interpreter"

check "the REPL reports an input left open" 0 "" "1:1: read error :unclosed-delimiter: '(' is never closed
" "(list 1
" "$interpreter"

check "exit! ends the REPL with its code" 4 "3
" "" "(+ 1 2)
(exit! 4) (display! \"never\")
(+ 5 6)
" "$interpreter"

check "the REPL has no arguments" 0 "()
" "" "(program-arguments!)
" "$interpreter"

echo "cli: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
