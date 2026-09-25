#!/bin/sh
# What OSS-Fuzz runs to build this project's fuzzer, in the shape its builds
# take: `$CC`, `$CFLAGS` and `$LIB_FUZZING_ENGINE` are its, and what is left in
# `$OUT` is the fuzzer, the standard library beside it, and the examples and
# the library as a starting corpus. Run here with those set the way clang's
# libFuzzer wants them, it is the same fuzzer `make tools/fuzz-cover` makes.
# Applying to OSS-Fuzz is a project somebody answers for, with an address to
# write to, so it is not done from here. See D1253.
set -eu
cd "$(dirname "$0")/.."
: "${CC:=clang}"
: "${CFLAGS:=-O1 -g -fsanitize=address,undefined -fsanitize=fuzzer-no-link}"
: "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${OUT:=build/oss-fuzz}"
mkdir -p "$OUT" build/oss-fuzz-objects
for source in src/*.c; do
    case "$source" in
    src/main.c) continue ;;
    esac
    object="build/oss-fuzz-objects/$(basename "$source" .c).o"
    # shellcheck disable=SC2086
    $CC -std=c11 $CFLAGS -Iinclude -c -o "$object" "$source"
done
# shellcheck disable=SC2086
$CC -std=c11 $CFLAGS $LIB_FUZZING_ENGINE -Iinclude -o "$OUT/kest_source" \
    tools/fuzz-cover.c build/oss-fuzz-objects/*.o -lm
rm -rf "$OUT/lib"
mkdir -p "$OUT/lib"
cp -r lib/std "$OUT/lib/std"
rm -f "$OUT/kest_source_seed_corpus.zip"
if command -v zip >/dev/null 2>&1; then
    zip -q "$OUT/kest_source_seed_corpus.zip" examples/*.kest lib/std/*.kest
fi
echo "built $OUT/kest_source"
