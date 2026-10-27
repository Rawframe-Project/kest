#!/bin/sh
# Programs for defects that once escaped, held to staying fixed. Each one in
# `test/regress` checks itself and answers 0. It is checked with nothing to
# say, then run every way the tree can run it: the release build, the checking
# build, the checking build walking the heap at every allocation, without the
# lowering's fusions, without the IR rewrites, and compiled to C. Every run has
# to answer 0 and write what the release build wrote. Each one in
# `test/refused` says on its first line which code it is refused with, and
# both builds have to refuse it with that code first. The comment at the top
# of each file names the defect it guards.
set -u

# What the build made: the preset's build directory, out/dev unless
# KEST_BUILD names another.
build_dir=${KEST_BUILD:-out/dev}
case $build_dir in /*) ;; *) build_dir=$(pwd)/$build_dir ;; esac

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cd "$(dirname "$0")/.." || exit 1
here=$(pwd)
failed=0

complain() {
    printf '%s: %s\n' "$1" "$2"
    failed=1
}

for need in kest kest-debug; do
    if [ ! -x "$build_dir/$need" ]; then
        printf 'regress: %s is not built in %s\n' "$need" "$build_dir"
        exit 1
    fi
done

ran=0
for file in test/regress/*.kest; do
    name=$(basename "$file" .kest)
    if [ -n "$($build_dir/kest check "$file" 2>&1 >/dev/null </dev/null)" ]; then
        complain "$file" "has something to say about itself under check"
    fi
    $build_dir/kest run "$file" >"$scratch/$name.want" 2>&1 </dev/null
    status=$?
    if [ "$status" -ne 0 ]; then
        complain "$file" "answered $status under the release build"
        sed 's/^/    /' "$scratch/$name.want" | head -5
        continue
    fi
    for way in checking walking plain noopt; do
        case $way in
        checking) $build_dir/kest-debug run "$file" ;;
        walking) KEST_WALK_EVERY=1 $build_dir/kest-debug run "$file" ;;
        plain) KEST_PLAIN=1 $build_dir/kest run "$file" ;;
        noopt) KEST_NOOPT=1 $build_dir/kest run "$file" ;;
        esac >"$scratch/$name.$way" 2>&1 </dev/null
        status=$?
        if [ "$status" -ne 0 ]; then
            complain "$file" "answered $status run $way"
            sed 's/^/    /' "$scratch/$name.$way" | head -5
        elif ! cmp -s "$scratch/$name.want" "$scratch/$name.$way"; then
            complain "$file" "wrote something else run $way"
            diff "$scratch/$name.want" "$scratch/$name.$way" | head -5
        fi
    done
    mkdir -p "$scratch/c"
    if ! (cd "$scratch/c" && $build_dir/kest build --release "$here/$file") \
            >"$scratch/$name.build" 2>&1 </dev/null; then
        complain "$file" "would not build as C"
        sed 's/^/    /' "$scratch/$name.build" | head -5
    else
        "$scratch/c/$name" >"$scratch/$name.c" 2>&1 </dev/null
        status=$?
        if [ "$status" -ne 0 ]; then
            complain "$file" "answered $status compiled to C"
            sed 's/^/    /' "$scratch/$name.c" | head -5
        elif ! cmp -s "$scratch/$name.want" "$scratch/$name.c"; then
            complain "$file" "wrote something else compiled to C"
            diff "$scratch/$name.want" "$scratch/$name.c" | head -5
        fi
    fi
    ran=$((ran + 1))
done

refused=0
for file in test/refused/*.kest; do
    code=$(sed -n '1s|^// Refused with \(K[0-9]\{4\}\)\..*|\1|p' "$file")
    if [ -z "$code" ]; then
        complain "$file" "does not say on its first line which code refuses it"
        continue
    fi
    for binary in kest kest-debug; do
        "$build_dir/$binary" run "$file" >"$scratch/refused" 2>&1 </dev/null
        status=$?
        first=$(grep -m 1 '^error\[' "$scratch/refused")
        if [ "$status" -eq 0 ]; then
            complain "$file" "ran under $binary instead of being refused"
        elif [ "${first#error\["$code"\]}" = "$first" ]; then
            complain "$file" "was refused under $binary with '$first', not $code"
        fi
    done
    refused=$((refused + 1))
done

if [ "$ran" -eq 0 ] || [ "$refused" -eq 0 ]; then
    complain "regress" "found no programs in test/regress or test/refused"
fi
if [ "$failed" -ne 0 ]; then
    exit 1
fi
printf 'regress: %u program(s) answer the same six ways, and %u are refused with their code under both builds\n' \
    "$ran" "$refused"
