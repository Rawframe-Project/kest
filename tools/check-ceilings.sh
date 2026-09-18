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
lower src/vm.c '#define MOST_STAMPS 16777215u' '#define MOST_STAMPS 1000u'

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


def blocks():
    # Nine blocks one inside another, which is one more than a body holds. The
    # nesting is lexical, so this is refused where it is written. See D966.
    return ("fn main() -> i32 {\n    let n = 0\n%s\n%s    n += 1\n%s\n"
            "    return n - 1\n}\n"
            % ("\n".join("    " * (i + 1) + "scratch {" for i in range(9)),
               "    " * 10,
               "\n".join("    " * (9 - i) + "}" for i in range(9))))


def held():
    # And the ones a machine holds open at once, which nesting in one body
    # cannot reach: a body with a block in it that calls itself opens one a
    # call deep. Met while running, so this is a program that runs.
    return ("fn deeper(n: i32) -> i32 {\n    scratch {\n"
            "        if n <= 0 {\n            return 0\n        }\n"
            "        return deeper(n - 1) + 1\n    }\n}\n\n"
            "fn main() -> i32 {\n    return deeper(200)\n}\n")


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
# The fourth of each row is which command meets it: all but one are refused
# where a program is written, and the blocks a machine holds open at once is met
# by a program that runs. See D966.
PROBES = [
    ("names in a function", names, "K0502", "emit"),
    ("names in a function", binding, "K0502", "emit"),
    ("loops one inside another", loops, "K0502", "emit"),
    ("expressions one inside another", nesting, "K0215", "emit"),
    # The other row with two sentences in it: what a loop holds of each.
    ("`break`s in one loop", breaks, "K0502", "emit"),
    ("`break`s in one loop", continues, "K0502", "emit"),
    ("`defer`s in a function", defers, "K0502", "emit"),
    # Two sentences under one row: what a loop reaches back over, and what a
    # jump reaches forward over. Meeting one of them is not meeting the other.
    ("bytes of code a jump reaches", reaches, "K0503", "emit"),
    ("bytes of code a jump reaches", jumps, "K0503", "emit"),
    ("bytes of code a jump reaches", walking, "K0503", "emit"),
    ("`scratch { }` blocks one inside another", blocks, "K0502", "emit"),
    ("`scratch { }` blocks one machine holds open", held, "K0656", "run"),
    ("things one `match` chooses between", subjects, "K0339", "emit"),
    ("combinations one `match` answers", combinations, "K0333", "emit"),
    ("elements a `[T; N]` holds", elements, "K0326", "emit"),
]

# The rows that are not met here: what `len` counts to is a refusal the machine
# makes, and how many names a program asks a host for is one nobody will wait
# for a program to have. Both are met further down this file, in a tree with the
# ceiling lowered.
LOWERED = ("elements an array or a store holds",
           "times a machine hands out a place, counting the ones taken back",
           "places in one store",
           "names a program asks the host for")

table = re.search(r"## What there is a most of(.*?)\n```",
                  open(os.path.join(WHERE, "docs/language.md")).read(), re.S)
rows = re.findall(r"\n\| (\d+) \| ([^|]+) \|", table.group(1))
failed = 0
for number, what in rows:
    if any(lowered in what for lowered in LOWERED):
        continue
    if not any(phrase in what for phrase, _, _, _ in PROBES):
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
    ("times a machine hands out a place, counting the ones taken back",
     "src/vm.c", "MOST_STAMPS", {}),
    ("places in one store", "src/vm.c", "MOST_PLACES", {}),
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
    # Written with the `u` a C constant carries, which the table does not.
    if value.endswith("u"):
        value = value[:-1]
    if value != number[0]:
        print("limits: the table says %s %s and `%s` is %s"
              % (number[0], phrase.strip(), define, value))
        failed = 1

