#!/bin/sh
# Checks the tutorial against the interpreter:
#
#   1. Every REPL session in a chapter. A fenced block opened with ```novelo
#      that has lines starting with "> " is one: those lines, without the
#      "> ", are typed into a fresh REPL (build/novelo with no file), and
#      everything it prints, standard output and standard error together,
#      must equal the block's other lines. A block with no "> " line is a
#      fragment of a program and is not run.
#   2. The programs of tutorial/programs, as tests/examples.sh runs the
#      examples.
#   3. Every exercise: its solution.nvl must pass its check.nvl, and its
#      answer.nvl, the stub the learner starts from, must not.
#
#   tests/tutorial.sh build/novelo

interpreter=$1
if [ -z "$interpreter" ]; then
    echo "usage: tests/tutorial.sh path/to/novelo" >&2
    exit 2
fi
case $interpreter in
    /*) ;;
    *) interpreter=$(pwd)/$interpreter ;;
esac

passed=0
failed=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# ---- sessions ---------------------------------------------------------------

for chapter in tutorial/*.md; do
    rm -f "$work"/block-*
    # Writes block-N.input, block-N.expected and block-N.line (the line of
    # the opening fence) for each session of the chapter.
    awk -v directory="$work" '
        /^```novelo[ \t]*$/ && !inside { inside = 1; block += 1; has_input = 0
            base = directory "/block-" block
            printf "" > (base ".input"); printf "" > (base ".expected")
            print NR > (base ".line"); next }
        /^```[ \t]*$/ && inside { inside = 0; close(base ".input")
            close(base ".expected"); close(base ".line")
            if (!has_input) system("rm -f \"" base "\".*"); next }
        inside && /^> / { has_input = 1; print substr($0, 3) > (base ".input"); next }
        inside && /^>$/ { has_input = 1; print "" > (base ".input"); next }
        inside { print > (base ".expected") }
    ' "$chapter"
    for input in "$work"/block-*.input; do
        [ -f "$input" ] || continue
        base=${input%.input}
        line=$(cat "$base.line")
        "$interpreter" < "$input" > "$base.actual" 2>&1
        if cmp -s "$base.expected" "$base.actual"; then
            passed=$((passed + 1))
        else
            echo "FAIL $chapter:$line: the session prints something else" >&2
            diff "$base.expected" "$base.actual" >&2
            failed=$((failed + 1))
        fi
    done
done

echo "tutorial sessions: $passed passed, $failed failed"
session_failures=$failed

# ---- programs ---------------------------------------------------------------

program_failures=0
if [ -n "$(ls tutorial/programs 2>/dev/null)" ]; then
    tests/examples.sh "$interpreter" tutorial/programs || program_failures=1
fi

# ---- exercises --------------------------------------------------------------

passed=0
failed=0
for directory in tutorial/exercises/*/; do
    name=$(basename "$directory")
    [ -f "$directory/check.nvl" ] || continue
    if NOVELO=$interpreter tutorial/check.sh "$name" "$directory/solution.nvl" > "$work/solution.out" 2>&1; then
        passed=$((passed + 1))
    else
        echo "FAIL exercise $name: the solution does not pass" >&2
        cat "$work/solution.out" >&2
        failed=$((failed + 1))
    fi
    if NOVELO=$interpreter tutorial/check.sh "$name" > /dev/null 2>&1; then
        echo "FAIL exercise $name: the stub answer.nvl already passes" >&2
        failed=$((failed + 1))
    fi
done

echo "tutorial exercises: $passed passed, $failed failed"
[ "$session_failures" -eq 0 ] && [ "$program_failures" -eq 0 ] && [ "$failed" -eq 0 ]
