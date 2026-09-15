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

# Every ceiling this lowers goes through one door. Three of them were three
# copies of four lines with a sentence each, and a sentence per copy is three
# things to watch where there is one thing to know: which number moved. What is
# read is the line as it is written, so a number written another way — the same
# number in brackets — is a ceiling that quietly stops being lowered and a
# refusal nothing reaches, which is what this says rather than passes over.
lower() {
    if ! grep -q "$2" "$work/$1"; then
        echo "ceilings: \`$2\` is not in $1, so the ceiling this lowers has" \
             "moved and the refusal under it is one nothing reaches"
        exit 1
    fi
    sed -i "s/$2/$3/" "$work/$1"
}

lower src/vm.c '#define MAX_COUNTED INT32_MAX' '#define MAX_COUNTED 100'

# And every host this writes goes through one door of its own, for the same
# reason: three copies of one compile said three things about one thing, which
# is that a host written against the public header stopped compiling. What it
# is for says which host it was.
builds() {
    if ${CC:-cc} -std=c11 -Wall -Wextra -Werror -Iinclude -o "$work/$1" \
            "$work/$1.c" libkest.a -lm 2>"$scratch"/ceilings-why; then
        return 0
    fi
    echo "ceilings: the host that $2 does not build"
    sed 's/^/    /' "$scratch"/ceilings-why | head -5
    return 1
}

# And the other number a machine runs out of: how many places in stores it can
# tell apart. A reference carries the stamp its slot was handed out with, and
# after four thousand million of them a stamp handed out again would make a
# reference from the first occupant read as the newest one. Lowered here for
# the same reason the other is: nobody is adding four thousand million things
# to a store to watch it.
lower src/vm.c '#define MOST_STAMPS 0xffffffffu' '#define MOST_STAMPS 1000u'

# And how many names a program may ask a host for. An extern is named in the
# instruction that calls it in two bytes, so the one past the last is called as
# whichever one it wraps to — the host's own function, with somebody else's
# arguments. Lowered here for the same reason as the two above: a program with
# sixty-five thousand externs in it takes longer to write down than anybody
# will wait for, and what is being watched is the refusal rather than the size.
lower src/compile.c '#define MAX_EXTERNS 65536' '#define MAX_EXTERNS 4'
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


def binding():
    # The other place a name is taken: a `match` arm names what the case it
    # answered was carrying, and that name is the two hundred and fifty
    # seventh. Every one before it is a `let`, so what refuses is the arm
    # rather than the run of them. See D528.
    return ("enum D {\n    Shut\n    Open(i32)\n}\n\nfn main() -> i32 {\n"
            "    let d = D.Open(1)\n%s\n    return match d {\n"
            "        Shut -> 0\n        Open(w) -> w\n    }\n}\n"
            % "\n".join("    let a%d = %d" % (i, i) for i in range(255)))


def walking():
    # And the other place a loop reaches back from: a walk over an array emits
    # its own loop with its own copy of the same guard, and a `while` never
    # goes through it. See D528.
    body = "\n".join("        n += %d" % (i % 7) for i in range(20000))
    return ("fn main() -> i32 {\n    let n = 0\n    let xs = [1, 2]\n"
            "    for x in xs {\n%s\n    }\n    return n\n}\n" % body)


def nesting():
    # Parentheses, because they are the one thing that nests with nothing else
    # in it: what this asks about is the depth and not what is at the bottom.
    return ("fn main() -> i32 {\n    return %s1%s\n}\n"
            % ("(" * 129, ")" * 129))


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
# The number is not written here. It is the table's, read out of the row the
# phrase finds, because a number written twice is two places to keep right and
# this list was the one nobody was keeping: the table could say 48 `defer`s
# while the compiler refused at 32, and the comment above said the two were
# held in step. See D521.
PROBES = [
    ("names in a function", names, "K0502"),
    ("names in a function", binding, "K0502"),
    ("loops one inside another", loops, "K0502"),
    ("expressions one inside another", nesting, "K0215"),
    # The other row with two sentences in it: what a loop holds of each.
    ("`break`s in one loop", breaks, "K0502"),
    ("`break`s in one loop", continues, "K0502"),
    ("`defer`s in a function", defers, "K0502"),
    # Two sentences under one row: what a loop reaches back over, and what a
    # jump reaches forward over. Meeting one of them is not meeting the other.
    ("bytes of code a jump reaches", reaches, "K0503"),
    ("bytes of code a jump reaches", jumps, "K0503"),
    ("bytes of code a jump reaches", walking, "K0503"),
    ("things one `match` chooses between", subjects, "K0339"),
    ("combinations one `match` answers", combinations, "K0333"),
    ("elements a `[T; N]` holds", elements, "K0326"),
]

# The rows that are not met here: what `len` counts to is a refusal the machine
# makes, and how many names a program asks a host for is one nobody will wait
# for a program to have. Both are met further down this file, in a tree with the
# ceiling lowered.
LOWERED = ("elements an array or a store holds",
           "places in stores a machine hands out",
           "names a program asks the host for")

table = re.search(r"## What there is a most of(.*?)\n```",
                  open(os.path.join(WHERE, "docs/language.md")).read(), re.S)
rows = re.findall(r"\n\| (\d+) \| ([^|]+) \|", table.group(1))
failed = 0
for number, what in rows:
    if any(lowered in what for lowered in LOWERED):
        continue
    if not any(phrase in what for phrase, _, _ in PROBES):
        print("limits: nothing runs into `%s`, so its message is one nobody "
              "has seen" % what.strip())
        failed = 1

