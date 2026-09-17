#!/bin/sh
# The fast tier. `make check` is the whole gate and takes minutes; this is what
# a change is tried against while it is being written, and it takes seconds.
#
# It is not a gate and it is not held by `check-tables.sh` the way the ten
# checks are, because it proves nothing about itself: everything here is also
# done by `make check`, in more shapes and under more builds. What this is for
# is the loop — a build, the language run, the boundary crossed, a diagnostic
# said — so that a change that breaks one of those is known about in the second
# after it is made rather than in the minute.
#
# Run by `make fast`. `make check` is what "it passes" means.
set -u

# Nothing here is given the terminal: an example that reads what it was handed
# waits forever when a check is run from one, which is a tier that hangs rather
# than a tier that is fast.
scratch=$(mktemp -d "${TMPDIR:-/tmp}/kest-fast.XXXXXX") || exit 1
trap 'rm -rf "$scratch"' EXIT INT TERM
failed=0

say() {
    printf '%-14s %s\n' "$1" "$2"
}

# The build, which is the first thing a change breaks.
if ! make -j"$(nproc 2>/dev/null || echo 4)" kest libkest.a examples/embed \
        >"$scratch"/built 2>&1 </dev/null; then
    say build "refused"
    cat "$scratch"/built
    exit 1
fi
say build "release and the other host"

# Every example, run for the answer it is written to give. An example checks
# itself and answers with which of its own checks failed, so a number here is a
# line in that file. This is the language's own coverage: the examples are what
# `docs/language.md` says each rule is run by.
# A file with a `main` is a program and has to run; one without is what a host
# drives, and there is nothing here to drive it with. Which it is is read from
# the file, because a program the command line cannot start says why it could
# not start rather than that there was nothing to run.
ran=0
driven=0
for one in examples/*.kest; do
    if ! grep -q '^fn main(' "$one"; then
        if ! ./kest check "$one" >"$scratch"/said 2>&1 </dev/null; then
            say examples "$one: $(head -3 "$scratch"/said)"
            failed=1
        fi
        driven=$((driven + 1))
        continue
    fi
    if ! ./kest run "$one" >"$scratch"/said 2>"$scratch"/why </dev/null; then
        say examples "$one answered $(cat "$scratch"/why | head -3)"
        failed=1
    fi
    ran=$((ran + 1))
done
say examples "$ran run, each answering nought, and $driven a host drives"

# The library and the instruments resolve. They are not run here -- an
# instrument is a measurement and a measurement is not a pass -- but a library
# that stopped checking is a library nothing can import.
resolved=0
for one in lib/std/*.kest tools/*.kest; do
    if ! ./kest check "$one" >"$scratch"/said 2>&1 </dev/null; then
        say library "$one: $(head -3 "$scratch"/said)"
        failed=1
    fi
    resolved=$((resolved + 1))
done
say library "$resolved file(s) resolve"

# The one form. A file that is not in it is a file the next command reads
# differently from the one that wrote it.
if ! ./kest fmt --check examples/*.kest lib/std/*.kest tools/*.kest \
        >"$scratch"/formed 2>&1 </dev/null; then
    say formatting "these are not in the one form:"
    cat "$scratch"/formed
    failed=1
else
    say formatting "every file is in the one form"
fi

# A diagnostic, said both ways. What is held here is that the compiler still
# reports rather than stops: three mistakes in one file come back as three.
cat >"$scratch"/wrong.kest <<'KEST'
module wrong

fn one() -> i32 {
    return "text"
}

fn two() -> i32 {
    return missing(1)
}

fn main() -> i32 {
    let x: i32 = 1.5
    return x
}
KEST
./kest check "$scratch"/wrong.kest >"$scratch"/said 2>&1 </dev/null
if [ $? -eq 0 ]; then
    say diagnostics "a file with three mistakes in it checked"
    failed=1
else
    codes=$(grep -c 'error\[K0' "$scratch"/said)
    if [ "$codes" -lt 3 ]; then
        say diagnostics "three mistakes in one file said $codes error(s)"
        cat "$scratch"/said
        failed=1
    else
        ./kest check --json "$scratch"/wrong.kest >"$scratch"/json 2>&1 \
                                                          </dev/null
        if ! python3 -c 'import json,sys; json.load(open(sys.argv[1]))' \
                "$scratch"/json 2>/dev/null; then
            say diagnostics "what it said as JSON is not JSON"
            failed=1
        else
            say diagnostics "$codes said at once, and as JSON"
        fi
    fi
fi

# A program that will not stop, stopped. The three memory ceilings are held by
# `check-ceilings.sh` and this is the fourth: a budget in steps, which is the
# one an embedding host cannot work around from outside. See D921.
cat >"$scratch"/spin.kest <<'KEST'
module spin

fn main() -> i32 {
    while true {
    }
    return 0
}
KEST
cat >"$scratch"/turns.kest <<'KEST'
module turns

fn turns(n: i32) -> i32 {
    let sum = 0
    for i in 0..n {
        sum += i
    }
    return sum
}

fn main() -> i32 {
    if turns(1000) != 499500 {
        return 1
    }
    return 0
}
KEST
if ./kest run --fuel 2000 "$scratch"/spin.kest >"$scratch"/spun 2>&1 </dev/null; then
    say fuel "a program with a loop that never ends ran to the end"
    failed=1
elif ! grep -q 'K0659' "$scratch"/spun; then
    say fuel "a program that ran out of steps said something else:"
    head -3 "$scratch"/spun
    failed=1
# And to the number rather than only to stopping: a budget that stops
# everything is a budget nobody can use. A thousand turns of a `for` and the
# call that reaches them is a thousand steps, and one fewer is not enough.
elif ! ./kest run --fuel 1000 "$scratch"/turns.kest >/dev/null 2>&1 </dev/null; then
    say fuel "a thousand turns would not run inside a thousand steps"
    failed=1
elif ./kest run --fuel 999 "$scratch"/turns.kest >/dev/null 2>&1 </dev/null; then
    say fuel "a thousand turns ran inside nine hundred and ninety-nine steps"
    failed=1
else
    say fuel "a loop that never ends stops, and a budget is a number"
fi

# The boundary. `examples/embed` is the other host in this tree and it runs the
# program beside it frame by frame, so it crosses in both directions. It is
# also where the budget is asked of the library rather than of the command
# line: exhausted, replenished, spent to a number, cancelled and taken back.
if ! ./examples/embed >"$scratch"/crossed 2>&1 </dev/null; then
    say embedding "the other host refused"
    tail -5 "$scratch"/crossed
    failed=1
else
    say embedding "the other host ran its program"
fi

if [ "$failed" -ne 0 ]; then
    printf 'fast: something above is wrong. `make check` is the whole of it.\n'
    exit 1
fi
printf 'fast: everything here passes. `make check` is the whole of it.\n'
