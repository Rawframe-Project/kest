#!/bin/sh
# What four workloads cost, in this language and in the two beside it. It is
# not part of `make check`: a duration is not a pass or a fail, and this is
# here to catch a change that made something slower and to say where this
# language sits rather than to hold anything.
#
# Four shapes of work, each answering with a checksum so that a run of one
# language and a run of another can be shown to have done the same thing:
#
#   kernel   numbers in and numbers out, which is a frame with no handles in it
#   control  branches rather than arithmetic, which is a rule of behaviour
#   graph    checked identity, which the others write by hand
#   words    text made, joined, split and searched
#
# `bench/graph` is the one worth reading twice: the comparators do with an
# index and a generation what this language does with `store` and `ref`, so
# what it measures is what the checking costs rather than what it saves.
#
# The comparators are found rather than built here: set `KEST_CPP`, `KEST_LUAU`
# and `KEST_DAS` to say where each is, or leave them out and the row is left
# out with them. Nothing is downloaded and nothing is built that is not this
# tree's. See D980.
#
# Two of the rows are of something built before it is run, because that is
# what the thing is: this language's release engine is C the host's compiler
# compiled (D1093), and daslang's `-exe` is its own compiler writing a
# binary. Building is not in the duration -- it is not what either of them
# does when a game runs -- and a row whose build failed is left out rather
# than shown as a slow one. See D1108.
set -eu

kest=${KEST:-./kest}
best=${BEST:-5}
cc=${CC:-cc}
built=$(mktemp -d)
nothing=""
trap 'rm -rf "$built" "$nothing"' EXIT

run_it() {
    # The best of several, because what a machine does once is what the machine
    # was doing at the time. Milliseconds, from the shell's own clock, which is
    # the host's -- nothing in this language measures a duration.
    #
    # The second word is what ran it, said rather than read off the command,
    # because one binary run two ways is two rows and a reader has to be able
    # to tell which is which: `luau -O2` and `luau -O2 --codegen` are the same
    # file. See D1090.
    name=$1
    ran_by=$2
    shift 2
    fastest=""
    answer=""
    i=0
    while [ "$i" -lt "$best" ]; do
        started=$(date +%s%N)
        said=$("$@" 2>/dev/null </dev/null) || said="(did not run)"
        ended=$(date +%s%N)
        took=$(( (ended - started) / 1000000 ))
        if [ -z "$fastest" ] || [ "$took" -lt "$fastest" ]; then
            fastest=$took
        fi
        answer=$said
        i=$((i + 1))
    done
    # A row that did not run has no duration. What was timed was the failing,
    # and a number beside `(did not run)` is a number a reader compares with
    # the rows that did. See D1116.
    if [ "$answer" = "(did not run)" ]; then
        printf '%-10s %-20s %6s     %s\n' "$name" "$ran_by" "--" "$answer"
        return
    fi
    printf '%-10s %-20s %6s ms  %s\n' "$name" "$ran_by" "$fastest" "$answer"
}

# What every row below includes before any of the work is done. A whole process
# is the work and everything that has to happen first, and for a short workload
# that is most of it: reading and compiling the library and the program, making
# a machine, and the shell's own two forks to take the time. Measured here with
# a program that does nothing, so a reader can take it off both sides rather
# than read a ratio of startups as a ratio of languages.
#
# What it is worth knowing: the ratio of the work is larger than the ratio of
# the processes on every workload here, by 1.1 to 3.4 times on the machine this
# was written on. See D1067.
nothing=$(mktemp -d)
cat > "$nothing"/nothing.kest <<'KEST'
module nothing

fn main() -> i32 {
    return 0
}
KEST
before=$(run_it "nothing" "kest" "$kest" run "$nothing"/nothing.kest |
    awk '{ print $3 }')
echo "every \`$kest\` row below holds $before ms of reading the library and \
making a machine before any of the workload runs, measured by running a \
program that does nothing. The ratio of the work is larger than the ratio of \
the rows, and this is how much larger."
echo

printf '%-10s %-20s %9s  %s\n' "workload" "ran by" "best of $best" "answered"
for one in kernel control graph words rules; do
    if [ ! -f "bench/$one.kest" ]; then
        continue
    fi
    run_it "$one" "kest" "$kest" run "bench/$one.kest"
    if [ -n "${KEST_CPP:-}" ] && [ -x "bench/$one-cpp" ]; then
        run_it "$one" "c++ -O2" "bench/$one-cpp"
    fi
    # Each comparator in the mode somebody shipping a game would use it in,
    # and both of the two it has: an interpreter and the thing it can turn
    # into. A row that names only the file is a row that hides which. See
    # D1090.
    if [ -n "${KEST_LUAU:-}" ] && [ -f "bench/$one.lua" ]; then
        run_it "$one" "luau -O2" "$KEST_LUAU" -O2 "bench/$one.lua"
        run_it "$one" "luau -O2 --codegen" "$KEST_LUAU" -O2 --codegen \
            "bench/$one.lua"
    fi
    # This language's other engine: the same program written as C and
    # compiled by the compiler a release is built with. A body it has no C
    # for is one the machine runs, so a row here is whatever mixture that
    # program turns out to be -- which is what shipping one would be.
    if "$kest" emit --c "bench/$one.kest" >"$built/$one.c" 2>/dev/null &&
            $cc -O2 -Iinclude -o "$built/$one" "$built/$one.c" libkest.a \
                -lm 2>/dev/null; then
        run_it "$one" "kest, compiled" "$built/$one" "bench/$one.kest"
    fi
    if [ -n "${KEST_DAS:-}" ] && [ -f "bench/$one.das" ]; then
        # Without its module cache, which is the same thing every other row
        # here does: `kest` reads and compiles the program on every run and so
        # does `luau`, and a row that reads a cache the row above it wrote is
        # not the same measurement. It also keeps a directory of somebody
        # else's out of this tree. See D1118.
        run_it "$one" "daslang" "$KEST_DAS" -no-module-cache "bench/$one.das"
        # And for the same reason without the JIT's own cache, which is a
        # directory of compiled code written wherever it was run from -- this
        # tree -- and read by every run after the first, so that the best of
        # five was four runs that compiled nothing. See D1161.
        run_it "$one" "daslang -jit" "$KEST_DAS" -no-module-cache -jit \
            -jit-no-cache "bench/$one.das"
        # And the one its documentation points at, named as what it is: the
        # compiler writing a binary rather than running the program. See
        # D1090's rule about naming the mode.
        # What it writes is the name it was given with `.exe` after it, on
        # this system as on the one the suffix is for, so the binary is looked
        # for under both. See D1161.
        if "$KEST_DAS" -no-module-cache -exe \
                -output "$built/$one.das.bin" "bench/$one.das" \
                >/dev/null 2>&1; then
            for exe in "$built/$one.das.bin" "$built/$one.das.bin.exe"; do
                if [ -x "$exe" ]; then
                    run_it "$one" "daslang -exe (AOT)" "$exe"
                    break
                fi
            done
        fi
    fi
done