met = 0
for phrase, program, code, how in PROBES:
    written = [number for number, what in rows if phrase in what]
    if not written:
        print("limits: nothing in the table says `%s`, so there is no number "
              "to hold its message to" % phrase)
        failed = 1
        continue
    number = written[0]
    path = os.path.join(WHERE, "one-too-many.kest")
    open(path, "w").write(program())
    said = subprocess.run([os.path.join(WHERE, "kest"), how, path],
                          capture_output=True, text=True,
                          stdin=subprocess.DEVNULL)
    out = said.stdout + said.stderr
    os.remove(path)
    # The number has to be in what was said and not in where it was said about.
    # What is printed carries the path the program was written at, and that path
    # is a name somebody's temporary directory handed out — so a number this is
    # looking for can turn up in it for no reason at all, and a check that reads
    # the whole of what was printed passes on a coincidence. A ceiling the words
    # no longer name would then be one nothing catches, now and again, on a
    # machine whose scratch happened to be called the right thing.
    #
    # Every line but that one, rather than the message alone: half of these
    # ceilings say the number in the message and half say it in the suggestion
    # under it, and both are the words somebody reads. See D863.
    says = "\n".join(line for line in out.splitlines() if path not in line)
    if code not in out or number not in says:
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

# And a ladder walked from the command line rather than from a host, over every
# program in this tree. The ladders above lower a number inside a copy of the
# compiler and walk one program down each; this one hands the compiler its own
# ceiling and walks every program down, which is the same question asked where
# anybody can ask it — and it is a question nothing asked at all until there was
# a way to say how much room a command may have.
#
# Reading, compiling, running, and being driven: the third is where the
# machine's own ways of running out are — a heap that cannot grow, a frame that cannot be made, a
# piece of text with nowhere to go — and it is the one where a rung that came
# back nought is a program that ran, wrote what it writes, and answered what it
# answers. Every example here answers nought, so nought is what a rung that did
# the whole job comes back as.
#
# And the fourth is the one a game is written against: a handler a frame, with a
# heap that grows across frames unless it is thrown away. A program with no
# handler is refused with no ceiling at all and skipped here, so what walks this
# is whatever takes events.
#
# What it holds of a rung is the whole of it: one that came back nought said
# what the same run with no ceiling said, and one that refused said which
# refusal it was. Both halves were broken the day this was written. A `check`
# with no room to work out which module a type belongs to wrote every type of
# every module the program imports, as though the file had declared them, and
# came back nought; and an `emit` whose compiler had no room for a layout wrote
# the program with one fewer layout than it has, and came back nought. See D845.
walked_down=0
walked_over=0
for reading in examples/*.kest lib/std/*.kest; do
    for asking in check emit run tick "tick --reset"; do
        # Only what answers with no ceiling at all. A program this command
        # refuses for its own reasons is one every rung refuses for the same
        # reason, and holding those would be holding the refusal rather than
        # the ceiling.
        # Unquoted, because what is walked is a command and the words that go
        # with it: `tick --reset` is one way of asking and `tick` is another,
        # and the two answer differently by the whole of what this is about.
        whole=$(./kest $asking "$reading" 2>&1 </dev/null) || continue
        # What it cost, which is where the ladder starts: twice that is a
        # ceiling every program here fits inside, and halving reaches one byte.
        # A number read out of the run rather than written here, because what a
        # program costs to read is a thing that moves.
        costs=$(./kest check --json "$reading" 2>/dev/null </dev/null |
            sed -n 's/.*"cost":\([0-9]*\).*/\1/p')
        if [ -z "$costs" ] || [ "$costs" -le 0 ]; then
            echo "ceilings: \`$reading\` says nothing about what reading it" \
                 "costs, so there is no ladder to walk it down"
            failed=1
            continue
        fi
        walked_over=$((walked_over + 1))
        rung=$((costs * 2))
        while [ "$rung" -gt 0 ]; do
            said=$(./kest $asking --room "$rung" "$reading" 2>&1 </dev/null)
            why=$?
            walked_down=$((walked_down + 1))
            if [ "$why" -eq 0 ]; then
                if [ "$said" != "$whole" ]; then
                    echo "ceilings: \`kest $asking --room $rung $reading\`" \
                         "came back nought and said something else than the" \
                         "same run with no ceiling:" \
                         "\`$(printf '%s' "$said" | head -1 | cut -c1-60)\`"
                    failed=1
                fi
            else
                case "$said" in
                *"error[K0"*) ;;
                *)
                    # Which covers a rung that was killed as well as one that
                    # came back on its own feet with nothing to say: a signal
                    # is a hundred and twenty-eight and the number of it, and
                    # neither of them named a refusal.
                    echo "ceilings: \`kest $asking --room $rung $reading\`" \
                         "came back $why saying" \
                         "\`$(printf '%s' "$said" | head -1 | cut -c1-60)\`," \
                         "which names no refusal"
                    failed=1
                    ;;
                esac
            fi
            rung=$((rung / 2))
        done
    done
