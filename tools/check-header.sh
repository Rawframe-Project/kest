#!/bin/sh
# A host is written against `include/kest.h` and nothing else. Nothing said so:
# the example beside it happens to include only that, and the day it stopped
# being true nothing would have noticed.
#
# So: a file that includes the header and nothing before it, names every
# function the header declares, and links against the library alone. The
# compiler proves the header stands on its own and the linker proves the
# library keeps every promise the header makes.
set -u
cc=${CC:-cc}
header=include/kest.h
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ ! -f libkest.a ]; then
    echo "the library is not built"
    exit 1
fi

# The header must not reach into the implementation, so the only includes it
# may have are the system's.
if grep -q '#include "' "$header"; then
    echo "the header includes something from the implementation:"
    grep -n '#include "' "$header"
    exit 1
fi

# Every `kest_...(` outside a comment. A name is taken rather than called, so
# nothing has to be given arguments that mean anything.
names=$(sed 's,//.*,,' "$header" | grep -oE '\bkest_[a-z_]+\(' |
        sed 's/(//' | sort -u)
count=$(printf '%s\n' "$names" | grep -c .)

{
    echo '#include "kest.h"'
    echo
    echo '// A function pointer converts to another function pointer and to no'
    echo '// object pointer, which is why these are not `void *`.'
    echo 'typedef void (*Anything)(void);'
    echo
    echo 'static Anything promised[] = {'
    for name in $names; do
        echo "    (Anything)$name,"
    done
    echo '};'
    echo
    echo 'int main(void) { return promised[0] == 0; }'
} > "$work/host.c"

# No `-lm` and nothing else: the library is meant to need libc and nothing
# beyond it, and this is where that is either true or not.
if ! "$cc" -std=c11 -Wall -Wextra -Werror -pedantic -Iinclude \
        -o "$work/host" "$work/host.c" libkest.a 2>"$work/why"; then
    echo "a host cannot be written against the header and libc alone:"
    cat "$work/why"
    exit 1
fi
if ! "$work/host"; then
    echo "the host the header describes did not run"
    exit 1
fi

# And that the document says each of them. A host reads two things: this header
# and `docs/language.md`, and a function in the first that the second never
# names is one nobody will find -- there is nowhere else to look. Three were in
# that state when this was written: what a build still holds, what a name is
# bound to, and which Kest this is. See D792.
unsaid=$(for name in $names; do
    if ! grep -qF "$name" docs/language.md; then
        echo "$name"
    fi
done)
if [ -n "$unsaid" ]; then
    echo "a host is given these and the document never says them:"
    printf '%s\n' "$unsaid" | sed 's/^/    /'
    exit 1
fi

echo "the header stands alone: $count function(s), libc and nothing else, and \
the document says every one of them"
