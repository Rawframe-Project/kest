#!/bin/sh
# What a host says when it lends, and what the machine says back. A lend is an
# address, a count and a name: the first two are held where they can be, and
# this is the third. A name that means two types is a lend of whichever was
# found first unless something refuses it, and nothing in this tree is a
# program with two of a name — every example is one module — so the program is
# written here.
set -u

# A scratch of this run's own, because two runs of this can happen at once when
# the backstops put one out of order while another is being asked.
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cd "$(dirname "$0")/.." || exit 1
failed=0

complain() {
    printf '%s\n' "$1"
    failed=1
}

# A name that is two types, which is what a host writing `Row` has when two
# modules declare one. Nothing in this tree is such a program — every example
# is one module — so the program is three files written here, and the host that
# lends to it is ten lines. What it holds is that a lend of a name meaning two
# things is refused with both of them pointed at, and that the name a host is
# told to write instead is one that works.
naming="$scratch"/lends
mkdir -p "$naming"
cat > "$naming/first.kest" <<'EOF'
module first

struct Row {
    n: i32
}

fn count(rows: [Row]) -> i32 {
    return len(rows)
}
EOF
cat > "$naming/second.kest" <<'EOF'
module second

struct Row {
    n: i32
    m: i32
}

fn count(rows: [Row]) -> i32 {
    return len(rows)
}
EOF
cat > "$naming/both.kest" <<'EOF'
module both

import first

import second

fn main() -> i32 {
    let a: [first.Row] = array()
    let b: [second.Row] = array()
    return first.count(a) + second.count(b)
}
EOF
cat > "$naming.c" <<'EOF'
#include <stdio.h>
#include "kest.h"

int main(int argc, char **argv) {
    (void)argc;
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    KestRuntime *runtime = kest_start(build, host, NULL);
    kest_host_free(host);
    if (runtime == NULL) {
        return 2;
    }
    int32_t rows[2] = {1, 2};
    if (kest_borrow(runtime, rows, 2, "Row", sizeof(rows[0])).object != NULL) {
        printf("a lend of a name that is two types was taken\n");
        return 3;
    }
    if (kest_borrow(runtime, rows, 2, "first.Row", sizeof(rows[0])).object
        == NULL) {
        printf("a lend of the name that says which was refused\n");
        kest_report(runtime, stdout, KEST_FORM_TEXT);
        return 3;
    }
    return 0;
}
EOF
if ! cc -std=c11 -Wall -Wextra -Werror -Iinclude -o "$naming.host" \
        "$naming.c" libkest.a -lm 2>"$scratch"/why; then
    complain "the host that lends by name does not build"
    sed 's/^/    /' "$scratch"/why | head -3
elif ! out=$("$naming.host" "$naming/both.kest" 2>&1 </dev/null); then
    complain "a name that is two types is not two types to a lend"
    printf '%s\n' "$out" | sed 's/^/    /' | head -3
fi
rm -rf "$naming" "$naming.c" "$naming.host"

if [ $failed -eq 0 ]; then
    echo "a lend names one type or is refused: two of a name, and the one that says which"
fi
exit $failed