done
if [ "$walked_down" -eq 0 ]; then
    echo "ceilings: no program was walked down a ceiling of its own, so" \
         "nothing here holds one"
    failed=1
fi

# And a program that cannot have done the job, walked down the same band. The
# sweep above reads a rung that ran the whole program and answered what it
# answers as a rung that did what it was asked — which it is, and which says
# nothing about whether the ceiling it was given was ever applied. This one
# wants sixteen megabytes of array against a ceiling of one, so a rung of it
# that comes back nought is a rung whose ceiling went nowhere.
#
# One did. A machine is built from a question about what the program needs, and
# that question is answered in the build's own arena — so a program big enough
# that the question runs out of room got no answer, and what this command line
# handed the machine then was nothing at all. Nothing at all is no ceiling: the
# program ran to the end and took thirty times what it had been allowed. The
# three hundred functions are what makes the question dear enough to run out.
# See D846.
{
    at=1
    while [ $at -le 300 ]; do
        printf 'fn f%d(n: i64) -> i64 no.alloc {\n    return n + %d\n}\n\n' \
            "$at" "$at"
        at=$((at + 1))
    done
    printf 'fn main() -> i32 {\n    let xs: [i64] = array()\n'
    printf '    let n: i64 = 0\n    while n < 2000000 {\n'
    printf '        push(xs, f1(n))\n        n += 1\n    }\n'
    printf '    return len(xs) - 2000000\n}\n'
} > "$work/hungry.kest"
# And the other program this walks, which is about the other half of a ceiling:
# what a handler keeps between the frames it is called in. A handler that
# allocates keeps what it allocated until something throws it away, so a ceiling
# a hundred frames fit inside is one two hundred do not — unless the heap goes
# back between them, which is what `--reset` says to do. Sixty-four elements a
# frame is small enough that one frame fits anywhere and two hundred fit
# nowhere, which is the gap this is walked in.
# And the same shape asking for more than any rung of the ladder in one event,
# which is the other half of the pair: what a ceiling does is refuse, and a
# ceiling that refuses nothing is a ceiling nobody applied. Before D996 the
# handler above did that on its own -- two hundred events of it abandoned two
# hundred runs of sixty-four numbers and met the ceiling on the way -- and it
# does not any more, because what a handler keeps nothing of is given back.
# That is the fix rather than a hole in this: what is held here now is that a
# handler which keeps nothing runs for as long as it is driven, and one that
# asks for more than there is in one event is refused however it is driven.
cat > "$work/greedy.kest" <<'KEST'
fn onEvent(event: i32) -> i32 {
    let kept: [i64] = array()
    let at: i64 = 0
    while at < 100000 {
        push(kept, at)
        at += 1
    }
    return len(kept) - 100000 + event - event
}

fn main() -> i32 {
    return onEvent(1)
}
KEST

cat > "$work/framed.kest" <<'KEST'
fn onEvent(event: i32) -> i32 {
    let kept: [i64] = array()
    let at: i64 = 0
    while at < 64 {
        push(kept, at)
        at += 1
    }
    return len(kept) - 64 + event - event
}

fn main() -> i32 {
    return onEvent(1)
}
KEST

# What each of them costs to compile, which is where its ladder starts. Both go
# through one door, because two readings of one number is one of them quietly
# stopping.
costs_of() {
    ./kest emit --json "$1" 2>/dev/null </dev/null |
        sed -n 's/.*"cost":\([0-9]*\).*/\1/p'
}
hungry_costs=$(costs_of "$work/hungry.kest")
framed_costs=$(costs_of "$work/framed.kest")
if [ -z "$hungry_costs" ] || [ "$hungry_costs" -le 0 ] ||
   [ -z "$framed_costs" ] || [ "$framed_costs" -le 0 ]; then
    echo "ceilings: a program written here to be walked down a ceiling of its" \
         "own says nothing about what compiling it costs"
    failed=1
