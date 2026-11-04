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

# What the build made: the preset's build directory, out/dev unless
# KEST_BUILD names another.
build_dir=${KEST_BUILD:-out/dev}
case $build_dir in /*) ;; *) build_dir=$(pwd)/$build_dir ;; esac
cc=${CC:-cc}
header=include/kest.h
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ ! -f $build_dir/libkest.a ]; then
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
        -o "$work/host" "$work/host.c" $build_dir/libkest.a 2>"$work/why"; then
    echo "a host cannot be written against the header and libc alone:"
    cat "$work/why"
    exit 1
fi
if ! "$work/host"; then
    echo "the host the header describes did not run"
    exit 1
fi

# And the hosts' own files, which are built without `-Wshadow`: each is one
# long main of blocks that reuse short names. MSVC drops only a local hiding a
# local (C4456) and refuses a local hiding a name at file scope (C4459), so
# that one kind was found on Windows and nowhere before it. Neither gcc nor
# clang can ask for it alone, so every shadow is said and the ones that hide a
# name at file scope are read out of what was said. Those words are the
# compiler's own, so a file that hides one on purpose is asked first: words
# this check does not know stop it rather than let every host through.
cat > "$work/hides.c" <<'C'
static int count;

int main(void) {
    int count = 1;
    return count;
}
C
global='shadows a global declaration|shadows a variable in the global scope'
"$cc" -std=c11 -Wshadow -fsyntax-only "$work/hides.c" >"$work/said" 2>&1
if ! grep -qE "$global" "$work/said"; then
    echo "$cc said this of a local hiding a name at file scope, which this" \
         "check does not read as one:"
    cat "$work/said"
    exit 1
fi
hosts="examples/embed.c examples/engine.c"
for host in $hosts; do
    if ! "$cc" -std=c11 -Wshadow -fsyntax-only -Iinclude "$host" \
            >"$work/said" 2>&1; then
        echo "$host does not compile:"
        cat "$work/said"
        exit 1
    fi
    if grep -E "$global" "$work/said" >"$work/hidden"; then
        echo "$host hides a name at file scope, which MSVC refuses (C4459):"
        cat "$work/hidden"
        exit 1
    fi
done

echo "the header stands alone: $count function(s), libc and nothing else;" \
     "and no host hides a name at file scope"
