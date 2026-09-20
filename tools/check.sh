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
if ! make debug embed embed-debug engine engine-debug least tools/inward \
        tools/fuzz-debug bench/measure bench/frame \
        >/dev/null \
        2>"$scratch"/check-why; then
    complain "build" "the sanitised build does not build"
    sed 's/^/    /' "$scratch"/check-why | head -10
    exit 1
fi
# And that what was built answers. A build that made no binary, or one that
# cannot start, is every check below this reporting its own confusing failure —
# a probe that passes when a command fails would pass for the wrong reason, and
# `make` saying nothing is not the same as there being something to run.
for built in ./kest ./kest-debug ./examples/embed ./examples/embed-debug ./examples/engine ./examples/engine-debug ./examples/least ./tools/inward ./bench/measure ./bench/frame; do
    if [ ! -x "$built" ]; then
        complain "build" "$built was built and is not there"
        exit 1
    fi
done
if ! ./kest help >/dev/null 2>&1 || ! ./kest-debug help >/dev/null 2>&1; then
    complain "build" "what was built does not answer"
    exit 1
fi
say "build" "release, sanitised, and every host, and the two command lines \
answer"

# A file with a `main` has to run and answer nought; one without has to
# resolve. Which it is comes from the file rather than from a list here -- read
# out of it, because a program whose doors this command line does not bind says
# why it could not start rather than that there was nothing to run, and the two
# are not the same news.
ran=0
resolved=0
for file in $sources; do
    if ! grep -q '^fn main(' "$file"; then
        if ./kest check "$file" >/dev/null 2>&1; then
            resolved=$((resolved + 1))
        else
            complain "examples" "$file does not resolve"
        fi
        continue
    fi
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

# A nought inside text. It is a character like any other and text carries how
# long it is, so what this asks is that it goes through and is counted: three
# bytes written, three bytes long, and the nought still the second of them.
# Nothing else in the tree writes one. See D971.
nought="$scratch"/check-nought.kest
printf 'fn main() -> i32 {\n    let s = "a\\0b"\n    return len(s) - 3\n}\n' \
    > "$nought"
if ! ./kest run "$nought" >"$scratch"/nought-said 2>&1 </dev/null; then
    complain "returns" "a nought written inside text is not text"
    sed 's/^/    /' "$scratch"/nought-said | head -3
fi
rm -f "$nought"

# And what text is not: a byte that begins no character, gathered into a run of
# bytes and asked to be text. A run of bytes holds whatever it holds and text
# is UTF-8, so this is the door between them and the only place the machine's
# own refusal is ever heard.
gathered="$scratch"/check-gathered.kest
cat > "$gathered" <<'EOF'
fn main() -> i32 {
    let a: [u8] = array()
    push(a, 104)
    push(a, 255)
    push(a, 105)
    return len(text(a))
}
EOF
said=$(./kest run "$gathered" 2>&1 </dev/null)
case "$said" in
*K0604*"begins no character"*) ;;
*)
    complain "returns" "a byte that begins no character is taken as text"
    printf '%s\n' "$said" | sed 's/^/    /' | head -3
    ;;
esac
rm -f "$gathered"

# And the nought the same way round: gathered into a run of bytes and asked to
# be text, where it is a character and goes through.
held="$scratch"/check-held.kest
cat > "$held" <<'EOF'
fn main() -> i32 {
    let a: [u8] = array()
    push(a, 104)
    push(a, 0)
    push(a, 105)
    return len(text(a)) - 3
}
EOF
if ! ./kest run "$held" >"$scratch"/held-said 2>&1 </dev/null; then
    complain "returns" "a nought gathered into text is not three bytes"
    sed 's/^/    /' "$scratch"/held-said | head -3
fi
rm -f "$held"

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
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT, 0);
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
        KestBuild *both = kest_build(argv[2], NULL, stderr, KEST_FORM_TEXT, 0);
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

# The three ways a reference must name nothing, asked of a host because two
# machines is what a host has and a program has one. What holds them is that a
# handout number is taken from one count for the whole process and is never
# handed out twice: another machine's reference, a machine that has been freed,
# and this store's own place from before it was given back are all a number
# this place was never stamped with. See D1033 for what a count of
# worlds could not promise.
identity="$scratch"/check-identity
cat > "$identity.kest" <<'EOF'
struct Thing {
    value: i32
}

fn build(base: i32) -> store<Thing> no.host {
    let world: store<Thing> = store()
    for i in 0..3 {
        add(world, Thing(base + i))
    }
    return world
}

fn first(world: store<Thing>) -> ref<Thing>? no.alloc no.host {
    for who in world {
        return who
    }
    return none
}

fn read(world: store<Thing>, who: ref<Thing>) -> i32 no.alloc no.host {
    if let one = get(world, who) {
        return one.value
    }
    return -1
}

fn drop(world: store<Thing>, who: ref<Thing>) no.alloc no.host {
    remove(world, who)
}

fn again(world: store<Thing>, value: i32) -> ref<Thing> no.host {
    return add(world, Thing(value))
}
EOF
cat > "$identity.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "kest.h"

static int nearer(const void *left, const void *right) {
    int64_t a = *(const int64_t *)left;
    int64_t b = *(const int64_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static int call(KestRuntime *rt, const char *name, KestValue *frame,
                uint32_t slots) {
    int32_t which = kest_entry(rt, name);
    return which >= 0 && kest_call(rt, which, frame, slots);
}

/* A machine holding three things, the store kept so it can be handed back, and
   the first reference read out as the number it is. */
static KestRuntime *a_world(KestBuild *build, KestValue *store, int64_t *first,
                            int32_t base) {
    KestHost *host = kest_host_new();
    KestRuntime *rt = host == NULL ? NULL : kest_start(build, host, NULL);
    kest_host_free(host);
    if (rt == NULL) {
        return NULL;
    }
    KestValue frame[2] = {{0}};
    frame[0].integer = base;
    if (!call(rt, "build", frame, 2)) {
        return NULL;
    }
    *store = frame[0];
    kest_keeps(rt, *store);
    KestValue asking[2] = {{0}};
    asking[0] = *store;
    if (!call(rt, "first", asking, 2) || asking[1].integer == 0) {
        return NULL;
    }
    *first = asking[0].integer;
    return rt;
}

static int32_t read_through(KestRuntime *rt, KestValue store, int64_t who) {
    KestValue frame[3] = {{0}};
    frame[0] = store;
    frame[1].integer = who;
    if (!call(rt, "read", frame, 3)) {
        return -99;
    }
    return (int32_t)frame[0].integer;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    KestBuild *build = kest_build(argv[1], "lib/", stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    KestValue mine = {0};
    int64_t ours = 0;
    KestRuntime *rt = a_world(build, &mine, &ours, 7);
    if (rt == NULL) {
        return 2;
    }
    printf("own %d\n", read_through(rt, mine, ours));

    /* One beside it, which is another machine and must refuse. */
    KestValue theirs = {0};
    int64_t another = 0;
    KestRuntime *beside = a_world(build, &theirs, &another, 1000);
    if (beside == NULL) {
        return 2;
    }
    printf("other %d\n", read_through(beside, theirs, ours));

    /* A machine that is gone, whose reference must not come back to life in
       one made after it. Freed first, so the new machine is the only one
       standing when it is asked. */
    KestValue gone_store = {0};
    int64_t gone_ref = 0;
    KestRuntime *gone = a_world(build, &gone_store, &gone_ref, 2000);
    if (gone == NULL || !kest_runtime_free(gone)) {
        return 2;
    }
    KestValue after_store = {0};
    int64_t after_ref = 0;
    KestRuntime *after = a_world(build, &after_store, &after_ref, 3000);
    if (after == NULL) {
        return 2;
    }
    printf("freed %d\n", read_through(after, after_store, gone_ref));

    /* And a place given back and handed out again, which is the same place and
       not the same thing. */
    KestValue frame[3] = {{0}};
    frame[0] = mine;
    frame[1].integer = ours;
    if (!call(rt, "drop", frame, 3)) {
        return 2;
    }
    KestValue making[3] = {{0}};
    making[0] = mine;
    making[1].integer = 55;
    if (!call(rt, "again", making, 3)) {
        return 2;
    }
    printf("stale %d live %d\n", read_through(rt, mine, ours),
           read_through(rt, mine, making[0].integer));

    /* And the thing the four above rest on, asked of enough machines to see it
       fail if it could: no two of them ever stamp a place with the same
       number. A world used to be sixteen bits of a count of the machines this
       process had made, so the 65,537th was handed the first one's numbers
       again -- and this is that said as the property rather than as the place
       it broke, so it holds whatever the representation becomes. Each machine
       is made, asked for its first reference and freed. A tenth of a second.
       See D1033. */
    long many = 70000;
    int64_t *seen = malloc(sizeof *seen * (size_t)many);
    if (seen == NULL) {
        return 2;
    }
    for (long i = 0; i < many; i++) {
        KestValue spun_store = {0};
        KestRuntime *spun = a_world(build, &spun_store, &seen[i], 1);
        if (spun == NULL || !kest_runtime_free(spun)) {
            return 2;
        }
    }
    qsort(seen, (size_t)many, sizeof *seen, nearer);
    long twice = 0;
    for (long i = 1; i < many; i++) {
        if (seen[i] == seen[i - 1]) {
            twice++;
        }
    }
    printf("over %ld machine(s) %ld reference(s) were handed out twice\n",
           many, twice);
    free(seen);

    kest_runtime_free(after);
    kest_runtime_free(beside);
    kest_runtime_free(rt);
    kest_build_free(build);
    return 0;
}
EOF
if ! cc -std=c11 -Wall -Wextra -Werror -Iinclude -o "$identity" "$identity.c" \
        libkest.a -lm 2>"$scratch"/check-why; then
    complain "identity" "the host that asks what a reference names does not build"
    sed 's/^/    /' "$scratch"/check-why | head -3
elif ! said=$("$identity" "$identity.kest" 2>&1); then
    complain "identity" "the host that asks what a reference names did not run"
    printf '%s\n' "$said" | sed 's/^/    /' | head -4
else
    identity_wrong=""
    case "$said" in
    *"own 7"*) ;;
    *) identity_wrong="a machine does not read its own reference" ;;
    esac
    case "$said" in
    *"other -1"*) ;;
    *) identity_wrong="another machine's reference resolved here" ;;
    esac
    case "$said" in
    *"freed -1"*) ;;
    *) identity_wrong="a freed machine's reference came back to life" ;;
    esac
    case "$said" in
    *"stale -1 live 55"*) ;;
    *) identity_wrong="a place handed out again answers what was there before" ;;
    esac
    case "$said" in
    *"were handed out twice"*) ;;
    *) identity_wrong="the machines were not asked whether any two of them \
stamp a place alike" ;;
    esac
    case "$said" in
    *" 0 reference(s) were handed out twice"*) ;;
    *) identity_wrong="two machines stamped a place with the same number, \
which is one reference naming two things" ;;
    esac
    if [ -n "$identity_wrong" ]; then
        complain "identity" "$identity_wrong"
        printf '%s\n' "$said" | sed 's/^/    /' | head -4
    else
        say "identity" "a reference names nothing in another machine, nothing \
in a machine made after the one it came from was freed, and nothing at a place \
that has been handed out again -- and no two of $(printf '%s' "$said" | sed -n \
's/^over \([0-9]*\) machine.*/\1/p') machines stamp a place alike, which is \
more than the count of worlds used to be able to tell apart"
    fi
