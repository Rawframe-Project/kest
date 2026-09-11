#!/bin/sh
# Everything, in one command, because "everything passes" said by hand is a
# claim and this is a command. Twice a column has been quietly missing from a
# sweep run by hand — once a command that does not exist, once one that was
# never added — and both times the sweep said it had passed.
#
# Nothing here takes a list of files. A list is the thing that goes stale.
set -u

# A scratch of this run's own. Two of these run at once when the backstops put
# one out of order while another is being asked, and fixed names in `/tmp` are
# two runs writing to one file.
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cd "$(dirname "$0")/.." || exit 1

# And every check asked below takes its room out of this one, because a check
# that leaves a directory behind works until the machine it runs on fills up:
# this gate stopped at `No space left on device` with nine hundred of them in
# `/tmp`, left by a check whose second `trap` had replaced its first. Handing
# the whole run one place to work makes what is left behind a thing this file
# can look at rather than a thing somebody finds later.
TMPDIR="$scratch"/room
export TMPDIR
mkdir "$TMPDIR"

failed=0
say() { printf '%-34s %s\n' "$1" "$2"; }
complain() { say "$1" "$2"; failed=1; }

sources=$(find examples lib -name '*.kest' | sort)
count=$(printf '%s\n' "$sources" | grep -c .)

# Kest under `tools` is an instrument rather than a program: it is held to
# resolving and to formatting, and not to running, because what it does is
# take a while on purpose.
instruments=$(find tools -name '*.kest' | sort)

# And what those lists are, because everything below is a sweep over them: a
# list that came back empty is every check in this file passing without reading
# a file. There is no number here to hold them to — a count is the thing that
# goes stale — but there is a floor, and the floor is one.
if [ -z "$sources" ] || [ -z "$instruments" ]; then
    printf 'check: nothing was found to check; this is not a tree with a\n'
    printf '       language in it\n'
    exit 1
fi

# Built twice, because the two are different programs: the release one is what
# ships and the debug one is what says whether it was right.
if ! make >/dev/null 2>"$scratch"/check-why; then
    complain "build" "the library does not build"
    sed 's/^/    /' "$scratch"/check-why | head -10
    exit 1
fi
if ! make debug embed embed-debug least >/dev/null 2>"$scratch"/check-why; then
    complain "build" "the sanitised build does not build"
    sed 's/^/    /' "$scratch"/check-why | head -10
    exit 1
fi
# And that what was built answers. A build that made no binary, or one that
# cannot start, is every check below this reporting its own confusing failure —
# a probe that passes when a command fails would pass for the wrong reason, and
# `make` saying nothing is not the same as there being something to run.
for built in ./kest ./kest-debug ./examples/embed ./examples/embed-debug ./examples/least; do
    if [ ! -x "$built" ]; then
        complain "build" "$built was built and is not there"
        exit 1
    fi
done
if ! ./kest help >/dev/null 2>&1 || ! ./kest-debug help >/dev/null 2>&1; then
    complain "build" "what was built does not answer"
    exit 1
fi
say "build" "release, sanitised, and both hosts, and all four answer"

# A file with a `main` has to run and answer nought; one without has to
# resolve. Which it is comes from the file rather than from a list here.
ran=0
resolved=0
for file in $sources; do
    # Nothing on the standard input, so an example that reads gets what it
    # would get from an empty file rather than what somebody's terminal
    # happens to have in it. An example is a program that answers the same
    # thing every time or it is not one.
    out=$(./kest run "$file" 2>&1 </dev/null)
    status=$?
    case "$out" in
    *"has no \`main\` to run"*)
        if ./kest check "$file" >/dev/null 2>&1; then
            resolved=$((resolved + 1))
        else
            complain "examples" "$file does not resolve"
        fi
        ;;
    *)
        if [ $status -eq 0 ]; then
            ran=$((ran + 1))
            # And the build that checks itself answers what the build that
            # ships answers. What it checks and the other does not is the
            # things nothing else can see: an arena that has lost track of
            # its own blocks, and the one read in this language that does not
            # ask where it is reading. Neither shows up as a sanitiser
            # report — the byte past the end of a piece of text is a byte the
            # arena handed out for something else — so a run of each and a
            # comparison is what there is. See D409.
            checked_said=$(./kest-debug run "$file" 2>&1 </dev/null)
            checked_status=$?
            if [ "$checked_said" != "$out" ] ||
               [ $checked_status -ne $status ]; then
                complain "examples" \
                    "$file answers differently under the build that checks itself"
                printf '%s\n' "$checked_said" | sed 's/^/    /' | head -4
            fi
        else
            complain "examples" "$file answered $status"
            printf '%s\n' "$out" | sed 's/^/    /' | head -6
            # An example checks itself and says which check failed by the
            # number it answers with. The number is in the file, so the file
            # is where the answer is: this shows the check that returned it,
            # rather than leaving somebody to count the returns.
            awk -v want="$status" '
                /^[ \t]*if / { held = $0; line = NR }
                $0 ~ ("^[ \t]*return " want "[ \t]*$") {
                    if (line == NR - 1 || line == NR - 2) {
                        printf "    %s:%d: %s\n", FILENAME, line, held
                    }
                    printf "    %s:%d: %s\n", FILENAME, NR, $0
                }
            ' "$file" | head -4
        fi
        ;;
    esac