# The two rows the walk above steps over, because what meets them is a tree
# with the ceiling lowered: the number a program is refused at there is the one
# this file lowered it to and not the table's. What can be held is the define
# the lowering reads, which is where the table's number actually lives — and
# until now nothing compared the two, so the table could say any number at all
# for either of them. See D522.
DEFINED = [
    ("elements an array or a store holds", "src/vm.c", "MAX_COUNTED",
     {"INT32_MAX": "2147483647"}),
    ("places in stores a machine hands out", "src/vm.c", "MOST_STAMPS",
     {"0xffffffffu": "4294967295"}),
    ("names a program asks the host for", "src/compile.c", "MAX_EXTERNS", {}),
]
for phrase, path, define, written_as in DEFINED:
    number = [held for held, what in rows if phrase in what]
    found = re.search(r"#define %s (\S+)" % define,
                      open(os.path.join(WHERE, path)).read())
    if not number or found is None:
        print("limits: `%s` is a row in the table and `%s` is a number in "
              "`%s`, and one of the two is not there" % (phrase, define, path))
        failed = 1
        continue
    value = written_as.get(found.group(1), found.group(1))
    if value != number[0]:
        print("limits: the table says %s %s and `%s` is %s"
              % (number[0], phrase.strip(), define, value))
        failed = 1

met = 0
for phrase, program, code in PROBES:
    written = [number for number, what in rows if phrase in what]
    if not written:
        print("limits: nothing in the table says `%s`, so there is no number "
              "to hold its message to" % phrase)
        failed = 1
        continue
    number = written[0]
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

# And the heap a host fills itself. A lend costs a header and a place in the
# list of what is lent, both on the machine's heap, so a host that lends every
# frame and ends nothing pays for every one of them until the heap goes. What
# it used to get when that ran out was a value with nothing in it and no words
# anywhere: a lend refused because the type is not there and a lend refused
# because there is no room were the same answer, and only one of them is about
# the program.
cat > "$work/holding.kest" <<'KEST'
fn counted(bytes: [u8]) -> i32 no.alloc {
    return len(bytes)
}

fn main() -> i32 {
    let mine: [u8] = array()
    return counted(mine)
}
KEST

cat > "$work/lending.c" <<'HOST'
#include <stdio.h>
#include "kest.h"

int main(int argc, char **argv) {
    (void)argc;
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    /* Small, so that what runs out is this number rather than the machine. */
    KestLimits limits = {0, 0, 65536};
    KestRuntime *runtime = kest_start(build, host, &limits);
    kest_host_free(host);
    if (runtime == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 2;
    }
    /* Lent and never ended, which is the host mistake this is about. */
    static unsigned char bytes[8];
    for (int i = 0; i < 100000; i++) {
        if (kest_borrow(runtime, bytes, 8, "u8", 1).object == NULL) {
            kest_report(runtime, stdout, KEST_FORM_TEXT);
            return 0;
        }
    }
    printf("a heap of 65536 bytes took a hundred thousand lends\n");
    return 3;
}
HOST

if ! builds lending "lends until it cannot"; then
    failed=1
elif out=$("$work/lending" "$work/holding.kest" 2>&1 </dev/null) &&
     printf '%s' "$out" | grep -q K0643 &&
     printf '%s' "$out" | grep -qF "of its 65536 bytes left" &&
     printf '%s' "$out" | grep -qF "end the ones this host is done with"; then
    reached=$((reached + 1))
else
    echo "ceilings: a host that lent until the heap it gave ran out was told" \
         "nothing"
    printf '%s\n' "$out" | sed 's/^/    /' | head -6
    failed=1
fi

# And the one the same copy refuses while compiling: how many names a program
# asks the host for. It is met with `emit` rather than `run`, because a program
# refused for one of these has no machine to be run on, and the ceiling is
# reached where the name is called rather than where it is declared: a slot is
# what a call needs and what an instruction names.
cat > "$work/asking-names.kest" <<'KEST'
extern fn Host.one() -> i32
extern fn Host.two() -> i32
extern fn Host.three() -> i32
extern fn Host.four() -> i32
extern fn Host.five() -> i32

fn main() -> i32 {
    return Host.one() + Host.two() + Host.three() + Host.four() + Host.five()
}
KEST

out=$("$work/kest" emit "$work/asking-names.kest" 2>&1 </dev/null)
if printf '%s' "$out" | grep -q K0502 &&
   printf '%s' "$out" | grep -qF "at most 4 names"; then
    # Counted with the ones the compiler refuses, because that is what this is:
    # the copy is lowered so that a program can reach it, not so that it
    # happens somewhere else.
    met=$((met + 1))
else
    echo "ceilings: a program asking for one name too many was not refused"
    printf '%s\n' "$out" | sed 's/^/    /' | head -6
    failed=1
fi

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

# And the same ceiling met through a value rather than by name. A call through
# a function value is a second instruction with a second copy of the check in
# front of it, and the two are the same sentence in the machine and two places
# to leave it out of: taking it out of this one let a program run the machine
# off its own stack, and nothing here had ever gone that way. See D439.
cat > "$work/through.kest" <<'KEST'
fn down(n: i32) -> i32 {
    if n <= 0 {
        return 0
    }
    let again: fn(i32) -> i32 = down
    return again(n - 1)
}

fn main() -> i32 {
    return down(100000)
}
KEST

# And the stack met through a value as well, which is the other half of the
# same pair: the machine says "out of stack" in front of a call by name and
# again in front of a call through one, and only the first had ever been
# reached. A frame wide enough that the stack runs out before the nesting does,
# entered through a value. See D440.
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
    echo '    let again: fn(i32) -> i32 = down'
    echo '    return a119 + again(n - 1)'
    echo '}'
    echo
    echo 'fn main() -> i32 {'
    echo '    return down(100000)'
    echo '}'
} > "$work/holding-through.kest"