fi
rm -f "$identity" "$identity.c" "$identity.kest"

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

# And the other half of what `defer` is, which the reference now says in full:
# it runs on every way out the program itself takes, and a fault is not one of
# those. The ways out a program takes are written down as a program that runs
# and answers nought — `examples/borrow.kest` covers fallthrough, `return`,
# `break`, `continue` and the reverse order of several in one block — and the
# two that cannot be, because a program that faults cannot be an example, are
# here. See D1040.
# One body reaches both of them: the array holds a nought at nought, so an
# index inside it divides by nought and an index outside it is refused before
# the division is reached.
for at in 0 4; do
    faulting="$scratch"/faulting.kest
    case "$at" in
    0) want="K0601" ;;
    *) want="K0604" ;;
    esac
    cat > "$faulting" <<EOF
module faulting

import std.io

fn takes(xs: [i32], which: i32) -> i32 {
    defer io.print("the deferred call ran")
    io.print("the block was entered")
    return 10 / xs[which]
}

fn main() -> i32 {
    let xs = array(1, 0)
    return takes(xs, $at)
}
EOF
    said=$(./kest run "$faulting" 2>&1 </dev/null)
    faulting_wrong=""
    case "$said" in
    *"the block was entered"*) ;;
    *) faulting_wrong="the block holding the \`defer\` was never entered" ;;
    esac
    case "$said" in
    *"$want"*) ;;
    *) faulting_wrong="a program that has to fault did not say $want" ;;
    esac
    case "$said" in
    *"the deferred call ran"*)
        faulting_wrong="a \`defer\` ran after a fault, which is unwinding by \
another name"
        ;;
    esac
    if [ -n "$faulting_wrong" ]; then
        complain "returns" "$faulting_wrong"
        printf '%s\n' "$said" | sed 's/^/    /' | head -4
    fi
    rm -f "$faulting"
done

# And the way out of a body that is not a way out at all: a loop written
# `while true` that nothing breaks out of. A body ending in one of those ends,
# so the `return` a program used to write after it -- a line the machine can
# never reach -- is not written any more. The one with a `break` in it is
# refused, and `check-commands.sh` asks for that; what is asked here is that
# the one without runs and answers. See D1080.
forever="$scratch"/forever.kest
cat > "$forever" <<'EOF'
module forever

import std.io

// Nothing after the loop, because there is nothing after the loop.
fn firstOver(n: i32) -> i32 {
    while true {
        if n > 3 {
            return n * 2
        }
        n += 1
    }
}

// A `break` that leaves a loop inside this one is that loop's, so this one is
// still a loop nothing comes back from.
fn inner(n: i32) -> i32 {
    while true {
        for i in 0..10 {
            if i > n {
                break
            }
        }
        if n > 3 {
            return n
        }
        n += 1
    }
}

fn main() -> i32 {
    io.print("{firstOver(1)} and {inner(1)}")
    return 0
}
EOF
said=$(./kest run "$forever" 2>&1 </dev/null)
case "$said" in
*"8 and 4"*) ;;
*)
    complain "returns" "a body ending in a loop nothing breaks out of did not \
run and answer"
    printf '%s\n' "$said" | sed 's/^/    /' | head -4
    ;;
esac
rm -f "$forever"

say "returns" "line endings, noughts inside text, a promise around a \`defer\`, \
the two ways out of a block a program does not take -- a \`defer\` runs on \
neither -- and a body that ends in a loop nothing breaks out of, which is a \
body that ends"

# What a budget is, asked of both builds. A program that would not stop has to
# stop; a program that would has to be given the number of steps it takes and
# not one fewer; and work an instruction does that is not a step has to be
# charged for, which is the part a budget that only counted steps could not see
# at all. See D921 and D950.
cat >"$scratch"/spin.kest <<'KEST'
module spin

fn main() -> i32 {
    while true {
    }
    return 0
}
KEST
cat >"$scratch"/turns.kest <<'KEST'
module turns

fn turns(n: i32) -> i32 {
    let sum = 0
    for i in 0..n {
        sum += i
    }
    return sum
}

fn main() -> i32 {
    if turns(1000) != 499500 {
        return 1
    }
    return 0
}
KEST
cat >"$scratch"/weighs.kest <<'KEST'
module weighs

fn main() -> i32 {
    let out: [u8] = array(200000, u8(65))
    let big = text(out)
    let joined = "{big}{big}"
    return len(joined) - 400000
}
KEST
for which in ./kest ./kest-debug; do
    if "$which" run --fuel 2000 "$scratch"/spin.kest \
            >"$scratch"/spun 2>&1 </dev/null; then
        complain "budget" "$which ran a loop that never ends to the end"
    elif ! grep -q 'K0659' "$scratch"/spun; then
        complain "budget" "$which said something else about a program that ran \
out of steps"
        sed 's/^/    /' "$scratch"/spun | head -3
    fi
    if ! "$which" run --fuel 1000 "$scratch"/turns.kest \
            >/dev/null 2>&1 </dev/null; then
        complain "budget" "$which would not run a thousand turns inside a \
thousand steps"
    fi
    if "$which" run --fuel 999 "$scratch"/turns.kest \
            >/dev/null 2>&1 </dev/null; then
        complain "budget" "$which ran a thousand turns inside nine hundred and \
ninety-nine steps"
    fi
    if "$which" run --fuel 100 "$scratch"/weighs.kest \
            >/dev/null 2>&1 </dev/null; then
        complain "budget" "$which copied four hundred thousand bytes of text \
inside a hundred steps"
    fi
    if ! "$which" run --fuel 40000 "$scratch"/weighs.kest \
            >/dev/null 2>&1 </dev/null; then
        complain "budget" "$which would not do that work inside forty thousand"
    fi
done
rm -f "$scratch"/spin.kest "$scratch"/turns.kest "$scratch"/weighs.kest
say "budget" "a loop that never ends stops, a thousand turns is a thousand \
steps and not nine hundred and ninety-nine, and work an instruction does is \
charged for"

# Who owns a machine. One machine is one thread's while it runs, and two
# machines of one build are two worlds that may run at once -- what a build
# holds is read-only once it is built, and the one field of it a machine writes
# is the count of what is standing on it. Asked rather than asserted: two
# threads, one build, a machine each, and then a machine cancelled from the
# thread that is not running it. See D952.
threads="$scratch"/threads
cat > "$threads".kest <<'KEST'
module threading

extern fn Watch.started() -> i32

fn work(n: i32) -> i32 no.alloc no.host deterministic {
    let sum = 0
    for i in 0..n {
        sum += i % 7
    }
    return sum
}

// The same work over a piece of it, which is what a shard is. A host that
// splits a frame across machines calls this with a range each and adds the
// answers up; nothing is shared, because there is nothing to share.
fn part(from: i32, upto: i32) -> i32 no.alloc no.host deterministic {
    let sum = 0
    for i in from..upto {
        sum += i % 7
    }
    return sum
}

fn forever() -> i32 {
    let n = Watch.started()
    while true {
        n += 1
    }
    return n
}
KEST
cat > "$threads".c <<'EOF'
#include <stdio.h>
#include <string.h>

#include "kest.h"

#ifdef __STDC_NO_THREADS__
int main(void) {
    printf("no threads\n");
    return 0;
}
#else
#include <stdatomic.h>
#include <threads.h>

static atomic_int running;

static void watch_started(KestValue *frame, KestRuntime *runtime,
                          void *context) {
    (void)runtime;
    (void)context;
    atomic_store(&running, 1);
    frame[0].integer = 0;
}

typedef struct {
    KestRuntime *runtime;
    const char *call;
    int64_t answered;
    int ran;
    // The piece of the work this one was given, for a shard. Nought and nought
    // is a worker that calls with one number instead.
    int64_t from;
    int64_t upto;
} Worker;

static int turning(void *given) {
    Worker *worker = given;
    KestValue frame[4] = {{0}};
    if (worker->upto > 0) {
        frame[0].integer = worker->from;
        frame[1].integer = worker->upto;
    } else {
        frame[0].integer = 100000;
    }
    worker->ran = kest_call(worker->runtime,
                            kest_entry(worker->runtime, worker->call), frame,
                            4) ? 1 : 0;
    worker->answered = frame[0].integer;
    return 0;
}

