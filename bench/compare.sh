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
# any machine running the same binary. Everything a row does is in it: reading
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

measure() {
    # The run with the least processor time, and the instructions of that
    # run. `perf` writes what it counted to the error stream, one line an
    # event, fields split by commas.
    workload=$1
    engine=$2
    shift 2
    least=""
    retired=""
    i=0
    while [ "$i" -lt "$best" ]; do
        if ! counted=$(perf stat -x, -e task-clock,instructions:u "$@" \
                           2>&1 >/dev/null </dev/null); then
            echo "$workload $engine did not run" >&2
            return
        fi
        took=$(printf '%s\n' "$counted" | awk -F, '$3 ~ /^task-clock/ { print $1 }')
        count=$(printf '%s\n' "$counted" | awk -F, '$3 ~ /^instructions/ { print $1 }')
        if [ -z "$least" ] || awk "BEGIN { exit !($took < $least) }"; then
            least=$took
            retired=$count
        fi
        i=$((i + 1))
    done
    printf '%s\t%s\t%.2f\t%s\n' "$workload" "$engine" "$least" "$retired" >>"$out"
}

commit=$(git rev-parse --short HEAD)
{
    printf '# taken\t%s\n' "$(date -u +%Y-%m-%d)"
    printf '# commit\t%s\n' "$commit"
    printf '# machine\t%s\n' "$(awk -F': ' '/^model name/ { print $2; exit }' /proc/cpuinfo)"
    printf '# best of\t%s\n' "$best"
    printf 'workload\tengine\tms\tinstructions\n'
} >"$out"

for one in kernel control graph words rules; do
    measure "$one" "Kest" "$kest" run "bench/$one.kest"
    if "$kest" emit --c "bench/$one.kest" >"$built/$one.c" 2>/dev/null &&
            $cc -O2 -Iinclude -o "$built/$one" "$built/$one.c" libkest.a \
                -lm 2>/dev/null; then
        measure "$one" "Kest, compiled" "$built/$one" "bench/$one.kest"
    fi
    # And the floor, where `KEST_CPP` names a C++ compiler: not a guest
    # language and not on the charts, but what the report measures against.
    if [ -n "${KEST_CPP:-}" ] && [ -f "bench/$one.cpp" ] &&
            "$KEST_CPP" -O2 -o "$built/$one-cpp" "bench/$one.cpp" 2>/dev/null; then
        measure "$one" "C++" "$built/$one-cpp"
    fi
    if [ -n "${KEST_LUAU:-}" ] && [ -f "bench/$one.lua" ]; then
        measure "$one" "Luau" "$KEST_LUAU" -O2 "bench/$one.lua"
        measure "$one" "Luau, native" "$KEST_LUAU" -O2 --codegen \
            "bench/$one.lua"
    fi
    if [ -n "${KEST_DAS:-}" ] && [ -f "bench/$one.das" ]; then
        measure "$one" "daslang" "$KEST_DAS" -no-module-cache "bench/$one.das"
        # It writes the name it was given with `.exe` after it on this system
        # as well, so both are looked for. See D1161.
        if "$KEST_DAS" -no-module-cache -exe -output "$built/$one.das.bin" \
                "bench/$one.das" >/dev/null 2>&1; then
            for exe in "$built/$one.das.bin" "$built/$one.das.bin.exe"; do
                if [ -x "$exe" ]; then
                    measure "$one" "daslang, AOT" "$exe"
                    break
                fi
            done
        fi
    fi
done

python3 bench/chart.py "$out" bench
echo "wrote $out and the charts beside it, taken at $commit"
