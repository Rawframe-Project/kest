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
set -eu

kest=${KEST:-./kest}
best=${BEST:-5}

run_it() {
    # The best of several, because what a machine does once is what the machine
    # was doing at the time. Milliseconds, from the shell's own clock, which is
    # the host's -- nothing in this language measures a duration.
    name=$1
    shift
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
    printf '%-10s %-10s %6s ms  %s\n' "$name" "$1" "$fastest" "$answer"
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
trap 'rm -rf "$nothing"' EXIT
cat > "$nothing"/nothing.kest <<'KEST'
module nothing

fn main() -> i32 {
    return 0
}
KEST
before=$(run_it "nothing" "$kest" run "$nothing"/nothing.kest |
    awk '{ print $3 }')
echo "every \`$kest\` row below holds $before ms of reading the library and \
making a machine before any of the workload runs, measured by running a \
program that does nothing. The ratio of the work is larger than the ratio of \
the rows, and this is how much larger."
echo

printf '%-10s %-10s %9s  %s\n' "workload" "ran by" "best of $best" "answered"
for one in kernel control graph words; do
    if [ ! -f "bench/$one.kest" ]; then
        continue
    fi
    run_it "$one" "$kest" run "bench/$one.kest"
    if [ -n "${KEST_CPP:-}" ] && [ -x "bench/$one-cpp" ]; then
        run_it "$one" "bench/$one-cpp"
    fi
    if [ -n "${KEST_LUAU:-}" ] && [ -f "bench/$one.lua" ]; then
        run_it "$one" "$KEST_LUAU" "bench/$one.lua"
    fi
    if [ -n "${KEST_DAS:-}" ] && [ -f "bench/$one.das" ]; then
        run_it "$one" "$KEST_DAS" "bench/$one.das"
    fi
done