done
# A file from a machine that ends its lines with a carriage return and nothing
# else. It reads and it runs; what this holds is that a message about it points
# somewhere a reader can find, which means counting those as line ends. The
# file is written here rather than kept in the tree, because every file in the
# tree is in the one form and the one form ends a line with one character.
returns="$scratch"/check-returns.kest
printf 'fn main() -> i32 {\r    return nope\r}\r' > "$returns"
said=$(./kest check "$returns" 2>&1 </dev/null)
case "$said" in
*"$returns:2:12"*) ;;
*)
    complain "returns" "a message about a file with carriage returns points nowhere"
    printf '%s\n' "$said" | sed 's/^/    /' | head -3
    ;;
esac
rm -f "$returns"

# The same byte inside a piece of text, which is a different thing: a line end
# there is a byte the program holds, and one written as itself is one nobody
# reading the file can see. A file that crossed machines has them without
# anybody having written one.
inside="$scratch"/check-inside.kest
printf 'fn main() -> i32 {\n    let s = "a\rb"\n    return len(s) - 3\n}\n' \
    > "$inside"
said=$(./kest check "$inside" 2>&1 </dev/null)
case "$said" in
*K0109*) ;;
*)
    complain "returns" "a carriage return written inside text is not refused"
    printf '%s\n' "$said" | sed 's/^/    /' | head -3
    ;;
esac
rm -f "$inside"

# A nought inside text, which is the third way to make a piece of text that
# says less than it holds. The machine refuses the other two — one that comes
# out of an array and one a host hands over — and this one is refused where it
# is written, which is the only one of the three that can be.
nought="$scratch"/check-nought.kest
printf 'fn main() -> i32 {\n    let s = "a\\0b"\n    return len(s) - 3\n}\n' \
    > "$nought"
said=$(./kest check "$nought" 2>&1 </dev/null)
case "$said" in
*K0110*) ;;
*)
    complain "returns" "a nought written inside text is not refused"
    printf '%s\n' "$said" | sed 's/^/    /' | head -3
    ;;
esac
rm -f "$nought"

# And the same nought coming the other way: gathered into a run of bytes and
# asked to be text. Nothing in the tree does that, so this is the only place
# the machine's own refusal is ever heard.
gathered="$scratch"/check-gathered.kest
cat > "$gathered" <<'EOF'
fn main() -> i32 {
    let a: [u8] = array()
    push(a, 104)
    push(a, 0)
    push(a, 105)
    return len(text(a))
}
EOF
said=$(./kest run "$gathered" 2>&1 </dev/null)
case "$said" in
*K0604*"is zero"*) ;;
*)
    complain "returns" "a nought gathered into text is not refused"
    printf '%s\n' "$said" | sed 's/^/    /' | head -3
    ;;
esac
rm -f "$gathered"

# A host asking what came back before anything came back. `kest_gave_text`
# says what is in a frame, and a frame nothing has been called with is
# noughts — a nought where text goes is not an empty piece of text but the
# absence of one, and reading it as text is a crash rather than a message.
# The one host in this tree calls first, so this is where the other way round
# is asked.
asking="$scratch"/check-asking
cat > "$asking.kest" <<'EOF'
enum Word {
    Said(text)
    Nothing
}

fn first() -> Word {
    return Word.Said("hello")
}

// A function taken as a value and called through, so that a host handing a
// number where one was wanted has somewhere for the machine to find out. The
// slot holding a function is a number saying which; a number a host wrote by
// hand says whichever it says, and `kest_call` knows how wide a frame must be
// and not what is in it. See D530.
fn twice(n: i32) -> i32 no.alloc {
    return n + n
}

fn through(step: fn(i32) -> i32 no.alloc, n: i32) -> i32 no.alloc {
    return step(n)
}
EOF
# And a name that is many functions rather than two. A host reaches a copy of
# a generic by walking the copies, and the walk used to gather them into
# sixty-four indexes and answer -1 for the sixty-fifth -- which is how a walk
# ends, so a host stopped there and was told nothing. Eighty-one of one body
# here, which is more than sixty-four and is written rather than counted on:
# the number this asks about is the one `emit` says the program has.
copies="$scratch"/check-copies.kest
{
    printf 'fn two<A, B>(a: A, b: B) -> i32 no.alloc {\n    return 1\n}\n\n'
    printf 'fn many() -> i32 {\n    let sum = 0\n'
    n=0
    for a in i8 i16 i32 i64 u8 u16 u32 u64 f32; do
        for b in i8 i16 i32 i64 u8 u16 u32 u64 f32; do
            case "$a" in f32) x=1.0 ;; *) x=1 ;; esac
            case "$b" in f32) y=1.0 ;; *) y=1 ;; esac
            printf '    let a%d: %s = %s\n    let b%d: %s = %s\n' \
                "$n" "$a" "$x" "$n" "$b" "$y"
            printf '    sum += two(a%d, b%d)\n' "$n" "$n"
            n=$((n + 1))
        done
    done
    printf '    return sum\n}\n'
} > "$copies"
made=$(./kest emit "$copies" 2>/dev/null | grep -c '^fn .*two#') || made=0

cat > "$asking.c" <<'EOF'
#include <stdio.h>
#include "kest.h"

