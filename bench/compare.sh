#!/bin/sh
# The five workloads against the two languages beside this one, in every mode
# each of them ships, written down where the front page reads them: a table in
# `bench/results.tsv`, with the date, the commit and the machine it was taken
# on, and the charts `bench/chart.py` draws from it. Not part of `make check`,
# for the reason none of `bench` is.
#
# What is measured is the process's own time on the processor and the
# instructions it retired, from `perf stat`, the best of several runs by time.
# Wall-clock time on a machine somebody else is also using is mostly theirs;
# processor time is this process's, and instructions are the same number on
# any machine running the same binary. The runs of one row are not taken one
# after another: a round runs every row once and the next round runs them all
# again, so what a neighbour did for half a second falls on one run of many
# rows rather than on every run of one. Taken back to back, the same binary
# was 54 ms on `kernel` in one sitting and 74 in the next. See D1195. Everything a row does is in it: reading
# the program, compiling it where that is what the engine does, and running
# it. Building a binary ahead of time -- this language's release engine and
# daslang's `-exe` -- is not, because it is not what a game does when it runs.
#
# `KEST_LUAU` and `KEST_DAS` say where the comparators are, and `KEST_CPP` a
# C++ compiler for the floor; a row whose engine is not there is left out
# rather than guessed. See D1180.
set -eu

kest=${KEST:-./kest}
best=${BEST:-5}
cc=${CC:-cc}
built=$(mktemp -d)
trap 'rm -rf "$built"' EXIT
out=${OUT:-bench/results.tsv}

commit=$(git rev-parse --short HEAD)
{
    printf '# taken\t%s\n' "$(date -u +%Y-%m-%d)"
    printf '# commit\t%s\n' "$commit"
    printf '# machine\t%s\n' "$(awk -F': ' '/^model name/ { print $2; exit }' /proc/cpuinfo)"
    printf '# best of\t%s\n' "$best"
    printf 'workload\tengine\tms\tinstructions\n'
} >"$out"

# A row is a workload, an engine and the command that runs it, kept one word
# a line so that it runs as it was given rather than through a shell, which
# would be measured with it.
rows=0
row() {
    rows=$((rows + 1))
    printf '%s\n%s\n' "$1" "$2" >"$built/row$rows.what"
    shift 2
    for word in "$@"; do
        printf '%s\n' "$word"
    done >"$built/row$rows.argv"
    : >"$built/row$rows.took"
}

# One run of one row: the processor time and the instructions, a line added to
# what that row has taken. `perf` writes what it counted to the error stream,
# one line an event, fields split by commas.
once() {
    which=$1
    set --
    while IFS= read -r word; do
        set -- "$@" "$word"
    done <"$built/row$which.argv"
    if ! counted=$(perf stat -x, -e task-clock,instructions:u "$@" \
                       2>&1 >/dev/null </dev/null); then
        echo failed >>"$built/row$which.took"
        return
    fi
    took=$(printf '%s\n' "$counted" | awk -F, '$3 ~ /^task-clock/ { print $1 }')
    count=$(printf '%s\n' "$counted" | awk -F, '$3 ~ /^instructions/ { print $1 }')
    printf '%s %s\n' "$took" "$count" >>"$built/row$which.took"
}

for one in kernel control graph words rules; do
    row "$one" "Kest" "$kest" run "bench/$one.kest"
    if "$kest" emit --c "bench/$one.kest" >"$built/$one.c" 2>/dev/null &&
            $cc -O2 -Iinclude -o "$built/$one" "$built/$one.c" libkest.a \
                -lm 2>/dev/null; then
        row "$one" "Kest, compiled" "$built/$one" "bench/$one.kest"
    fi
    # And the floor, where `KEST_CPP` names a C++ compiler: not a guest
    # language and not on the charts, but what the report measures against.
    if [ -n "${KEST_CPP:-}" ] && [ -f "bench/$one.cpp" ] &&
            "$KEST_CPP" -O2 -o "$built/$one-cpp" "bench/$one.cpp" 2>/dev/null; then
        row "$one" "C++" "$built/$one-cpp"
    fi
    if [ -n "${KEST_LUAU:-}" ] && [ -f "bench/$one.lua" ]; then
        row "$one" "Luau" "$KEST_LUAU" -O2 "bench/$one.lua"
        row "$one" "Luau, native" "$KEST_LUAU" -O2 --codegen "bench/$one.lua"
    fi
    if [ -n "${KEST_DAS:-}" ] && [ -f "bench/$one.das" ]; then
        row "$one" "daslang" "$KEST_DAS" -no-module-cache "bench/$one.das"
        # It writes the name it was given with `.exe` after it on this system
        # as well, so both are looked for. See D1161.
        if "$KEST_DAS" -no-module-cache -exe -output "$built/$one.das.bin" \
                "bench/$one.das" >/dev/null 2>&1; then
            for exe in "$built/$one.das.bin" "$built/$one.das.bin.exe"; do
                if [ -x "$exe" ]; then
                    row "$one" "daslang, AOT" "$exe"
                    break
                fi
            done
        fi
    fi
done

round=0
while [ "$round" -lt "$best" ]; do
    ran=1
    while [ "$ran" -le "$rows" ]; do
        once "$ran"
        ran=$((ran + 1))
    done
    round=$((round + 1))
done

# The run with the least processor time, and the instructions of that run; a
# row one of whose runs failed is said and left out rather than answered by the
# runs that did not.
ran=1
while [ "$ran" -le "$rows" ]; do
    workload=$(sed -n 1p "$built/row$ran.what")
    engine=$(sed -n 2p "$built/row$ran.what")
    if grep -q '^failed$' "$built/row$ran.took"; then
        echo "$workload $engine did not run" >&2
    else
        sort -n "$built/row$ran.took" | head -n 1 |
            awk -v w="$workload" -v e="$engine" \
                '{ printf "%s\t%s\t%.2f\t%s\n", w, e, $1, $2 }' >>"$out"
    fi
    ran=$((ran + 1))
done

python3 bench/chart.py "$out" bench
echo "wrote $out and the charts beside it, taken at $commit"