static KestRuntime *machine(KestBuild *build) {
    KestHost *host = kest_host_new();
    if (host == NULL || !kest_host_bind(host, "Watch.started", watch_started,
                                        NULL)) {
        return NULL;
    }
    KestRuntime *runtime = kest_start(build, host, NULL);
    kest_host_free(host);
    return runtime;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return 2;
    }
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    // Two machines of one build, one thread each, both running at once. What
    // they share is the program and nothing else: the heap, the stack, the
    // stamps and the world are the machine's.
    Worker first = {machine(build), "work", 0, 0, 0, 0};
    Worker second = {machine(build), "work", 0, 0, 0, 0};
    if (first.runtime == NULL || second.runtime == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 3;
    }
    thrd_t one;
    thrd_t two;
    if (thrd_create(&one, turning, &first) != thrd_success ||
        thrd_create(&two, turning, &second) != thrd_success) {
        return 4;
    }
    thrd_join(one, NULL);
    thrd_join(two, NULL);
    if (!first.ran || !second.ran || first.answered != second.answered) {
        fprintf(stderr,
                "two machines of one build answered %lld and %lld\n",
                (long long)first.answered, (long long)second.answered);
        return 5;
    }
    printf("two machines answered %lld\n", (long long)first.answered);

    // And a machine asked to stop by the thread that is not running it, which
    // is one store of one word and is the only thing a host may do to a
    // machine somebody else is running.
    // And a world split across four machines on four threads, which is what
    // partitioning is: four runtimes of one build, each given a quarter of
    // the work, and the four answers added up. What this holds is that the
    // sum is the number one machine gives for the whole of it -- so a host
    // may shard a frame and get the same answer, and `deterministic` survives
    // being cut into pieces. There is no shared memory: what each machine has
    // is its own, and the merge is the host adding four numbers. See D988.
    Worker shards[4];
    thrd_t running_them[4];
    for (int i = 0; i < 4; i++) {
        shards[i].runtime = machine(build);
        shards[i].call = "part";
        shards[i].answered = 0;
        shards[i].ran = 0;
        shards[i].from = i * 25000;
        shards[i].upto = (i + 1) * 25000;
        if (shards[i].runtime == NULL) {
            return 8;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (thrd_create(&running_them[i], turning, &shards[i]) !=
            thrd_success) {
            return 9;
        }
    }
    int64_t merged = 0;
    for (int i = 0; i < 4; i++) {
        thrd_join(running_them[i], NULL);
        if (!shards[i].ran) {
            fprintf(stderr, "a quarter of a world did not run\n");
            return 10;
        }
        merged += shards[i].answered;
    }
    if (merged != first.answered) {
        fprintf(stderr,
                "a world in four machines answered %lld and the whole of it "
                "answered %lld\n",
                (long long)merged, (long long)first.answered);
        return 11;
    }
    printf("four machines over a quarter each answered %lld, which is what "
           "one machine answers for the whole of it\n", (long long)merged);
    for (int i = 0; i < 4; i++) {
        if (!kest_runtime_free(shards[i].runtime)) {
            return 12;
        }
    }

    Worker held = {machine(build), "forever", 0, 0, 0, 0};
    if (held.runtime == NULL) {
        return 3;
    }
    atomic_store(&running, 0);
    thrd_t spinning;
    if (thrd_create(&spinning, turning, &held) != thrd_success) {
        return 4;
    }
    while (atomic_load(&running) == 0) {
    }
    kest_cancel(held.runtime);
    thrd_join(spinning, NULL);
    if (held.ran) {
        fprintf(stderr,
                "a machine cancelled from another thread ran on\n");
        return 6;
    }
    kest_report(held.runtime, stdout, KEST_FORM_TEXT);
    if (!kest_runtime_free(held.runtime) ||
        !kest_runtime_free(first.runtime) ||
        !kest_runtime_free(second.runtime) || !kest_build_free(build)) {
        fprintf(stderr,
                "what two threads made could not be given back\n");
        return 7;
    }
    return 0;
}
#endif
EOF
if ! cc -std=c11 -Wall -Wextra -Werror -Iinclude -o "$threads" "$threads".c \
        libkest.a -lm 2>"$scratch"/check-why; then
    complain "threads" "the host that runs two machines at once does not build"
    sed 's/^/    /' "$scratch"/check-why | head -3
elif ! said=$("$threads" "$threads".kest 2>&1); then
    complain "threads" "two machines of one build on two threads: $said"
elif [ "${said#*no threads}" != "$said" ]; then
    say "threads" "this C library has no threads of its own, so who owns a \
machine was not asked"
elif [ "${said#*K0660}" = "$said" ]; then
    complain "threads" "a machine cancelled from another thread said \
\`$(printf '%s' "$said" | tail -1)\`"
elif [ "${said#*four machines over a quarter each}" = "$said" ]; then
    complain "threads" "a world split across four machines did not answer \
what one machine answers for the whole of it"
    printf '%s\n' "$said" | sed 's/^/    /' | head -3
else
    say "threads" "two machines of one build ran at once, a world in four \
machines over a quarter each answered what one machine answers for the whole \
of it, and one was stopped from the thread that was not running it"
fi

# And the same thing watched rather than argued about. Everything above says
# two machines of one build do not see each other; this is the sanitiser that
# would say so if they did. It is the one this tree did not run -- the other
# two watch memory and cannot be in the same binary as this one -- and what it
# watches is the only shared thing there is: a build every machine reads and
# one count in the process that stamps a place.
#
# The host here uses POSIX threads rather than the C11 ones the probe above
# uses, because this compiler's thread sanitiser does not know
# `thrd_create`: a C11 thread that does nothing at all dies under it on the
# machine this was written on, before any of this library is reached. What is
# being watched is the library and not which door a host opened a thread
# with. See D1053.
races="$scratch"/races
cat > "$races".kest <<'KEST'
module racing

struct Thing {
    name: text
    n: i32
}

// A world of its own in every machine, with a store, text, references into it
// and things taken out again -- which is every part of this library a walk of
// what can still be reached has to follow, done at once on four threads.
fn part(from: i32, upto: i32) -> i32 {
    let world: store<Thing> = store()
    let held: [ref<Thing>] = array()
    let sum = 0
    for i in from..upto {
        let one = add(world, Thing("thing {i}", i))
        push(held, one)
        if i % 3 == 0 {
            remove(world, one)
        }
    }
    for r in held {
        if let one = get(world, r) {
            sum += one.n % 7
        }
    }
    return sum
}

fn main() -> i32 {
    return part(0, 10)
}
KEST
cat > "$races".c <<'EOF'
#include <stdio.h>
#include <pthread.h>

#include "kest.h"

typedef struct {
    KestRuntime *runtime;
    int64_t from;
    int64_t upto;
    int64_t answered;
    int ran;
} Worker;

static void *turning(void *given) {
    Worker *worker = given;
    KestValue frame[4] = {{0}};
    frame[0].integer = worker->from;
    frame[1].integer = worker->upto;
    worker->ran = kest_call(worker->runtime,
                            kest_entry(worker->runtime, "part"), frame, 4)
                      ? 1
                      : 0;
    worker->answered = frame[0].integer;
    return NULL;
}

// The other half of the sentence the reference makes: *machines may be started
// and freed from any thread*. The four above are started and freed on the
// thread that made them, which says nothing about that, so this one starts its
// own, runs it and frees it, all on a thread of its own and all at the same
// time as three others doing the same on one build. What is shared is the
// build's count of how many machines are standing on it -- an atomic, added to
// with `relaxed` and taken from with `release`, read by `kest_build_free` with
// `acquire` (D952) -- and this is the run that watches it. See D1071.
typedef struct {
    KestBuild *build;
    int64_t rounds;
    int64_t answered;
    int ran;
} Alone;

static void *its_own(void *given) {
    Alone *one = given;
    KestRuntime *runtime = kest_start(one->build, NULL, NULL);
    if (runtime == NULL) {
        one->ran = 0;
        return NULL;
    }
    KestValue frame[4] = {{0}};
    frame[0].integer = 0;
    frame[1].integer = one->rounds;
    one->ran = kest_call(runtime, kest_entry(runtime, "part"), frame, 4) ? 1 : 0;
    one->answered = frame[0].integer;
    if (!kest_runtime_free(runtime)) {
        one->ran = 0;
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return 2;
    }
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    enum { MANY = 4, EACH = 2000 };
    Worker shard[MANY];
    pthread_t running[MANY];
    for (int i = 0; i < MANY; i++) {
        shard[i].runtime = kest_start(build, NULL, NULL);
        shard[i].from = (int64_t)i * EACH;
        shard[i].upto = (int64_t)(i + 1) * EACH;
        shard[i].answered = 0;
        shard[i].ran = 0;
        if (shard[i].runtime == NULL) {
            kest_build_report(build, stderr, KEST_FORM_TEXT);
            return 2;
        }
    }
    for (int i = 0; i < MANY; i++) {
        if (pthread_create(&running[i], NULL, turning, &shard[i]) != 0) {
            return 3;
        }
    }
    int64_t merged = 0;
    for (int i = 0; i < MANY; i++) {
        pthread_join(running[i], NULL);
        if (!shard[i].ran) {
            kest_report(shard[i].runtime, stderr, KEST_FORM_TEXT);
            return 4;
        }
        merged += shard[i].answered;
    }
    Worker whole = {kest_start(build, NULL, NULL), 0, MANY * EACH, 0, 0};
    if (whole.runtime == NULL) {
        return 2;
    }
    turning(&whole);
    if (!whole.ran || merged != whole.answered) {
        fprintf(stderr, "four machines answered %lld and one answered %lld\n",
                (long long)merged, (long long)whole.answered);
        return 5;
    }
    printf("four machines each with a world of its own answered %lld, which "
           "is what one machine answers for the whole of it\n",
           (long long)merged);
    for (int i = 0; i < MANY; i++) {
        if (!kest_runtime_free(shard[i].runtime)) {
            return 6;
        }
    }
    kest_runtime_free(whole.runtime);

    // And four machines started, run and freed on four threads at once, which
    // is the sentence about starting and freeing from any thread rather than
    // about running on one.
    Alone alone[MANY];
    pthread_t apart[MANY];
    for (int i = 0; i < MANY; i++) {
        alone[i].build = build;
        alone[i].rounds = EACH;
        alone[i].answered = 0;
        alone[i].ran = 0;
    }
    for (int i = 0; i < MANY; i++) {
        if (pthread_create(&apart[i], NULL, its_own, &alone[i]) != 0) {
            fprintf(stderr, "a thread would not start\n");
            return 2;
        }
    }
    for (int i = 0; i < MANY; i++) {
        pthread_join(apart[i], NULL);
    }
    for (int i = 0; i < MANY; i++) {
        if (!alone[i].ran || alone[i].answered != alone[0].answered) {
            fprintf(stderr,
                    "a machine started on its own thread answered %lld and the "
                    "first answered %lld\n",
                    (long long)alone[i].answered, (long long)alone[0].answered);
            return 2;
        }
    }
    printf("four machines each started, run and freed on a thread of its own\n");

    kest_build_free(build);
    return 0;
}
EOF
printf 'int main(void) { return 0; }\n' > "$scratch"/watching.c
if ! cc -fsanitize=thread -o "$scratch"/watching "$scratch"/watching.c \
        2>/dev/null; then
    say "races" "this compiler cannot watch threads, so two machines of one \
build were not watched"
elif ! make -s races >"$scratch"/check-why 2>&1; then
    complain "races" "the library does not build under the thread sanitiser"
    sed 's/^/    /' "$scratch"/check-why | head -3
