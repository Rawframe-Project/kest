#!/bin/sh
# What a formatter has to be true of: its output parses, means the same thing,
# formatting it again changes nothing, and a file it cannot read is left as it
# was found. And what this tree has to be true of: every file in it is already
# in the one form, because a language with one form is written in it.
#
# A file's path has to match what it calls itself for its imports to resolve,
# so the comparison is done in place: the file is formatted where it is, read,
# and put back.
set -u

# A scratch of this run's own. Two of these run at once when the backstops put
# one out of order while another is being asked, and fixed names in `/tmp` are
# two runs writing to one file.
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
kest=./kest
failed=0
backup="$scratch"/fmt-backup

# What this was given. A check that reads the files it is handed passes when it
# is handed none: the loop runs no times and the count at the end is nought,
# which reads like a success. Nothing in the tree is an empty list, so this is
# only ever a caller that lost its own.
if [ "$#" -eq 0 ]; then
    echo "formatting: nothing was given to look at"
    exit 1
fi

# What was said in a file, one comment a line. A `//` inside a string begins
# nothing, so the strings are stepped over first — the same rule the formatter
# reads a file by, and the reason this is not a search for two slashes.
said() {
    python3 -c '
import sys

QUOTE = chr(34)
text = open(sys.argv[1]).read()
at = 0
while at < len(text):
    if text[at] == QUOTE:
        # A hole may hold a string of its own, so the quote that closes this
        # one is the one found outside every brace.
        depth = 0
        at += 1
        while at < len(text):
            if text[at] == "\\":
                at += 1
            elif text[at] == "{":
                depth += 1
            elif text[at] == "}" and depth > 0:
                depth -= 1
            elif text[at] == QUOTE and depth == 0:
                break
            at += 1
        at += 1
        continue
    if text.startswith("//", at):
        end = text.find("\n", at)
        end = len(text) if end < 0 else end
        print(text[at:end].rstrip())
        at = end
        continue
    at += 1
' "$1"
}

for file in "$@"; do
    if ! "$kest" fmt "$file" > "$scratch"/fmt-1 2>/dev/null; then
        continue
    fi
    # A language with one form is written in it. Nothing held this before, and
    # three files had drifted out of it — two of them by being written before
    # the formatter learned what to do with the line they hold.
    if ! cmp -s "$scratch"/fmt-1 "$file"; then
        echo "not in the one form: $file"
        failed=1
    fi
    if ! "$kest" fmt "$scratch"/fmt-1 > "$scratch"/fmt-2 2>/dev/null; then
        echo "output does not format: $file"
        failed=1
        continue
    fi
    if ! cmp -s "$scratch"/fmt-1 "$scratch"/fmt-2; then
        echo "not idempotent: $file"
        failed=1
    fi

    # A file that does not parse has no tree to compare.
    if ! "$kest" parse "$file" > "$scratch"/tree-1 2>/dev/null; then
        continue
    fi

    # On a copy rather than on the file. `fmt -w` is the only thing in this
    # project that writes over somebody's source, and a check that does it to
    # the tree is a check nothing else can run beside: everything here reads
    # these files.
    cp "$file" "$backup" || exit 1
    "$kest" fmt -w "$backup" > /dev/null 2>&1
    "$kest" parse "$backup" > "$scratch"/tree-2 2>/dev/null

    if ! cmp -s "$scratch"/tree-1 "$scratch"/tree-2; then
        echo "tree changed: $file"
        failed=1
    fi

    # And every comment is still there, in the order it was written. The tree
    # says nothing about them: a formatter that dropped one would keep every
    # promise above this and lose what a reader was told.
    said "$file" > "$scratch"/said-1
    said "$scratch"/fmt-1 > "$scratch"/said-2
    # Two readings of what a comment is: this one, and the compiler's. The
    # comparison above is only worth what this one sees, so a reading that
    # sees fewer than the compiler does is a check that has gone quiet.
    mine=$(wc -l < "$scratch"/said-1)
    theirs=$("$kest" lex "$file" --json 2>/dev/null </dev/null |
             python3 -c 'import json, sys; print(len(json.load(sys.stdin).get("comments", [])))')
    if [ "$mine" -ne "$theirs" ]; then
        echo "read $mine comment(s) and the compiler read $theirs: $file"
        failed=1
    fi
    if ! cmp -s "$scratch"/said-1 "$scratch"/said-2; then
        echo "comments changed: $file"
        failed=1
    fi