# These four are the host's own two numbers, which are not rows of the table on
# purpose: how deep the calls go and how much stack there is are a host's to
# choose, and `kest.h` is where they are written. What is held here is that a
# program reaching either is told which number it reached — by name and through
# a value, which are two instructions with two copies of the guard, and D439
# and D440 are what taking one of them out cost. See D523.
for one in "nesting:calls nest more than 1024 deep" \
           "through:calls nest more than 1024 deep" \
           "holding:more than the 65536 slots of stack there are" \
           "holding-through:more than the 65536 slots of stack there are"; do
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

# And what a machine says it would have needed, which is the other half of
# every one of those four. A host picks the two numbers and the only thing that
# says whether it picked well is the program, so the refusal carries the answer
# — a number to ask for, or the reason there is not one. The four above are
# programs with no answer, and every one of them is told so; this is the other
# kind, and the command line cannot be the host for it because the command line
# asks first and gets what it asked for. A host that picked too small a number
# is the one that needs telling. See D569.
cat > "$work/chained.kest" <<'KEST'
fn fifth(n: i32) -> i32 {
    return n + 1
}

fn fourth(n: i32) -> i32 {
    return fifth(n) + 1
}

fn third(n: i32) -> i32 {
    return fourth(n) + 1
}

fn second(n: i32) -> i32 {
    return third(n) + 1
}

fn first(n: i32) -> i32 {
    return second(n) + 1
}

fn main() -> i32 {
    return first(0)
}
KEST

cat > "$work/narrow.c" <<'HOST'
#include <stdio.h>
#include <stdlib.h>
#include "kest.h"

int main(int argc, char **argv) {
    (void)argc;
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    /* A host that picks rather than asks, which is what every host did before
       there was anything to ask, and what one does when what it asks about is
       not what it calls. The numbers are this host's arguments so that one
       run is a machine too shallow and another is a machine wide enough. */
    KestLimits limits = {(uint32_t)strtoul(argv[2], NULL, 10),
                         (uint32_t)strtoul(argv[3], NULL, 10),
                         (size_t)strtoul(argv[4], NULL, 10)};
    KestRuntime *runtime = kest_start(build, host, &limits);
    kest_host_free(host);
    if (runtime == NULL) {
        return 2;
    }
    KestValue frame[2] = {{0}};
    if (kest_call(runtime, kest_entry(runtime, "main"), frame, 2)) {
        printf("ran, answering %lld\n", (long long)frame[0].integer);
        return 0;
    }
    kest_report(runtime, stdout, KEST_FORM_TEXT);
    return 0;
}
HOST

if ! builds narrow "picks its own two numbers"; then
    failed=1
else
    out=$("$work/narrow" "$work/chained.kest" 4096 3 0 2>&1 </dev/null)
    what_it_said=$(printf '%s' "$out" | grep -o "needs .* was given .*" |
                   head -1)
    wanted=$(printf '%s' "$what_it_said" |
             sed -n 's/.* \([0-9]*\) frames.*/\1/p')
    had=$(printf '%s' "$what_it_said" |
          sed -n 's/.*was given [0-9]* and \([0-9]*\).*/\1/p')
    if printf '%s' "$out" | grep -q K0602 &&
       [ -n "$wanted" ] && [ -n "$had" ] && [ "$wanted" -gt "$had" ]; then
        reached=$((reached + 1))
    else
        echo "ceilings: a machine too shallow for a program that has an" \
             "answer did not say what the answer was"
        printf '%s\n' "$out" | sed 's/^/    /' | head -6
        failed=1
    fi
    # And the same program on a machine given what it was told to give, which
    # is what the number is for: a refusal that names a number nothing can be
    # run with is a refusal a reader cannot act on.
    out=$("$work/narrow" "$work/chained.kest" 4096 "$wanted" 0 2>&1 </dev/null)
    if ! printf '%s' "$out" | grep -qF "ran, answering 5"; then
        echo "ceilings: the number a refusal said to ask for was not enough"
        printf '%s\n' "$out" | sed 's/^/    /' | head -6
        failed=1
    fi
    # And the same machine with no heap left to work it out on. Working out
    # what a program needs is itself memory, so a machine that has spent its
    # heap and then runs off its stack has run out of both at once, and what
    # it says then is this machine's trouble rather than the program's. Walked
    # down rather than written as one number, because which heap is too small
    # to hold the working out is the arena's arithmetic and not a thing to
    # keep a copy of here. See D570.
    said_both=""
    for heap in 256 192 128 96 64 48; do
        out=$("$work/narrow" "$work/chained.kest" 4096 3 "$heap" 2>&1 \
              </dev/null)
        if printf '%s' "$out" | grep -q K0602 &&
           printf '%s' "$out" |
               grep -qF "cannot be worked out with the heap"; then
            said_both=$heap
            break
        fi
    done
    if [ -n "$said_both" ]; then
        reached=$((reached + 1))
    else
        echo "ceilings: a machine with nothing left to work out what it" \
             "needed said something else"
        printf '%s\n' "$out" | sed 's/^/    /' | head -6
        failed=1
    fi
fi

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
#include <stdlib.h>
#include "kest.h"

int main(int argc, char **argv) {
    (void)argc;
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    /* The ceiling is this host's second argument, and nought is a host that
       sets none: the same run either way, so what changes is which of the two
       says no. Small enough to be spent while the numbers are still small, so
       what the message says about what was growing is a number a reader can
       hold. */
    KestLimits limits = {0, 0, (size_t)strtoul(argv[2], NULL, 10)};
    KestRuntime *runtime = kest_start(build, host, &limits);
    kest_host_free(host);
    if (runtime == NULL) {
        return 2;
    }
    KestValue frame[2] = {{0}};
    if (kest_call(runtime, kest_entry(runtime, "main"), frame, 2)) {
        return 3;
    }
    /* Which of the two refused it, said before the report so that a reader of
       this output has it beside the message rather than after it. A list with
       nothing else in it: a fourth answer stops this host compiling. */
    switch (kest_heap_refused_by(runtime)) {
    case KEST_REFUSED_CEILING:
        printf("refused by a ceiling this host set\n");
        break;
    case KEST_REFUSED_MACHINE:
        printf("refused by the machine underneath\n");
        break;
    case KEST_REFUSED_NOTHING:
        printf("refused by nobody\n");
        break;
    }
    kest_report(runtime, stdout, KEST_FORM_TEXT);
    return 0;
}
HOST