elif ! cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=thread \
        -Iinclude -o "$races" "$races".c build/races/*.o -lm -lpthread \
        2>"$scratch"/check-why; then
    complain "races" "the host that watches two machines does not build"
    sed 's/^/    /' "$scratch"/check-why | head -3
else
    said=$(TSAN_OPTIONS=halt_on_error=1 "$races" "$races".kest 2>&1)
    if [ "${said#*ThreadSanitizer}" != "$said" ]; then
        complain "races" "the thread sanitiser saw two machines of one build \
reach the same memory"
        printf '%s\n' "$said" | sed 's/^/    /' | head -6
    elif [ "${said#*four machines each with a world}" = "$said" ]; then
        complain "races" "four machines of one build did not answer what one \
answers for the whole of it"
        printf '%s\n' "$said" | sed 's/^/    /' | head -4
    elif [ "${said#*started, run and freed on a thread of its own}" = "$said" ];
    then
        complain "races" "a machine started and freed on its own thread did \
not answer what the others did"
        printf '%s\n' "$said" | sed 's/^/    /' | head -4
    else
        say "races" "four machines of one build, each with a world of its \
own, ran at once under a build that watches threads, and answered what one \
machine answers for the whole of it — and four more were started, run and \
freed on four threads at once, which is the other half of what the reference \
says a host may do"
    fi
fi
rm -f "$races" "$races".c "$races".kest "$scratch"/watching \
    "$scratch"/watching.c

# What two machines of one build share, said the other way round. Everything
# above holds that they do not see each other; this holds the one thing they
# do, because the reference now says it rather than leaving a host to find it
# out: the program is the build's, so a breakpoint written for one machine is
# an instruction every machine of that build runs into. Written in place is
# what makes a breakpoint cost a machine nobody is debugging nothing at all
# (D991), and the price is this. The host writes the byte through the first
# machine and calls the second, which stops in a body no debugger was ever
# pointed at. See D1077.
sharing="$scratch"/sharing
cat > "$sharing".kest <<'KEST'
module sharing

fn work(n: i32) -> i32 no.alloc no.host deterministic {
    let sum = 0
    for i in 0..n {
        sum += i % 7
    }
    return sum
}
KEST
cat > "$sharing".c <<'EOF'
#include <stdio.h>
#include "kest.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        return 2;
    }
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    KestRuntime *first = kest_start(build, NULL, NULL);
    KestRuntime *second = kest_start(build, NULL, NULL);
    if (first == NULL || second == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 3;
    }
    int32_t here = kest_entry(first, "work");
    int32_t there = kest_entry(second, "work");
    uint32_t many = 0;
    uint32_t also = 0;
    uint8_t *ours = kest_code_of(first, here, &many);
    uint8_t *theirs = kest_code_of(second, there, &also);
    if (ours == NULL || theirs == NULL || many == 0 || many != also) {
        fprintf(stderr, "a machine would not say what it is running\n");
        return 4;
    }
    // The header says every machine of one build reads the same bytes. If this
    // ever stops being true the sentence beside `kest_code_of` and `kest_start`
    // is the thing to change, and so is what a debugger costs.
    if (ours != theirs) {
        fprintf(stderr, "two machines of one build read two programs\n");
        return 5;
    }
    uint8_t was = ours[0];
    ours[0] = kest_break_byte();
    if (theirs[0] != kest_break_byte()) {
        fprintf(stderr, "a byte written into one program was not in the "
                        "other\n");
        return 6;
    }
    KestValue frame[4] = {{0}};
    frame[0].integer = 100;
    if (kest_call(second, there, frame, 4) || kest_stopped(second) < 0) {
        fprintf(stderr, "a breakpoint written into one machine did not stop "
                        "the other\n");
        return 7;
    }
    printf("a breakpoint written for one machine stopped another at %lld\n",
           (long long)kest_stopped(second));
    // And the byte going back is the program going back, for both of them.
    ours[0] = was;
    if (!kest_resume(second, frame, 4)) {
        kest_report(second, stderr, KEST_FORM_TEXT);
        return 8;
    }
    int64_t answered = frame[0].integer;
    KestValue mine[4] = {{0}};
    mine[0].integer = 100;
    if (!kest_call(first, here, mine, 4) || mine[0].integer != answered) {
        fprintf(stderr, "the byte going back did not give the program back\n");
        return 9;
    }
    printf("the byte went back and both answered %lld\n", (long long)answered);
    if (!kest_runtime_free(first) || !kest_runtime_free(second) ||
        !kest_build_free(build)) {
        fprintf(stderr, "what two machines held could not be given back\n");
        return 10;
    }
    return 0;
}
EOF
if ! cc -std=c11 -Wall -Wextra -Werror -Iinclude -o "$sharing" "$sharing".c \
        libkest.a -lm 2>"$scratch"/check-why; then
    complain "sharing" "the host that debugs one machine of two does not build"
    sed 's/^/    /' "$scratch"/check-why | head -3
elif ! said=$("$sharing" "$sharing".kest 2>&1); then
    complain "sharing" "a breakpoint written into one machine of a build: \
$said"
elif [ "${said#*stopped another}" = "$said" ]; then
    complain "sharing" "a breakpoint written for one machine did not reach \
the other, which is what the reference says it does"
    printf '%s\n' "$said" | sed 's/^/    /' | head -3
else
    say "sharing" "two machines of one build read one program: a breakpoint \
written through the first stopped the second in a body no debugger was \
pointed at, and the byte going back gave both of them the program back"
fi
rm -f "$sharing" "$sharing".c "$sharing".kest

# And what a machine that is stopped is: in the middle of a call. Nothing of
# the host's is on its stack, so every door that guards the heap by asking
# whether the program is running heard no, and a walk asked for there read to
# where the slots had got to when the host last called in -- the bottom of the
# stack -- reached nothing, and gave the frames' memory back. The host below
# stops a machine with an array under the stop, asks for all five of those
# doors, and then lets it carry on and add the array up. See D1078.
stopped="$scratch"/stopped
cat > "$stopped".kest <<'KEST'
module stopping

fn inner(n: i32) -> i32 no.alloc {
    return n + 1
}

// Something on the heap in the frame under the stop, which is what the walk
// used to give back: the breakpoint goes in `inner`, so `said` is live and
// reachable from nowhere but a frame.
fn work(n: i32) -> i32 {
    let said: [i32] = array()
    for i in 0..64 {
        push(said, i)
    }
    let got = inner(n)
    let sum = 0
    for i in 0..len(said) {
        sum += said[i]
    }
    return got + sum
}
KEST
cat > "$stopped".c <<'EOF'
#include <stdio.h>
#include "kest.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        return 2;
    }
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    KestRuntime *runtime = kest_start(build, NULL, NULL);
    if (runtime == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 3;
    }
    int32_t work = kest_entry(runtime, "work");
    int32_t inner = kest_entry(runtime, "inner");
    uint32_t many = 0;
    uint8_t *code = kest_code_of(runtime, inner, &many);
    if (work < 0 || code == NULL || many == 0) {
        fprintf(stderr, "there is nothing to put a breakpoint in\n");
        return 4;
    }
    uint8_t was = code[0];
    code[0] = kest_break_byte();
    KestValue frame[4] = {{0}};
    frame[0].integer = 1;
    if (kest_call(runtime, work, frame, 4) || kest_stopped(runtime) < 0 ||
        kest_frames_deep(runtime) != 2) {
        fprintf(stderr, "a machine did not stop under a frame\n");
        return 5;
    }
    size_t holding = kest_heap_used(runtime);
    if (holding == 0) {
        fprintf(stderr, "there is no heap under the stop to take away\n");
        return 6;
    }
    // Every door that would take it: a walk, the heap thrown away, the two
    // halves of a mark, and the ceiling moved under a program standing on it.
    if (kest_collect(runtime) || kest_heap_reset(runtime) ||
        kest_scratch_mark(runtime) != 0 || kest_scratch_rewind(runtime, 1) ||
        kest_heap_allow(runtime, 4096)) {
        fprintf(stderr, "a stopped machine let the heap under it be taken "
                        "away\n");
        return 7;
    }
    if (kest_heap_used(runtime) != holding) {
        fprintf(stderr, "a stopped machine held %zu bytes and then %zu\n",
                holding, kest_heap_used(runtime));
        return 8;
    }
    code[0] = was;
    if (!kest_resume(runtime, frame, 4) || frame[0].integer != 2018) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "a machine that carried on answered %lld\n",
                (long long)frame[0].integer);
        return 9;
    }
    printf("a stopped machine kept the %zu bytes under it through five doors "
           "that would have taken them, and answered %lld\n",
           holding, (long long)frame[0].integer);
    if (!kest_runtime_free(runtime) || !kest_build_free(build)) {
        fprintf(stderr, "what was stopped could not be given back\n");
        return 10;
    }
    return 0;
}
EOF
if ! cc -std=c11 -Wall -Wextra -Werror -Iinclude -o "$stopped" "$stopped".c \
        libkest.a -lm 2>"$scratch"/check-why; then
    complain "stopped" "the host that asks a stopped machine for its heap \
does not build"
    sed 's/^/    /' "$scratch"/check-why | head -3
elif ! said=$("$stopped" "$stopped".kest 2>&1); then
    complain "stopped" "a machine stopped at a breakpoint: $said"
elif [ "${said#*through five doors}" = "$said" ]; then
    complain "stopped" "a stopped machine did not keep what its frames are \
standing on"
    printf '%s\n' "$said" | sed 's/^/    /' | head -3
else
    say "stopped" "a machine stopped at a breakpoint is in the middle of a \
call: the five doors that would take the heap its frames are standing on, or \
move the ceiling over it, refuse -- and it carries on and reads what it was \
holding"
fi
rm -f "$stopped" "$stopped".c "$stopped".kest

# What a world costs when it is worked on rather than grown, which is the
# question a persistent-world language has to answer. `examples/churn.kest` is
# one round written six ways over a world whose live set never changes: a new
# piece of text and a new run of numbers for every thing; the same round
# written into what the thing already holds; a name built in a block of working
# memory and copied into what the thing holds; a run of things that each hold
# text, replaced whole at a size that differs every round; half the world taken
# out at once and made again; and identities going and coming back.
#
# What is held here is that every one of them settles. Not that the first is
# cheaper than the second -- it is not, and it never will be -- but that a
# world with two hundred things in it costs what two hundred things cost
# however long it is driven, which is what a program that abandons memory it
# replaced cannot do. Before D996 the first of them needed more room the longer
# it ran and ran out of sixty-four megabytes in ten thousand rounds.
#
# Read as the most the machine ever held at once rather than as what it is
# holding at the end: what it is holding at the end is wherever the last walk
# left it, and the most is the number a host has to make room for. Ten times
# the rounds and a hundred times the rounds have to answer the same figure,
# under a room tight enough that nothing can hide in it.
memory_most() {
    ./kest profile --room 2M examples/churn.kest -- "$2" "$1" 2>&1 >/dev/null |
        sed -n '1s/.*heap and \([0-9]*\) at most.*/\1/p'
}
for shape in replace reuse keep turn nest burst; do
    ten=$(memory_most "$shape" 2000)
    hundred=$(memory_most "$shape" 20000)
    if [ -z "$ten" ] || [ -z "$hundred" ]; then
        complain "memory" "\`$shape\` would not run in two megabytes at ten \
or a hundred times the rounds"
        continue
    fi
    if [ "$ten" != "$hundred" ]; then
        # A number rather than a fraction: two figures that differ at all are
        # two figures, and what a reader wants is both of them.
        grew=$((hundred - ten))
        if [ "$grew" -lt 0 ]; then
            grew=$((0 - grew))
        fi
        if [ $((grew * 20)) -gt "$ten" ]; then
            complain "memory" "\`$shape\` held $ten bytes at most over two \
thousand rounds and $hundred over twenty thousand"
        fi
    fi