done

rm -f "$backup"

# A file nobody has formatted yet, with a comment in every place one can be
# written: at the end of a line, inside a signature, inside the value of a
# match arm, in an empty block, and after the last statement. Every file in
# this tree is already in the one form, so none of them is this.
said="$scratch"/fmt-said.kest
cat > "$said" <<'EOF'
module said

enum Door {
    Shut
    Open(i32)
}

fn act(d: Door) -> i32 { // what it does
    return match d {
        Shut -> 0
        Open(w) ->
            // the width matters
            w
    }
}

fn quiet() {
    // nothing to do yet
}

fn main() -> i32 {
    let x = act(Door.Open(1)) // one open door
    // a string may hold two slashes that begin nothing
    let where = "http://kest" // and a comment may follow one
    // the last thing
    return x - len(where) + 11 - 1
}
EOF
if ! "$kest" fmt "$said" > "$scratch"/fmt-said-1 2>/dev/null; then
    echo "the file with comments in it does not format"
    failed=1
else
    said "$said" > "$scratch"/said-1
    said "$scratch"/fmt-said-1 > "$scratch"/said-2
    if ! cmp -s "$scratch"/said-1 "$scratch"/said-2; then
        echo "comments changed: a file nobody had formatted"
        failed=1
    fi
    if ! "$kest" run "$scratch"/fmt-said-1 >/dev/null 2>&1 </dev/null; then
        echo "the file with comments in it stopped running once formatted"
        failed=1
    fi
fi
rm -f "$said" "$scratch"/fmt-said-1

# A file bigger than the numbers the formatter used to carry: more comments
# than the run it kept them in, and a chain longer than the one it collected.
# Both were quiet — the comments past the end were dropped and the chain past
# the end came out in a shape nobody asked for — and no file in this tree is
# either.
big="$scratch"/fmt-big.kest
{
    echo "module big"
    echo
    i=0
    while [ $i -lt 4200 ]; do
        echo "// said $i"
        i=$((i + 1))
    done
    echo "fn main() -> i32 {"
    printf "    let n = 1"
    i=1
    while [ $i -lt 40 ]; do
        printf " + 1"
        i=$((i + 1))
    done
    echo
    echo "    return n - 40"
    echo "}"
} > "$big"
if ! "$kest" fmt "$big" > "$scratch"/fmt-big-1 2>/dev/null; then
    echo "the big file does not format"
    failed=1
else
    said "$big" > "$scratch"/said-1
    said "$scratch"/fmt-big-1 > "$scratch"/said-2
    if ! cmp -s "$scratch"/said-1 "$scratch"/said-2; then
        echo "comments changed: a file with more of them than fitted"
        failed=1
    fi
    if ! "$kest" fmt "$scratch"/fmt-big-1 > "$scratch"/fmt-big-2 2>/dev/null ||
       ! cmp -s "$scratch"/fmt-big-1 "$scratch"/fmt-big-2; then
        echo "not idempotent: a file with a chain longer than the line"
        failed=1
    fi
    if ! "$kest" run "$scratch"/fmt-big-1 >/dev/null 2>&1 </dev/null; then
        echo "the big file stopped running once formatted"
        failed=1
    fi
fi
rm -f "$big" "$scratch"/fmt-big-1 "$scratch"/fmt-big-2