else
    step=$hungry_costs
    while [ "$step" -ge 1 ]; do
        rung=$((hungry_costs + step))
        said=$(./kest run --room $rung "$work/hungry.kest" 2>&1 </dev/null)
        why=$?
        if [ "$why" -eq 0 ]; then
            echo "ceilings: a program wanting sixteen megabytes of heap ran" \
                 "under \`--room $rung\`, so the ceiling it was given went" \
                 "nowhere"
            failed=1
        fi
        step=$((step / 2))
    done

    # And the frames, from both sides. A handler that keeps nothing between
    # events runs to the end under a ceiling several times smaller than what
    # two hundred of its frames would once have come to, with `--reset` and
    # without it: what it made is given back when nothing can reach it, which
    # is what D996 is. A handler that asks for more than the whole ceiling in
    # one event is refused either way, which is what says the ceiling is
    # applied at all -- a machine given one and never applying it would run
    # both. See D847 and D996.
    # From nine times what compiling it costs down to twice it, which is a band
    # this program is always inside: it compiled in `framed_costs`, every rung
    # is well above that and what a command keeps back to say things with, and
    # the lowest still leaves several frames of it on the heap while the
    # highest leaves nothing like two hundred. A floor rather than a walk to
    # nothing, because under the band a rung has no heap to speak of and says
    # nothing about what is kept between frames — two hundred frames and one
    # are refused alike where one of them does not fit.
    framed_rungs=0
    step=$((framed_costs * 8))
    while [ "$step" -ge $((framed_costs * 2)) ]; do
        rung=$((framed_costs + step))
        framed_rungs=$((framed_rungs + 1))
        answered=$(./kest tick --json --room $rung "$work/framed.kest" 200 \
            2>/dev/null </dev/null)
        kept=$?
        # And what the whole of it came to: what reading and compiling took,
        # what a machine for it took, and what the program put on the heap,
        # against what the command was allowed. `--room` says it is the most a command asks for, all of it,
        # and the build kept a ceiling of the whole number while the program
        # ran — a second purse the same size as the first, and twenty thousand
        # bytes allowed came to twenty-four thousand spent. Read whatever the
        # run did, because what was spent was spent whether it finished or not.
        # See D849.
        spent=$(printf '%s' "$answered" | sed -n 's/.*"cost":\([0-9]*\).*/\1/p')
        # The widest moment rather than what is left at the end of it. What is
        # left at the end is wherever the last walk put it, and a machine with
        # room to spare walks less often -- so a run that ends just before one
        # holds what a run that ends just after does not. What a ceiling has to
        # cover is the widest moment. See D996.
        grew=$(printf '%s' "$answered" | sed -n 's/.*"peak":\([0-9]*\).*/\1/p')
        # The machine beside them, which is the third of the three: what it
        # cost to make and what it may spend saying what happens, weighed
        # together because a wall against the first is a wall against nothing.
        engine=$(printf '%s' "$answered" |
            sed -n 's/.*"machine":{"bytes":\([0-9]*\).*/\1/p')
        if [ -z "$spent" ] || [ -z "$grew" ] || [ -z "$engine" ]; then
            echo "ceilings: a run under \`--room $rung\` says nothing about" \
                 "what it took, so the number it was given holds nothing"
            failed=1
        elif [ $((spent + engine + grew)) -gt "$rung" ]; then
            echo "ceilings: a run allowed $rung bytes took $spent reading and" \
                 "compiling, $engine for a machine and $grew more on the" \
                 "heap, which is $((spent + engine + grew))"
            failed=1
        fi
        # And that `--reset` does what it says, which is now a thing to read
        # rather than a thing to infer from a refusal: a handler that keeps
        # nothing between events runs either way, so what tells the two apart
        # is the machine saying how many times it threw the heap away. See
        # D996.
        threw=$(./kest tick --reset --json --room $rung "$work/framed.kest" \
            200 2>/dev/null </dev/null |
            sed -n 's/.*"thrown":\([0-9]*\).*/\1/p')
        kept_it=$(printf '%s' "$answered" |
            sed -n 's/.*"thrown":\([0-9]*\).*/\1/p')
        if [ "$threw" != "200" ] || [ "$kept_it" != "0" ]; then
            echo "ceilings: 200 events under \`--reset\` threw the heap away" \
                 "$threw time(s) and 200 without it threw it $kept_it, so" \
                 "throwing the heap away between events did not"
            failed=1
        fi
        if [ "$kept" -ne 0 ]; then
            echo "ceilings: a handler that keeps nothing between events was" \
                 "refused at \`--room $rung\` over 200 of them, so what it" \
                 "made is kept nowhere it can be given back from"
            failed=1
        fi
        ./kest tick --room $rung "$work/greedy.kest" 200 \
            >/dev/null 2>&1 </dev/null
        greedy_kept=$?
        ./kest tick --reset --room $rung "$work/greedy.kest" 200 \
            >/dev/null 2>&1 </dev/null
        greedy_thrown=$?
        if [ "$greedy_kept" -eq 0 ] || [ "$greedy_thrown" -eq 0 ]; then
            echo "ceilings: a handler asking for more than the whole of" \
                 "\`--room $rung\` in one event was not refused, so the" \
                 "ceiling it was given is kept nowhere"
            failed=1
        fi
        step=$((step / 2))
    done
