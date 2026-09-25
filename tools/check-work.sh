#!/bin/sh
# How long compiling may take, held the way how much it may hold is. Every
# stage counts the work it does -- a word read, a piece of the tree made or
# checked, an instruction laid down or walked over while it is proved, a byte
# of a name built for a copy -- and a build given a ceiling stops where the
# count crosses it and says K0666. What is held here is that the count is a
# count: the same for every run of the same program, let through at exactly
# what it says and refused one unit under it; that a build stopped anywhere
# between nought and done says K0666 and nothing else; and that a file whose
# compiling doubles with every line it adds is a count that doubles too, so a
# ceiling is a bound on the time and not only on the words. See D1248.
set -u
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cd "$(dirname "$0")/.." || exit 1

if [ ! -x ./kest ]; then
    echo "work: the compiler is not built"
    exit 1
fi

failed=0
complain() {
    echo "work: $*"
    failed=1
}

# What `emit --json` says compiling took, which is the whole of compiling: a
# check stops before the instructions are laid down and proved.
work_of() {
    ./kest emit --json "$@" 2>/dev/null </dev/null |
        sed -n 's/.*"work":\([0-9]*\).*/\1/p' | head -1
}

held=0
rungs=0
for program in examples/*.kest; do
    counted=$(work_of "$program")
    if [ -z "$counted" ] || [ "$counted" -le 0 ]; then
        complain "\`$program\` says nothing about the work compiling it took"
        continue
    fi
    again=$(work_of "$program")
    if [ "$again" != "$counted" ]; then
        complain "\`$program\` took $counted units once and $again again"
        continue
    fi
    whole=$(./kest emit "$program" 2>&1 </dev/null)
    within=$(./kest emit --work "$counted" "$program" 2>&1 </dev/null)
    if [ "$within" != "$whole" ]; then
        complain "\`$program\` given the $counted units it takes said" \
                 "something else than with no ceiling:" \
                 "\`$(printf '%s' "$within" | grep -m1 error)\`"
        continue
    fi
    # One under, and then a rung at every sixteenth of the way down: each is a
    # build stopped in some other stage, and each has to stop in words.
    short=$((counted - 1))
    step=0
    while [ $step -le 16 ]; do
        if [ $step -eq 16 ]; then
            rung=$short
        else
            rung=$((counted * step / 16))
        fi
        [ "$rung" -le 0 ] && rung=1
        said=$(./kest emit --work "$rung" "$program" 2>&1 </dev/null)
        why=$?
        rungs=$((rungs + 1))
        case "$said" in
        "error[K0666]: compiling this took all $rung units of work it was given")
            if [ $why -ne 1 ]; then
                complain "\`$program\` under $rung units came back $why"
            fi
            ;;
        *)
            complain "\`$program\` under $rung of its $counted units came" \
                     "back $why saying" \
                     "\`$(printf '%s' "$said" | head -1 | cut -c1-80)\`"
            ;;
        esac
        step=$((step + 1))
    done
    told=$(./kest emit --json --work "$short" "$program" 2>/dev/null \
               </dev/null | sed -n 's/.*"work":\([0-9]*\).*/\1/p' | head -1)
    if [ "$told" != "$short" ]; then
        complain "\`$program\` refused at $short units says it took" \
                 "\`$told\`, which is not what it was given"
    fi
    held=$((held + 1))
done
if [ $held -eq 0 ]; then
    complain "no program was held to the work it takes"
fi

# A copy of a shape over two copies of the one before, as deep as the words
# ask: the name of each is twice as long as the last, and so is the time it
# takes to build, find and compare. A count that did not see that would grow
# by the same few units a level while the clock doubled, which is what it did
# before names were counted.
deep() {
    {
        printf 'struct Pair<A, B> {\n    first: A\n    second: B\n}\n\n'
        at=0
        while [ $at -lt "$1" ]; do
            printf 'fn f%u<T>(x: T) -> i32 {\n    return f%u(Pair(x, x))\n}\n\n' \
                "$at" $((at + 1))
            at=$((at + 1))
        done
        printf 'fn f%u<T>(x: T) -> i32 {\n    return 1\n}\n\n' "$1"
        printf 'fn main() -> i32 {\n    return f0(1)\n}\n'
    } >"$scratch/deep$1.kest"
}
deep 8
deep 10
eight=$(work_of "$scratch/deep8.kest")
ten=$(work_of "$scratch/deep10.kest")
if [ -z "$eight" ] || [ -z "$ten" ] || [ "$ten" -lt $((eight * 3)) ]; then
    complain "a copy twice as deep two levels on took \`${ten:-nothing}\`" \
             "units where eight took \`${eight:-nothing}\`, so the count" \
             "does not see what doubles"
fi
# And one deep enough that nobody waits for it, stopped by the ceiling a
# host that compiles what it was sent would give: refused, in words, at the
# units it was given, and before it had done anything like the whole.
deep 22
said=$(./kest check --json --work 1000000 "$scratch/deep22.kest" 2>/dev/null \
           </dev/null)
case "$said" in
*'"code":"K0666"'*'"work":1000000'*) ;;
*)
    complain "a copy twenty-two deep under a million units said" \
             "\`$(printf '%s' "$said" | cut -c1-120)\`"
    ;;
esac

# The words, which are read by the door every other count on this command
# line is: nothing after it, and something that is not a count.
for asking in "--work@\`--work\` says how many units, and there is nothing after it" \
        "--work 4X@\`4X\` is not a number of units of work" \
        "--work 0@\`0\` is not a number of units of work"; do
    words=${asking%%@*}
    refused_with=${asking#*@}
    # shellcheck disable=SC2086
    said=$(./kest check examples/boxes.kest $words 2>&1 </dev/null)
    case "$said" in
    *"K0649"*"$refused_with"*) ;;
    *)
        complain "\`kest check examples/boxes.kest $words\` said" \
                 "\`$(printf '%s' "$said" | head -1)\`"
        ;;
    esac
done

if [ $failed -eq 0 ]; then
    echo "compiling counts its work and stops where it is told: $held" \
         "program(s) each counted the same twice, let through at their own" \
         "count and refused one under it, $rungs rung(s) between nought and" \
         "done each refused in words at the units it was given, a copy" \
         "doubling with every level counted $eight then $ten, and one" \
         "twenty-two deep stopped at a million units"
fi
exit $failed