done
say "memory" "six rounds over a world whose live set never changes -- text \
replaced, runs replaced, a name built in working memory, a run of things that \
each hold text, half the world taken out at once, and identities going and \
coming back -- each hold the same memory at ten times the rounds and at a \
hundred times"

# And the same thing said as a ceiling, because a ceiling is what a host gives
# a machine and `--room` is where a program meets it: a shape that settles runs
# in the same room however many rounds it is given. Said of the one that used
# to be the counter-example, in a room a tenth of the one it ran out of.
for turns in 100 1000 10000; do
    if ! ./kest run --room 4M examples/churn.kest -- "$turns" replace \
            >/dev/null 2>&1; then
        complain "memory" "$turns rounds of \`replace\` would not run in four \
megabytes"
    fi
done
say "memory" "the round that replaces what a thing holds runs ten thousand \
times in four megabytes, where it used to run out of sixty-four"

# Every word this language keeps, written where a name belongs. It has to be
# refused there — the parse wants a name and a keyword is not one — and what
# this holds is that it is refused *in a moment*. One of them was not: a
# keyword where a field name goes was refused without being eaten, the loop
# round it recovered to where a statement could begin, which is where it
# already was, and the parse asked the same question for ever, keeping one more
# copy of the answer each time until the host had none left to give. Six lines
# took the machine down and nothing here would have said so. See D841.
#
# Ten places rather than the one, because the loop that stood still is a shape
# there are several of — the fields of a record, the cases of an enum, the
# statements of a block — and every word rather than the one, because which
# words a recovery stops at is a list somebody will add to.
keyword_places="field parameter local function record case flag generic
                arm walk"
keyword_program() {
    case $2 in
    field)
        printf 'struct P {\n    %s: i32\n}\n\nfn main() -> i32 {\n    return 0\n}\n' "$1" ;;
    parameter)
        printf 'fn one(%s: i32) -> i32 {\n    return 0\n}\n\nfn main() -> i32 {\n    return 0\n}\n' "$1" ;;
    local)
        printf 'fn main() -> i32 {\n    let %s = 1\n    return 0\n}\n' "$1" ;;
    function)
        printf 'fn %s() -> i32 {\n    return 0\n}\n\nfn main() -> i32 {\n    return 0\n}\n' "$1" ;;
    record)
        printf 'struct %s {\n    x: i32\n}\n\nfn main() -> i32 {\n    return 0\n}\n' "$1" ;;
    case)
        printf 'enum D {\n    %s\n}\n\nfn main() -> i32 {\n    return 0\n}\n' "$1" ;;
    flag)
        printf 'flags S: u8 {\n    %s\n}\n\nfn main() -> i32 {\n    return 0\n}\n' "$1" ;;
    generic)
        printf 'struct P<%s> {\n    x: i32\n}\n\nfn main() -> i32 {\n    return 0\n}\n' "$1" ;;
    arm)
        printf 'enum D {\n    Open(i32)\n}\n\nfn main() -> i32 {\n    let d = D.Open(1)\n    return match d {\n        Open(%s) -> 0\n    }\n}\n' "$1" ;;
    walk)
        printf 'fn main() -> i32 {\n    for %s in [1, 2] {\n        return 1\n    }\n    return 0\n}\n' "$1" ;;
    esac
}
# Read where the lexer's own list is read from, which is held beside the lexer
# by `check-tables.sh`: a word held and not printed there is a word this would
# never try.
keywords=$(awk '/^## Keywords$/ {inside = 1; next}
                inside && /^```$/ {fence++; next}
                inside && fence == 1' docs/language.md)
if [ -z "$keywords" ]; then
    complain "keywords" "the reference prints no keywords, so none were tried"
fi
kept="$scratch"/check-keyword.kest
tried=0
for word in $keywords; do
    for place in $keyword_places; do
        keyword_program "$word" "$place" > "$kept"
        # A while to answer in, because what this is looking for is a compiler
        # that does not. Every one of these answers in milliseconds; the number
        # is a wall, not a measurement.
        said=$(timeout 10 ./kest check "$kept" 2>&1 </dev/null)
        why=$?
        tried=$((tried + 1))
        if [ "$why" -eq 124 ]; then
            complain "keywords" "\`$word\` where a $place name belongs is never answered"
        elif [ "$why" -eq 0 ]; then
            complain "keywords" "\`$word\` is taken as a $place name"
        elif [ -z "$said" ]; then
            complain "keywords" "\`$word\` where a $place name belongs is refused and nothing is said"
        fi
    done
done
rm -f "$kept"
say "keywords" "every one of the $(printf '%s\n' $keywords | grep -c .) word(s) this language keeps is refused, and in a moment, in each of $(printf '%s\n' $keyword_places | grep -c .) place(s) a name belongs: $tried run(s)"

# What this tree may not do, said by itself about itself. A promise is proved by
# the compiler wherever it is written; whether it is written wherever it could be
# is not proved by anything, and a promise nobody writes is a promise nobody
# keeps. So every function in the library is asked, once for each promise there
# is: a copy of it with that promise on every signature is checked, and what the
# compiler refuses there has to be exactly what carries it in none.
#
# Counted rather than compared name by name, and the count is the comparison: a
# function that promises it here is proved to keep it, so it is never one of the
# refused, and the refused are therefore always among the ones promising
# nothing. Equal counts is then equal lists. See D854 and D855.
#
# Both promises, because the older of the two was written on eighty-six
# signatures by somebody and asked for by nothing: a promise written wherever it
# can be kept is a rule or it is a habit, and the two are told apart by whether
# anything notices when it stops being true.
promise_kept=""
for promise in no.alloc no.host deterministic; do
    promised="$scratch"/promised
    rm -rf "$promised"
    cp -r lib "$promised"
    for one in "$promised"/std/*.kest; do
        sed -i "/$promise/! s/^\(fn [^{]*\) {\$/\1 $promise {/" "$one"
    done
    # Until it stops finding any. A body that reaches the heap only through
    # something else is not refused while that something else promises too, so
    # one pass answers about the ones that reach it themselves and says
    # nothing about the ones above them. `table.compact` was the first of
    # those in this library: it allocates by calling `empty`, `refill` and
    # `set`, and with the promise written on all four nothing was wrong with
    # any of them. So the promise comes off whatever was refused and the
    # question is asked again, until asking it again finds nobody -- which is
    # the same propagation the contract proof does, done from outside.
    # See D1052.
    refused=0
    while true; do
        said=$(KEST_LIB="$promised" ./kest check "$promised"/std/*.kest 2>&1 \
            </dev/null | grep "^error\[K040[12]\].*promises \`$promise\`" \
            || true)
        found=$(printf '%s' "$said" | grep -c . || true)
        if [ "$found" -eq 0 ]; then
            break
        fi
        refused=$((refused + found))
        printf '%s\n' "$said" |
            sed -n 's/.*`\([A-Za-z0-9_.]*\)` promises.*/\1/p' |
            sed 's/.*\.//' | sort -u > "$scratch"/refused-names
        while read -r one; do
            [ -n "$one" ] || continue
            sed -i "s/^\(fn $one\b[^{]*\) $promise \(.*\)\$/\1 \2/" \
                "$promised"/std/*.kest
            sed -i "s/^\(fn $one\b[^{]*\) $promise {\$/\1 {/" \
                "$promised"/std/*.kest
        done < "$scratch"/refused-names
    done
    without=$(cat lib/std/*.kest | grep '^fn ' | grep -vc "$promise" || true)
    keeps=$(cat lib/std/*.kest | grep '^fn ' | grep -c "$promise" || true)
    if [ "$refused" -ne "$without" ]; then
        complain "promises" "writing \`$promise\` on every function in the \
library refuses $refused of them and $without are written without it, so \
$((without - refused)) could promise it and do not"
    fi
    # And the same question of the examples, where a promise belongs at the
    # boundary rather than everywhere. The reference says it in as many words:
    # a promise is written at entry points, and a callee in the same unit is
    # judged by its body. Every function of the library is an entry point
    # because another unit calls it; in an example the entry points are what a
    # host enters, which is `main` and the two handlers — so that is what is
    # asked, and the rest of an example is left to be read rather than
    # decorated. See D856.
    #
    # Copied under its own name, because a file that says `module
    # examples.game` is found by the path its module spells and a copy under
    # another name is a program whose imports cannot be read. That is how this
    # first counted: every rung stopped at `cannot read`, before the promise
    # was ever weighed, and answered that thirty-two functions could promise
    # what two of them could not.
    rm -rf "$scratch"/side
    mkdir "$scratch"/side
    cp -r examples "$scratch"/side/examples
    for one in $(find "$scratch"/side/examples -name '*.kest' | sort); do
        sed -i "/$promise/! s/^\(fn \(main\|onEvent\|onEvents\)[( ][^{]*\) \
{\$/\1 $promise {/" "$one"
    done
    entered=0
    for one in $(find "$scratch"/side/examples -name '*.kest' | sort); do
        entered=$((entered + $(./kest check "$one" 2>&1 </dev/null |
            grep -c "^error\[K040[12]\].*promises \`$promise\`" || true)))
    done
    at_the_door=$(find examples -name '*.kest' -exec cat {} \; |
        grep '^fn \(main\|onEvent\|onEvents\)[( ]' | grep -vc "$promise" \
        || true)
    if [ "$entered" -ne "$at_the_door" ]; then
        complain "promises" "writing \`$promise\` on every function a host \
enters in the examples refuses $entered of them and $at_the_door are written \
without it, so $((at_the_door - entered)) could promise it and do not"
    fi
    rm -rf "$scratch"/side
    doors=$(find examples -name '*.kest' -exec cat {} \; |
        grep '^fn \(main\|onEvent\|onEvents\)[( ]' | grep -c "$promise" \
        || true)
    promise_kept="$promise_kept, \`$promise\` on $keeps of the library's and \
$doors of the doors a host enters in the examples, against $without and \
$at_the_door the compiler refuses it to"
done
rm -rf "$promised"
say "promises" "every function that can keep a promise says so — \
${promise_kept#, }"

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

# And a project the way somebody has one: a `kest.project`, sources under it,
# tests beside them, and the commands a reader is told to type. `examples/slice`
# is section 35's vertical slice -- one program exercising a long-lived world,
# churning references, an inventory, rules under a promise, a save read back,
# two modules whose names end in one word, and text a person reads. Running its
# files one at a time is what the sweeps above do; this is the other question,
# which is whether the project is a project. See D1066.
tree=$(pwd)
if ! (cd "$tree"/examples/slice && "$tree"/kest build) >"$scratch"/check-why 2>&1; then
    complain "project" "\`kest build\` does not work in examples/slice"
    sed 's/^/    /' "$scratch"/check-why | head -6
fi
if ! (cd "$tree"/examples/slice && "$tree"/kest test tests/rounds.kest) \
        >"$scratch"/check-why 2>&1; then
    complain "project" "\`kest test\` does not work in examples/slice"
    sed 's/^/    /' "$scratch"/check-why | head -6
fi
# And without naming one, which is what the manifest's `tests` line is for: a
# project is a thing to be inside rather than a thing to name at every command.
# The line was read and nothing asked for it until D1082, so `kest test` in a
# project ran nothing and said so as though that were a pass.
if ! (cd "$tree"/examples/slice && "$tree"/kest test) \
        >"$scratch"/check-why 2>&1; then
    complain "project" "\`kest test\` with nothing named does not run what \
the project says its tests are"
    sed 's/^/    /' "$scratch"/check-why | head -6
elif ! grep -q "rounds.kest" "$scratch"/check-why; then
    complain "project" "\`kest test\` with nothing named ran something other \
than what the project says its tests are"
    sed 's/^/    /' "$scratch"/check-why | head -6
fi
# And a project that says where its tests are and has none there, which is a
# project saying something that is not so: a run of no tests that answers
# nought is a gate that passes for having done nothing.
empty_project="$scratch"/empty
mkdir -p "$empty_project"/src "$empty_project"/tests
cat > "$empty_project"/kest.project <<'PROJECT'
project empty
entry src/main.kest
source src
tests tests
PROJECT
cat > "$empty_project"/src/main.kest <<'KEST'
module empty

fn main() -> i32 {
    return 0
}
KEST
if (cd "$empty_project" && "$tree"/kest test) >"$scratch"/check-why 2>&1; then
    complain "project" "a project with no program where it says its tests are \
answered as though its tests had passed"
    sed 's/^/    /' "$scratch"/check-why | head -4
elif ! grep -q "K0649" "$scratch"/check-why; then
    complain "project" "a project with no program where it says its tests are \
was refused without saying which refusal it was"
    sed 's/^/    /' "$scratch"/check-why | head -4
fi
rm -rf "$empty_project"

say "project" "\`lib/std\` reads as one project rather than as files, and \
\`examples/slice\` builds and tests as the project it is -- named and not \
named, with a project that says where its tests are and has none there \
refused rather than passed"

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
# And every layout a module writes says which type it is the layout of. What
# comes back beside it is how many are written the same as one already laid
# out, which is not the same as how many are the same type: two `T` standing
# for two things in two copies of one generic are two types that print alike.
# See D779, D780. Two
# that differ only in that read as one without it -- every `[T]` is one word
# whatever `T` is -- so a reader counting what a module holds counts wrongly,
# and the machine that packs a value across the boundary reads the very field
# the list left out. See D779.
told = 0
twice = 0
for path in sys.argv[1:]:
    said = subprocess.run(['./kest', 'emit', path, '--json'],
                          capture_output=True, text=True,
                          stdin=subprocess.DEVNULL).stdout
    try:
        held = json.loads(said)
    except ValueError:
        continue
    seen = set()
    for one in held.get('layouts', []):
        if one.get('of'):
            told += 1
            if one['of'] in seen:
                twice += 1
            seen.add(one['of'])
if told == 0:
    print("layouts: nothing here says which type a layout is the layout of")
    raise SystemExit(1)
# What a shape takes in memory is this machine's: a handle is eight bytes where
# a pointer is eight bytes and something else elsewhere, and the slots beside it
# are the language's. So the line says whose the numbers are, the way the two
# checks that measure a machine do. See D690.
print("%u shape(s) take %u slots of stack and %u bytes of memory, %u slots if "
      "a slot held whatever fitted, and the widest gap is `%s` at %u slots "
      "against %u bytes, and %u layout(s) each saying which type they are of, "
      "%u written the same as one already laid out, laid out for the machine "
      "this ran on"
      % (shapes, slots, bytes_of, packed, widest[0], widest[2], widest[3],
         told, twice))
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

# The same programs compiled the other way. `KEST_PLAIN` turns off the fusions
# the lowering makes, so the same body comes out as more instructions doing the
# same thing -- and what holds a fusion to being one is that the program
# answers what it answered. It is the whole of the differential test a
# transformation needs: not that the numbers look right, but that two ways of
# writing the same body down are the same program.
#
# Both the answer and everything written, because a program that answers nought
# either way and prints something else the second time is a program one of the
# two got wrong. See D1009 and D1013.
plainly=0
for file in $sources; do
    if ! grep -q '^fn main(' "$file"; then
        continue
    fi
    fused_said=$(./kest run "$file" 2>&1 </dev/null)
    fused_was=$?
    plain_said=$(KEST_PLAIN=1 ./kest run "$file" 2>&1 </dev/null)
    plain_was=$?
    # And the same again with the optimizer turned off, which is the other
    # half of the same test: `KEST_PLAIN` holds the lowering's fusions and
    # `KEST_NOOPT` holds what the optimizer does to a body before the lowering
    # reads it. A pass that changes what a program answers is a pass that is
    # wrong, whatever it saved. See D1025.
    bare_said=$(KEST_NOOPT=1 ./kest run "$file" 2>&1 </dev/null)
    bare_was=$?
    case "$fused_said" in
    *"has no \`main\` to run"*) continue ;;
    esac
    if [ "$fused_was" -ne "$plain_was" ]; then
        complain "optimized" "$file answers $fused_was fused and $plain_was \
plainly"
        continue
    fi
    if [ "$fused_said" != "$plain_said" ]; then
        complain "optimized" "$file says something else when it is compiled \
plainly"
        continue
    fi
    if [ "$fused_was" -ne "$bare_was" ]; then
        complain "optimized" "$file answers $fused_was optimized and \
$bare_was with the optimizer off"
        continue
    fi
    if [ "$fused_said" != "$bare_said" ]; then
        complain "optimized" "$file says something else with the optimizer \
turned off"
        continue
    fi
    plainly=$((plainly + 1))