fi

# And what a machine costs, which is the third thing a command pays for and was
# weighed against nothing at all. A program that calls itself has no deepest
# frame, so what it is given is the usual number of slots and the usual depth
# rather than its own — fifty thousand bytes of machine for nine lines of
# program — and a command allowed twenty thousand made it and used it and never
# counted it. Weighed now, against what is left after reading and compiling,
# and refused where it does not fit. See D850.
cat > "$work/calls-itself.kest" <<'KEST'
fn down(n: i32) -> i32 {
    if n <= 0 {
        return 0
    }
    return down(n - 1) + 1
}

fn main() -> i32 {
    return down(3) - 3
}
KEST
itself_costs=$(costs_of "$work/calls-itself.kest")
if [ -z "$itself_costs" ] || [ "$itself_costs" -le 0 ]; then
    echo "ceilings: a program written here to be walked down a ceiling of its" \
         "own says nothing about what compiling it costs"
    failed=1
else
    # Twice what compiling it costs and less, which is nowhere near what its
    # machine wants: every rung of this is one the machine does not fit in.
    step=$itself_costs
    while [ "$step" -ge $((itself_costs / 4)) ]; do
        rung=$((itself_costs + step))
        # Read by what it said rather than by what it answered: what a run of
        # a program answers is the program's, and a refusal is the only thing
        # a status has to say that this is about.
        said=$(./kest run --room $rung "$work/calls-itself.kest" 2>&1 </dev/null)
        case "$said" in
        *"error[K0"*) ;;
        *)
            echo "ceilings: a program whose machine costs more than this" \
                 "command was allowed ran under \`--room $rung\` instead of" \
                 "being refused"
            failed=1
            ;;
        esac
        step=$((step / 2))
    done
fi

# And what a run says when it has no room left to say anything with. A
# diagnostic is words written into the arena the stage is working in, so a
# program with something wrong with it, compiled in a ceiling too small to
# write the message in, used to answer that there was not enough memory —
# which is true of the message and says nothing about the program. It says
# both now: the words it was about to say, kept in the list itself where a run
# with nothing left still has somewhere to put them, and then that it ran out.
#
# Sixteenths rather than halves, because the band this happens in is the one
# between reading the file and writing what is wrong with it, and halving walks
# straight over it. See D848.
cat > "$work/wrong.kest" <<'KEST'
fn main() -> i32 {
    let x: Nope = 1
    return 0
}
KEST
wrong_costs=$(costs_of "$work/wrong.kest")
if [ -z "$wrong_costs" ] || [ "$wrong_costs" -le 16 ]; then
    echo "ceilings: a program written here to be walked down a ceiling of its" \
         "own says nothing about what compiling it costs"
    failed=1
else
    told=0
    rung=$wrong_costs
    while [ "$rung" -gt 0 ]; do
        said=$(./kest check --room $rung "$work/wrong.kest" 2>&1 </dev/null)
        case "$said" in
        *"K0301"*"K06"*) told=$((told + 1)) ;;
        esac
        rung=$((rung - wrong_costs / 16))
    done
    if [ "$told" -eq 0 ]; then
        echo "ceilings: no rung of a program with something wrong with it was" \
             "refused for room and still said what was wrong, so what a run" \
             "was about to say when it ran out is kept nowhere"
        failed=1
    fi
