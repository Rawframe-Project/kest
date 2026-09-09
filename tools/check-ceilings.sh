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

# And the numbers a program runs into while it is being compiled, which are the
# other half of the same table: every row of "What there is a most of" in the
# reference is a message somebody meets the day they write one too many, and
# not one of them had ever been met here. The list is held to the table — a row
# nothing runs into is a message nobody has seen — and each message has to say
# the number the table prints, which keeps the define, the table and the words
# in step.
python3 - "$PWD" > "$scratch/limits" 2>&1 <<'LIMITS'
import os
import re
import subprocess
import sys

WHERE = sys.argv[1]


def defers():
    return "fn note(n: i32) {\n}\n\nfn main() -> i32 {\n%s\n    return 0\n}\n" % (
        "\n".join("    defer note(%d)" % i for i in range(33)))


def names():
    return "fn main() -> i32 {\n%s\n    return 0\n}\n" % (
        "\n".join("    let n%d = %d" % (i, i) for i in range(300)))


def loops():
    out = ""
    for i in range(17):
        out += "    " * (i + 1) + "while true {\n"
    out += "    " * 18 + "break\n"
    for i in range(17, 0, -1):
        out += "    " * i + "}\n"
    return "fn main() -> i32 {\n%s    return 0\n}\n" % out


def breaks():
    return ("fn main() -> i32 {\n    let n = 0\n    while true {\n%s\n"
            "        break\n    }\n    return 0\n}\n"
            % "\n".join("        if n == %d { break }" % i for i in range(33)))


def reaches():
    body = "\n".join("        n += %d" % (i % 7) for i in range(20000))
    return ("fn main() -> i32 {\n    let n = 0\n    while n < 1 {\n%s\n"
            "        n += 1\n    }\n    return 0\n}\n" % body)


def subjects():
    return ("fn main() -> i32 {\n    let a = true\n    return match %s {\n"
            "        %s -> 0\n    }\n}\n"
            % (", ".join("a" for _ in range(9)),
               ", ".join("_" for _ in range(9))))


def combinations():
    return ("enum Four {\n%s\n}\n\nfn main() -> i32 {\n    let a = Four.C0\n"
            "    return match %s {\n        %s -> 0\n    }\n}\n"
            % ("\n".join("    C%d" % i for i in range(4)),
               ", ".join("a" for _ in range(5)),
               ", ".join("_" for _ in range(5))))


def elements():
    return ("struct Big {\n    cells: [i32; 65536]\n}\n\nfn main() -> i32 {\n"
            "    let b: Big = Big(array(4, 0))\n    return 0\n}\n")


# One a row of the table, found by a phrase out of the row itself, so a row
# nobody has written a program for is a row this names.
PROBES = [
    ("names in a function", names, "K0502", "256"),
    ("loops one inside another", loops, "K0502", "16"),
    ("`break`s in one loop", breaks, "K0502", "32"),
    ("`defer`s in a function", defers, "K0502", "32"),
    ("bytes of code a jump reaches", reaches, "K0503", "65535"),
    ("things one `match` chooses between", subjects, "K0339", "8"),
    ("combinations one `match` answers", combinations, "K0333", "256"),
    ("elements a `[T; N]` holds", elements, "K0326", "65535"),
]

# The one that is not met while compiling: what `len` counts to is a refusal
# the machine makes, and the three probes above reach it with the ceiling
# lowered in a tree of their own.
WHILE_RUNNING = "elements an array or a store holds"

table = re.search(r"## What there is a most of(.*?)\n```",
                  open(os.path.join(WHERE, "docs/language.md")).read(), re.S)
rows = re.findall(r"\n\| (\d+) \| ([^|]+) \|", table.group(1))
failed = 0
for number, what in rows:
    if WHILE_RUNNING in what:
        continue
    if not any(phrase in what for phrase, _, _, _ in PROBES):
        print("limits: nothing runs into `%s`, so its message is one nobody "
              "has seen" % what.strip())
        failed = 1

met = 0
for phrase, program, code, number in PROBES:
    path = os.path.join(WHERE, "one-too-many.kest")
    open(path, "w").write(program())
    said = subprocess.run([os.path.join(WHERE, "kest"), "emit", path],
                          capture_output=True, text=True,
                          stdin=subprocess.DEVNULL)
    out = said.stdout + said.stderr
    os.remove(path)
    if code not in out or number not in out:
        print("limits: one too many `%s` is not `%s` with %s in it; it said %r"
              % (phrase, code, number, out.strip()[:120]))
        failed = 1
    else:
        met += 1

print("met %u" % met)
sys.exit(failed)

LIMITS
if [ $? -ne 0 ]; then
    sed '/^met /d' "$scratch/limits"
    exit 1
fi
met=$(sed -n 's/^met //p' "$scratch/limits")

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
    echo "every ceiling is a message at the line that asked:" \
         "$reached while running, $met while compiling"
fi
exit $failed
