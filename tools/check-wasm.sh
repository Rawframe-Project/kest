#!/bin/sh
# The library and the command line built as WebAssembly, and every example run
# by it for the same words and the same status the machine built for this one
# answers. A fault in the machine built this way stays inside WebAssembly's own
# memory, which is what a host that runs it inside a WebAssembly runtime gets
# for the cost D1255 measured; what is held here is that it is the same
# language there. See D1255.
set -u
scratch=$(mktemp -d)
# And a day `examples/colony.kest` left at the root of the machine, which a
# build that forgets where it was run writes as whoever may write there: this
# run's to take away, and only if this run put it there. See D1255.
at_the_root=false
[ -e /kest-colony-day.txt ] && at_the_root=true
trap 'rm -rf "$scratch"; [ "$at_the_root" = false ] && rm -f /kest-colony-day.txt' EXIT
cd "$(dirname "$0")/.." || exit 1
here=$(pwd)

if [ ! -x ./kest ]; then
    echo "wasm: the compiler is not built"
    exit 1
fi
# What makes one: clang for the target, a WebAssembly linker, the C library
# WebAssembly's system interface has, and Node to run it. Each missing one is
# said, because a gate that passes for having found nothing to build has
# built nothing.
linker=$(command -v wasm-ld 2>/dev/null ||
         ls /usr/bin/wasm-ld-* 2>/dev/null | sort -V | tail -1)
if [ -z "$linker" ] || ! command -v clang >/dev/null 2>&1 ||
        ! command -v node >/dev/null 2>&1; then
    echo "wasm: clang, a WebAssembly linker and node are what this builds" \
         "and runs with, and one of them is not here"
    exit 1
fi
mkdir -p "$scratch"/objects
for source in src/*.c; do
    if ! clang --target=wasm32-wasi -std=c11 -O2 -Wall -Wextra -Werror \
            -Iinclude -DKEST_LIB_DIR='"/lib/kest/"' \
            -c -o "$scratch/objects/$(basename "$source" .c).o" "$source" \
            2>"$scratch"/why; then
        echo "wasm: \`$source\` does not compile for WebAssembly:"
        sed 's/^/    /' "$scratch"/why | head -4
        exit 1
    fi
done
if ! clang --target=wasm32-wasi -O2 -fuse-ld="$linker" \
        -o "$scratch"/kest.wasm "$scratch"/objects/*.o -lm \
        2>"$scratch"/why; then
    echo "wasm: the command line does not link for WebAssembly:"
    sed 's/^/    /' "$scratch"/why | head -4
    exit 1
fi

failed=0
same=0
# A program that writes a file where it was run from, which a module there
# does from `/` unless it is told otherwise: here that was the root of the
# machine, written as whoever ran it and refused where that is nobody's to
# write, so the one run that could tell the two apart is the file being where
# it was run. See D1255.
rm -f kest-colony-day.txt
KEST_LIB="$here"/lib/ node tools/wasi-run.mjs "$scratch"/kest.wasm run \
    examples/colony.kest >/dev/null 2>&1 </dev/null
if [ ! -f kest-colony-day.txt ]; then
    echo "wasm: \`examples/colony.kest\` wrote its day somewhere other than" \
         "where it was run"
    failed=1
fi
rm -f kest-colony-day.txt
# Each run in a room of this check's own: a program that writes a file where it
# was run writes it there, and two checks running the colony in the tree at
# once read each other's day half written -- `status 12` from the one here and
# nought from the page, which has no disk to share. See D1268.
mkdir -p "$scratch"/runs
for program in examples/*.kest; do
    grep -q '^fn main(' "$program" || continue
    native=$(cd "$scratch"/runs && KEST_LIB="$here"/lib/ "$here"/kest run \
                 "$here/$program" 2>/dev/null </dev/null; echo "status $?")
    inside=$(cd "$scratch"/runs && KEST_LIB="$here"/lib/ node \
                 "$here"/tools/wasi-run.mjs "$scratch"/kest.wasm run \
                 "$here/$program" 2>/dev/null </dev/null; echo "status $?")
    if [ "$native" != "$inside" ]; then
        echo "wasm: \`$program\` answered" \
             "\`$(printf '%s' "$inside" | tail -1)\` as WebAssembly and" \
             "\`$(printf '%s' "$native" | tail -1)\` here"
        failed=1
    else
        same=$((same + 1))
    fi
done
if [ $same -eq 0 ]; then
    echo "wasm: no example was run both ways"
    failed=1
fi
# And the page: the same module under the system interface the playground
# gives it in a browser, which is not Node's, over every example that is one
# file -- what somebody types into the page is one file. See D1266.
paged=0
for program in examples/*.kest; do
    grep -q '^fn main(' "$program" || continue
    grep -q '^import examples\.' "$program" && continue
    native=$(cd "$scratch"/runs && KEST_LIB="$here"/lib/ "$here"/kest run \
                 "$here/$program" 2>/dev/null </dev/null; echo "status $?")
    page=$(node tools/page-run.mjs "$scratch"/kest.wasm "$program" \
               2>/dev/null </dev/null; echo "status $?")
    if [ "$native" != "$page" ]; then
        echo "wasm: \`$program\` answered" \
             "\`$(printf '%s' "$page" | tail -1)\` in the playground and" \
             "\`$(printf '%s' "$native" | tail -1)\` here"
        failed=1
    else
        paged=$((paged + 1))
    fi
done
if [ $paged -eq 0 ]; then
    echo "wasm: no example was run in the playground"
    failed=1
fi
if [ $failed -eq 0 ]; then
    echo "the library and the command line build as WebAssembly with no" \
         "warning, and $same example(s) run by it say the same words and" \
         "come back the same as here, $paged of them in the playground too"
fi
exit $failed
