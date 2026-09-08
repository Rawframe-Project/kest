#!/bin/sh
# Everything, in one command, because "everything passes" said by hand is a
# claim and this is a command. Twice a column has been quietly missing from a
# sweep run by hand — once a command that does not exist, once one that was
# never added — and both times the sweep said it had passed.
#
# Nothing here takes a list of files. A list is the thing that goes stale.
set -u
cd "$(dirname "$0")/.." || exit 1

failed=0
say() { printf '%-34s %s\n' "$1" "$2"; }
complain() { say "$1" "$2"; failed=1; }

sources=$(find examples lib -name '*.kest' | sort)
count=$(printf '%s\n' "$sources" | grep -c .)

# Kest under `tools` is an instrument rather than a program: it is held to
# resolving and to formatting, and not to running, because what it does is
# take a while on purpose.
instruments=$(find tools -name '*.kest' | sort)

# Built twice, because the two are different programs: the release one is what
# ships and the debug one is what says whether it was right.
if ! make >/dev/null 2>/tmp/kest-check-why; then
    complain "build" "the library does not build"
    sed 's/^/    /' /tmp/kest-check-why | head -10
    exit 1
fi
if ! make debug embed embed-debug >/dev/null 2>/tmp/kest-check-why; then
    complain "build" "the sanitised build does not build"
    sed 's/^/    /' /tmp/kest-check-why | head -10
    exit 1
fi
say "build" "release, sanitised, and both hosts"

# A file with a `main` has to run and answer nought; one without has to
# resolve. Which it is comes from the file rather than from a list here.
ran=0
resolved=0
for file in $sources; do
    out=$(./kest run "$file" 2>&1)
    status=$?
    case "$out" in
    *"has no \`main\` to run"*)
        if ./kest check "$file" >/dev/null 2>&1; then
            resolved=$((resolved + 1))
        else
            complain "examples" "$file does not resolve"
        fi
        ;;
    *)
        if [ $status -eq 0 ]; then
            ran=$((ran + 1))
        else
            complain "examples" "$file answered $status"
            printf '%s\n' "$out" | sed 's/^/    /' | head -6
        fi
        ;;
    esac
done
say "examples" "$ran ran, $resolved resolved"

for file in $instruments; do
    if ! ./kest check "$file" >/dev/null 2>/tmp/kest-check-why; then
        complain "instruments" "$file does not resolve"
        sed 's/^/    /' /tmp/kest-check-why | head -6
    fi
done
say "instruments" "$(printf '%s\n' "$instruments" | grep -c .) resolved"

for host in ./examples/embed ./examples/embed-debug; do
    if ! "$host" >/dev/null 2>/tmp/kest-check-why; then
        complain "host" "$host failed"
        sed 's/^/    /' /tmp/kest-check-why | head -10
    fi
done
say "host" "both crossings, sanitised and not"

# Every command against every file, under the sanitisers, looking at what it
# said rather than at what it returned: a command that fails for a reason is
# fine and one that walks off the end of an array is not.
sweep=0
for file in $sources; do
    for command in lex parse check fmt run emit; do
        out=$(./kest-debug "$command" "$file" 2>&1)
        case "$out" in
        *"unknown command"*)
            complain "sanitisers" "there is no \`$command\`"
            ;;
        *ERROR:*|*"runtime error"*|*Sanitizer*)
            complain "sanitisers" "$command $file"
            printf '%s\n' "$out" | grep -m2 -E 'ERROR:|runtime error' |
                sed 's/^/    /'
            ;;
        esac
        sweep=$((sweep + 1))
    done
    out=$(./kest-debug tick "$file" 8 2>&1)
    case "$out" in
    *ERROR:*|*"runtime error"*)
        complain "sanitisers" "tick $file"
        printf '%s\n' "$out" | grep -m2 -E 'ERROR:|runtime error' |
            sed 's/^/    /'
        ;;
    esac
    sweep=$((sweep + 1))
done
say "sanitisers" "$sweep runs over $count file(s)"

run() {
    what=$1
    shift
    out=$("$@" 2>&1)
    if [ $? -eq 0 ]; then
        say "$what" "$(printf '%s' "$out" | tail -1)"
    else
        complain "$what" "refused"
        printf '%s\n' "$out" | sed 's/^/    /' | head -12
    fi
}

# shellcheck disable=SC2086
run "formatting" tools/check-fmt.sh $sources $instruments
# shellcheck disable=SC2086
run "commands" tools/check-commands.sh $sources
run "tables" tools/check-tables.sh
run "header" tools/check-header.sh
run "documentation" tools/check-docs.sh docs/language.md docs/decisions.md
run "backstops" tools/check-backstops.sh

if [ $failed -eq 0 ]; then
    echo
    echo "everything passes"
fi
exit $failed