fi

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
    printf '%s' "$out" > "$scratch"/rung-said
    if grep -q K0630 "$scratch"/rung-said &&
       grep -qF "$said_it" "$scratch"/rung-said; then
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
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    /* Small, so that what runs out is this number rather than the machine. */
    KestLimits limits = {0, 0, 65536, 0};
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
     { printf '%s' "$out" > "$scratch"/lent-said; } &&
     grep -q K0643 "$scratch"/lent-said &&
     grep -qF "of its 65536 bytes left" "$scratch"/lent-said &&
     grep -qF "end the ones this host is done with" "$scratch"/lent-said; then
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
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT, 0);
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
                         (size_t)strtoul(argv[4], NULL, 10), 0};
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
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    /* The ceiling is this host's second argument, and nought is a host that
       sets none: the same run either way, so what changes is which of the two
       says no. Small enough to be spent while the numbers are still small, so
       what the message says about what was growing is a number a reader can
       hold. */
    KestLimits limits = {0, 0, (size_t)strtoul(argv[2], NULL, 10), 0};
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
       [ -n "$used" ] && [ "$used" -ge 0 ] &&
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
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    KestLimits limits = {4000000000u, 1024, 0, 0};
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
# And a program whose own run meets the ceiling before its reading does, which
# is a kind of its own rather than a thing gone wrong: a program that makes as
# much as `churn.kest` makes runs out of heap where a small one is still being
# compiled. See D956.
ran_out_running=0
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
                there=$?
                # A rung that was killed reads as a rung that ran, because
                # what this looks at is whether it refused in words and a run
                # that died said none. So the search walks past it and the
                # ladder it reports has a hole where the compiler crashes:
                # `parse.kest` in four and a half megabytes died here every
                # time this check has ever run, and what anybody saw was a
                # line on the standard error of a check that passed. See D845.
                if [ $there -ge 128 ]; then
                    echo "ceilings: with ${middle}K of memory \`$program\`" \
                         "died while this looked for where it first refuses:" \
                         "it came back $there"
                    failed=1
                    break
                fi
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
    K0605) ran_out_running=$((ran_out_running + 1)) ;;
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
# a check that reads no programs and says the two numbers agree. One of each is
# what that guards against; it asked for two until the tree grew a directory of
# workloads and a program moved from one of these counts to another, which is a
# count of what is in the tree rather than a thing about the language. What is
# held is the shape -- that neither of them is nought -- for the reason the
# rules in `CLAUDE.md` say. See D990.
if [ $read_ran_out -lt 1 ] || [ $wanted_a_machine -lt 1 ]; then
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

# And what a compiler says when it has none left, asked of a particular
# nothing. Which message a half-built program gives depends on exactly which
# allocation failed, so a ladder over ceilings meets one of them by luck and
# nothing can be asked of it twice: `examples/inventory.kest` said four
# different things at four rungs, each of them blaming the program for the
# compiler's afternoon. `KEST_REFUSE_AT` refuses the nth allocation in the
# build that checks itself, which is the only way to mean a particular nothing
# — and what is held here is that a run which says it has run out says that and
# nothing else. See D880.
# Three things are asked of every one of them, because an allocation coming
# back with nothing has three ways to go wrong and only one of them says
# anything: a run that refuses and blames the program, a run that dies rather
# than refusing, and a run that answers as though nothing had happened. The
# last is the quietest and the worst — a program compiled with a piece missing
# and nobody told. See D881.
# How many allocations one way of reading one program takes, found rather than
# written down: a number here would be one more thing to keep in step with the
# compiler, and the answer moves every time anything in the tree does.
allocations_of() {
    low=1
    high=200000
    while [ $((high - low)) -gt 1 ]; do
        middle=$(((high + low) / 2))
        if KEST_REFUSE_AT=$middle ./kest-debug "$1" "$2" \
                >/dev/null 2>&1 </dev/null; then
            high=$middle
        else
            low=$middle
        fi
    done
    echo $high
}

