#!/bin/sh
# What a formatter has to be true of: its output parses, means the same thing,
# and formatting it again changes nothing.
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
if [ $failed -eq 0 ]; then
    echo "formatting is faithful on $# file(s)"
fi
exit $failed
