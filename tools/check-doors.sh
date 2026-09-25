#!/bin/sh
# What a host's doors do with what a program nobody trusts hands them.
# `kest hostile` writes the file a host runs with a function after it for every
# call of every door it declares -- numbers at the ends of their widths, floats
# that are not numbers, text of no length and of a great deal, every case of an
# enum -- and `examples/embed`, built under the sanitisers, binds its doors and
# makes every one of those calls. What is held is that each call answers or is
# refused by the door itself in words: no report from the sanitisers, and no
# refusal from the machine for something a door answered that the program
# could not have made, which is a door that did not look at what it was handed.
# And that some of them were refused, because a run in which every door
# answered everything was not handed anything hard. See D1254.
set -u
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cd "$(dirname "$0")/.." || exit 1
here=$(pwd)

if [ ! -x ./kest ] || [ ! -x examples/embed-debug ]; then
    echo "doors: the compiler and the sanitised engine are not built"
    exit 1
fi

# The program is read where it would be, under a root of its own: what it
# imports resolves beside it, and the library it names is `lib`.
mkdir -p "$scratch"/examples
ln -s "$here"/examples/twins "$scratch"/examples/twins
ln -s "$here"/lib "$scratch"/lib

failed=0
calls=0
refused=0
for seed in 1 2 3 4 5 6 7 8; do
    if ! ./kest hostile examples/embed.kest "$seed" \
            >"$scratch"/examples/embed.kest 2>"$scratch"/written; then
        echo "doors: \`kest hostile\` at seed $seed said" \
             "\`$(head -1 "$scratch"/written)\`"
        failed=1
        continue
    fi
    said=$(cd "$scratch" && "$here"/examples/embed-debug examples/embed.kest \
               2>&1 </dev/null)
    why=$?
    case "$said" in
    *Sanitizer*)
        echo "doors: seed $seed made a door read or write what it does not own:"
        printf '%s\n' "$said" | grep -m3 "ERROR\|SUMMARY\|#1 " |
            sed 's/^/    /'
        failed=1
        continue
        ;;
    esac
    if [ $why -ne 0 ]; then
        echo "doors: the engine came back $why at seed $seed:" \
             "\`$(printf '%s' "$said" | grep -m1 error)\`"
        failed=1
        continue
    fi
    ran=$(printf '%s' "$said" |
          sed -n 's/^hostile: \([0-9]*\) call(s).*/\1/p')
    if [ -z "$ran" ] || [ "$ran" -eq 0 ]; then
        echo "doors: at seed $seed the engine called no door"
        failed=1
        continue
    fi
    calls=$((calls + ran))
    # A door refusing is the door's own words, K0662. Anything else the
    # machine says about a door is about what the door did.
    wrong=$(printf '%s' "$said" | grep '^error\[' | grep -v '^error\[K0662\]' |
            head -1)
    if [ -n "$wrong" ]; then
        echo "doors: at seed $seed a door answered what it should have" \
             "refused: \`$wrong\`"
        failed=1
    fi
    refused=$((refused + $(printf '%s' "$said" | grep -c '^error\[K0662\]')))
done
if [ $failed -eq 0 ] && [ $refused -eq 0 ]; then
    echo "doors: $calls call(s) and not one refused, so nothing hard was" \
         "handed over"
    failed=1
fi
if [ $failed -eq 0 ]; then
    echo "every door the engine binds answered or refused in words: $calls" \
         "call(s) from eight seeds with what a program nobody trusts could" \
         "hand them, $refused of them refused by the door itself, under the" \
         "sanitisers"
fi
exit $failed