if ! builds spending "spends a heap"; then
    failed=1
elif out=$("$work/spending" "$work/spending.kest" 65536 2>&1 </dev/null) &&
     printf '%s' "$out" | grep -q K0617 &&
     printf '%s' "$out" | grep -qF "of the 65536 bytes it was given" &&
     printf '%s' "$out" | grep -qF "refused by a ceiling this host set" &&
     printf '%s' "$out" | grep -qF "growing to"; then
    reached=$((reached + 1))
else
    echo "ceilings: a heap a host said was all there is was spent in silence"
    printf '%s\n' "$out" | sed 's/^/    /' | head -6
    failed=1
fi

# And the same host with no ceiling at all, on a machine that has less than the
# program wants. The number a host reads is the same number in both, and what
# it does about it is not: a ceiling is a thing to raise and a machine with
# nothing left is not. Which of them it was is a thing the machine knows, and
# the only way to find out it says so is to be refused both ways.
cat > "$work/hungry.kest" <<'KEST'
fn main() -> i32 {
    let all: [i32] = array()
    for i in 0..100000000 {
        push(all, i)
    }
    return len(all)
}
KEST

out=$(ulimit -v 40000 2>/dev/null;
      exec "$work/spending" "$work/hungry.kest" 0 2>&1 </dev/null)
if printf '%s' "$out" | grep -q K0605 &&
   printf '%s' "$out" | grep -qF "refused by the machine underneath"; then
    reached=$((reached + 1))
else
    echo "ceilings: a machine with nothing left was read as a host's own" \
         "ceiling"
    printf '%s\n' "$out" | sed 's/^/    /' | head -6
    failed=1
fi

# And the same thing with nobody's ceiling on it: a program that keeps growing
# an array on a machine that runs out. A host that sets no heap is what the
# command line is, so this needs no host of its own — what it needs is a
# machine with less memory than the program wants, which is `ulimit -v` again.
# The two numbers are the point: `out of memory` on its own tells a reader
# nothing it did not know, and whether this is a program that wants a gigabyte
# or a machine that has a megabyte left is the whole of what anybody does about
# it.
# Both ways to want more than there is: a thing that grows a bit at a time and
# a thing made in one go. Both end at the same refusal — what grows asks for a
# bigger block, is told no, and then asks for a new one, which is the same door
# — and what each says about what it was doing is not the same sentence, so
# both are asked for. Neither had ever been run.
cat > "$work/growing.kest" <<'KEST'
fn main() -> i32 {
    let all: [i32] = array()
    for i in 0..100000000 {
        push(all, i)
    }
    return len(all)
}
KEST

cat > "$work/atonce.kest" <<'KEST'
fn main() -> i32 {
    let all = array(100000000, 0)
    return len(all)
}
KEST

ran_out() {
    out=$(ulimit -v 40000 2>/dev/null;
          exec ./kest run "$work/$1.kest" 2>&1 </dev/null)
    used=$(printf '%s\n' "$out" |
           sed -n 's/.*has used \([0-9][0-9]*\) bytes.*/\1/p')
    more=$(printf '%s\n' "$out" |
           sed -n 's/.*asked for \([0-9][0-9]*\) more.*/\1/p')
    if printf '%s' "$out" | grep -q K0605 &&
       printf '%s' "$out" | grep -qF "this machine has not got" &&
       printf '%s' "$out" | grep -qF "$2" &&
       [ -n "$used" ] && [ "$used" -gt 0 ] &&
       [ -n "$more" ] && [ "$more" -gt 0 ]; then
        reached=$((reached + 1))
    else
        echo "ceilings: a program ran the machine out of memory $3 and was" \
             "told nothing about it"
        printf '%s\n' "$out" | sed 's/^/    /' | head -6
        failed=1
    fi
}

ran_out growing "push(all, i)" "a bit at a time"
ran_out atonce "array(100000000, 0)" "in one go"

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

if ! builds asking "asks for too much"; then
    failed=1
else
    out=$(ulimit -v 1000000 2>/dev/null;
          exec "$work/asking" "$work/spending.kest" 2>&1 </dev/null)
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
# Walked for two programs rather than one. `examples/numbers.kest` only
# computes and `examples/grow.kest` allocates while it runs, and D647 walked
# the first alone on the reading that the second would be the same measurement:
# the compiler runs out before either program fills a heap, which it does. What
# that missed is where the bands sit. Compiling numbers costs 446082 bytes and
# grow 56600, and their first refusals are eight hundred kilobytes apart, in the
# same place every time they are walked. The level a band starts at is the
# program's own cost rather than what the machine had left, and one program
# cannot show that. See D649.
all_rungs=0
all_ranged=0
library_goes=0
all_refused=0
died=0
walked=""