int main(int argc, char **argv) {
    (void)argc;
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT);
    KestHost *host = kest_host_new();
    KestRuntime *runtime = build == NULL ? NULL : kest_start(build, host, NULL);
    if (runtime == NULL) {
        return 2;
    }
    KestValue frame[4] = {{0}};
    char out[64];
    int32_t at = kest_entry(runtime, "first");
    if (kest_gave_text(runtime, at, frame, out, sizeof(out)) >= 0) {
        return 3;
    }
    // And which refusal that was, rather than only that there was one. A host
    // gets `-1` and a report, and the report is the half that says what to do
    // about it. See D426.
    kest_report(runtime, stdout, KEST_FORM_TEXT);
    // And a function value that is not a function. Everything else a host
    // hands over has a width the machine can check; this is a number saying
    // which function, and the only place it can be wrong is where it is
    // called through. See D530.
    KestValue wrong[2] = {{0}};
    wrong[0].integer = 999999;
    wrong[1].integer = 1;
    if (kest_call(runtime, kest_entry(runtime, "through"), wrong, 2)) {
        return 5;
    }
    kest_report(runtime, stdout, KEST_FORM_TEXT);
    // And the same slot with a function in it, which is the half that says
    // the refusal above is about the number rather than about the crossing.
    KestValue right[2] = {{0}};
    right[0].integer = kest_entry(runtime, "twice");
    right[1].integer = 21;
    if (!kest_call(runtime, kest_entry(runtime, "through"), right, 2) ||
        right[0].integer != 42) {
        return 6;
    }
    // And how many functions a name is, counted by walking until the walk
    // ends. A walk that stops short ends the way one that finishes does, so
    // the only thing that can say it stopped short is somebody else's count
    // of the same thing. See D435.
    if (argc > 2) {
        KestBuild *both = kest_build(argv[2], NULL, stderr, KEST_FORM_TEXT);
        KestRuntime *walking = both == NULL ? NULL
                                            : kest_start(both, host, NULL);
        if (walking == NULL) {
            return 4;
        }
        uint32_t copies = 0;
        while (kest_entry_of(walking, "two", copies) >= 0) {
            copies++;
        }
        printf("copies %u\n", copies);
    }
    return 0;
}
EOF
if ! cc -std=c11 -Wall -Wextra -Werror -Iinclude -o "$asking" "$asking.c"         libkest.a -lm 2>"$scratch"/check-why; then
    complain "asking" "the host that asks before calling does not build"
    sed 's/^/    /' "$scratch"/check-why | head -3
elif ! asked=$("$asking" "$asking.kest" 2>/dev/null); then
    complain "asking" "asking what came back before anything did is not a message"
elif [ "${asked#*K0632}" = "$asked" ]; then
    complain "asking" "asking before calling said \`$(printf '%s' "$asked" | head -1)\`"
elif [ "${asked#*K0609}" = "$asked" ]; then
    complain "asking" "a number where a function value was wanted said \
\`$(printf '%s' "$asked" | sed -n '/K06/p' | tail -1)\`"
elif [ "$made" -lt 65 ]; then
    complain "asking" "the program of many copies has $made of them, not enough"
elif ! walked=$("$asking" "$asking.kest" "$copies" 2>/dev/null); then
    complain "asking" "walking the copies of a generic did not finish"
elif [ "${walked#*copies $made}" = "$walked" ]; then
    complain "asking" "the program has $made copies of one body and a host walking them found $(printf '%s' "$walked" | sed -n 's/^copies //p')"
else
    say "asking" "a host asking what came back before anything came back, a \
number where a function value was wanted, and $made copies of one body walked \
to the end"
fi
rm -f "$asking" "$asking.c" "$asking.kest"

# A promise that defers something which allocates. What counts against
# `no.alloc` is what the deferred call does and not the `defer`, which is a
# thing the contract has always held and nothing has ever asked: every `defer`
# in this tree is in a function that promises nothing or defers something that
# takes nothing.
deferred="$scratch"/deferred.kest
cat > "$deferred" <<'EOF'
fn note(log: [i32], n: i32) {
    push(log, n)
}

fn quiet(log: [i32]) -> i32 no.alloc {
    defer note(log, 1)
    return 0
}

fn main() -> i32 {
    let log: [i32] = array()
    return quiet(log)
}
EOF
said=$(./kest check "$deferred" 2>&1 </dev/null)
case "$said" in
*K0401*"promises \`no.alloc\`"*)
    # And the path is the whole of it: what allocates, where the promise was
    # made, and the `defer` in between.
    case "$said" in
    *"defer note(log, 1)"*) ;;
    *)
        complain "returns" "a deferred call that allocates is refused without naming the \`defer\`"
        ;;
    esac
    ;;
*)
    complain "returns" "a promise that defers something which allocates is not refused"
    printf '%s\n' "$said" | sed 's/^/    /' | head -3
    ;;
esac
rm -f "$deferred"

say "returns" "line endings, noughts inside text, and a promise around a \`defer\`"

# And nothing in the tree has anything to say about itself. Four of the
# warnings this compiler gives are about a name nothing reaches — an extern,
# a function, a constant, a shape — and a project that says those to everybody
# else and carries them itself is a project nobody should believe. The sweep
# was three lines of shell run by hand before each of them was written; this is
# where it lives now.
# Asked of the two commands that read a whole program, because they do not
# know the same things: the checker settles names and the compiler settles what
# can be emitted, and `K05xx` is a sentence only the second one says. A library
# file is otherwise only ever compiled as part of something else.
quiet=0
for file in $sources $instruments; do
    said=$( { ./kest check "$file" 2>&1 </dev/null;
              ./kest emit "$file" 2>&1 </dev/null; } |
            grep '^warning\[\|^error\[' | head -3)
    if [ -n "$said" ]; then
        complain "warnings" "$file says something about itself"
        printf '%s\n' "$said" | sed 's/^/    /'
    else
        quiet=$((quiet + 1))
    fi
