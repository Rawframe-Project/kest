#!/bin/sh
# What every command has to be true of: it produces something of the right
# shape, it says why when it does not, and what it says as JSON is JSON.
#
# The examples check their own answers by returning a number, so `run` exiting
# zero is the answer being right. What this adds is that a command which
# prints nothing no longer looks the same as one that works.
set -u
kest=./kest
failed=0

complain() {
    echo "$1"
    failed=1
}

# Output of the right kind, or a reported reason and a non-zero exit.
expect() {
    file=$1
    command=$2
    pattern=$3

    out=$("$kest" "$command" "$file" 2>/tmp/kest-cmd-err)
    status=$?
    if [ $status -ne 0 ]; then
        if [ ! -s /tmp/kest-cmd-err ]; then
            complain "$command $file: failed and said nothing"
        fi
        return
    fi
    if [ -z "$out" ]; then
        complain "$command $file: succeeded and printed nothing"
        return
    fi
    if ! printf '%s' "$out" | grep -qE "$pattern"; then
        complain "$command $file: printed nothing matching /$pattern/"
    fi
}

# A file that holds nothing is the one a command is likeliest to answer with
# silence, and no file in this tree is one. `fmt` is left out on purpose: what
# it writes is the file, and the file is empty, so `kest fmt` over a file that
# holds nothing has to hold nothing after it.
nothing=$(mktemp -d)/nothing.kest
: > "$nothing"
expect "$nothing" lex 'end of file'
expect "$nothing" parse 'declares nothing'
expect "$nothing" check 'declares nothing'
expect "$nothing" emit '^nothing to run'
if [ -n "$($kest fmt "$nothing" 2>&1)" ]; then
    complain "fmt $nothing: a file that holds nothing formatted to something"
fi
# And running it is a refusal that says which of the two reasons it is.
if $kest run "$nothing" >/dev/null 2>/tmp/kest-cmd-err </dev/null; then
    complain "run $nothing: a file that holds nothing ran"
elif ! grep -q "declares nothing" /tmp/kest-cmd-err; then
    complain "run $nothing: refused without saying the file holds nothing"
fi
rm -rf "$(dirname "$nothing")"

for file in "$@"; do
    expect "$file" lex 'end of file'
    expect "$file" parse '^\(|^// '
    expect "$file" fmt '.'
    expect "$file" check '^(fn|struct|const|import) '
    # A file of nothing but generic functions has no bodies until a call
    # asks for one, and it says so rather than printing nothing.
    expect "$file" emit '^fn |^nothing to run'

    # `call` needs the name of a function, and a list of them here would go
    # stale, so the file is asked: the first one it declares that takes
    # nothing but numbers, text or a bool. Nought for a number and a letter
    # for text, which is enough for a call to happen.
    chosen=$("$kest" check "$file" --json 2>/dev/null </dev/null |
             python3 -c '
import json
import sys

TYPED = {"i8": "0", "i16": "0", "i32": "0", "i64": "0", "u8": "0", "u16": "0",
         "u32": "0", "u64": "0", "f32": "0", "f64": "0", "bool": "false",
         "text": "x"}

held = json.load(sys.stdin)
for one in held.get("functions", []):
    if one.get("foreign") or one.get("file") != sys.argv[1]:
        continue
    takes = one.get("parameters") or []
    if any(what not in TYPED for what in takes):
        continue
    print(" ".join([one["name"]] + [TYPED[what] for what in takes]))
    break
' "$file")
    if [ -n "$chosen" ]; then
        # shellcheck disable=SC2086
        out=$("$kest" call "$file" $chosen 2>/tmp/kest-cmd-err </dev/null)
        status=$?
        if [ $status -ne 0 ]; then
            if [ ! -s /tmp/kest-cmd-err ]; then
                complain "call $file $chosen: failed and said nothing"
            fi
        elif [ -z "$out" ]; then
            complain "call $file $chosen: succeeded and printed nothing"
        fi
    fi

    # Running is the answer being right, because an example that disagrees
    # with itself returns which check it failed.
    "$kest" run "$file" >/dev/null 2>/tmp/kest-cmd-err </dev/null
    status=$?
    if [ $status -ne 0 ] && [ ! -s /tmp/kest-cmd-err ]; then
        complain "run $file: exit $status and said nothing"
    fi

    # Not "starts with a brace": an object that goes wrong in the middle
    # starts with one too, which is how a command spent a while writing plain
    # words inside a JSON array without anything noticing.
    for command in lex parse check emit run fmt tick; do
        if ! "$kest" "$command" "$file" --json 2>/dev/null </dev/null | python3 -c '
import json
import sys

lines = [line for line in sys.stdin.read().splitlines() if line.strip()]
if not lines:
    raise SystemExit(1)
for line in lines:
    if not isinstance(json.loads(line), dict):
        raise SystemExit(1)
' 2>/dev/null; then
            complain "$command $file --json: not one object a line"
        fi
    done
done

rm -f /tmp/kest-cmd-err
if [ $failed -eq 0 ]; then
    echo "every command does something on $# file(s)"
fi
exit $failed
