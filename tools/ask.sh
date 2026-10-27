#!/bin/sh
# Runs one check the way the gate reads every check, for ctest: a check that
# passes says what it did as its last line, and a line that begins with a space
# is a detail rather than that sentence; a check that refuses says why. A
# check that passes and says nothing, or says more after what it did, or
# refuses with nothing to say, fails here.
#
#   tools/ask.sh NAME COMMAND [ARGUMENT...]
set -u

what=${1:?say which check this is}
shift
out=$("$@" 2>&1)
code=$?
if [ "$code" -eq 0 ]; then
    last=$(printf '%s' "$out" | tail -1)
    case "$last" in
    "")
        echo "$what: passed and said nothing about what it did"
        exit 1
        ;;
    " "*)
        echo "$what: said what it did and then said more"
        printf '%s\n' "$out" | tail -3 | sed 's/^/    /'
        exit 1
        ;;
    esac
    printf '%-14s %s\n' "$what" "$last"
    exit 0
fi
if [ -z "$out" ]; then
    echo "$what: refused and said nothing about why"
else
    echo "$what: refused"
    printf '%s\n' "$out" | sed 's/^/    /'
fi
exit 1
