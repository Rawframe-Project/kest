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

for file in "$@"; do
    expect "$file" lex 'end of file'
    expect "$file" parse '^\(|^// '
    expect "$file" fmt '.'
    expect "$file" check '^(fn|struct|const|import) '
    expect "$file" emit '^fn '

    # Running is the answer being right, because an example that disagrees
    # with itself returns which check it failed.
    "$kest" run "$file" >/dev/null 2>/tmp/kest-cmd-err
    status=$?
    if [ $status -ne 0 ] && [ ! -s /tmp/kest-cmd-err ]; then
        complain "run $file: exit $status and said nothing"
    fi

    for command in lex parse check emit run; do
        if ! "$kest" "$command" "$file" --json 2>/dev/null | head -c 1 | grep -q '{'; then
            complain "$command $file --json: not an object"
        fi
    done
done

rm -f /tmp/kest-cmd-err
if [ $failed -eq 0 ]; then
    echo "every command does something on $# file(s)"
fi
exit $failed
