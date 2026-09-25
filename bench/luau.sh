#!/bin/sh
# Luau's own benchmarks beside the same work written in Kest. The workloads
# under `bench` are this project's choice; these are Luau's -- from the
# `bench/tests` of its own repository, run there by its own `bench_support.lua`
# -- and `bench/luau/` is each written in Kest with the same sizes, the same
# section timed, twenty-four runs and the four slowest thrown away, and said
# the way `bench_support.lua` says it. What is printed is the middle of what is
# left, in milliseconds: Luau's interpreter at -O2, its native tier, this
# language's machine, and its release engine. Each Kest one says a duration only
# when it is handed `time`; without it it does the work once and says whether it
# came out right, which is all the gate asks of it. Not part of the gate: a
# duration is not a pass or a fail. See D1267.
#
#   KEST_LUAU=path/to/luau LUAU_BENCH=path/to/luau/bench sh bench/luau.sh
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
if [ -z "${KEST_LUAU:-}" ] || [ ! -x "${KEST_LUAU:-}" ] ||
        [ -z "${LUAU_BENCH:-}" ] || [ ! -d "${LUAU_BENCH:-}/tests" ]; then
    echo "set KEST_LUAU to a luau binary and LUAU_BENCH to its bench directory"
    exit 2
fi
room=$(mktemp -d)
trap 'rm -rf "$room"' EXIT
middle() {
    tr '|' '\n' | grep -E '^[0-9.]+$' | sort -g |
        awk '{ at[NR] = $1 } END { if (NR) print at[int((NR + 1) / 2)] }'
}
printf 'test\tluau -O2\tluau --codegen\tkest\tkest release\n'
for program in "$here"/bench/luau/*.kest; do
    name=$(basename "$program" .kest)
    interpreted=$(cd "$LUAU_BENCH" && "$KEST_LUAU" -O2 "tests/$name.lua" |
                  middle)
    native=$(cd "$LUAU_BENCH" &&
             "$KEST_LUAU" -O2 --codegen "tests/$name.lua" | middle)
    machine=$("$here"/kest run "$program" -- time | middle)
    (cd "$room" && KEST_LIB="$here"/lib/ "$here"/kest build --release \
        "$program" >/dev/null 2>&1)
    release=$("$room/$name" time | middle)
    printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$interpreted" "$native" \
        "$machine" "$release"
done
