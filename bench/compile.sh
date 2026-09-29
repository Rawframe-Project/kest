#!/bin/sh
# What getting a program ready to run costs: the programs `bench/compile/
# write.py` writes, the same one in every language, taken from source to the
# first line of `main` by each engine's own command line -- reading it,
# checking it where the language checks, and compiling it where the engine
# does -- by processor time, the best of several. The long program is a game's
# worth of scripts and more, in every engine here; the generic taken 3,200 ways
# is in the languages that write a copy per type, C++ and Rust compiled to an
# object as a game's build would. Written to `bench/compile.tsv` for
# `bench/chart.py`. Not part of `make check`, for the reason none of `bench`
# is. See D1273.
#
# Where each comparator is, the way `bench/compare.sh` takes them, and
# `KEST_QJS` for QuickJS's `qjs`, `KEST_CXX` a C++ compiler and `KEST_RUSTC`
# Rust's; a row whose engine is not given is left out.
set -eu

best=${BEST:-5}
lines=${LINES_WRITTEN:-100000}
copies=${COPIES:-3200}
out=${OUT:-bench/compile.tsv}
room=$(mktemp -d)
trap 'rm -rf "$room"' EXIT

python3 bench/compile/write.py "$room" "$lines" "$copies"

# A machine somebody else is using says what they were doing as much as what
# this was, so wait a while for it to be quiet, and write down how busy it was.
quiet() {
    waited=0
    while [ "$waited" -lt "${QUIET_WAIT:-600}" ]; do
        busy=$(cut -d' ' -f1 /proc/loadavg)
        if awk -v b="$busy" -v q="${QUIET_LOAD:-2}" 'BEGIN { exit !(b < q) }'
        then
            return
        fi
        sleep 10
        waited=$((waited + 10))
    done
}

rows=0
row() {
    rows=$((rows + 1))
    printf '%s\n%s\n' "$1" "$2" >"$room/row$rows.what"
    shift 2
    for word in "$@"; do
        printf '%s\n' "$word"
    done >"$room/row$rows.argv"
    : >"$room/row$rows.took"
}

once() {
    which=$1
    set --
    while IFS= read -r word; do
        set -- "$@" "$word"
    done <"$room/row$which.argv"
    if ! counted=$(cd "$room" && perf stat -x, -e task-clock "$@" \
                       2>&1 >/dev/null </dev/null); then
        echo failed >>"$room/row$which.took"
        return
    fi
    printf '%s\n' "$counted" |
        awk -F, '$3 ~ /^task-clock/ { print $1 }' >>"$room/row$which.took"
}

kest=$(pwd)/kest
export KEST_LIB="$(pwd)/lib/"
row long "Kest" "$kest" run long.kest
[ -n "${KEST_LUAU:-}" ] && row long "Luau" "$KEST_LUAU" -O2 long.lua
[ -n "${KEST_LUA:-}" ] && row long "Lua 5.4" "$KEST_LUA" long.lua
[ -n "${KEST_LUAJIT:-}" ] && row long "LuaJIT" "$KEST_LUAJIT" long.lua
[ -n "${KEST_QJS:-}" ] && row long "QuickJS" "$KEST_QJS" long.js
[ -n "${KEST_DAS:-}" ] && row long "daslang" "$KEST_DAS" -no-module-cache \
    long.das
row copies "Kest" "$kest" run copies.kest
[ -n "${KEST_CXX:-}" ] && row copies "C++" "$KEST_CXX" -std=c++17 -O0 -c \
    -o copies.o copies.cpp
[ -n "${KEST_RUSTC:-}" ] && row copies "Rust" "$KEST_RUSTC" --edition 2021 \
    -C opt-level=0 --emit=obj -o copies.rs.o copies.rs
[ -n "${KEST_DAS:-}" ] && row copies "daslang" "$KEST_DAS" -no-module-cache \
    copies.das

quiet
round=0
while [ "$round" -lt "$best" ]; do
    ran=1
    while [ "$ran" -le "$rows" ]; do
        once "$ran"
        ran=$((ran + 1))
    done
    round=$((round + 1))
done

{
    printf '# taken\t%s\n' "$(date -u +%Y-%m-%d)"
    printf '# commit\t%s\n' "$(git rev-parse --short HEAD)"
    printf '# machine\t%s\n' \
        "$(awk -F': ' '/^model name/ { print $2; exit }' /proc/cpuinfo)"
    printf '# load\t%s\n' "$(cut -d' ' -f1 /proc/loadavg)"
    printf '# best of\t%s\n' "$best"
    printf '# lines\t%s\n' "$(wc -l <"$room/long.kest")"
    printf '# copies\t%s\n' "$copies"
    printf 'engine\tmeasure\tms\tanswer\n'
} >"$out"
ran=1
while [ "$ran" -le "$rows" ]; do
    measure=$(sed -n 1p "$room/row$ran.what")
    engine=$(sed -n 2p "$room/row$ran.what")
    if grep -q '^failed$' "$room/row$ran.took"; then
        echo "$measure $engine did not run" >&2
    else
        sort -n "$room/row$ran.took" | head -n 1 |
            awk -v m="$measure" -v e="$engine" \
                '{ printf "%s\t%s\t%.2f\t0\n", e, m, $1 }' >>"$out"
    fi
    ran=$((ran + 1))
done

python3 bench/chart.py bench/results.tsv bench
echo "wrote $out"