walk_the_ladder() {
    program=$1
    # What to call it in the sentence. Handed over rather than cut off the
    # path, because a name cut off a path is a name with a path in it as far as
    # the check that holds one name to one thing is concerned, and it is right:
    # the two are not the same kind.
    called=$2
    rungs=0
    ranged=0
    refused=0
    runnable=0
    first_refusal=0
    codes=""
    in_order=""
    before=""
    before_stage=0
    level=4000
    while [ $level -le 65536 ]; do
        out=$(ulimit -v $level 2>/dev/null;
              exec ./kest run "$program" 2>&1 </dev/null)
        if [ -n "$out" ] && [ "${out#*error}" = "$out" ]; then
            runnable=$level
            break
        fi
        level=$((level * 2))
    done
    if [ $runnable -eq 0 ]; then
        echo "ceilings: there is no amount of memory this program runs in"
        failed=1
        return
    fi
    level=$runnable
    while [ $level -ge 1000 ]; do
        out=$(ulimit -v $level 2>/dev/null;
              exec ./kest run "$program" 2>&1 </dev/null)
        answered=$?
        # Below some level the C library cannot be mapped and this program
        # never starts. That is the machine refusing rather than this compiler,
        # and it is where the ladder ends. Kept, because it is the bottom of
        # every other walk this check makes as well. See D652.
        #
        # There are two ways the loader says it, and only one of them mentions
        # a shared library: below the level where it can map one is a level
        # where it cannot make the first thread's own storage, and that says
        # `cannot allocate TLS data structures`. Which of the two a run meets
        # depends on how big the binary is, so a compiler that grows by a few
        # hundred bytes walks from one into the other. See D761.
        case "$out" in
        *"loading shared libraries"*|*"TLS data structures"*)
            library_goes=$level
            break
            ;;
        esac
        rungs=$((rungs + 1))
        if [ $answered -eq 0 ] && [ -n "$out" ]; then
            ranged=$((ranged + 1))
        elif [ $answered -ne 0 ] && printf '%s' "$out" | grep -q 'error\[K'; then
            refused=$((refused + 1))
            # Where this program's refusals begin, which is where the room it
            # takes to compile ran out. It is the program's number rather than
            # the machine's: two programs walked here start refusing eight
            # hundred kilobytes apart and each of them in the same place every
            # time. See D649.
            if [ $first_refusal -eq 0 ]; then
                first_refusal=$level
            fi
            # And what it refused with. Every rung of this ladder is the
            # compiler running out while it reads a program, not a program
            # running out while it runs: a machine this small never gets as far
            # as a heap. So the codes are the ones this compiler says about
            # itself, and a rung that answered with a program's would be news.
            # See D647.
            said=$(printf '%s' "$out" | grep -o 'K[0-9][0-9][0-9][0-9]' | head -1)
            case " $codes " in
            *" $said "*) ;;
            *)
                codes="$codes $said"
                in_order=${in_order:+$in_order then }$said
                ;;
            esac
            # And where on the ladder each of them is. A refusal names the
            # stage that gave way: `K0605` is a program that filled its heap,
            # `K0638` is a machine that cannot be made, `K0639` is a read that
            # cannot finish. The later the stage, the more has already been
            # spent getting to it, so walking down it is the later stage that
            # gives way first and the bands come in that order. A rung saying
            # an earlier stage above one saying a later stage is this compiler
            # having grown somewhere a level cannot say. The order is what
            # another machine reads; the levels are this one's. See D648.
            case "$said" in
            K0605) stage=1 ;;
            K0638) stage=2 ;;
            K0639) stage=3 ;;
            *) stage=$before_stage ;;
            esac
            if [ $stage -lt $before_stage ]; then
                echo "ceilings: with ${level}K a rung refused with $said" \
                     "below one that refused with $before, so an earlier" \
                     "stage held on further down the ladder than a later one"
                failed=1
            fi
            before_stage=$stage
            before=$said
        elif [ $answered -ge 128 ]; then
            # A rung that neither ran nor refused, and was killed to boot: a
            # signal is a hundred and twenty-eight and the number of it. What a
            # reader needs is which rung and what it managed to say, not the
            # first one only, so the walk carries on — one rung dying is told
            # apart from every rung below it dying. See D646.
            died=$((died + 1))
            if [ $died -le 3 ]; then
                echo "ceilings: with ${level}K of memory a run died rather" \
                     "than running or refusing: it came back $answered"
                printf '%s\n' "$out" | sed 's/^/    /' | head -3
            fi
            failed=1
        else
            # And a rung that came back on its own feet with nothing to say,
            # which is the shape a refusal takes when the thing that would have
            # said it could not be written down. See D377.
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
    # What the ladder is a ladder of: this compiler's own room, not a
    # program's. `K0638` is a machine that cannot have what it was asked for,
    # `K0639` is a run with nothing left to finish in, and `K0605` is the same
    # thing said where a value is made. A program's own `K0617` here would mean
    # a program had got as far as filling a heap at this size, which nothing on
    # this ladder does. See D647.
    for said in $codes; do
        case "$said" in
        K0638|K0639|K0605) ;;
        *)
            echo "ceilings: a rung refused with $said, which is not this" \
                 "compiler saying it has run out"
            failed=1
            ;;
        esac
    done
    all_rungs=$((all_rungs + rungs))
    all_ranged=$((all_ranged + ranged))
    all_refused=$((all_refused + refused))
    walked="${walked:+$walked, }$called from ${runnable}K, first"
    walked="$walked refusing at ${first_refusal}K with $in_order"
}

walk_the_ladder examples/numbers.kest numbers.kest
computing=$first_refusal
walk_the_ladder examples/grow.kest grow.kest
allocating=$first_refusal
# And one that asks for a big machine. D814 worked out what a call through a
# value costs, and with it every program here but one came down from 65536
# slots to under a hundred — so the rung where a machine cannot be made, which
# is the middle of the three bands, stopped happening at all and the ladder
# had one band left. `examples/tree.kest` reaches itself and still takes the
# usual numbers, so it is the program that walks through all three. A check
# that lost a band because the compiler got better is a check that needs a
# program the improvement did not reach. See D814.
walk_the_ladder examples/tree.kest tree.kest
# Two ladders are two measurements only where they part company. The level a
# band starts at is what the program cost to get that far, so a program that
# costs four hundred kilobytes more to compile starts refusing further up; two
# that start refusing at the same rung are one program walked twice, whatever
# the second one is called. See D649.
if [ $computing -eq $allocating ]; then
    echo "ceilings: both ladders started refusing at ${computing}K, which is" \
         "one program walked twice rather than two programs"
    failed=1
