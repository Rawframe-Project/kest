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
done

rm -f "$backup"

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

if [ $failed -eq 0 ]; then
    echo "$# file(s) are in the one form, which is faithful and refuses what it cannot read"
fi
exit $failed
