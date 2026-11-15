#!/bin/sh
# Runs each example, examples/NAME/main.nvl, with the interpreter and checks
# that it exits with 0 and prints exactly examples/NAME/output.txt. When
# examples/NAME/input.txt exists, it is the program's standard input, and
# when examples/NAME/arguments.txt exists, each of its lines is one argument
# of the program. A second argument names another directory laid out the
# same way, such as tutorial/programs. What it writes to standard error is shown only when it
# fails. Other files in the directory belong to the example, such
# as files it includes.
#
#   tests/examples.sh build/novelo
#   tests/examples.sh build/novelo tutorial/programs

interpreter=$1
examples=${2:-examples}
if [ -z "$interpreter" ]; then
    echo "usage: tests/examples.sh path/to/novelo [directory]" >&2
    exit 2
fi

passed=0
failed=0
actual=$(mktemp)
errors=$(mktemp)
trap 'rm -f "$actual" "$errors"' EXIT

for program in "$examples"/*/main.nvl; do
    directory=${program%/main.nvl}
    input=/dev/null
    [ -f "$directory/input.txt" ] && input=$directory/input.txt
    set --
    if [ -f "$directory/arguments.txt" ]; then
        while IFS= read -r argument; do set -- "$@" "$argument"; done < "$directory/arguments.txt"
    fi
    if "$interpreter" "$program" "$@" < "$input" > "$actual" 2> "$errors"; then
        if cmp -s "$actual" "$directory/output.txt"; then
            passed=$((passed + 1))
            continue
        fi
        echo "FAIL $program: the output differs from $directory/output.txt" >&2
        diff "$directory/output.txt" "$actual" >&2
    else
        echo "FAIL $program: exit code $?" >&2
        cat "$errors" >&2
    fi
    failed=$((failed + 1))
done

echo "$examples: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