done
say "warnings" "$quiet file(s) have nothing to say about themselves"

# Every example says which of its checks failed by the number it answers with,
# so every example answers one. A `main` that gives nothing back is a shape the
# language has anyway, and it exits nought — which nothing above can say now
# that no example is written that way.
gives_nothing="$scratch"/quiet-main.kest
cat > "$gives_nothing" <<'EOF'
module quiet

fn main() {
    let n = 1 + 1
}
EOF
if ! ./kest run "$gives_nothing" >/dev/null 2>&1; then
    complain "examples" "a \`main\` that gives nothing back does not exit 0"
fi
rm -f "$gives_nothing"

# What a file calls itself has to be where it is. An import is a path — `import
# game.world` is `game/world.kest` beside the file that wrote it — so a file
# whose `module` line does not match its own path is a file nothing can import,
# and nothing else would ever say so.
for file in $sources $instruments; do
    named=$(sed -n 's/^module \([a-zA-Z0-9_.]*\).*/\1/p' "$file" | head -1)
    path=$(printf '%s' "${file%.kest}" | tr '/' '.')
    case "$path" in
    *"$named") ;;
    *)
        complain "modules" "$file calls itself \`$named\`"
        ;;
    esac
done

say "modules" "every file is where its \`module\` line says it is"

# The library as one project, which is what `kest check *.kest` is for.
# Reading files one at a time never asks whether two of them can be read
# together, and that is where a file named on the command line turned out to
# be a different file from the same one an import reached.
#
# The examples are not one project: they are thirty programs that live in one
# directory, and two of them may put their names under the same one without
# either being wrong. `lib/std` is a project, so it is read as one.
if ! ./kest check lib/std/*.kest >"$scratch"/check-why 2>&1; then
    complain "project" "the library does not check as one project"
    grep -m 4 -E '^(error|warning)' "$scratch"/check-why | sed 's/^/    /'
fi

say "project" "\`lib/std\` reads as one project rather than as files"

# The two layouts, put beside each other. D016 says a value on the stack is a
# run of eight-byte slots and the same value in memory is what a C compiler
# would give it, and says what that costs is "waste that nothing has measured".
# This measures it, and holds the one thing about the pair that has to be true:
# a piece is widened into a slot when it is read out of memory, so nothing can
# be wider in memory than it is on the stack. A type with a sixteen-byte field
# would be, and there is no widening it into eight. See D553.
python3 - $sources <<'LAYOUTS' > "$scratch"/layouts 2>&1
import json
import subprocess
import sys

slots = 0
bytes_of = 0
packed = 0
shapes = 0
widest = None
for path in sys.argv[1:]:
    said = subprocess.run(['./kest', 'check', path, '--json'],
                          capture_output=True, text=True,
                          stdin=subprocess.DEVNULL).stdout
    try:
        held = json.loads(said)
    except ValueError:
        continue
    for one in held.get('types', []):
        if 'slots' not in one or 'bytes' not in one:
            continue
        if one['bytes'] > one['slots'] * 8:
            print("layouts: `%s` is %u bytes in memory and %u slots on the "
                  "stack, and a piece wider than a slot cannot be widened into "
                  "one" % (one['name'], one['bytes'], one['slots']))
            raise SystemExit(1)
        shapes += 1
        slots += one['slots']
        bytes_of += one['bytes']
        # And what the same shape would be if a slot held whatever fitted in
        # it. It is not what this machine does and the number is here so that
        # the next person to argue about it argues with a number: D554 says no
        # to packing and says what it would cost.
        packed += (one['bytes'] + 7) // 8
        gap = one['slots'] * 8 - one['bytes']
        if widest is None or gap > widest[1]:
            widest = (one['name'], gap, one['slots'], one['bytes'])
if shapes == 0 or widest is None:
    print("layouts: nothing here says what a value is laid out as")
    raise SystemExit(1)
print("%u shape(s) take %u slots of stack and %u bytes of memory, %u slots if "
      "a slot held whatever fitted, and the widest gap is `%s` at %u slots "
      "against %u bytes"
      % (shapes, slots, bytes_of, packed, widest[0], widest[2], widest[3]))
LAYOUTS
if [ $? -ne 0 ]; then
    complain "layouts" "$(head -2 "$scratch"/layouts)"
else
    say "layouts" "$(cat "$scratch"/layouts)"
fi

# A file under `lib` has no `main`. The library is a library: what is in it is
# named by whoever imports it, and a `main` there is a program this would run
# as though it were an example and count among the ones that ran. Nothing else
# says so — the reference's table of what runs each rule covers `examples` and
# not `lib`, which is right, because a library module runs no rule of its own.
# See D548.
for file in $(find lib -name '*.kest' | sort); do
    if grep -q '^fn main(' "$file"; then
        complain "project" "$file has a \`main\`, and a file in the library \
is one somebody imports"
    fi
done

say "examples" "$ran ran, $resolved resolved, and one that gives nothing back"

# An instrument is checked, and then run for its answer rather than for its
# number. `make check` does not read a duration — a duration is not a pass or a
# fail, which is why `make time` is a target of its own — but what the one
# measurement answers with is whether it did the work: every entity alive in
# every step of every round, counted, and compared with what that comes to. An
# instrument that stopped measuring would go on printing a number, and a
# smaller number reads like a faster machine. See D549.
for file in $instruments; do
    if ! ./kest check "$file" >/dev/null 2>"$scratch"/check-why; then
        complain "instruments" "$file does not resolve"
        sed 's/^/    /' "$scratch"/check-why | head -6
        continue
    fi
    if ! measured=$(./kest run "$file" 2>"$scratch"/check-why </dev/null); then
        complain "instruments" "$file ran and says it did not do its work"
        sed 's/^/    /' "$scratch"/check-why | head -6
        continue
    fi
    # And the shape of what it says, which is the half of a measurement that is
    # not the number: how many rounds it was the best of and how many entities
    # it was over. A number without a scale is a number nobody can read, and
    # two numbers read a week apart are two measurements of the same thing only
    # if they were taken over the same work. The numbers in the line come from
    # the constants by interpolation, so what this holds is that they are the
    # right constants — `best of 10000 over 7` is a line somebody swapped, and
    # it reads like a measurement. See D579.
    rounds=$(sed -n 's/^const ROUNDS: i32 = \([0-9]*\)$/\1/p' "$file")
    over=$(sed -n 's/^const ENTITIES: i32 = \([0-9]*\)$/\1/p' "$file")
    # An instrument that declares neither is one whose line cannot name them,
    # and the same complaint says so: what is looked for is `best of  over ,`
    # and nothing says that.
    case $measured in
    *"best of $rounds over $over, spread "*"%"*) ;;
    *)
        complain "instruments" "$file did not say what it measured over"
        printf '%s\n' "$measured" | sed 's/^/    /' | head -3
        ;;
    esac
done

# And the third of that line, which is the instrument deciding what to say
# about its own number: under a quarter of spread it says nothing, over it says
# the machine was somebody else's. Nothing here can make a machine busy and
# nothing needs to — the clock is the host's, so a host of this gate's own is
# what makes an instrument say it. What is held is both ways round: a clock
# that ticks evenly and one that loses a round. See D580.
cat > "$scratch"/steady.c <<'HOST'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kest.h"

/* What each round is to look as if it took, in the order they are run. The
   instrument asks the clock twice a round — once before and once after — so
   this hands back a total that grows by the round's own number at the second
   of them, and what the instrument reads as a duration is the difference. */