done
# And one that is not a program at all, to see the two ways differ where they
# are meant to: what `emit` prints is the instructions, and a body the lowering
# fused is fewer of them. A differential test that could not tell the two
# builds apart would be one comparing a thing with itself.
fused_code=$(./kest emit bench/kernel.kest 2>/dev/null | grep -c '^ ')
plain_code=$(KEST_PLAIN=1 ./kest emit bench/kernel.kest 2>/dev/null | grep -c '^ ')
# The optimizer is read the same way and on another program, because the
# kernel has no copy in it to take: what a pass does not fire on says nothing
# about whether it fires.
kept_code=$(./kest emit bench/agents.kest 2>/dev/null | grep -c '^ ')
bare_code=$(KEST_NOOPT=1 ./kest emit bench/agents.kest 2>/dev/null | grep -c '^ ')
if [ "$kept_code" -ge "$bare_code" ]; then
    complain "optimized" "a body compiled with the optimizer and without it \
writes the same number of instructions, so the optimizer is not happening"
fi
if [ "$fused_code" -ge "$plain_code" ]; then
    complain "optimized" "the two ways of compiling a body write the same \
number of instructions, so one of them is not happening"
else
    say "optimized" "$plainly program(s) answer the same thing and write the \
same words compiled either way, and the two read for their instructions are \
$fused_code fused against $plain_code plainly and $kept_code optimized \
against $bare_code not"
fi

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
    # What it was over, under whichever name that scale has: one instrument
    # counts entities, one counts calls and one counts reads, and what the rule
    # is about is that the number in the line is the constant the work was done
    # with rather than a number somebody typed.
    over=$(sed -n 's/^const \(ENTITIES\|CALLS\|READS\): i32 = \([0-9]*\)$/\2/p' \
        "$file")
    # An instrument that declares neither is one whose line cannot name them,
    # and the same complaint says so: what is looked for is `best of  over `
    # and nothing says that.
    case $measured in
    *"best of $rounds over $over"*", spread "*"%"*) ;;
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
#include <math.h>
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

/* The one crossing an instrument makes that is not the clock and not the
   writer: something cheap, so that what is being weighed against a call in the
   program is the crossing rather than the work on the other side of it. */
static void floored(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].real = floor(frame[0].real);
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
    KestBuild *build = kest_build(argv[1], "lib/", stderr, KEST_FORM_TEXT, 0);
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
    static const char *const provides[] = {"Host.clock", "Io.write",
                                           "Math.floor"};
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
    /* And the other way round — a name this host binds that nothing asks for —
       is asked outside, where every instrument is in front of somebody at
       once. One of them asks for two of these and the other for three, so
       there is no number here that is right for both. */
    KestHost *host = kest_host_new();
    if (host == NULL || !kest_host_bind(host, "Host.clock", clock_says, NULL) ||
        !kest_host_bind(host, "Io.write", wrote, NULL) ||
        !kest_host_bind(host, "Math.floor", floored, NULL)) {
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
    # And the other half of the rule the host inside keeps. It refuses an
    # instrument that asks for a name it has not got; a name it binds that no
    # instrument asks for is the same mistake the other way round — a host
    # written for a program that has changed since, which goes on building and
    # goes on running. Asked of the whole list at once, because one instrument
    # asks for two of them and the other for three, and asked of what a program
    # asks rather than of what it writes: `Io.write` is the library's and no
    # instrument names it.
    asked_for=$(for file in $instruments; do
        ./kest emit "$file" 2>/dev/null </dev/null | sed -n 's/^host //p'
    done | sort -u)
    for name in Host.clock Io.write Math.floor; do
        case "$asked_for" in
        *"$name"*) ;;
        *)
            complain "instruments" "the host that holds a clock binds \
\`$name\` and no instrument asks for it"
            ;;
        esac
    done
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
# And the other direction, which is a host rather than an instrument: a program
# cannot measure a call into itself, so what measures one is C. It is built with
# everything else at the top, where everything this gate reaches for is built;
# what is read here is whether it did its work and what shape its line is. The
# number is for `make time`, like the other two. See D859.
inward_said=""
if ! inward_said=$(./tools/inward 2>"$scratch"/check-why </dev/null); then
    complain "instruments" "the host that measures a call in says it did not \
do its work"
    sed 's/^/    /' "$scratch"/check-why | head -6
