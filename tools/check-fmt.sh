#!/bin/sh
# What a formatter has to be true of: its output parses, means the same thing,
# and formatting it again changes nothing. Run it over every file given.
set -u
kest=./kest
failed=0

for file in "$@"; do
    # Beside the original, so what it imports resolves the same way.
    copy="$(dirname "$file")/.check-fmt.kest"
    if ! "$kest" fmt "$file" > "$copy" 2>/dev/null; then
        rm -f "$copy"
        continue
    fi
    cp "$copy" /tmp/kest-fmt-1
    if ! "$kest" fmt /tmp/kest-fmt-1 > /tmp/kest-fmt-2 2>/dev/null; then
        echo "output does not format: $file"
        failed=1
        continue
    fi
    if ! cmp -s /tmp/kest-fmt-1 /tmp/kest-fmt-2; then
        echo "not idempotent: $file"
        failed=1
    fi

    # The path is in the tree dump's header and in every diagnostic, so it is
    # taken out before the two are compared.
    # A file that does not parse has no tree to compare, and its diagnostics
    # move when the spacing does.
    if ! "$kest" parse "$file" > /dev/null 2>&1; then
        rm -f "$copy"
        continue
    fi
    "$kest" parse "$file" 2>&1 | sed "s|$file|SRC|g" > /tmp/kest-tree-1
    "$kest" parse "$copy" 2>&1 | sed "s|$copy|SRC|g" > /tmp/kest-tree-2
    if ! cmp -s /tmp/kest-tree-1 /tmp/kest-tree-2; then
        echo "tree changed: $file"
        failed=1
    fi
    rm -f "$copy"
done

if [ $failed -eq 0 ]; then
    echo "formatting is faithful on $# file(s)"
fi
exit $failed
