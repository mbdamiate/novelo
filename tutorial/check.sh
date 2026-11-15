#!/bin/sh
# Checks one exercise of the tutorial.
#
#   tutorial/check.sh 02-seconds              checks your answer.nvl
#   tutorial/check.sh 02-seconds other.nvl    checks another file instead
#
# It runs the exercise's check.nvl against your answer and prints one line
# per failing test, then how many passed. It exits with 0 when every test
# passed. Set NOVELO to use an interpreter other than build/novelo.

root=$(cd "$(dirname "$0")/.." && pwd)
interpreter=${NOVELO:-$root/build/novelo}
name=$1
if [ -z "$name" ]; then
    echo "usage: tutorial/check.sh EXERCISE [FILE]" >&2
    echo "exercises:" >&2
    ls "$root/tutorial/exercises" | grep -v '\.nvl$' | sed 's/^/  /' >&2
    exit 2
fi
directory=$root/tutorial/exercises/$name
if [ ! -f "$directory/check.nvl" ]; then
    echo "check.sh: no exercise named $name" >&2
    exit 2
fi
answer=${2:-$directory/answer.nvl}
if [ ! -f "$answer" ]; then
    echo "check.sh: cannot read $answer" >&2
    exit 2
fi
if [ ! -x "$interpreter" ]; then
    echo "check.sh: build the interpreter first with make" >&2
    exit 2
fi

# The check includes "harness.nvl" and "answer.nvl" from its own directory,
# so the three files are put side by side in a fresh one.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$root/tutorial/exercises/harness.nvl" "$directory/check.nvl" "$work/"
cp "$answer" "$work/answer.nvl"
"$interpreter" "$work/check.nvl" < /dev/null