static long long tock;
static int asked;
static int rounds;
static long long *took;

static void clock_says(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    if (asked % 2 == 1) {
        tock += took[(asked / 2) % rounds];
    }
    asked++;
    frame[0].integer = tock;
}

static void wrote(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    fputs(frame[0].text, stdout);
}

int main(int argc, char **argv) {
    rounds = argc - 2;
    took = calloc((size_t)(rounds > 0 ? rounds : 1), sizeof(long long));
    if (took == NULL || rounds <= 0) {
        return 2;
    }
    for (int i = 0; i < rounds; i++) {
        took[i] = strtoll(argv[i + 2], NULL, 10);
    }
    /* Where `std` lives, which is beside this tree rather than installed: a
       host says it, and this one is run from the root of the tree. */
    KestBuild *build = kest_build(argv[1], "lib/", stderr, KEST_FORM_TEXT);
    if (build == NULL) {
        return 2;
    }
    /* What this host provides against what the program asks for, both ways
       round. A name the program wants and this host has not got is what
       `kest_start` refuses for, by name; a name this host binds that nothing
       asks for is the other way round and nothing refuses it at all — it is a
       host written for a program that has changed since, which goes on
       building and goes on running. So it is read here, where the two lists
       are both in front of somebody. */
    static const char *const provides[] = {"Host.clock", "Io.write"};
    size_t has = sizeof(provides) / sizeof(provides[0]);
    uint32_t asks = 0;
    for (const char *name; (name = kest_build_extern(build, asks)) != NULL;
         asks++) {
        bool known = false;
        for (size_t i = 0; i < has; i++) {
            known = known || strcmp(name, provides[i]) == 0;
        }
        if (!known) {
            fprintf(stderr, "the instrument asks a host for `%s`\n", name);
            return 2;
        }
    }
    if (asks != has) {
        fprintf(stderr, "this host binds %zu names and the instrument asks "
                        "for %u\n", has, asks);
        return 2;
    }
    KestHost *host = kest_host_new();
    if (host == NULL || !kest_host_bind(host, "Host.clock", clock_says, NULL) ||
        !kest_host_bind(host, "Io.write", wrote, NULL)) {
        return 2;
    }
    KestRuntime *runtime = kest_start(build, host, NULL);
    kest_host_free(host);
    if (runtime == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 2;
    }
    KestValue frame[4] = {{0}};
    if (!kest_call(runtime, kest_entry(runtime, KEST_MAIN), frame, 4)) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 3;
    }
    free(took);
    kest_runtime_free(runtime);
    kest_build_free(build);
    return (int)frame[0].integer;
}
HOST
if ! ${CC:-cc} -std=c11 -Wall -Wextra -Werror -Iinclude \
        -o "$scratch"/steady "$scratch"/steady.c libkest.a -lm \
        2>"$scratch"/check-why; then
    complain "instruments" "the host that holds a clock does not build"
    sed 's/^/    /' "$scratch"/check-why | head -5
