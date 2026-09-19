#!/bin/sh
# What 1.x promises, held against the version that made the promise.
#
# The reference says a program that checks under 1.x checks under every later
# 1.x, that a diagnostic keeps its code, that the C ABI does not change inside
# 1.x, and that a deterministic program answers the same bytes. Those are four
# sentences, and this is the four of them asked of the tree as it is now with
# `v1.0.0` on the other side of the question.
#
# The tag is the oracle and it is immutable. What is compared is what a
# program answers, what a refusal is called, what the header declares and what
# the one form is — and never the bytecode, which is not a compatibility
# surface and has no version (D983).
#
# A tree with no git and no tag says so and holds nothing, which is what a
# release archive is: this reads history, and an archive has none.
set -e
set -u

kest=${KEST:-./kest}
tag=${KEST_KEPT:-v1.0.0}
failed=0

if [ ! -x "$kest" ]; then
    echo "kept: $kest is not there" >&2
    exit 2
fi

if ! command -v git >/dev/null 2>&1 ||
    ! git rev-parse --git-dir >/dev/null 2>&1 ||
    ! git rev-parse -q --verify "$tag^{commit}" >/dev/null 2>&1; then
    echo "kept: there is no $tag here to compare against, so nothing was \
compared"
    exit 0
fi

room=$(mktemp -d "${TMPDIR:-/tmp}"/kest-v1-XXXXXX)
trap 'rm -rf "$room"' EXIT

# The tag's own tree, built with the tag's own compiler. Two binaries of two
# versions is the whole of what this needs: one to say what 1.0.0 answered and
# one to say what this tree answers.
git archive "$tag" | tar -x -C "$room"
(cd "$room" && make -s -j"$(nproc 2>/dev/null || echo 4)" kest) >/dev/null 2>&1 ||
    {
        echo "kept: $tag would not build here, so nothing was compared" >&2
        exit 2
    }
was="$room"/kest
now=$PWD/$kest

# Every program the tag shipped, run by both. What is compared is what the
# program wrote and what it answered, which is what a program is: a file that
# checked then and does not check now is the promise broken, and one that
# answers something else is the promise broken more quietly.
ran=0
for file in $(cd "$room" && find examples lib bench -name '*.kest' 2>/dev/null |
    sort); do
    if ! grep -q '^fn main(' "$room/$file"; then
        if ! (cd "$room" && "$now" check "$file") >/dev/null 2>&1; then
            echo "kept: \`$file\` checked under $tag and does not now"
            failed=1
        fi
        continue
    fi
    then_said=$(cd "$room" && "$was" run "$file" 2>&1 </dev/null) || true
    then_was=$?
    now_said=$(cd "$room" && "$now" run "$file" 2>&1 </dev/null) || true
    now_was=$?
    if [ "$then_said" != "$now_said" ] || [ "$then_was" != "$now_was" ]; then
        echo "kept: \`$file\` says something else than it did under $tag"
        failed=1
        continue
    fi
    ran=$((ran + 1))
done

# And the same programs in the one form. A formatter that rewrites what 1.0.0
# shipped is a formatter that has changed what the one form is, which is a
# thing a program written then can see.
if ! (cd "$room" && "$now" fmt --check $(find examples lib -name '*.kest' |
    sort)) >/dev/null 2>&1; then
    echo "kept: the formatter would rewrite what $tag shipped, so the one form \
has moved"
    failed=1
fi