else
    inward_rounds=$(sed -n 's/^#define ROUNDS \([0-9]*\)$/\1/p' tools/inward.c)
    inward_over=$(sed -n 's/^#define CALLS \([0-9]*\)$/\1/p' tools/inward.c)
    case $inward_said in
    *"best of $inward_rounds over $inward_over"*", spread "*"%"*) ;;
    *)
        complain "instruments" "the host that measures a call in did not say \
what it measured over"
        printf '%s\n' "$inward_said" | sed 's/^/    /' | head -3
        ;;
    esac
fi

say "instruments" "$(printf '%s\n' "$instruments" | grep -c .) resolved, run, saying what it measured over, and told what to say about a machine that was somebody else's, and a host of this gate's own measuring the crossing the other way"

# The four families of workload, at the smallest scale that still fills every
# row. Nothing here is a duration and nothing here is about speed: what it
# holds is that the instrument is still attached to the thing it measures. It
# was not. `bench/families.sh` calls twenty-three micro bodies by name, and
# D1039 made a chunk compile under the whole module name, so every one of them
# printed `would not run` under a heading and a family with numbers in it --
# which reads like a table. `bench` is not in the gate because a duration is
# not a pass or a fail, and that is the reason nothing said a word. A row that
# is a number is a pass or a fail. See D1057.
if ! QUICKLY=1 sh bench/families.sh >"$scratch"/families 2>&1; then
    complain "benches" "the four families would not run"
    sed 's/^/    /' "$scratch"/families | tail -6
elif grep -q "would not run" "$scratch"/families; then
    complain "benches" "a row of a family is not a number"
    grep -n "would not run" "$scratch"/families | sed 's/^/    /' | head -6
else
    families=$(grep -c "^[a-z][A-Za-z]*  *[0-9]" "$scratch"/families)
    say "benches" "the four families of workload run at the smallest scale \
that fills them, and $families row(s) of them are a number rather than a \
reason: an instrument that has come away from what it measures prints a table \
and it is not a duration that says so"
fi

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

# And what a frame of it costs. The program the smallest host runs has a run of
# calls that comes back round, so it has no worst chain to add up: a frame is at
# most the widest body and the host says how many frames there are, so the slots
# follow from the frames. See D815.
if ! grep -q "has no least, and is" "$scratch"/least-said ||
   ! grep -q "on its own is" "$scratch"/least-said ||
   ! grep -q "called back in from" "$scratch"/least-said ||
   ! grep -q "frames, so it takes" "$scratch"/least-said; then
    complain "least" "the smallest host ran its own program and said nothing \
about what a frame of it costs, what one name of it wants or where it could \
be called back in from, or did not run it in a machine sized by that"
    sed 's/^/    /' "$scratch"/least-said | head -4
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
# A shape is written the way a program writes one since D876, so this is the
# host reading one back rather than being refused it.
if ! ./examples/least examples/least.kest pair >"$scratch"/least-shape 2>&1 ||
   ! grep -q "gave back Pair(1, 2)" "$scratch"/least-shape; then
    complain "least" "the smallest host did not read back a shape written the \
way a program writes one"
    sed 's/^/    /' "$scratch"/least-shape | tail -4
    least_wrong=1
fi
# And what still has none, which is a handle: what it says as text is what is
# behind it, and reaching through one is the host's question.
if ! ./examples/least examples/least.kest held >"$scratch"/least-held 2>&1 ||
   ! grep -q "K0646" "$scratch"/least-held; then
    complain "least" "the smallest host wrote an answer the language has no \
text of its own for"
    sed 's/^/    /' "$scratch"/least-held | tail -4
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
   ! grep -q "\] warning\[K0509\]" "$scratch"/least-warned; then
    complain "least" "the smallest host said nothing of its own about a \
program that compiled with something to say"
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
nothing, reads back an answer that is not a number, a shape written the way a \
program writes one and a handle the language has no text of its own for, \
refuses one that asks for a name it has not got, and two \
that ask for its own in another shape, calls with a word what takes one, says \
what a frame of a program with no least costs, what one name of it wants and \
where it could be called back in from, runs it in a machine sized by that, \
and \
says what compiling had to say about a program that compiled"
fi

# The meta-test that asks every door, and the engine that drives a world a
# frame at a time and reloads the program under it. Both under both builds,
# because a host is where the public boundary is crossed in both directions and
# the sanitised build is the only thing that can say whether that went right.
for host in ./examples/embed ./examples/embed-debug ./examples/engine \
        ./examples/engine-debug; do
    if ! "$host" >/dev/null 2>"$scratch"/check-why; then
        complain "host" "$host failed"
        sed 's/^/    /' "$scratch"/check-why | head -10
    fi
done
say "host" "every crossing, sanitised and not: the one that asks every door \
and the engine that drives a world and reloads under it"

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
        elif [ -z "$out" ]; then
            # The other half of the sentence above it. A check that passes says
            # what it did; a check that refuses says what is wrong — and one
            # that refuses with nothing to say leaves a reader a word and no
            # reason. It is what `failed = 1` under a condition whose two
            # complaints both found nothing looks like from here: twelve
            # conditions in `check-tables.sh` compare two lists that differ by
            # order alone if anything ever stops sorting them, and that is
            # exactly this. See D878.
            complain "$what" "refused and said nothing about why"
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
ask "documentation" tools/check-docs.sh docs/language.md \
    docs/decisions.md CHANGELOG.md
ask "costs" tools/check-costs.sh
ask "ceilings" tools/check-ceilings.sh
ask "backstops" tools/check-backstops.sh

wait
heard

# The edits a reload has to have an answer for, each one driven through the
# host that does the whole protocol. What is held is not that every edit
# reloads -- most of them must not -- but that every one of them ends with a
# world: either the new program's, or the one the host was already holding,
# and never half of either. See D985.
#
# One sentence, because what went wrong is written into it.
mkdir -p "$scratch"/reloading
reload_wrong=""
for edit in \
    "a body only|s/^fn describe(w: World) -> text {$/fn describe(w: World) -> text {\\n    let unused = 0/|kept" \
    "a field added|s/^    id: i32$/    id: i32\n    weight: f32/|refused" \
    "a field taken away|s/^    vy: f32$//|refused" \
    "a field renamed|s/^    vx: f32$/    dx: f32/|refused" \
    "a type changed to one that does not fit|s/^    x: f32$/    x: f64/|refused" \
    "a signature changed|s/^fn round(w: World, from: i32)/fn round(w: World, from: i32, more: i32)/|refused" \
    "a case put in the middle of an enum|s/^    Drifting$/    Drifting\n    Resting/;s/^                Drifting -> 0$/                Drifting -> 0\n                Resting -> 2/|refused" \
    "a case added at the end of an enum|s/^    Chasing$/    Chasing\n    Resting/;s/^                Chasing -> 1$/                Chasing -> 1\n                Resting -> 2/|refused" \
    "a bit put in the middle of a set|s/^    Seen$/    Seen\n    Rested/|refused" \
    "a bit added at the end of a set|s/^    Hit$/    Hit\n    Rested/|refused" \
    "a program that will not build|s/^struct Body {/struct Body {{/|refused"; do
    what=${edit%%|*}
    rest_of=${edit#*|}
    doing=${rest_of%%|*}
    wanted=${rest_of##*|}
    cp examples/engine.kest "$scratch"/reloading/after.kest
    sed -i "$doing" "$scratch"/reloading/after.kest 2>/dev/null
    said=$(./examples/engine examples/engine.kest \
        "$scratch"/reloading/after.kest 2>&1)
    status=$?
    if [ $status -ne 0 ]; then
        reload_wrong="$what left the host with nothing"
        break
    fi
    case "$said" in
    *"a reload kept the ring"*) got=kept ;;
    *"the reload did not happen and the world is the one it was"*) got=refused ;;
    *) got="said nothing about what happened" ;;
    esac
    if [ "$got" != "$wanted" ]; then
        reload_wrong="$what was $got and the study says $wanted"
        break
    fi
done
if [ -n "$reload_wrong" ]; then
    complain "reload" "an edit a reload has to have an answer for: $reload_wrong"
else
    say "reload" "eleven edits a reload has to have an answer for, each ending \
with a world: the new program's where the shape did not move, and the one the \
host was holding where it did"
fi

# The boundaries a stranger's bytes arrive through, sanitised. Eight seeds and
# four hundred inputs each, which is a minute rather than an afternoon: a gate
# can afford a short campaign and a long one is the same command with other
# numbers.
#
# Six of them, because a compiler is not the only thing here somebody else's
# bytes reach. The doors a host hands a handle to are reached by a host, and so
# are the life of a lend, a reference into a world being changed underneath it,
# bytes handed over as text, and a program edited under a world that is already
# running. What each holds is written where it is written; what they share is
# that every input ends in an answer or a refusal, which is what not crashing
# looks like from outside. See D984 and D997.
if [ ! -x tools/fuzz-debug ]; then
    complain "fuzzing" "there is no sanitised fuzzer to run"