else
    for file in $instruments; do
        even=$("$scratch"/steady "$file" 100 100 100 100 100 100 100 \
               2>"$scratch"/check-why </dev/null)
        lost=$("$scratch"/steady "$file" 100 100 100 500 100 100 100 \
               2>"$scratch"/check-why </dev/null)
        case $even in
        *"spread 0%") ;;
        *)
            complain "instruments" "$file read an even clock as a spread"
            printf '%s\n' "$even" | sed 's/^/    /' | head -2
            sed 's/^/    /' "$scratch"/check-why | head -3
            ;;
        esac
        case $lost in
        *"the machine was somebody else's") ;;
        *)
            complain "instruments" "$file lost a round and said nothing"
            printf '%s\n' "$lost" | sed 's/^/    /' | head -2
            sed 's/^/    /' "$scratch"/check-why | head -3
            ;;
        esac
    done
fi
say "instruments" "$(printf '%s\n' "$instruments" | grep -c .) resolved, run, saying what it measured over, and told what to say about a machine that was somebody else's"

# The smallest host runs on the program it was written for, and refuses a
# program that asks for a name it has not got rather than binding whatever it
# is handed. A host writer copies this one, so it is held to working and to
# saying no. See D624.
least_wrong=0
if ! ./examples/least >"$scratch"/least-said 2>&1; then
    complain "least" "the smallest host did not run"
    sed 's/^/    /' "$scratch"/least-said | head -4
    least_wrong=1
elif ! grep -q "hello, host" "$scratch"/least-said ||
     ! grep -q "gave back 0" "$scratch"/least-said; then
    complain "least" "the smallest host ran and did not say what crossed"
    sed 's/^/    /' "$scratch"/least-said | head -4
    least_wrong=1
elif ./examples/least examples/embed.kest >"$scratch"/least-other 2>&1; then
    complain "least" "the smallest host ran a program asking for names it has \
not got"
    sed 's/^/    /' "$scratch"/least-other | head -4
    least_wrong=1
elif ! grep -q "does not provide" "$scratch"/least-other; then
    complain "least" "the smallest host refused another program without saying \
which name it has not got"
    sed 's/^/    /' "$scratch"/least-other | head -4
    least_wrong=1
fi

# And the one name it has, asked for in two other shapes: one that wants an
# answer back, and one that hands a number where this host reads text. What an
# extern takes is written in the program and what a host function does with it
# is written in the host: they are two files, and a host that binds on the name
# alone finds out at the first call, in a frame — the second of these reads the
# number as a pointer and is the one a machine cannot catch.
mkdir "$scratch"/least
cat > "$scratch"/least/answering.kest <<'KEST'
module answering

extern fn Host.write(value: text) -> i32

fn main() -> i32 {
    return Host.write("hello\n")
}
KEST
cat > "$scratch"/least/numbering.kest <<'KEST'
module numbering

extern fn Host.write(value: i32)

fn main() -> i32 {
    Host.write(7)
    return 0
}
KEST
# And what a call answers, which a host reads the same way whatever it is: a
# number, or text, or an answer the language has no text of its own for — and
# the last of those says so rather than being written wrongly.
if ! ./examples/least examples/least.kest motto >"$scratch"/least-text 2>&1 ||
   ! grep -q "whatever it is" "$scratch"/least-text; then
    complain "least" "the smallest host did not read back an answer that is \
not a number"
    sed 's/^/    /' "$scratch"/least-text | head -4
    least_wrong=1
fi
if ! ./examples/least examples/least.kest pair >"$scratch"/least-shape 2>&1 ||
   ! grep -q "K0646" "$scratch"/least-shape; then
    complain "least" "the smallest host wrote an answer the language has no \
text of its own for"
    sed 's/^/    /' "$scratch"/least-shape | head -4
    least_wrong=1
fi

# And what to call something with, handed over as words. A function that takes
# text called with one answers; called with none, the frame is noughts and the
# machine refuses it rather than letting the program read no address at all.
if ! ./examples/least examples/least.kest greeting world \
        >"$scratch"/least-word 2>&1 ||
   ! grep -q "hello, world" "$scratch"/least-word; then
    complain "least" "the smallest host did not call with a word what takes one"
    sed 's/^/    /' "$scratch"/least-word | head -4
    least_wrong=1
fi
if ./examples/least examples/least.kest greeting >"$scratch"/least-empty 2>&1 ||
   ! grep -q "\[greeting\] error\[K0636\]" "$scratch"/least-empty; then
    complain "least" "a frame nobody filled was not refused in this host's \
own words"
    sed 's/^/    /' "$scratch"/least-empty | head -4
    least_wrong=1
fi

# And what compiling had to say about a program it compiled. `kest_build`
# writes what stopped it; a shape nothing names stops nothing and is waiting in
# the report, so a host that never asks drops every warning its programs have.
cat > "$scratch"/least/warned.kest <<'KEST'
module warned

struct Nobody {
    n: i32
}

fn main() -> i32 {
    return 0
}
KEST
if ! ./examples/least "$scratch"/least/warned.kest \
        >"$scratch"/least-warned 2>&1 ||
   ! grep -q "K0509" "$scratch"/least-warned; then
    complain "least" "the smallest host said nothing about a program that \
compiled with something to say"
    sed 's/^/    /' "$scratch"/least-warned | head -4
    least_wrong=1
fi

