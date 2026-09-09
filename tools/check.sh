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
if ! make debug embed embed-debug >/dev/null 2>"$scratch"/check-why; then
    complain "build" "the sanitised build does not build"
    sed 's/^/    /' "$scratch"/check-why | head -10
    exit 1
fi
# And that what was built answers. A build that made no binary, or one that
# cannot start, is every check below this reporting its own confusing failure —
# a probe that passes when a command fails would pass for the wrong reason, and
# `make` saying nothing is not the same as there being something to run.
for built in ./kest ./kest-debug ./examples/embed ./examples/embed-debug; do
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
EOF
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
else
    say "asking" "a host asking what came back before anything came back"
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
quiet="$scratch"/quiet-main.kest
cat > "$quiet" <<'EOF'
module quiet

fn main() {
    let n = 1 + 1
}
EOF
if ! ./kest run "$quiet" >/dev/null 2>&1; then
    complain "examples" "a \`main\` that gives nothing back does not exit 0"
fi
rm -f "$quiet"

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

say "examples" "$ran ran, $resolved resolved, and one that gives nothing back"

for file in $instruments; do
    if ! ./kest check "$file" >/dev/null 2>"$scratch"/check-why; then
        complain "instruments" "$file does not resolve"
        sed 's/^/    /' "$scratch"/check-why | head -6
    fi
done
say "instruments" "$(printf '%s\n' "$instruments" | grep -c .) resolved"

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
