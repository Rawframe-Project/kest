#!/bin/sh
# The tasks a model is given, held to being tasks. `make check` runs this.
#
# Each one under `ai/tasks` is the same gameplay work written twice, in Kest
# and in an incumbent, with tests whoever does the task is not shown. What
# this holds is that the tests are worth being judged by: the answer written
# here passes every one of them, the scaffold nobody has filled in does not,
# and a plausible wrong answer is caught rather than passed. A test suite
# nobody has seen fail is indistinguishable from no test suite, which is the
# same rule the backstops are written under.
#
# Luau is found rather than built, the way the benchmarks find their
# comparators: without `KEST_LUAU` the Luau half of every task is counted and
# not run. See D1101.
set -u
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$(dirname "$0")/.." || exit 1

if [ ! -x ./kest ]; then
    echo "the compiler is not built; \`make\` first"
    exit 1
fi

tasks=$(find ai/tasks -mindepth 1 -maxdepth 1 -type d | sort)
if [ -z "$tasks" ]; then
    echo "there are no tasks under \`ai/tasks\`, so this reads nothing"
    exit 1
fi

# What the suite can say without a model having been near it: of the answers
# that are wrong, how many the language refused before they ran and how many
# only a test caught. A mistake a compiler refuses costs a model one turn; a
# mistake only a hidden test catches costs it a run, and one nothing catches
# escapes. Nothing here escapes, because the gate below would not pass if it
# did -- what this counts is which of the two caught it, per language. See
# D1125.
wrong=0
asked=0
skipped=0
kest_refused=0
kest_tested=0
luau_refused=0
luau_tested=0
for one in $tasks; do
    name=$(basename "$one")
    if [ ! -f "$one"/ask.md ]; then
        echo "\`$name\` has no \`ask.md\`, so nobody could do it"
        wrong=$((wrong + 1))
        continue
    fi
    for language in kest luau; do
        case $language in
        kest) suffix=kest ;;
        luau) suffix=lua ;;
        esac
        if [ ! -f "$one/$language/done.$suffix" ]; then
            echo "\`$name\` has no answer written in $language"
            wrong=$((wrong + 1))
            continue
        fi
        if [ "$language" = luau ] &&
                { [ -z "${KEST_LUAU:-}" ] || [ ! -x "${KEST_LUAU:-}" ]; }; then
            skipped=$((skipped + 1))
            continue
        fi
        # The answer written here keeps every sentence of the task.
        said=$(./ai/run.sh "$name" "$language" "$one/$language/done.$suffix")
        if [ "$said" != "0" ]; then
            echo "\`$name\` in $language: the answer written here answers \
$said, and the tests are what a task is judged by"
            wrong=$((wrong + 1))
        fi
        asked=$((asked + 1))
        # The scaffold does not, or the task asks for nothing.
        said=$(./ai/run.sh "$name" "$language" "$one/$language/start.$suffix")
        if [ "$said" = "0" ]; then
            echo "\`$name\` in $language: the scaffold nobody filled in passes, \
so the tests ask for nothing"
            wrong=$((wrong + 1))
        fi
        # And the wrong answer is caught rather than passed, which is the
        # whole of what a held-out test is for -- and which of the two caught
        # it is counted, because that is the difference between a mistake a
        # model is told about at once and one it has to run to find.
        said=$(./ai/run.sh "$name" "$language" "$one/$language/astray.$suffix")
        if [ "$said" = "0" ]; then
            echo "\`$name\` in $language: the wrong answer written for it \
passes, so nothing here would catch that mistake"
            wrong=$((wrong + 1))
        else
            case "$said" in
            refused*)
                case "$language" in
                kest) kest_refused=$((kest_refused + 1)) ;;
                luau) luau_refused=$((luau_refused + 1)) ;;
                esac
                ;;
            *)
                case "$language" in
                kest) kest_tested=$((kest_tested + 1)) ;;
                luau) luau_tested=$((luau_tested + 1)) ;;
                esac
                ;;
            esac
        fi
    done
done

if [ "$wrong" -ne 0 ]; then
    echo "$wrong thing(s) wrong with the tasks a model is given"
    exit 1
fi
echo "$asked task(s) and language(s): the answer written here keeps every \
test, the scaffold does not, and none of the wrong answers escapes: \
$kest_tested caught by a test and $kest_refused refused before running in \
Kest, $luau_tested and $luau_refused in Luau -- and \
$skipped left out for want of the language they are written beside"
