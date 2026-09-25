#!/bin/sh
# The library and the command line built as WebAssembly, and every example run
# by it for the same words and the same status the machine built for this one
# answers. A fault in the machine built this way stays inside WebAssembly's own
# memory, which is what a host that runs it inside a WebAssembly runtime gets
# for the cost D1255 measured; what is held here is that it is the same
# language there. See D1255.
set -u
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
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
for program in examples/*.kest; do
    grep -q '^fn main(' "$program" || continue
    native=$(./kest run "$program" 2>/dev/null </dev/null; echo "status $?")
    inside=$(KEST_LIB="$here"/lib/ node tools/wasi-run.mjs \
                 "$scratch"/kest.wasm run "$here/$program" \
                 2>/dev/null </dev/null; echo "status $?")
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
if [ $failed -eq 0 ]; then
    echo "the library and the command line build as WebAssembly with no" \
         "warning, and $same example(s) run by it say the same words and" \
         "come back the same as here"
fi
exit $failed
