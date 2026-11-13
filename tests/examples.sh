#!/bin/sh
# Runs each examples/NAME.nvl with the interpreter and checks that it exits
# with 0 and prints exactly examples/NAME.output. When examples/NAME.input
# exists, it is the program's standard input.
#
#   tests/examples.sh build/novelo

interpreter=$1
if [ -z "$interpreter" ]; then
    echo "usage: tests/examples.sh path/to/novelo" >&2
    exit 2
fi

passed=0
failed=0
actual=$(mktemp)
trap 'rm -f "$actual"' EXIT

for program in examples/*.nvl; do
    name=${program%.nvl}
    input=/dev/null
    [ -f "$name.input" ] && input=$name.input
    if "$interpreter" "$program" < "$input" > "$actual"; then
        if cmp -s "$actual" "$name.output"; then
            passed=$((passed + 1))
            continue
        fi
        echo "FAIL $program: the output differs from $name.output" >&2
        diff "$name.output" "$actual" >&2
    else
        echo "FAIL $program: exit code $?" >&2
    fi
    failed=$((failed + 1))
done

echo "examples: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
