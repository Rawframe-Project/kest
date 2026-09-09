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

# Inside this run's own scratch rather than beside it: a second `trap ... EXIT`
# replaces the first rather than adding to it, so a check with two of them
# takes one of its two rooms away and leaves the other on the machine it ran
# on. This one left nine hundred of them.
work="$scratch"/work
mkdir "$work"
# What nothing writes into is the same bytes under another name where the
# machine allows a name to be that, and a copy where it does not. What a build
# writes into is copied whatever the machine allows, because a compiler opens
# its output and cuts it short. The number below is lowered with `sed -i`,
# which writes a new file and moves it over the old name rather than opening
# that name, so the tree's own is left as it is either way.
for what in src include lib Makefile; do
    cp -al "$what" "$work" 2>/dev/null || cp -a "$what" "$work" || exit 1
done
# Only the objects this builds: `kest` is a release build, and the sanitised
# ones are nine megabytes of something nothing here asks for.
mkdir -p "$work/build"
cp -a build/release "$work/build/release" || exit 1
cp -a libkest.a "$work" || exit 1

was='#define MAX_COUNTED INT32_MAX'
if ! grep -q "$was" "$work/src/vm.c"; then
    echo "ceilings: the ceiling this lowers has moved"
    exit 1
fi
sed -i "s/$was/#define MAX_COUNTED 100/" "$work/src/vm.c"

# And the other number a machine runs out of: how many places in stores it can
# tell apart. A reference carries the stamp its slot was handed out with, and
# after four thousand million of them a stamp handed out again would make a
# reference from the first occupant read as the newest one. Lowered here for
# the same reason the other is: nobody is adding four thousand million things
# to a store to watch it.
stamped='#define MOST_STAMPS 0xffffffffu'
if ! grep -q "$stamped" "$work/src/vm.c"; then
    echo "ceilings: the number of places a machine tells apart has moved"
    exit 1
fi
sed -i "s/$stamped/#define MOST_STAMPS 1000u/" "$work/src/vm.c"
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


def continues():
    return ("fn main() -> i32 {\n    let n = 0\n    while n < 100 {\n%s\n"
            "        n += 1\n    }\n    return 0\n}\n"
            % "\n".join("        if n == %d {\n            n += 1\n"
                        "            continue\n        }" % i
                        for i in range(33)))


def reaches():
    body = "\n".join("        n += %d" % (i % 7) for i in range(20000))
    return ("fn main() -> i32 {\n    let n = 0\n    while n < 1 {\n%s\n"
            "        n += 1\n    }\n    return 0\n}\n" % body)


def jumps():
    body = "\n".join("        n += %d" % (i % 7) for i in range(20000))
    return ("fn main() -> i32 {\n    let n = 0\n    if n == 0 {\n%s\n"
            "    }\n    return 0\n}\n" % body)


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
    # The other row with two sentences in it: what a loop holds of each.
    ("`break`s in one loop", breaks, "K0502", "32"),
    ("`break`s in one loop", continues, "K0502", "32"),
    ("`defer`s in a function", defers, "K0502", "32"),
    # Two sentences under one row: what a loop reaches back over, and what a
    # jump reaches forward over. Meeting one of them is not meeting the other.
    ("bytes of code a jump reaches", reaches, "K0503", "65535"),
    ("bytes of code a jump reaches", jumps, "K0503", "65535"),
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
# It adds and gives back rather than filling one, because what runs out is how
# many places a machine has handed out and not how many a store holds: a store
# of one, filled and emptied a thousand times, is a thousand stamps and one
# slot.
cat > "$work/stamping.kest" <<'KEST'
struct Thing {
    n: i32
}

fn main() -> i32 {
    let s: store<Thing> = store()
    for i in 0..1001 {
        let one = add(s, Thing(i))
        remove(s, one)
    }
    return len(s)
}
KEST

for one in "stamping:this machine has handed out 1000 places in stores" \
           "counting:this array holds 100" \
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