aimed=0
# `machine` is `run` with the compiler's share taken off the front. A program
# that runs spends most of its allocations being read and compiled — for
# `queue.kest` two thousand two hundred of two thousand eight hundred — so a
# spread over the whole of it lands eight refusals in the machine and thirty in
# the compiler. These two are the machine's own life and nothing else: the heap
# it hands arrays and text out of, the frames it stands them on, and what it
# needs to say any of it went wrong. See D882.
for aimed_at in "check examples/inventory.kest" "run examples/queue.kest" \
                "check lib/std/text.kest" "run examples/flags.kest" \
                "emit examples/boxes.kest" "machine examples/words.kest" \
                "machine examples/pieces.kest"; do
    how=${aimed_at%% *}
    what=${aimed_at#* }
    from_one=0
    if [ "$how" = machine ]; then
        how=run
        from_one=$(allocations_of check "$what")
    fi
    takes=$(allocations_of "$how" "$what")
    span=$((takes - from_one))
    step=1
    while [ $step -le 40 ]; do
        at_one=$((from_one + (span * step) / 41))
        [ $at_one -lt 1 ] && at_one=1
        said=$(KEST_REFUSE_AT=$at_one ./kest-debug "$how" "$what" \
               2>&1 </dev/null)
        answered=$?
        step=$((step + 1))
        # A death is not a status here. The build that checks itself catches
        # the signal and writes a report, and what it comes back as is one —
        # the same number a refusal comes back as. So what says one from the
        # other is the report. See D881.
        case "$said" in
        *AddressSanitizer*|*"Sanitizer:"*)
            echo "ceilings: refusing allocation $at_one of $takes in" \
                 "\`kest $how $what\` killed it rather than being refused by it"
            printf '%s\n' "$said" | sed -n '2,4p' | sed 's/^/    /'
            failed=1
            continue
            ;;
        esac
        if [ $answered -ge 128 ]; then
            echo "ceilings: refusing allocation $at_one of $takes in" \
                 "\`kest $how $what\` killed it: it came back $answered rather" \
                 "than saying it had run out"
            failed=1
            continue
        fi
        if [ $answered -eq 0 ]; then
            echo "ceilings: refusing allocation $at_one of $takes in" \
                 "\`kest $how $what\` changed nothing it said, so a piece of" \
                 "the work went missing and the answer came back anyway"
            failed=1
            continue
        fi
        aimed=$((aimed + 1))
        blamed=$(printf '%s' "$said" | grep -o 'K[0-9][0-9][0-9][0-9]' |
                 grep -v -e K0639 -e K0658 | sort -u | tr '\n' ' ')
        if [ -n "$blamed" ]; then
            echo "ceilings: refusing allocation $at_one of $takes in" \
                 "\`kest $how $what\` made it say ${blamed}before it said it" \
                 "had run out, and a compiler with no room left has nothing to" \
                 "say about a program"
            failed=1
        fi
    done
done
if [ $aimed -eq 0 ]; then
    echo "ceilings: no allocation of any program could be" \
         "refused, so nothing here asked what a compiler says when it has" \
         "none left"
    failed=1
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
         "$wanted_its_input that wanted an input, $ran_out_running whose own" \
         "run met the ceiling before their reading did, $no_answer with no" \
         "answer for what they need, $refused_anywhere refused wherever they" \
         "are run, $ran_throughout that ran at every rung and $said_nothing" \
         "that said nothing about what they cost, and a ladder of its own" \
         "under every program this command line reads —" \
         "$walked_down rung(s) over $walked_over of them, each either saying" \
         "what it says with no ceiling at all or naming the refusal it met," \
         "and what a handler keeps between the frames it is called in held" \
         "from both sides over $framed_rungs rung(s), and $told rung(s) where" \
         "a run with no room to write what was wrong wrote it anyway —" \
         "all of it measured on the machine this ran on, and" \
         "$aimed allocation(s) refused one at a time over seven ways of" \
         "reading six programs, two of them the machine's own life with the" \
         "compiler's share taken off the front, each of them a run that said" \
         "it had run out, said nothing else, and neither died nor answered as" \
         "though nothing had happened"
fi
exit $failed