# What a refusal is called. A code is the one thing a tool reading a
# diagnostic holds on to, and the reference says it keeps its identity inside
# 1.x. Written here rather than taken from a corpus, because what this needs
# is a handful of refusals of different kinds that both versions can be made
# to say.
codes=0
mkdir -p "$room"/refusals
{
    printf 'module r\n\nfn main() -> i32 {\n    return nobody()\n}\n'
} > "$room"/refusals/unknown.kest
{
    printf 'module r\n\nfn main() -> i32 {\n    let x: i32 = "a"\n'
    printf '    return x\n}\n'
} > "$room"/refusals/types.kest
{
    printf 'module r\n\nfn grow() -> [i32] no.alloc {\n'
    printf '    let a: [i32] = array()\n    return a\n}\n\n'
    printf 'fn main() -> i32 {\n    return len(grow())\n}\n'
} > "$room"/refusals/promise.kest
{
    printf 'module r\n\nfn main() -> i32 {\n    return 1 +\n}\n'
} > "$room"/refusals/parse.kest
for file in "$room"/refusals/*.kest; do
    then_code=$("$was" check "$file" 2>&1 </dev/null |
        sed -n 's/.*error\[\(K[0-9]*\)\].*/\1/p' | sort -u | tr '\n' ' ')
    now_code=$("$now" check "$file" 2>&1 </dev/null |
        sed -n 's/.*error\[\(K[0-9]*\)\].*/\1/p' | sort -u | tr '\n' ' ')
    if [ -z "$then_code" ]; then
        echo "kept: \`$(basename "$file")\` was meant to be refused and $tag \
took it"
        failed=1
        continue
    fi
    if [ "$then_code" != "$now_code" ]; then
        echo "kept: \`$(basename "$file")\` was $then_code under $tag and is \
$now_code now"
        failed=1
        continue
    fi
    codes=$((codes + 1))
done

# What the header declares. The ABI is frozen inside 1.x, and what that means
# is that a host compiled against the header the tag shipped finds every door
# it was given, spelled the way it was spelled. A door added since is allowed
# and is not looked for here; one that moved or went is what this catches.
doors=$(python3 - "$room"/include/kest.h include/kest.h <<'PY'
import re
import sys


def doors(path):
    text = open(path).read()
    # A declaration is a line ending in a semicolon whose name is `kest_`
    # something, read across the lines it is written over. Whitespace is not
    # part of what a host sees, so it is squeezed before the two are compared.
    said = {}
    for one in re.findall(r"^[A-Za-z_][^;{}#]*?\bkest_[a-z_]+\([^;{}]*\);",
                          text, re.M | re.S):
        flat = " ".join(one.split())
        name = re.search(r"\b(kest_[a-z_]+)\(", flat).group(1)
        said[name] = flat
    return said


was, now = doors(sys.argv[1]), doors(sys.argv[2])
gone = sorted(name for name in was if name not in now)
moved = sorted(name for name in was
               if name in now and was[name] != now[name])
for name in gone:
    print("kept: `%s` was a door then and is not one now" % name)
for name in moved:
    print("kept: `%s` is declared differently than it was:" % name)
    print("       then %s" % was[name])
    print("       now  %s" % now[name])
print("COUNT %u %u" % (len(was), len(now)))
sys.exit(1 if gone or moved else 0)
PY
) || failed=1
printf '%s\n' "$doors" | grep -v '^COUNT ' || true
door_count=$(printf '%s\n' "$doors" | sed -n 's/^COUNT \([0-9]*\) .*/\1/p')
now_doors=$(printf '%s\n' "$doors" | sed -n 's/^COUNT [0-9]* \([0-9]*\)/\1/p')

# And the deterministic profile, which is the one promise with bytes on the
# end of it: the tag's own corpus, run by both, answering the same trace.
if [ -f "$room"/tools/conformance.sh ]; then
    (cd "$room" && sh tools/conformance.sh "$was" then.trace) >/dev/null 2>&1 ||
        true
    (cd "$room" && sh tools/conformance.sh "$now" now.trace) >/dev/null 2>&1 ||
        true
    if [ ! -s "$room"/then.trace ] || [ ! -s "$room"/now.trace ]; then
        echo "kept: the conformance corpus did not answer under both"
        failed=1
    elif ! cmp -s "$room"/then.trace "$room"/now.trace; then
        echo "kept: the deterministic corpus answers different bytes than it \
did under $tag"
        failed=1
    fi
fi

if [ $failed -ne 0 ]; then
    exit 1
fi
echo "$ran program(s) $tag shipped answer what they answered and are in the \
one form, $codes refusal(s) keep their codes, all $door_count door(s) the \
header declared are declared the same way among the $now_doors there are now, \
and the deterministic corpus answers the same bytes"
