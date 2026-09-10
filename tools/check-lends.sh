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

# And what a lend may not hold. Five kinds are a machine word standing for
# something the machine keeps — the bytes of a piece of text, the header of an
# array or a store, the place a reference names, the function a value stands
# for — and three more carry one: a struct, an enum's case, a run of a written
# length and an optional. A host lending any of them would hand over a pointer
# the machine did not put there and cannot take back, so all eight are refused
# and the message names the one that is the machine's own rather than the shape
# that carries it. One of the eight was asked for and seven were not. See D544.
holding="$scratch"/holding
mkdir -p "$holding"
cat > "$holding/holds.kest" <<'EOF'
module holds

struct Carrier {
    n: i32
}

struct WithText {
    t: text
}

struct WithArray {
    xs: [i32]
}

struct WithStore {
    st: store<Carrier>
}

struct WithRef {
    r: ref<Carrier>
}

struct WithFn {
    f: fn(i32) -> i32
}

enum InEnum {
    Held(WithText)
    Nothing
}

struct InRun {
    run: [WithText; 2]
}

struct InOptional {
    maybe: WithText?
}

fn plain(rows: [Carrier]) -> i32 {
    return len(rows)
}

fn withText(x: [WithText]) -> i32 {
    return len(x)
}

fn withArray(x: [WithArray]) -> i32 {
    return len(x)
}

fn withStore(x: [WithStore]) -> i32 {
    return len(x)
}

fn withRef(x: [WithRef]) -> i32 {
    return len(x)
}

fn withFn(x: [WithFn]) -> i32 {
    return len(x)
}

fn inEnum(x: [InEnum]) -> i32 {
    return len(x)
}

fn inRun(x: [InRun]) -> i32 {
    return len(x)
}

fn inOptional(x: [InOptional]) -> i32 {
    return len(x)
}

fn main() -> i32 {
    let z: [Carrier] = array()
    return plain(z)
}
EOF
cat > "$holding.c" <<'EOF'
#include <stdio.h>
#include <string.h>
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
    /* The five kinds and the three that carry one, with what each of them
       holds that the machine keeps: the message names that rather than the
       shape it was found in. */
    static const char *const held[8][2] = {
        {"WithText", "text"},
        {"WithArray", "[i32]"},
        {"WithStore", "store<holds.Carrier>"},
        {"WithRef", "ref<holds.Carrier>"},
        {"WithFn", "fn(i32) -> i32"},
        {"InEnum", "text"},
        {"InRun", "text"},
        {"InOptional", "text"}};
    static unsigned char room[256];
    for (size_t i = 0; i < 8; i++) {
        if (kest_borrow(runtime, room, 1, held[i][0], 8).object != NULL) {
            printf("`%s` was lent and holds the machine's own\n", held[i][0]);
            return 3;
        }
        FILE *why = tmpfile();
        if (why == NULL) {
            return 2;
        }
        kest_report(runtime, why, KEST_FORM_TEXT);
        rewind(why);
        char line[512];
        int named = 0;
        while (fgets(line, sizeof(line), why) != NULL) {
            if (strstr(line, "K0647") != NULL &&
                strstr(line, held[i][1]) != NULL) {
                named = 1;
            }
        }
        fclose(why);
        if (!named) {
            printf("`%s` was refused without naming the `%s` it holds\n",
                   held[i][0], held[i][1]);
            return 3;
        }
    }
    /* And one that holds nothing of the machine's, which is what says the
       eight above are refused for what they hold rather than for being lent. */
    if (kest_borrow(runtime, room, 1, "Carrier", 4).object == NULL) {
        printf("a lend of a shape holding nothing of the machine's was "
               "refused\n");
        return 3;
    }
    return 0;
}
EOF
if ! cc -std=c11 -Wall -Wextra -Werror -Iinclude -o "$holding.host" \
        "$holding.c" libkest.a -lm 2>"$scratch"/why; then
    complain "the host that lends what the machine keeps does not build"
    sed 's/^/    /' "$scratch"/why | head -3
elif ! out=$("$holding.host" "$holding/holds.kest" 2>&1 </dev/null); then
    complain "a lend of what the machine keeps is not refused for what it holds"
    printf '%s\n' "$out" | sed 's/^/    /' | head -3
fi
rm -rf "$holding" "$holding.c" "$holding.host"

if [ $failed -eq 0 ]; then
    echo "a lend names one type or is refused: two of a name, the one that says which, and eight shapes holding what the machine keeps"
fi
exit $failed