# And a program that asks for nothing, which needs no host at all: the loop
# binds nothing, `kest_start` is handed NULL, and what is left is a build, a
# call and what came back. A host writer meeting Kest with a program of their
# own writes that much and no more.
if ! ./examples/least examples/frame.kest >"$scratch"/least-none 2>&1; then
    complain "least" "the smallest host would not run a program that asks for \
nothing"
    sed 's/^/    /' "$scratch"/least-none | head -4
    least_wrong=1
fi

for shape in answering numbering; do
    if ./examples/least "$scratch"/least/$shape.kest \
            >"$scratch"/least-shape 2>&1; then
        complain "least" "the smallest host bound its own name out of \
\`$shape.kest\`, which asks for another shape of it"
        sed 's/^/    /' "$scratch"/least-shape | head -4
        least_wrong=1
    fi
done
if [ $least_wrong -eq 0 ]; then
    say "least" "the smallest host runs its own program and one that asks for \
nothing, reads back an answer that is not a number and one the language has no \
text of its own for, refuses one that asks for a name it has not got, and two \
that ask for its own in another shape, calls with a word what takes one, and \
says what compiling had to say about a program that compiled"
fi

for host in ./examples/embed ./examples/embed-debug; do
    if ! "$host" >/dev/null 2>"$scratch"/check-why; then
        complain "host" "$host failed"
        sed 's/^/    /' "$scratch"/check-why | head -10
    fi
done
say "host" "both crossings, sanitised and not"

# Every command against every file, under the sanitisers, looking at what it
# said rather than at what it returned: a command that fails for a reason is
# fine and one that walks off the end of an array is not.
# One file, every command, under the sanitisers. It says nothing unless
# something is wrong, which is what lets these run at once and be read back in
# the order the files were given.
# The three that read each file on its own can be asked about all of them in
# one run, which is one mapping of the sanitiser's shadow memory rather than a
# hundred and fourteen. What that loses is which file, so a run that says
# anything is asked again file by file, which is the only time the slow way
# happens.
alone_at_once() {
    command=$1
    out=$(./kest-debug "$command" $sources 2>&1 </dev/null)
    case "$out" in
    *"unknown command"*)
        complain "sanitisers" "there is no \`$command\`"
        return
        ;;
    *ERROR:*|*"runtime error"*|*Sanitizer*)
        ;;
    *)
        return
        ;;
    esac
    for file in $sources; do
        out=$(./kest-debug "$command" "$file" 2>&1 </dev/null)
        sweep=$((sweep + 1))
        case "$out" in
        *ERROR:*|*"runtime error"*|*Sanitizer*)
            complain "sanitisers" "$command $file"
            printf '%s\n' "$out" | grep -m2 -E 'ERROR:|runtime error' |
                sed 's/^/    /'
            ;;
        esac
    done
}

sanitise_one() {
    file=$1
    for command in check run emit; do
        out=$(./kest-debug "$command" "$file" 2>&1 </dev/null)
        case "$out" in
        *"unknown command"*)
            echo "there is no \`$command\`"
            ;;
        *ERROR:*|*"runtime error"*|*Sanitizer*)
            echo "$command $file"
            printf '%s\n' "$out" | grep -m2 -E 'ERROR:|runtime error' |
                sed 's/^/    /'
            ;;
        esac
    done
    out=$(./kest-debug tick "$file" 8 2>&1 </dev/null)
    case "$out" in
    *ERROR:*|*"runtime error"*)
        echo "tick $file"
        printf '%s\n' "$out" | grep -m2 -E 'ERROR:|runtime error' |
            sed 's/^/    /'
        ;;
    esac
}

sweep=0
for command in lex parse fmt; do
    alone_at_once "$command"
    sweep=$((sweep + 1))
done

swept="$scratch"/swept
mkdir "$swept"
at=0
for file in $sources; do
    at=$((at + 1))
    sanitise_one "$file" > "$swept/$(printf %04d $at)" 2>&1 &
    if [ $((at % 8)) -eq 0 ]; then
        # `jobs` says nothing in a script — job control is off — so what
        # holds the number down is counting them: eight are started and
        # waited for, and then eight more.
        wait
    fi
    sweep=$((sweep + 4))
done
wait
at=0
for file in $sources; do
    at=$((at + 1))
    mine="$swept/$(printf %04d $at)"
    if [ -s "$mine" ]; then
        while IFS= read -r line; do
            complain "sanitisers" "$line"
        done < "$mine"
    fi
done
rm -rf "$swept"

# And that the build those ran under is the one that checks itself. Several
# things in this library are shortcuts held by a walk that only that build
# does, and the guard they are behind is a name one compiler defines and
# another answers a question about: a build where it stopped matching would run
# every file above, find nothing, and print this same line. So the two builds
# are asked what they are, and each has to give the other's answer back.
checked=$(./kest-debug --version 2>&1)
shipped=$(./kest --version 2>&1)
case "$checked" in
*checked*) ;;
*)
    complain "sanitisers" "the sanitised build does not check itself: \
$checked"
    ;;
esac
case "$shipped" in
*checked*)
    complain "sanitisers" "the build that ships carries the checks: $shipped"
    ;;
esac
say "sanitisers" "$sweep runs over $count file(s), under a build that says it \
checks itself"

run() {
    what=$1
    shift
    out=$("$@" 2>&1)
    if [ $? -eq 0 ]; then
        say "$what" "$(printf '%s' "$out" | tail -1)"
    else
        complain "$what" "refused"
        printf '%s\n' "$out" | sed 's/^/    /' | head -12
    fi
}