fi

# The two numbers this project has for what a program takes: `check-costs.sh`
# weighs compiling in bytes and this weighs it in rungs. Held against each
# other here, over every example the compiler can size a machine for.
#
# They agree where they are about the same thing. A program's first refusal is
# whichever ceiling it reaches first, and they are not one measurement: `K0638`
# is a machine that cannot be made, which is what the program asked for rather
# than what reading it cost, and `K0642` is a program whose standard input
# would not open, which is not memory at all. `K0639` is the reading itself
# running out, and that is the one the bytes are about — so those are the
# programs weighed, and the rest are left out rather than read as though the
# same thing had happened to them.
#
# Over the thirteen that are left, from 67359 bytes to 629474 and from 4400K to
# 4900K, the rung never falls as the cost rises. That is the two numbers holding
# each other: a program that costs more to read runs out of room to read it
# sooner, and a day when it does not is one of them measuring something else.
# See D650.
read_ran_out=0
# And why each of the others is not weighed, by kind rather than as one number.
# A program leaves this weighing when the ceiling it meets first changes, and
# what that means is a machine that has grown or an input that has moved — news
# either way, and news nothing could see while every reason it was left out was
# counted as the same reason. See D651.
no_answer=0
wanted_a_machine=0
wanted_its_input=0
refused_anywhere=0
ran_throughout=0
said_nothing=0
# And two programs nobody wrote by hand. Every example is written to show the
# language, and the dearest of them costs about six hundred thousand bytes to
# compile — one order of magnitude, where a compiler has to hold for several. So
# the check writes its own, costing about ten times the dearest example and
# pushing the rung they refuse at from four thousand to eleven thousand. What
# they are for is the range rather than the programs. See D654.
#
# Two of them, because big is not one shape. Measured over five shapes of the
# same source size: a long chain of operators costs 59.85 bytes of this
# compiler's memory for every byte of source, nesting to sixty deep 53.78, one
# long function 46.12, small functions 43.69 and struct declarations 31.86. So
# the two written here are the dearest shape there is and the one the examples
# are mostly made of, which is the widest pair of the five. See D655.
#
# How big is asked rather than written down. These were two numbers — 1300
# functions and 340 chains — measured once against an example that cost six
# hundred thousand bytes, and every example written since walked toward them: a
# function added to `embed.kest` is a few thousand bytes of the gap, and the day
# the gap closed this check refused for a reason that was nothing to do with
# ceilings. So the dearest example is weighed first and the two programs are
# sized from it, which is the rule the numbers were standing in for. See D703.
#
# Every example, including the ones the weighing below leaves out: what these
# two are written to be past is what anybody wrote, and a program this check
# cannot weigh is still a program somebody wrote.
dearest=0
for program in examples/*.kest; do
    cost=$(./kest emit --json "$program" 2>/dev/null |
           grep -o '"cost":[0-9]*' | head -1 | cut -d: -f2)
    if [ -n "$cost" ] && [ "$cost" -gt "$dearest" ]; then
        dearest=$cost
    fi
done
if [ "$dearest" -eq 0 ]; then
    echo "ceilings: no example said what compiling it costs, so there is" \
         "nothing to write a bigger program than"
    exit 1
fi
# What each of them costs a unit, measured: a chain of thirty terms is about
# 14069 bytes of this compiler's memory and a function of three statements about
# 3816. Eleven times the dearest example rather than ten, because ten is the
# rule and a program written to sit exactly on a rule is one rounding away from
# under it.
#
# These two go stale every time compiling gets cheaper, and they are meant to:
# the examples get cheaper by the same change and the ratio is what moves, so
# the check refuses and says both numbers. They were 4991 and 18855 until the
# token array stopped being taken again at every size (D746), 4214 and 16364
# until where an instruction was written stopped being kept for every byte of
# it (D751), 3816 and 14069 until a node stopped being as wide as its rarest
# inhabitant (D782), and 3106 and 12992 until a token array stopped being
# doubled for a copy it does not make (D783).
steps=$((dearest * 11 / 3280 + 1))
chains=$((dearest * 11 / 12900 + 1))
{
    echo "module steps"
    echo
    at=0
    while [ $at -lt $steps ]; do
        echo "fn step$at(n: i32) -> i32 {"
        echo "    let a = n * $((at % 7 + 1)) + $at"
        echo "    let b = a - n / $((at % 5 + 1))"
        echo "    return a + b"
        echo "}"
        echo
        at=$((at + 1))
    done
    echo "fn main() -> i32 {"
    echo "    let total = 0"
    at=0
    while [ $at -lt $steps ]; do
        echo "    total += step$at($((at % 11)))"
        at=$((at + 1))
    done
    echo "    if total == 0 {"
    echo "        return 1"
    echo "    }"
    echo "    return 0"
    echo "}"
} >"$scratch"/steps.kest
{
    echo "module chains"
    echo
    at=0
    while [ $at -lt $chains ]; do
        echo "fn chain$at(n: i32) -> i32 {"
        printf '    return n * 1'
        times=2
        while [ $times -le 30 ]; do
            printf ' + n * %s' $times
            times=$((times + 1))
        done
        echo
        echo "}"
        echo
        at=$((at + 1))
    done
    echo "fn main() -> i32 {"
    echo "    let total = 0"
    at=0
    while [ $at -lt $chains ]; do
        echo "    total += chain$at(2)"
        at=$((at + 1))
    done
    echo "    if total == 0 {"
    echo "        return 1"
    echo "    }"
    echo "    return 0"
    echo "}"
} >"$scratch"/chains.kest
: >"$scratch"/rungs-reading
: >"$scratch"/rungs-machine
: >"$scratch"/first-refusals
for program in examples/*.kest "$scratch"/steps.kest "$scratch"/chains.kest; do
    said=$(./kest emit --json "$program" 2>/dev/null)
    case "$said" in
    *'"needs":{"slots":null'*)
        # No answer for what it needs, so its machine is the default one
        # rather than its own and its first ceiling is that default.
        no_answer=$((no_answer + 1))
        continue
        ;;
    esac
    cost=$(printf '%s' "$said" | grep -o '"cost":[0-9]*' | head -1 | cut -d: -f2)
    if [ -z "$cost" ]; then
        said_nothing=$((said_nothing + 1))
        continue
    fi
    # And where it starts refusing, found by halving rather than by walking
    # every rung from the top. A run either runs at a level or refuses at it and
    # there is no third answer between them, so the level where it changes is
    # found in six runs rather than thirty-six. Both walks were run over every
    # example and answered the same for every one of them, which is what makes
    # this the same measurement and not a cheaper one. See D652.
    #
    # The two ends are known before the search: a program that refuses at the
    # top of the ladder is one this cannot weigh — it wants a host, or something
    # that is not there — and one that still runs at the bottom rung never
    # refuses at all.
    first_refusal=0
    said_first=""
    at_the_top=0
    high=$runnable
    low=$((library_goes + 100))
    out=$(ulimit -v $high 2>/dev/null;
          exec ./kest run "$program" 2>&1 </dev/null)
    # A program dearer than the ladder's own refuses where the ladder runs, so
    # the top of the search is found rather than taken: doubled until the
    # program runs in it, and a program that runs nowhere is one this cannot
    # weigh. See D654.
    while [ $high -lt 65536 ]; do
        if ! printf '%s' "$out" | grep -q 'error\[K'; then
            break
        fi
        high=$((high * 2))
        out=$(ulimit -v $high 2>/dev/null;
              exec ./kest run "$program" 2>&1 </dev/null)
    done
    if printf '%s' "$out" | grep -q 'error\[K'; then
        at_the_top=1
    else
        # The bottom rung is the lowest level the C library still maps in,
        # which the ladder found. A level under it answers neither way — the
        # program never starts — so the bottom is raised until it does, which
        # is nothing on a day the ladder found it and the whole search on a day
        # it did not.
        out=$(ulimit -v $low 2>/dev/null;
              exec ./kest run "$program" 2>&1 </dev/null)
        while [ $low -lt $high ]; do
            case "$out" in
            *"loading shared libraries"*|*"TLS data structures"*) ;;
            *) break ;;
            esac
            low=$((low + 100))
            out=$(ulimit -v $low 2>/dev/null;
                  exec ./kest run "$program" 2>&1 </dev/null)
        done
        if printf '%s' "$out" | grep -q 'error\[K'; then
            while [ $((high - low)) -gt 100 ]; do
                middle=$(((high + low) / 200 * 100))
                said_there=$(ulimit -v $middle 2>/dev/null;
                             exec ./kest run "$program" 2>&1 </dev/null)
                if printf '%s' "$said_there" | grep -q 'error\[K'; then
                    low=$middle
                    out=$said_there
                else
                    high=$middle
                fi
            done
            first_refusal=$low
            said_first=$(printf '%s' "$out" |
                         grep -o 'K[0-9][0-9][0-9][0-9]' | head -1)
        fi
    fi
    if [ $at_the_top -eq 1 ]; then
        refused_anywhere=$((refused_anywhere + 1))
        continue
    fi
    # Where this program first refuses, whichever ceiling it met there. Kept
    # for every program rather than only for the weighed ones: what is held
    # against the ladder below is the rung, and which kind a program is
    # counted as depends on the ceiling it meets first, which is a thing about
    # this compiler rather than about the program. A table that held only the
    # weighed would lose the ladder's own program the day it met another
    # ceiling first. See D653.
    printf '%s %s\n' "$program" "$first_refusal" >>"$scratch"/first-refusals
    case "$said_first" in
    K0639)
        read_ran_out=$((read_ran_out + 1))
        printf '%s %s %s\n' "$cost" "$first_refusal" "$program" \
               >>"$scratch"/rungs-reading
        ;;
    K0638)
        wanted_a_machine=$((wanted_a_machine + 1))
        printf '%s %s %s\n' "$cost" "$first_refusal" "$program" \
               >>"$scratch"/rungs-machine
        ;;
    K0642) wanted_its_input=$((wanted_its_input + 1)) ;;
    "") ran_throughout=$((ran_throughout + 1)) ;;
    *)
        echo "ceilings: $program was left out of the weighing because it" \
             "first said $said_first, which is not a reason this names"
        failed=1
        ;;
    esac
done
# The ordering, asked of each ceiling on its own. Programs that ran out of room
# being read and programs whose machine could not be made are two measurements,
# and each of them is in the same order by bytes as by rungs — but only against
# its own kind, which is what D650 found by weighing them together and getting
# two pairs the wrong way round.
# What the two numbers say about each other, said rather than held. D650
# measured an ordering — the dearer a program is to compile, the higher up it
# starts refusing — and a week of work on the folder broke it: `inline.kest`
# costs 387244 bytes and refuses at 4800K while `parse.kest` costs 497408 and
# refuses at 4700K. Near each other the two are about different things. A rung
# is the address space a run peaks at, which is blocks taken and doubled; the
# bytes are what the arena handed out. They agree over an order of magnitude
# and not over a hundred kilobytes, so what is said here is the span rather
# than a rule nothing could break. See D684.
span_of() {
    ceiling=$1
    sort -n "$scratch"/rungs-$ceiling >"$scratch"/rungs-by-cost
    # Named for what they are here rather than `cheapest` and `dearest`: the
    # second of those is the dearest example anybody wrote, which the programs
    # this check writes are held against, and one name is one thing.
    span_first=$(head -1 "$scratch"/rungs-by-cost)
    span_last=$(tail -1 "$scratch"/rungs-by-cost)
    if [ -z "$span_first" ] || [ -z "$span_last" ]; then
        return
    fi
    spans="${spans:+$spans, }$ceiling from $(echo "$span_first" | cut -d' ' -f1)"
    spans="$spans bytes at $(echo "$span_first" | cut -d' ' -f2)K to"
    spans="$spans $(echo "$span_last" | cut -d' ' -f1) bytes at"
    spans="$spans $(echo "$span_last" | cut -d' ' -f2)K"
}
spans=""
span_of reading
span_of machine
# And the two dearest of all of them, which are the two written here: they cost
# ten times anything anybody wrote, so they are the dear end of the weighing
# whichever ceiling each of them meets first.
#
# Which ceiling that is, is not theirs to decide. Measured: `steps` runs at
# 11200K, cannot be given a machine at 11100K and 11000K, and cannot be read at
# 10800K; `chains` runs at 10800K and cannot be read at 10700K, with no rung in
# between. Every program has a band where it has been read and the machine
# cannot be made, and the band is narrower than the hundred kilobytes a rung
# is — so which kind a program is counted as is where the rungs fall against
# its band, not what the program is. That is why this holds the pair rather
# than each kind's own end. See D686.
cat "$scratch"/rungs-reading "$scratch"/rungs-machine |
    sort -n | tail -2 | cut -d' ' -f3 >"$scratch"/dearest-two
if [ "$(grep -c 'steps.kest\|chains.kest' "$scratch"/dearest-two)" != "2" ]; then
    echo "ceilings: the two dearest programs weighed are" \
         "$(tr '\n' ' ' <"$scratch"/dearest-two), and the two written here" \
         "for the range cost ten times anything anybody wrote"
    failed=1
fi
# And the two ways of asking, held to each other. `grow.kest` is walked rung by
# rung by the second ladder and found by halving here, and the halving is worth
# having only while it lands where the walk lands. Measured once by hand when it
# was written, which is a measurement that stops being true the day somebody
# rounds the other way; this is what the second ladder's rungs are for now that
# the weighing walks thirty-two programs over the same ground. See D653.
halved=$(grep '^examples/grow.kest ' "$scratch"/first-refusals |
         cut -d' ' -f2)
if [ -n "$halved" ] && [ "$halved" != "$allocating" ]; then
    echo "ceilings: walking every rung puts the first refusal of grow.kest at" \
         "${allocating}K and halving puts it at ${halved}K, so the two ways of" \
         "asking do not answer the same"
    failed=1
fi
# And a weighing of nothing weighs nothing: a filter that stops matching leaves
# a check that reads no programs and says the two numbers agree.
if [ $read_ran_out -lt 2 ] || [ $wanted_a_machine -lt 2 ]; then
    echo "ceilings: $read_ran_out program(s) ran out of room being read and" \
         "$wanted_a_machine could not be given a machine, which is not enough" \
         "to hold what compiling costs against what it costs in rungs"
    failed=1
fi
# And the program this check writes for itself, which is there for the range:
# an order of magnitude past the dearest example, where nothing was written by
# hand. One that stopped being big would leave the weighing holding what the
# examples hold and saying it twice. See D654.
written=""
for shape in steps chains; do
    # From whichever of the two it landed in, and the first field of the first
    # line: a path with a colon in it would otherwise make `cut` read the
    # middle of a name as a number, and the name is a scratch directory's.
    costs=$(cat "$scratch"/rungs-reading "$scratch"/rungs-machine |
            grep "$shape.kest" | head -1 | cut -d' ' -f1)
    if [ -z "$costs" ] || [ "$costs" -lt $((dearest * 10)) ]; then
        echo "ceilings: the program this check writes as $shape costs" \
             "${costs:-no} bytes to compile and the dearest example costs" \
             "$dearest, which is not the order of magnitude past them it is" \
             "written for"
        failed=1
    fi
    # And what that is for every hundred bytes of the source it came from,
    # which is the one number here that is about the shape rather than the
    # size. Said rather than held: nothing written could be made to put the
    # chain shape under the function shape — a chain of one term is still
    # dearer per byte than a function of four lines — so a check holding the
    # order would be a net nobody has seen catch anything. See D655.
    source=$(wc -c <"$scratch"/$shape.kest)
    per_hundred=$((costs * 100 / source))
    written="${written:+$written and }$shape at ${costs:-no} bytes,"
    written="$written $per_hundred for every hundred of source"
done
if [ $died -gt 0 ]; then
    echo "ceilings: $died of $all_rungs rungs were killed rather than running" \
         "or refusing"
fi

if [ $failed -eq 0 ]; then
    # What the ladder walked, said so that another machine reads the same
    # sentence with its own numbers: from where this program first runs down to
    # where the C library can no longer be mapped, every rung ran or refused
    # and none died. The counts are this machine's and the sentence is not.
    # See D646.
    echo "every ceiling is a message at the line that asked:" \
         "$reached while running, $met while compiling, and a ladder for each" \
         "of three programs down to where the library stops being mappable —" \
         "$walked — $all_rungs rungs in all, $all_ranged run and" \
         "$all_refused refused in words, and none died, and the bytes" \
         "compiling costs read beside the rungs it costs — $spans — over" \
         "$read_ran_out program(s) that ran out of room being read and" \
         "$wanted_a_machine that could not be given a machine, two of them" \
         "written here in the two shapes furthest apart, $written against" \
         "$dearest for the dearest example anybody wrote — beside" \
         "$wanted_its_input that wanted an input, $no_answer with no" \
         "answer for what they need, $refused_anywhere refused wherever they" \
         "are run, $ran_throughout that ran at every rung and $said_nothing" \
         "that said nothing about what they cost, all of it measured on the" \
         "machine this ran on"
fi
exit $failed