# And the two a machine has rather than a program: how deep calls may nest and
# how much stack there is. Neither needs a lowered ceiling — a program reaches
# both in a moment — and neither was reached by anything here, so the two
# messages a host is likeliest to meet were the two nobody had seen.
cat > "$work/nesting.kest" <<'KEST'
fn down(n: i32) -> i32 {
    if n <= 0 {
        return 0
    }
    return 1 + down(n - 1)
}

fn main() -> i32 {
    return down(100000)
}
KEST

# A frame wide enough that the stack runs out before the nesting does, which is
# the same ceiling met from the other side: what a call needs is what it holds
# and not how many of it there are.
{
    echo 'fn down(n: i32) -> i32 {'
    at=0
    while [ $at -lt 120 ]; do
        echo "    let a$at = n + $at"
        at=$((at + 1))
    done
    echo '    if n <= 0 {'
    echo '        return a0'
    echo '    }'
    echo '    return a119 + down(n - 1)'
    echo '}'
    echo
    echo 'fn main() -> i32 {'
    echo '    return down(100000)'
    echo '}'
} > "$work/holding.kest"

for one in "nesting:calls nest more than 1024 deep" \
           "holding:out of stack"; do
    file=${one%%:*}
    said_it=${one#*:}
    out=$(./kest run "$work/$file.kest" 2>&1 </dev/null)
    if printf '%s' "$out" | grep -q K0602 &&
       printf '%s' "$out" | grep -qF "$said_it"; then
        reached=$((reached + 1))
    else
        echo "ceilings: $file.kest was not told what the machine has"
        printf '%s\n' "$out" | sed 's/^/    /' | head -6
        failed=1
    fi
done

# And the sixth, which is the one this project talks about most: the heap a
# host says the program may have. There is no way to reach it from a command
# line — how much heap a program may have is a host's to choose and this one
# does not choose — so the host that reaches it is written here, the way
# `check.sh` writes the one that asks what came back before anything did.
cat > "$work/spending.kest" <<'KEST'
fn main() -> i32 {
    let many: [i32] = array()
    for i in 0..400000 {
        push(many, i)
    }
    return len(many)
}
KEST

cat > "$work/spending.c" <<'HOST'
#include <stdio.h>
#include "kest.h"

int main(int argc, char **argv) {
    (void)argc;
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    /* Small enough to be spent while the numbers are still small, so what the
       message says about what was growing is a number a reader can hold. */
    KestLimits limits = {0, 0, 65536};
    KestRuntime *runtime = kest_start(build, host, &limits);
    kest_host_free(host);
    if (runtime == NULL) {
        return 2;
    }
    KestValue frame[2] = {{0}};
    if (kest_call(runtime, kest_entry(runtime, "main"), frame, 2)) {
        return 3;
    }
    kest_report(runtime, stdout, KEST_FORM_TEXT);
    return 0;
}
HOST

if ! ${CC:-cc} -std=c11 -Wall -Wextra -Werror -Iinclude -o "$work/spending" \
        "$work/spending.c" libkest.a -lm 2>"$scratch"/ceilings-why; then
    echo "ceilings: the host that spends a heap does not build"
    sed 's/^/    /' "$scratch"/ceilings-why | head -5
    failed=1
elif out=$("$work/spending" "$work/spending.kest" 2>&1 </dev/null) &&
     printf '%s' "$out" | grep -q K0617 &&
     printf '%s' "$out" | grep -qF "of the 65536 bytes it was given" &&
     printf '%s' "$out" | grep -qF "growing to"; then
    reached=$((reached + 1))
else
    echo "ceilings: a heap a host said was all there is was spent in silence"
    printf '%s\n' "$out" | sed 's/^/    /' | head -6
    failed=1
fi

# And the number a host picks rather than the number a program runs into: a
# stack of four billion slots is sixty-four gigabytes, and a host that asks for
# one used to get nothing back and no word about why. What it means to be
# unable to have it is a machine with less than that, so this asks under one:
# the memory a run may have is cut to a gigabyte and the asking is the same.
cat > "$work/asking.c" <<'HOST'
#include <stdio.h>
#include "kest.h"

int main(int argc, char **argv) {
    (void)argc;
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    KestLimits limits = {4000000000u, 1024, 0};
    KestRuntime *runtime = kest_start(build, host, &limits);
    kest_host_free(host);
    if (runtime != NULL) {
        return 3;
    }
    kest_build_report(build, stdout, KEST_FORM_TEXT);
    return 0;
}
HOST

if ! ${CC:-cc} -std=c11 -Wall -Wextra -Werror -Iinclude -o "$work/asking" \
        "$work/asking.c" libkest.a -lm 2>"$scratch"/ceilings-why; then
    echo "ceilings: the host that asks for too much does not build"
    sed 's/^/    /' "$scratch"/ceilings-why | head -5
    failed=1
else
    out=$(ulimit -v 1000000 2>/dev/null;
          "$work/asking" "$work/spending.kest" 2>&1 </dev/null)
    if printf '%s' "$out" | grep -q K0638 &&
       printf '%s' "$out" | grep -qF "4000000000 slots of stack"; then
        reached=$((reached + 1))
    else
        echo "ceilings: a host asked for more stack than there is and was told" \
             "nothing"
        printf '%s\n' "$out" | sed 's/^/    /' | head -6
        failed=1
    fi
fi

# And the ceiling nobody sets: the memory the machine this runs on has. Every
# allocation in this compiler answers NULL when there is none, and every caller
# handles it — by giving up. What a caller gives up with is a diagnostic
# written into the arena that just refused, so a run with nothing left said
# nothing, came back nought, and read from outside exactly like a program that
# ran and printed nothing. Four levels of `ulimit -v` did that.
#
# So the ladder is walked rather than argued about: from a level where the
# program runs down to the level where the loader itself cannot start, every
# rung either runs or refuses in words. The numbers are this machine's and are
# found rather than written down, because what a run needs is what the C
# library beside it needs too.
rungs=0
ranged=0
refused=0
runnable=0
level=4000
while [ $level -le 65536 ]; do
    out=$(ulimit -v $level 2>/dev/null;
          ./kest run examples/numbers.kest 2>&1 </dev/null)
    if [ -n "$out" ] && [ "${out#*error}" = "$out" ]; then
        runnable=$level
        break
    fi
    level=$((level * 2))
done
if [ $runnable -eq 0 ]; then
    echo "ceilings: there is no amount of memory this program runs in"
    failed=1
else
    level=$runnable
    while [ $level -ge 1000 ]; do
        out=$(ulimit -v $level 2>/dev/null;
              ./kest run examples/numbers.kest 2>&1 </dev/null)
        answered=$?
        # Below some level the C library cannot be mapped and this program
        # never starts. That is the machine refusing rather than this compiler,
        # and it is where the ladder ends.
        case "$out" in
        *"loading shared libraries"*) break ;;
        esac
        rungs=$((rungs + 1))
        if [ $answered -eq 0 ] && [ -n "$out" ]; then
            ranged=$((ranged + 1))
        elif [ $answered -ne 0 ] && printf '%s' "$out" | grep -q 'error\[K'; then
            refused=$((refused + 1))
        else
            echo "ceilings: with ${level}K of memory a run came back" \
                 "$answered and said:"
            printf '%s\n' "$out" | sed 's/^/    /' | head -3
            failed=1
            break
        fi
        level=$((level - 100))
    done
    # A ladder that never crossed the line walked no rungs that matter: every
    # one of them running is a ladder that started too low to say anything.
    if [ $refused -eq 0 ] || [ $ranged -eq 0 ]; then
        echo "ceilings: $rungs rungs of a ladder from ${runnable}K down," \
             "$ranged of them ran and $refused refused, so the line between" \
             "them was never crossed"
        failed=1
    fi
fi

if [ $failed -eq 0 ]; then
    echo "every ceiling is a message at the line that asked:" \
         "$reached while running, $met while compiling, and $rungs rungs of" \
         "less and less memory, $ranged run and $refused refused in words"
fi
exit $failed
