#!/bin/sh
# What one program costs, both ways: the machine, and the bodies the other
# backend wrote. `bench/measure` keeps every sample and answers with the
# middle, the tails and the worst; it is generic, and a generated file belongs
# to one program, so the second half is built here rather than kept.
#
# It is not part of `make check`: a duration is not a pass or a fail. It is
# what to read when a number moved and what to read before saying a frame fits
# in a budget. Everything `bench/measure` takes it takes.
#
#   bench/tails.sh examples/slice/src/main.kest --samples 50
#
# See D1124.
set -eu

one=${1:-}
if [ -z "$one" ]; then
    echo "usage: bench/tails.sh <file.kest> [options for bench/measure]" >&2
    exit 2
fi
shift
cc=${CC:-cc}
kest=${KEST:-./kest}
if [ ! -x "$kest" ] || [ ! -f libkest.a ]; then
    echo "bench/tails.sh: the compiler and the library are not built" >&2
    exit 2
fi
if [ ! -x ./bench/measure ]; then
    echo "bench/tails.sh: \`make bench/measure\` first" >&2
    exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# What the other backend writes for this program, and this instrument built
# around it. A body it had no C for is a body the machine runs, so what the
# second half measures is whatever mixture that program turns out to be --
# which is what shipping one would be.
if ! "$kest" emit --c "$one" >"$work"/built.c 2>"$work"/why; then
    echo "bench/tails.sh: the other backend would not write \`$one\`" >&2
    sed 's/^/    /' "$work"/why >&2
    exit 2
fi
written=$(sed -n 's,^// \([0-9]*\) of \([0-9]*\) bodies written$,\1 of \2,p' \
          "$work"/built.c)
if ! $cc -O2 -Iinclude -DKEST_NO_MAIN -c -o "$work"/built.o "$work"/built.c \
        2>"$work"/why; then
    echo "bench/tails.sh: the host's compiler will not read what was written" >&2
    sed 's/^/    /' "$work"/why | head -5 >&2
    exit 2
fi
if ! $cc -O2 -Iinclude -DKEST_MEASURE_BUILT -o "$work"/measure-built \
        bench/measure.c "$work"/built.o libkest.a -lm 2>"$work"/why; then
    echo "bench/tails.sh: the instrument would not link against it" >&2
    sed 's/^/    /' "$work"/why | head -5 >&2
    exit 2
fi

echo "== by the machine"
./bench/measure "$one" "$@"
echo
echo "== by the bodies the other backend wrote, which are $written"
"$work"/measure-built "$one" "$@"
