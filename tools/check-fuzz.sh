#!/bin/sh
# The boundaries a stranger's bytes arrive through, under the checking build:
# eight seeds of four hundred inputs over each of the nine, which is a minute
# rather than an afternoon. A long campaign is the same fuzzer with other
# numbers. What each boundary holds is written in tools/fuzz.c; what they share
# is that every input ends in an answer or a refusal, which is what not
# crashing looks like from outside. Then the source boundary again with the
# lowering's fusions and the IR optimizer turned off: what it folds is what
# every one of those programs answered, so three runs that fold to the same
# number are three runs where neither changed anything a program can see.
set -u

# What the build made: the preset's build directory, out/dev unless
# KEST_BUILD names another.
build_dir=${KEST_BUILD:-out/dev}
case $build_dir in /*) ;; *) build_dir=$(pwd)/$build_dir ;; esac

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cd "$(dirname "$0")/.." || exit 1

fuzzer=$build_dir/tools/fuzz-debug
if [ ! -x "$fuzzer" ]; then
    echo "fuzzing: there is no checking fuzzer in $build_dir to run"
    exit 1
fi

# One boundary each, side by side: none of them reads what another writes.
for what in source handles lends refs text migrate chunks fmt manifest; do
    (
        for seed in 1 2 3 4 5 6 7 8; do
            said=$("$fuzzer" "$seed" 400 "$scratch/$what-$seed.kest" "$what" \
                2>&1) || {
                printf '%s at seed %s stopped it\n' "$what" "$seed"
                continue
            }
            case "$said" in
            *"none of them stopped this"*) echo 400 ;;
            *) printf '%s at seed %s: %s\n' "$what" "$seed" \
                "$(printf '%s' "$said" | head -3)" ;;
            esac
        done >"$scratch/$what.said"
    ) &
done
# And the source boundary compiled the other two ways, side by side with them.
for how in fused plain bare; do
    (
        case $how in
        plain) export KEST_PLAIN=1 ;;
        bare) export KEST_NOOPT=1 ;;
        esac
        for seed in 1 2 3 4 5 6 7 8; do
            "$fuzzer" "$seed" 400 "$scratch/folding-$how-$seed.kest" source \
                2>/dev/null | sed -n 's|.*folds to ||p'
        done >"$scratch/folded-$how"
    ) &
done
wait

fuzzed=$(cat "$scratch"/*.said | grep -cx 400)
wrong=$(cat "$scratch"/*.said | grep -vx 400 | head -5)
failed=0
folded=$(cat "$scratch/folded-fused")
if [ -z "$folded" ] || [ "$folded" != "$(cat "$scratch/folded-plain")" ]; then
    echo "fuzzing: programs nobody wrote answer something else when the" \
         "lowering's fusions are turned off"
    failed=1
fi
if [ -z "$folded" ] || [ "$folded" != "$(cat "$scratch/folded-bare")" ]; then
    echo "fuzzing: programs nobody wrote answer something else when the" \
         "optimizer is turned off"
    failed=1
fi
if [ -n "$wrong" ]; then
    echo "fuzzing: bytes this compiler was not written for stopped it:"
    printf '%s\n' "$wrong" | sed 's/^/    /'
    failed=1
fi
if [ "$failed" -ne 0 ]; then
    exit 1
fi
echo "fuzzing: $((fuzzed * 400)) input(s) made from eight seeds over nine" \
     "boundaries, every one of them an answer or a refusal under a build that" \
     "checks itself, and the source ones folding to the same answers compiled" \
     "the other two ways"
