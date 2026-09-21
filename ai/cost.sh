#!/bin/sh
# What a mistake costs whoever made it, in the two languages, with no model
# anywhere near it. Four things per task and language:
#
#   check     how long the language takes to say nothing is wrong about the
#             answer that is right -- `kest check` against `luau-analyze`,
#             which is the loop somebody edits in
#   caught    `compiler` or `test`: which of the two said the wrong answer was
#             wrong. A compiler costs a turn; a test costs a run
#   told      how long being told takes: the same one-file command where the
#             compiler catches it, and the whole run of the hidden tests where
#             only a test does
#   named     where a compiler caught it, whether the line it points at is one
#             of the lines that have to change for the answer to be right --
#             which is the difference between a diagnostic that repairs and
#             one that only complains
#
# Milliseconds are the least of three batches of `RUNS` runs, because this is
# a shared box and the least disturbed sample is the one worth reading. They
# are wall clock and carry a process launch each, which is what somebody
# waiting for an answer actually waits for.
#
# This measures; it does not check. `tools/check-ai.sh` is what holds the
# suite to being a suite, and this is read beside it. See D1126.
set -u
here=$(dirname "$0")
cd "$here/.." || exit 1
if [ ! -x ./kest ]; then
    echo "the compiler is not built; \`make\` first"
    exit 1
fi
runs=${RUNS:-20}

# The lines of `astray` that are not in `done`: what has to change. `diff`
# says `30,32c30` for a run of them, so a range is every line in it and not
# the one it starts at.
changed() {
    diff "$1" "$2" 2>/dev/null |
        sed -n 's/^\([0-9]*\),\([0-9]*\)[cd].*$/\1 \2/p;s/^\([0-9]*\)[cd].*$/\1 \1/p' |
        while read -r from to; do
            at=$from
            while [ "$at" -le "$to" ]; do
                echo "$at"
                at=$((at + 1))
            done
        done
}

# `each` and not `one`: the sweep below walks tasks in `one`, and a function
# in a shell shares the caller's names.
inside() {
    wanted=$1
    shift
    for each in "$@"; do
        if [ "$each" = "$wanted" ]; then
            return 0
        fi
    done
    return 1
}

# Tenths of a millisecond for one run, the least of three batches.
took() {
    least=0
    batch=0
    while [ "$batch" -lt 3 ]; do
        started=$(date +%s%N)
        at=0
        while [ "$at" -lt "$runs" ]; do
            "$@" >/dev/null 2>&1
            at=$((at + 1))
        done
        ended=$(date +%s%N)
        this=$(((ended - started) / runs / 100000))
        if [ "$batch" -eq 0 ] || [ "$this" -lt "$least" ]; then
            least=$this
        fi
        batch=$((batch + 1))
    done
    echo "$least"
}

milliseconds() {
    printf '%d.%d' $(($1 / 10)) $(($1 % 10))
}

printf '%-9s %-5s %6s  %-8s %6s  %s\n' task language check caught told named
kest_checks=""
luau_checks=""
for one in $(find ai/tasks -mindepth 1 -maxdepth 1 -type d | sort); do
    name=$(basename "$one")
    for language in kest luau; do
        case $language in
        kest) suffix=kest ;;
        luau) suffix=lua ;;
        esac
        astray="$one/$language/astray.$suffix"
        answer="$one/$language/done.$suffix"
        [ -f "$astray" ] || continue
        if [ "$language" = luau ] &&
                { [ -z "${KEST_LUAU:-}" ] || [ ! -x "${KEST_LUAU:-}" ]; }; then
            printf '%-9s %-5s %6s  %-8s %6s  %s\n' "$name" "$language" "--" \
                "--" "--" "no luau here"
            continue
        fi
        case $language in
        kest) checking=$(took ./kest check "$answer") ;;
        luau) checking=$(took "$KEST_LUAU"-analyze "$answer") ;;
        esac
        case $language in
        kest) kest_checks="$kest_checks $checking" ;;
        luau) luau_checks="$luau_checks $checking" ;;
        esac
        said=$(./ai/run.sh "$name" "$language" "$astray" 2>/dev/null | head -1)
        case "$said" in
        refused*)
            caught=compiler
            case $language in
            kest)
                where=$(./kest check "$astray" 2>&1 |
                    sed -n 's/^ *--> .*:\([0-9]*\):[0-9]*$/\1/p' | head -1)
                told=$(took ./kest check "$astray")
                ;;
            luau)
                where=$("$KEST_LUAU"-analyze "$astray" 2>&1 |
                    sed -n 's/^[^(]*(\([0-9]*\),[0-9]*).*$/\1/p' | head -1)
                told=$(took "$KEST_LUAU"-analyze "$astray")
                ;;
            esac
            if [ -n "$where" ] && inside "$where" $(changed "$astray" "$answer")
            then
                named="line $where, one of the lines that have to change"
            else
                named="line ${where:-none}"
            fi
            ;;
        *)
            caught=test
            told=$(took ./ai/run.sh "$name" "$language" "$astray")
            named="check $said"
            ;;
        esac
        printf '%-9s %-5s %6s  %-8s %6s  %s\n' "$name" "$language" \
            "$(milliseconds "$checking")" "$caught" \
            "$(milliseconds "$told")" "$named"
    done
done

# The middle of each set of checks, which is the loop somebody edits in.
middle() {
    set -- $(printf '%s\n' $1 | sort -n)
    if [ $# -eq 0 ]; then
        echo 0
        return
    fi
    at=$((($# + 1) / 2))
    shift $((at - 1))
    echo "$1"
}
printf '\nthe middle of being told nothing is wrong: %s ms by `kest check`' \
    "$(milliseconds "$(middle "$kest_checks")")"
if [ -n "$luau_checks" ]; then
    printf ', %s ms by `luau-analyze`' \
        "$(milliseconds "$(middle "$luau_checks")")"
fi
printf '\n'

# And what starting the program costs, so that what is left is the reading.
# Neither of these reads a file: `kest --version` says what it is, and
# `luau-analyze` with no file says it wants one.
floor_kest=$(took ./kest --version)
printf 'of which starting it is %s ms for `kest`' \
    "$(milliseconds "$floor_kest")"
if [ -n "$luau_checks" ]; then
    floor_luau=$(took "$KEST_LUAU"-analyze)
    printf ' and %s ms for `luau-analyze`' "$(milliseconds "$floor_luau")"
fi
printf '\n'