# The tools are asked at once. None of them writes anything the others read:
# each has a scratch of its own, and the one that used to write over a file in
# the tree — `fmt -w`, to see whether a formatted file still says the same
# thing — does it to a copy. What each says is kept and read back in the order
# they are written here, which is the order somebody reads a failure in.
asked="$scratch"/asked
mkdir "$asked"
at=0
# What was asked is written down where it is asked, and what was heard is read
# out of what the asking wrote. A check whose run never started — a shell that
# could not fork, a file nothing could be written to — leaves no answer, and an
# answer nobody left reads exactly like a check that had nothing to say. So the
# two lists are held to each other at the end.
ask() {
    at=$((at + 1))
    what=$1
    shift
    printf '%s\n' "$what" >> "$asked/asked"
    {
        out=$("$@" 2>&1)
        code=$?
        printf '%s\n' "$what"
        printf '%s\n' "$code"
        printf '%s\n' "$out"
    } > "$asked/$(printf %02d $at)" 2>&1 &
}

heard() {
    for mine in "$asked"/*; do
        [ -f "$mine" ] || continue
        case $mine in
            */asked) continue ;;
        esac
        what=$(sed -n 1p "$mine")
        code=$(sed -n 2p "$mine")
        out=$(sed -n '3,$p' "$mine")
        if [ "$code" -eq 0 ]; then
            # What a check says it did is its last line, so a check that says
            # nothing leaves a blank where a sentence goes, and one that says
            # what it did and then says something else is read as the
            # something else. What a detail looks like here is a line that
            # begins with a space; what a summary looks like is a line that
            # does not.
            last=$(printf '%s' "$out" | tail -1)
            case "$last" in
            "")
                complain "$what" "passed and said nothing about what it did"
                ;;
            " "*)
                complain "$what" "said what it did and then said more"
                printf '%s\n' "$out" | tail -3 | sed 's/^/    /'
                ;;
            *)
                say "$what" "$last"
                ;;
            esac
        else
            complain "$what" "refused"
            printf '%s\n' "$out" | sed 's/^/    /' | head -12
        fi
    done
    # Every one that was asked, answered. The order they finished in is not the
    # order they were asked in, so it is the names that are compared and not
    # the two files.
    : > "$asked/answered"
    for mine in "$asked"/*; do
        [ -f "$mine" ] || continue
        case $mine in
            */asked|*/answered) continue ;;
        esac
        sed -n 1p "$mine" >> "$asked/answered"
    done
    for what in $(sort "$asked/asked"); do
        if ! grep -qx "$what" "$asked/answered"; then
            complain "$what" "was asked and said nothing"
        fi
    done
    rm -rf "$asked"
}

# shellcheck disable=SC2086
ask "formatting" tools/check-fmt.sh $sources $instruments
# shellcheck disable=SC2086
ask "commands" tools/check-commands.sh $sources
ask "tables" tools/check-tables.sh
ask "header" tools/check-header.sh
ask "declarations" tools/check-dead.sh
# And the same check over a document with nothing in it, which is what every
# pattern in it finding nothing looks like from outside. A check that reads
# documents with patterns passes when the patterns stop matching, unless it
# refuses to read nothing; this is where that is asked, because no document in
# this tree is empty and none of them can be made so to ask it.
# And the checks that read what they are handed, handed nothing. Every sweep in
# one of those runs no times over an empty list and the count it prints is
# nought, which reads like a success; nothing in this tree is an empty list, so
# what this stands for is a caller that lost its own. It is asked here because
# nothing but the check itself can catch it.
for tool in check-fmt.sh check-commands.sh; do
    if tools/"$tool" >"$scratch"/check-none 2>&1; then
        complain "$tool" "was given nothing and looked at nothing"
        sed 's/^/    /' "$scratch"/check-none | head -3
    elif ! grep -q "nothing was given" "$scratch"/check-none; then
        complain "$tool" "was given nothing and refused for some other reason"
        sed 's/^/    /' "$scratch"/check-none | head -3
    fi
done

empty="$scratch"/check-empty.md
: > "$empty"
if tools/check-docs.sh "$empty" >"$scratch"/check-empty-said 2>&1; then
    complain "documentation" "a document with nothing in it was read and held"
    sed 's/^/    /' "$scratch"/check-empty-said | head -4
elif ! grep -q "is where this reads it from" "$scratch"/check-empty-said; then
    complain "documentation" "a document with nothing in it was refused for \
some other reason"
    sed 's/^/    /' "$scratch"/check-empty-said | head -4
fi

say "nothing" "a document with nothing in it, and two checks handed no files"

ask "lends" tools/check-lends.sh
ask "documentation" tools/check-docs.sh docs/language.md docs/decisions.md
ask "costs" tools/check-costs.sh
ask "ceilings" tools/check-ceilings.sh
ask "backstops" tools/check-backstops.sh

wait
heard

# And what the run leaves on the machine it ran on. Every check above works in
# a room under this one, so what is still there now is what somebody made and
# did not take away. The names are printed rather than counted: a check leaves
# its own name-shaped directory, and one of them is enough to say which check
# it was.
left=$(ls -A "$TMPDIR" 2>/dev/null | head -4)
if [ -n "$left" ]; then
    complain "room" "a check left something behind"
    printf '%s\n' "$left" | sed 's/^/    /'
else
    say "room" "every check handed back the room it took"
fi

if [ $failed -eq 0 ]; then
    echo
    echo "everything passes"
fi
exit $failed
