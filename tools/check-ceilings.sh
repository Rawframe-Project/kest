#!/bin/sh
# What a program can be told it has stops at 2147483647, because `len` gives
# back an `i32`. Two of the three refusals that says take a minute and four
# gigabytes to reach, and the third — a store's — wants thirty-two gigabytes,
# since a slot is sixteen bytes before the three arrays beside it. So they are
# messages nobody has seen, which is the same as no message: the one that
# crashed instead of saying anything was found by running it, not by reading
# it.
#
# Here the ceiling is lowered in a copy of the tree and the three are reached
# in a hundred lines of work each. What is being held is that every one of
# them is a message with the number in it, at the line that asked, rather than
# a wrap, a truncation, or a machine that stops.
set -u

# A scratch of this run's own. Two of these run at once when the backstops put
# one out of order while another is being asked, and fixed names in `/tmp` are
# two runs writing to one file.
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cd "$(dirname "$0")/.." || exit 1

# The tree's own objects come along, so that lowering one number rebuilds one
# file rather than sixteen. What makes that safe is the `.d` files beside them:
# the build says what each object was made from, so an object older than what
# it was made from is made again. The tree is built first for the same reason,
# since objects that are behind the source they were made from would be a copy
# that is neither.
if ! make -s kest >"$scratch"/ceilings-why 2>&1; then
    echo "ceilings: the tree does not build"
    sed 's/^/    /' "$scratch"/ceilings-why | head -5
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for what in src include lib Makefile build libkest.a; do
    cp -a "$what" "$work" || exit 1
done

was='#define MAX_COUNTED INT32_MAX'
if ! grep -q "$was" "$work/src/vm.c"; then
    echo "ceilings: the ceiling this lowers has moved"
    exit 1
fi
sed -i "s/$was/#define MAX_COUNTED 100/" "$work/src/vm.c"
if ! make -C "$work" -s kest >"$scratch"/ceilings-why 2>&1; then
    echo "ceilings: the tree with a lower ceiling does not build"
    sed 's/^/    /' "$scratch"/ceilings-why | head -5
    exit 1
fi

cat > "$work/counting.kest" <<'KEST'
fn main() -> i32 {
    let xs: [i8] = array()
    let i = 0
    while i < 200 {
        push(xs, i8(1))
        i += 1
    }
    return 0
}
KEST

cat > "$work/holding.kest" <<'KEST'
struct Npc {
    n: i32
}

fn main() -> i32 {
    let world: store<Npc> = store()
    let i = 0
    while i < 200 {
        add(world, Npc(i))
        i += 1
    }
    return 0
}
KEST

cat > "$work/joining.kest" <<'KEST'
fn main() -> i32 {
    let t = "01234567"
    let i = 0
    while i < 8 {
        t = "{t}{t}"
        i += 1
    }
    return 0
}
KEST

failed=0
reached=0
# Each of the three, and the words it has to say: the number a program can be
# told it has, at the line that asked for one more.
for one in "counting:this array holds 100" \
           "holding:this store holds 100" \
           "joining:this text would hold 128"; do
    file=${one%%:*}
    said_it=${one#*:}
    out=$("$work/kest" run "$work/$file.kest" 2>&1 </dev/null)
    if printf '%s' "$out" | grep -q K0630 &&
       printf '%s' "$out" | grep -qF "$said_it"; then
        reached=$((reached + 1))
    else
        echo "ceilings: $file.kest was not told it had reached the ceiling"
        printf '%s\n' "$out" | sed 's/^/    /' | head -6
        failed=1
    fi
done

if [ $failed -eq 0 ]; then
    echo "every ceiling is a message at the line that asked: $reached"
fi
exit $failed
