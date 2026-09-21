#!/bin/sh
# One task, one language, one answer: what the hidden checks say about it.
#
#   ai/run.sh cooldown kest ai/tasks/cooldown/kest/done.kest
#   ai/run.sh cooldown luau somewhere/cooldown.lua
#
# What comes back is the number of the first check the answer did not keep, or
# nought for all of them. The checks are not in the directory the answer is
# written in: what a task is judged by is not something the answer can read.
#
# Luau is found rather than built: `KEST_LUAU` says where it is, and without
# it the Luau half of a task cannot be run here. The Kest half needs `./kest`,
# which `make` builds.
set -u
if [ $# -ne 3 ]; then
    echo "usage: ai/run.sh <task> <kest|luau> <the answer>"
    exit 2
fi
task=$1
language=$2
answer=$3
here=$(dirname "$0")
tasks="$here"/tasks
if [ ! -d "$tasks/$task" ]; then
    echo "there is no task called \`$task\` in $tasks"
    exit 2
fi
if [ ! -f "$answer" ]; then
    echo "there is no answer at \`$answer\`"
    exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

case "$language" in
kest)
    if [ ! -x ./kest ]; then
        echo "the compiler is not built; \`make\` first"
        exit 2
    fi
    cp "$answer" "$work"/"$task".kest
    cp "$tasks/$task"/kest/checks.kest "$work"/checks.kest
    ./kest run "$work"/checks.kest >/dev/null 2>"$work"/said
    said=$?
    # A program that would not compile is not a failing check: it is an
    # answer the compiler refused, which is the thing a task is measuring.
    if grep -q '^error' "$work"/said; then
        echo "refused"
        sed 's/^/    /' "$work"/said | head -12
        exit 1
    fi
    echo "$said"
    exit 0
    ;;
luau)
    if [ -z "${KEST_LUAU:-}" ] || [ ! -x "${KEST_LUAU:-}" ]; then
        echo "no luau here; set KEST_LUAU to where it is"
        exit 2
    fi
    cp "$answer" "$work"/"$task".lua
    cp "$tasks/$task"/luau/checks.lua "$work"/checks.lua
    ( cd "$work" && "$KEST_LUAU" checks.lua ) >"$work"/out 2>"$work"/said
    if ! grep -q '^checks ' "$work"/out; then
        echo "refused"
        sed 's/^/    /' "$work"/said | head -12
        exit 1
    fi
    sed -n 's/^checks //p' "$work"/out
    exit 0
    ;;
*)
    echo "the languages are \`kest\` and \`luau\`"
    exit 2
    ;;
esac