else
    fuzzed=0
    fuzz_wrong=""
    for what in source handles lends refs text migrate; do
        for seed in 1 2 3 4 5 6 7 8; do
            said=$(./tools/fuzz-debug "$seed" 400 "$scratch"/fuzz.kest \
                "$what" 2>&1) || fuzz_wrong="$what at seed $seed stopped it"
            case "$said" in
            *"none of them stopped this"*) fuzzed=$((fuzzed + 400)) ;;
            *) fuzz_wrong="$what at seed $seed: \
$(printf '%s' "$said" | head -3)" ;;
            esac
        done
    done
    # And the same seeds compiled the other way. What the source boundary
    # folds is what every one of those programs answered, so two runs that
    # fold to the same number are two runs where the lowering's fusions
    # changed nothing a program can see -- over thousands of programs nobody
    # wrote, which is where a miscompilation would hide rather than in the
    # thirty-seven somebody did. See D1015.
    fuzzer=./tools/fuzz-debug
    fuzz_one="$scratch"/fuzz-fused.kest
    fuzz_two="$scratch"/fuzz-plain.kest
    fuzz_three="$scratch"/fuzz-bare.kest
    folded_fused=""
    folded_plain=""
    folded_bare=""
    for seed in 1 2 3 4 5 6 7 8; do
        folded_fused="$folded_fused$("$fuzzer" "$seed" 400 "$fuzz_one" \
            source 2>/dev/null | sed -n 's|.*folds to ||p')"
        folded_plain="$folded_plain$(KEST_PLAIN=1 "$fuzzer" "$seed" 400 \
            "$fuzz_two" source 2>/dev/null | sed -n 's|.*folds to ||p')"
        folded_bare="$folded_bare$(KEST_NOOPT=1 "$fuzzer" "$seed" 400 \
            "$fuzz_three" source 2>/dev/null | sed -n 's|.*folds to ||p')"
    done
    if [ -z "$folded_fused" ] || [ "$folded_fused" != "$folded_plain" ]; then
        complain "fuzzing" "programs nobody wrote answer something else when \
the lowering's fusions are turned off"
    fi
    # And the optimizer read the same way. A pass that reads a whole body and
    # decides a slot is written once is a pass whose mistake is a program
    # nobody wrote rather than one of the thirty-seven somebody did: the first
    # thing this one did was take `let at = from` out of `text.trim`. See
    # D1025.
    if [ -z "$folded_fused" ] || [ "$folded_fused" != "$folded_bare" ]; then
        complain "fuzzing" "programs nobody wrote answer something else when \
the optimizer is turned off"
    fi
    if [ -n "$fuzz_wrong" ]; then
        complain "fuzzing" "bytes this compiler was not written for stopped \
it: $fuzz_wrong"
    else
        say "fuzzing" "$fuzzed input(s) made from eight seeds over six \
boundaries -- what a program is written in, the handles a host hands over, the \
life of a lend, a reference into a world being changed underneath it, bytes \
handed over as text, and a program edited under a world that is running -- \
every one of them an answer or a refusal, under a build that checks itself, \
and the source ones answer the same folded over compiled the other way"
    fi
fi

# And what is in the tree that a compiler made. A repository carrying a built
# thing is one where somebody's `git add -A` swept up what their last build
# left: this one carried a ten-megabyte sanitised fuzzer and two caches of
# another language's compiler, and nothing anywhere looked. What makes a built
# thing tellable from a written one is its first bytes -- an executable, a
# library and an object all say what they are in the first four.
#
# What a build may leave is the list `make clean` takes away, read from there
# rather than written again here: two lists of the same names are one list the
# day somebody adds to the other. Anything else with those first bytes is
# something nobody meant to keep. See D999.
what_a_compiler_made() {
    allowed=$(sed -n '/^clean:/,/^$/p' "$1"/Makefile |
              tr -d '\\' | tr ' \t' '\n\n' |
              sed -n 's/^rm$//;s/^-rf$//;s/^-f$//;/^$/d;/^clean:$/d;p' |
              tr '\n' ' ')
    found=""
    for file in $(cd "$1" && find . -type f \
            -not -path './build/*' -not -path './.git/*' \
            -not -name '*.d' | sort); do
        here=${file#./}
        case " $allowed " in
        *" $here "*) continue ;;
        esac
        case "$here" in
        *.o|*.a|*.obj|*.lib|*.pdb|*.exe|*.dll|*.so|*.dylib|*.dascache|core|core.*)
            found="$found $here"
            continue
            ;;
        esac
        # The first four bytes, which is what a thing a compiler made says it
        # is. Read with `od` rather than by asking `file`, which is not on
        # every machine this runs on.
        case "$(od -An -tx1 -N4 "$1/$here" 2>/dev/null | tr -d ' \n')" in
        7f454c46|4d5a*|feedface|feedfacf|cffaedfe|cafebabe|213c6172)
            found="$found $here"
            ;;
        esac
    done
    if [ -n "$found" ]; then
        printf 'something a compiler made is in the tree and `make clean` '
        printf 'does not take it away:%s\n' "$found"
    fi
}

# And the other way a file arrives in a tree that nobody meant: a name with a
# space in it, which is what an unquoted redirect leaves behind. `> $places`
# with `places` unset writes a file called whatever the next two words were,
# and a check that did it once left `extern fn` in the root of this tree for
# nine days -- 1774 bytes of a program written to hold every keyword, committed,
# shipped in nothing, read by nothing, and invisible to every sweep here
# because the sweeps are over `*.kest` and it had no extension. The walk above
# could not see it either: it reads `$(find ...)`, and a name with a space in
# it is two words to a shell. So this one reads a line at a time.
#
# No file in this tree has a space in its name, and none is meant to.
a_name_nobody_meant() {
    found=""
    while IFS= read -r file; do
        here=${file#./}
        case "$here" in
        *" "*|*"	"*) found="$found
    $here" ;;
        esac
    done <<INNER
$(cd "$1" && find . -type f -not -path './build/*' -not -path './.git/*' |
      sort)
INNER
    if [ -n "$found" ]; then
        printf 'a file with a space in its name is in the tree, which is '
        printf 'what an unquoted redirect leaves:%s\n' "$found"
    fi
}

# And the third way something built gets into a tree: not left in it, but
# committed to it. `make clean` takes `bench/measure` away, so the walk above
# is right to allow it in a working tree -- and it was committed anyway, by a
# `git add -A` after a build, and sat there for five commits. What the walk
# above cannot see is which files are in the repository, so this asks.
#
# Nothing here needs git to work: a tree without it is a tree this says
# nothing about, which is what a release archive is.
what_was_committed() {
    if ! command -v git >/dev/null 2>&1 ||
        ! git -C "$1" rev-parse --git-dir >/dev/null 2>&1; then
        return 0
    fi
    found=""
    for name in $(sed -n '/^clean:/,/^$/p' "$1"/Makefile |
                  tr -d '\\' | tr ' \t' '\n\n' |
                  sed -n 's/^rm$//;s/^-rf$//;s/^-f$//;/^$/d;/^clean:$/d;p'); do
        case "$name" in
        *[*?[]*) continue ;;
        esac
        if git -C "$1" ls-files --error-unmatch "$name" >/dev/null 2>&1; then
            found="$found $name"
        fi
    done
    if [ -n "$found" ]; then
        printf 'something `make clean` takes away is committed:%s\n' "$found"
    fi
}

made_by_a_compiler=$(what_a_compiler_made .)
nobody_meant=$(a_name_nobody_meant .)
committed_and_built=$(what_was_committed .)
if [ -n "$committed_and_built" ]; then
    complain "tree" "$committed_and_built"
fi
if [ -n "$nobody_meant" ]; then
    complain "tree" "$nobody_meant"
fi
if [ -n "$made_by_a_compiler" ]; then
    complain "tree" "$made_by_a_compiler"
else
    # And the same walk over a room with a built thing in it, because a walk
    # that found nothing and a walk that looked at nothing print the same
    # nothing. This is the gate's own guard, which is what a check written
    # here has instead of a hole. See D999.
    mkdir -p "$scratch"/tree/tools
    cp Makefile "$scratch"/tree/Makefile
    cp kest "$scratch"/tree/tools/left-behind
    printf 'module places\n' > "$scratch"/tree/"extern fn"
    if ! what_a_compiler_made "$scratch"/tree |
            grep -q "something a compiler made is in the tree"; then
        complain "tree" "a room with a built thing in it was walked and \
nothing was said about it"
    elif ! a_name_nobody_meant "$scratch"/tree |
            grep -q "a file with a space in its name is in the tree"; then
        complain "tree" "a room with a file nobody meant in it was walked and \
nothing was said about it"
    else
        say "tree" "nothing a compiler made is in the tree but what \
\`make clean\` takes away, no file in it has a name nobody meant, and a room \
with one of each in it is named"
    fi
fi

# What the machine says it moved, held to what it ran. The counters in the
# build that counts instructions say bytes; the histogram beside them says how
# many times each instruction ran; and for the instructions whose width is
# fixed the two have to agree exactly. A counter added at a new movement and
# forgotten at an old one is what this catches, and a program written here
# rather than taken from the tree so that the arithmetic is one line. See
# D1023.
cat > "$scratch"/moved.kest <<'MOVED'
module moved

fn main() -> i32 {
    let a = 1
    let b = 2
    let sum = 0
    for i in 0..1000 {
        a += i % 3
        b += a % 5
        sum += a + b
    }
    return sum % 7
}
MOVED
moved_said=$(KEST_DEEP=1 ./kest-debug run "$scratch"/moved.kest 2>&1 >/dev/null)
if ! printf '%s' "$moved_said" | grep -q '^moved '; then
    complain "moved" "the build that counts instructions said nothing about \
what it moved"
else
    moved_wrong=$(printf '%s\n' "$moved_said" | python3 -c '
import sys

ran = {}
moved = {}
for line in sys.stdin:
    words = line.split()
    if words and words[0] == "ran":
        ran[words[1]] = int(words[2])
    if words and words[0] == "moved":
        moved = {words[at]: int(words[at + 1])
                 for at in range(1, len(words), 2)}
# The ones whose width is an operand or a layout cannot be worked out from a
# count, so a program that runs one of them is a program this cannot check.
wide = ("load.n", "store.n", "load.slots", "store.slots", "const.run",
        "const.at", "field", "rotate", "push", "fit", "array", "make.array",
        "pop.last", "take", "get", "set", "add", "concat", "text.in")
for name in wide:
    if ran.get(name):
        print("the program written for this runs `%s`, whose width is not a "
              "count" % name)
        break
else:
    slots = 8
    want = (slots * ran.get("load", 0) + 2 * slots * ran.get("load2", 0)
            + slots * ran.get("load.k", 0))
    if moved.get("loaded") != want:
        print("it loaded %s byte(s) and ran the instructions for %s"
              % (moved.get("loaded"), want))
    want = slots * (ran.get("store", 0) + ran.get("add.i.narrow.to", 0)
                    + ran.get("sub.i.narrow.to", 0) + ran.get("add.f.to", 0)
                    + ran.get("sub.f.to", 0))
    if moved.get("stored") != want:
        print("it stored %s byte(s) and ran the instructions for %s"
              % (moved.get("stored"), want))
    want = slots * (ran.get("const", 0) + ran.get("true", 0)
                    + ran.get("false", 0) + ran.get("load.k", 0))
    if moved.get("held") != want:
        print("it held out %s byte(s) and ran the instructions for %s"
              % (moved.get("held"), want))
')
    if [ -n "$moved_wrong" ]; then
        complain "moved" "$moved_wrong"
    else
        say "moved" "what the machine says it moved is what the \
instructions it ran move: every byte loaded, stored and held out of the chunk \
is accounted for by a count and a width"
    fi
fi

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
