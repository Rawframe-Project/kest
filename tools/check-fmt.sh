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
kest=./kest
failed=0
backup=/tmp/kest-fmt-backup

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
    if ! "$kest" fmt "$file" > /tmp/kest-fmt-1 2>/dev/null; then
        continue
    fi
    # A language with one form is written in it. Nothing held this before, and
    # three files had drifted out of it — two of them by being written before
    # the formatter learned what to do with the line they hold.
    if ! cmp -s /tmp/kest-fmt-1 "$file"; then
        echo "not in the one form: $file"
        failed=1
    fi
    if ! "$kest" fmt /tmp/kest-fmt-1 > /tmp/kest-fmt-2 2>/dev/null; then
        echo "output does not format: $file"
        failed=1
        continue
    fi
    if ! cmp -s /tmp/kest-fmt-1 /tmp/kest-fmt-2; then
        echo "not idempotent: $file"
        failed=1
    fi

    # A file that does not parse has no tree to compare.
    if ! "$kest" parse "$file" > /tmp/kest-tree-1 2>/dev/null; then
        continue
    fi

    cp "$file" "$backup" || exit 1
    "$kest" fmt -w "$file" > /dev/null 2>&1
    "$kest" parse "$file" > /tmp/kest-tree-2 2>/dev/null
    cp "$backup" "$file" || exit 1

    if ! cmp -s /tmp/kest-tree-1 /tmp/kest-tree-2; then
        echo "tree changed: $file"
        failed=1
    fi

    # And every comment is still there, in the order it was written. The tree
    # says nothing about them: a formatter that dropped one would keep every
    # promise above this and lose what a reader was told.
    said "$file" > /tmp/kest-said-1
    said /tmp/kest-fmt-1 > /tmp/kest-said-2
    if ! cmp -s /tmp/kest-said-1 /tmp/kest-said-2; then
        echo "comments changed: $file"
        failed=1
    fi
done

rm -f "$backup"

# A file nobody has formatted yet, with a comment in every place one can be
# written: at the end of a line, inside a signature, inside the value of a
# match arm, in an empty block, and after the last statement. Every file in
# this tree is already in the one form, so none of them is this.
said=/tmp/kest-fmt-said.kest
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
if ! "$kest" fmt "$said" > /tmp/kest-fmt-said-1 2>/dev/null; then
    echo "the file with comments in it does not format"
    failed=1
else
    said "$said" > /tmp/kest-said-1
    said /tmp/kest-fmt-said-1 > /tmp/kest-said-2
    if ! cmp -s /tmp/kest-said-1 /tmp/kest-said-2; then
        echo "comments changed: a file nobody had formatted"
        failed=1
    fi
    if ! "$kest" run /tmp/kest-fmt-said-1 >/dev/null 2>&1 </dev/null; then
        echo "the file with comments in it stopped running once formatted"
        failed=1
    fi
fi
rm -f "$said" /tmp/kest-fmt-said-1

# A file it cannot read is one it must not write. `fmt -w` is the only thing
# in this project that replaces somebody's source, and half a program written
# over the whole of one deletes the other half.
broken=/tmp/kest-fmt-broken.kest
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

rm -f /tmp/kest-said-1 /tmp/kest-said-2

if [ $failed -eq 0 ]; then
    echo "$# file(s) are in the one form, which is faithful, keeps what was said, and refuses what it cannot read"
fi
exit $failed