# `--check` is the one a build runs: it names what it would rewrite, writes
# nothing, and answers with its status. Nothing in this tree had ever run it in
# anger, so all three of those were promises.
if [ $# -gt 0 ]; then
    # shellcheck disable=SC2086
    if ! out=$("$kest" fmt --check "$@" 2>&1); then
        echo "fmt --check: refused a tree that is in the one form"
        printf '%s\n' "$out" | head -3
        failed=1
    elif [ -n "$out" ]; then
        echo "fmt --check: named a file in a tree that is in the one form"
        printf '%s\n' "$out" | head -3
        failed=1
    fi
fi

crooked="$scratch"/fmt-crooked.kest
cat > "$crooked" <<'EOF'
module crooked
fn  main( )->i32 {
  let x=1
   return x-1 }
EOF
cp "$crooked" "$crooked.was" || exit 1
if "$kest" fmt --check "$crooked" > "$scratch"/fmt-named 2>&1; then
    echo "fmt --check: said nothing about a file that is not in the one form"
    failed=1
elif ! grep -q "$crooked" "$scratch"/fmt-named; then
    echo "fmt --check: refused without naming the file"
    failed=1
fi
if ! cmp -s "$crooked" "$crooked.was"; then
    echo "fmt --check: wrote the file it was only asked about"
    failed=1
fi
rm -f "$crooked" "$crooked.was" "$scratch"/fmt-named

# A line too long to fit whose operator is `>`, which is the one operator a
# line may end after: `ref<Npc>` ends in one. Breaking there gives two
# statements the parser refuses, and breaking before it ends the line on a
# value, which is the same refusal from the other side — so it stays on the
# line it is on however long that is. What the one form is, first of all, is
# something that reads back as the same program.
wide="$scratch"/fmt-wide.kest
cat > "$wide" <<'EOF'
fn main() -> i32 {
    if 1000000000000000000000000.0 / 100000000000000000000.0 - 1.4142135 > 0.0001 {
        return 1
    }
    return 0
}
EOF
if ! "$kest" fmt "$wide" > "$scratch"/fmt-wide-out.kest 2>&1; then
    echo "fmt: refused a comparison too long for the line"
    failed=1
elif ! "$kest" check "$scratch"/fmt-wide-out.kest \
     > "$scratch"/fmt-wide-said 2>&1; then
    echo "fmt: what it made of a long comparison does not parse"
    sed 's/^/    /' "$scratch"/fmt-wide-said | head -3
    failed=1
fi
rm -f "$wide" "$scratch"/fmt-wide-out.kest "$scratch"/fmt-wide-said

# A file written on a machine that ends its lines with two characters. The
# formatter reads it and writes the one form, which ends lines with one, so
# what it gives back is a file that differs everywhere — and then it has to be
# stable, or every run would differ again.
crlf="$scratch"/fmt-crlf.kest
printf 'module crlf\r\n\r\nfn main() -> i32 {\r\n    return 0\r\n}\r\n' > "$crlf"
if ! "$kest" fmt "$crlf" > "$scratch"/fmt-crlf-once 2>&1; then
    echo "fmt: refused a file whose lines end with two characters"
    failed=1
else
    if grep -q $'\r' "$scratch"/fmt-crlf-once; then
        echo "fmt: kept a carriage return in the one form"
        failed=1
    fi
    cp "$scratch"/fmt-crlf-once "$crlf.once" || exit 1
    if ! "$kest" fmt "$crlf.once" > "$scratch"/fmt-crlf-twice 2>&1 ||
       ! cmp -s "$scratch"/fmt-crlf-once "$scratch"/fmt-crlf-twice; then
        echo "fmt: what it made of a file with two-character line ends is not "\
             "in the one form"
        failed=1
    fi
fi
rm -f "$crlf" "$crlf.once" "$scratch"/fmt-crlf-once "$scratch"/fmt-crlf-twice

# And a file from an older machine still, which ends its lines with the other
# one of the two. What that costs if a comment does not end there is the whole
# file: everything after the first `//` is one comment, and a program that
# says something is read as a file that declares nothing.
returns="$scratch"/fmt-returns.kest
printf '// a note\rfn main() -> i32 {\r    return 0\r}\r' > "$returns"
if ! "$kest" fmt "$returns" > "$scratch"/fmt-returns-out 2>&1; then
    echo "fmt: refused a file whose lines end with a carriage return"
    failed=1
elif ! grep -q "^// a note$" "$scratch"/fmt-returns-out ||
     ! grep -q "^fn main() -> i32 {$" "$scratch"/fmt-returns-out; then
    echo "fmt: lost what a file with carriage returns said"
    sed 's/^/    /' "$scratch"/fmt-returns-out | head -3
    failed=1
fi
rm -f "$returns" "$scratch"/fmt-returns-out

# A file it cannot read is one it must not write. `fmt -w` is the only thing
# in this project that replaces somebody's source, and half a program written
# over the whole of one deletes the other half.
broken="$scratch"/fmt-broken.kest
cat > "$broken" <<'EOF'
module broken

fn main() -> i32 {
    let n = (1 +
    return n
}
EOF
cp "$broken" "$broken.was" || exit 1
if "$kest" fmt -w "$broken" > /dev/null 2>&1; then
    echo "formatted a file that does not parse"
    failed=1
fi
if ! cmp -s "$broken" "$broken.was"; then
    echo "wrote over a file that does not parse"
    failed=1
fi
rm -f "$broken" "$broken.was"

# A name longer than a line. Nothing in this tree has one — every name here is
# short enough to read — and what a formatter does with a line it cannot make
# fit is a thing to decide rather than to discover: it breaks what can break
# and leaves what cannot. A name is one thing; breaking it in half makes a
# different name.
lengthy="$scratch"/fmt-lengthy.kest
huge=$(printf 'a%.0s' $(seq 90))
{
    printf 'module lengthy\n\n'
    printf 'fn %s(one: i32, two: i32, three: i32) -> i32 {\n' "$huge"
    printf '    return one + two + three\n}\n\n'
    printf 'fn main() -> i32 {\n    return %s(1, 2, 3) + %s(4, 5, 6)\n}\n' \
           "$huge" "$huge"
} > "$lengthy"
if ! "$kest" fmt "$lengthy" > "$scratch"/fmt-lengthy-once 2>&1; then
    echo "a file with a name longer than a line was not written"
    sed 's/^/    /' "$scratch"/fmt-lengthy-once | head -3
    failed=1
elif ! "$kest" fmt "$scratch"/fmt-lengthy-once > "$scratch"/fmt-lengthy-twice \
        2>&1 ||
     ! cmp -s "$scratch"/fmt-lengthy-once "$scratch"/fmt-lengthy-twice; then
    echo "a file with a name longer than a line is not in the one form"
    failed=1
else
    # Every line that is over the limit holds the name that cannot be broken,
    # and nothing else was left long because of it.
    over=$(awk -v name="$huge" 'length($0) > 80 && index($0, name) == 0' \
           "$scratch"/fmt-lengthy-once)
    if [ -n "$over" ]; then
        echo "a line that could have been broken was left long"
        printf '%s\n' "$over" | cut -c1-60 | sed 's/^/    /' | head -3
        failed=1
    fi
    # And what could break did: a list that does not fit goes one item to a
    # line, and a line it could never have fitted on is not a reason to stop
    # trying. Without this, a formatter that gives up when a line is over the
    # limit anyway writes the same file back and every other rule holds.
    if ! grep -qx '        1,' "$scratch"/fmt-lengthy-once; then
        echo "a list beside a name too long to break was left on one line"
        grep -n "$(printf '%s' "$huge" | cut -c1-20)" \
             "$scratch"/fmt-lengthy-once | cut -c1-60 | sed 's/^/    /' |
            head -3
        failed=1
    fi
fi
rm -f "$lengthy" "$scratch"/fmt-lengthy-once "$scratch"/fmt-lengthy-twice

# A file with nothing in it but a comment. Every other rule this holds is about
# what a declaration looks like, and a file with no declarations has none of
# them to be true of: a formatter that wrote nothing at all for one would parse
# the same, mean the same, and come out the same twice. What it may not do is
# lose what somebody wrote.
saying="$scratch"/fmt-saying.kest
printf '\n\n// what this file is for\n\n\n' > "$saying"
if ! "$kest" fmt "$saying" > "$scratch"/fmt-saying-once 2>&1; then
    echo "a file of nothing but a comment was not written"
    sed 's/^/    /' "$scratch"/fmt-saying-once | head -3
    failed=1
elif ! grep -qx '// what this file is for' "$scratch"/fmt-saying-once; then
    echo "a file of nothing but a comment came back without it"
    sed 's/^/    /' "$scratch"/fmt-saying-once | head -3
    failed=1
elif [ "$(wc -l < "$scratch"/fmt-saying-once)" -ne 1 ]; then
    echo "a file of nothing but a comment came back with more than it"
    cat -A "$scratch"/fmt-saying-once | sed 's/^/    /' | head -4
    failed=1
elif ! "$kest" fmt "$scratch"/fmt-saying-once > "$scratch"/fmt-saying-twice \
        2>&1 ||
     ! cmp -s "$scratch"/fmt-saying-once "$scratch"/fmt-saying-twice; then
    echo "a file of nothing but a comment is not in the one form"
    failed=1
fi
rm -f "$saying" "$scratch"/fmt-saying-once "$scratch"/fmt-saying-twice

rm -f "$scratch"/said-1 "$scratch"/said-2

if [ $failed -eq 0 ]; then
    echo "$# file(s) are in the one form, which is faithful, keeps what was said, names what it would rewrite, and refuses what it cannot read"
fi
exit $failed
