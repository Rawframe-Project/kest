#!/bin/sh
# Everything, in one command, because "everything passes" said by hand is a
# claim and this is a command. Twice a column has been quietly missing from a
# sweep run by hand — once a command that does not exist, once one that was
# never added — and both times the sweep said it had passed.
#
# Nothing here takes a list of files. A list is the thing that goes stale.
set -u
cd "$(dirname "$0")/.." || exit 1

failed=0
say() { printf '%-34s %s\n' "$1" "$2"; }
complain() { say "$1" "$2"; failed=1; }

sources=$(find examples lib -name '*.kest' | sort)
count=$(printf '%s\n' "$sources" | grep -c .)

# Kest under `tools` is an instrument rather than a program: it is held to
# resolving and to formatting, and not to running, because what it does is
# take a while on purpose.
instruments=$(find tools -name '*.kest' | sort)

# Built twice, because the two are different programs: the release one is what
# ships and the debug one is what says whether it was right.
if ! make >/dev/null 2>/tmp/kest-check-why; then
    complain "build" "the library does not build"
    sed 's/^/    /' /tmp/kest-check-why | head -10
    exit 1
fi
if ! make debug embed embed-debug >/dev/null 2>/tmp/kest-check-why; then
    complain "build" "the sanitised build does not build"
    sed 's/^/    /' /tmp/kest-check-why | head -10
    exit 1
fi
say "build" "release, sanitised, and both hosts"

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
returns=/tmp/kest-check-returns.kest
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
inside=/tmp/kest-check-inside.kest
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
nought=/tmp/kest-check-nought.kest
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
gathered=/tmp/kest-check-gathered.kest
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
asking=/tmp/kest-check-asking
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
    return kest_gave_text(runtime, at, frame, out, sizeof(out)) < 0 ? 0 : 3;
}
EOF
if ! cc -std=c11 -Wall -Wextra -Werror -Iinclude -o "$asking" "$asking.c"         libkest.a -lm 2>/tmp/kest-check-why; then
    complain "asking" "the host that asks before calling does not build"
    sed 's/^/    /' /tmp/kest-check-why | head -3
elif ! "$asking" "$asking.kest" >/dev/null 2>&1; then
    complain "asking" "asking what came back before anything did is not a message"
fi
rm -f "$asking" "$asking.c" "$asking.kest"

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
quiet=/tmp/kest-quiet-main.kest
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

# The library as one project, which is what `kest check *.kest` is for.
# Reading files one at a time never asks whether two of them can be read
# together, and that is where a file named on the command line turned out to
# be a different file from the same one an import reached.
#
# The examples are not one project: they are thirty programs that live in one
# directory, and two of them may put their names under the same one without
# either being wrong. `lib/std` is a project, so it is read as one.
if ! ./kest check lib/std/*.kest >/tmp/kest-check-why 2>&1; then
    complain "project" "the library does not check as one project"
    grep -m 4 -E '^(error|warning)' /tmp/kest-check-why | sed 's/^/    /'
fi

say "examples" "$ran ran, $resolved resolved, and one that gives nothing back"

for file in $instruments; do
    if ! ./kest check "$file" >/dev/null 2>/tmp/kest-check-why; then
        complain "instruments" "$file does not resolve"
        sed 's/^/    /' /tmp/kest-check-why | head -6
    fi
done
say "instruments" "$(printf '%s\n' "$instruments" | grep -c .) resolved"

for host in ./examples/embed ./examples/embed-debug; do
    if ! "$host" >/dev/null 2>/tmp/kest-check-why; then
        complain "host" "$host failed"
        sed 's/^/    /' /tmp/kest-check-why | head -10
    fi
done
say "host" "both crossings, sanitised and not"

# Every command against every file, under the sanitisers, looking at what it
# said rather than at what it returned: a command that fails for a reason is
# fine and one that walks off the end of an array is not.
# One file, every command, under the sanitisers. It says nothing unless
# something is wrong, which is what lets these run at once and be read back in
# the order the files were given.
sanitise_one() {
    file=$1
    for command in lex parse check fmt run emit; do
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
swept=$(mktemp -d)
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
    sweep=$((sweep + 7))
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
say "sanitisers" "$sweep runs over $count file(s)"

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

# shellcheck disable=SC2086
run "formatting" tools/check-fmt.sh $sources $instruments
# shellcheck disable=SC2086
run "commands" tools/check-commands.sh $sources
run "tables" tools/check-tables.sh
run "header" tools/check-header.sh
run "declarations" tools/check-dead.sh
run "documentation" tools/check-docs.sh docs/language.md docs/decisions.md
run "costs" tools/check-costs.sh
run "ceilings" tools/check-ceilings.sh
run "backstops" tools/check-backstops.sh

if [ $failed -eq 0 ]; then
    echo
    echo "everything passes"
fi
exit $failed
