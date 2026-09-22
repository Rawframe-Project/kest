#!/bin/sh
# The suite run blind: a room for every task, language and attempt holding what
# whoever does the task may see and nothing else, and the answers left in those
# rooms judged afterwards by the tests they never saw.
#
#   ai/blind.sh make <where> [attempts]
#   ai/blind.sh score <where>
#
# `make` writes one room a run -- `<task>-<kest|luau>-<n>` -- with `ask.md`,
# the scaffold under the name the checks import it by, and for Kest the
# reference and the standard library's source, which is what a newcomer to it
# has. The right answer, the wrong one and the tests stay here. Each room gets
# an `INSTRUCTIONS.md` that says what to do, where, with which tools and what to
# report; what reads it is a model started on its own, one a room, and starting
# them is whoever runs this and not this script. `score` hands every answer to
# `ai/run.sh` and writes one line a room: the room, what the tests said, and
# nothing else. See D1148.
#
# Luau is found rather than built, as everywhere in `ai`: `KEST_LUAU` says
# where it is, and `luau-analyze` is looked for beside it.
set -u
if [ $# -lt 2 ]; then
    echo "usage: ai/blind.sh make <where> [attempts] | score <where>"
    exit 2
fi
what=$1
where=$2
here=$(cd "$(dirname "$0")/.." && pwd)
tasks="$here"/ai/tasks

case "$what" in
make)
    attempts=${3:-3}
    if [ -z "${KEST_LUAU:-}" ] || [ ! -x "${KEST_LUAU:-}" ]; then
        echo "no luau here; set KEST_LUAU to where it is"
        exit 2
    fi
    mkdir -p "$where"
    where=$(cd "$where" && pwd)
    for one in "$tasks"/*/; do
        task=$(basename "$one")
        n=1
        while [ "$n" -le "$attempts" ]; do
            room="$where/$task-kest-$n"
            mkdir -p "$room/reference/std"
            cp "$one/ask.md" "$room/"
            cp "$one/kest/start.kest" "$room/$task.kest"
            cp "$here/docs/language.md" "$room/reference/"
            cp "$here"/lib/std/*.kest "$room/reference/std/"
            cat > "$room/INSTRUCTIONS.md" <<EOF
You are doing a small programming task. Your working directory is $room. Everything you need is in it:

- $room/ask.md -- the task.
- $room/$task.kest -- the file to finish. It is written in Kest, a statically typed language for game logic. The shapes, names and signatures already in it belong to the task and must not change.
- $room/reference/language.md -- the language reference. $room/reference/std/ -- the source of the standard library.

Tools: \`$here/kest check FILE\` checks a file; \`$here/kest run FILE\` runs a file that has \`fn main() -> i32\`; \`$here/kest help\` lists the rest. You may write scratch files of your own inside $room, for example a test program $room/try.kest beginning \`module try\` and \`import $task\`.

Rules:
- Read, list, search and write only inside $room. Do not look at any other directory on this machine, do not use the web, and do not start other agents. The only program outside $room you may run is $here/kest.
- Stop when you believe $room/$task.kest does what ask.md asks. It is judged afterwards by tests you are not shown.

End with a short report: (1) finished or gave up; (2) how many times you ran the compiler (check or run); (3) how many of those refused your code with an error, and in a few words what each was about; (4) anything in the task, the language or the tools that confused you; (5) a confirmation that you read nothing outside $room.
EOF
            room="$where/$task-luau-$n"
            mkdir -p "$room"
            cp "$one/ask.md" "$room/"
            cp "$one/luau/start.lua" "$room/$task.lua"
            cat > "$room/INSTRUCTIONS.md" <<EOF
You are doing a small programming task. Your working directory is $room. Everything you need is in it:

- $room/ask.md -- the task.
- $room/$task.lua -- the file to finish. It is written in Luau (strict mode). The shapes, names and signatures already in it belong to the task and must not change.

Tools: \`$KEST_LUAU FILE\` runs a file; \`$KEST_LUAU-analyze FILE\` type-checks one. You may write scratch files of your own inside $room, for example a test script $room/try.lua that does \`local $task = require("./$task")\`.

Rules:
- Read, list, search and write only inside $room. Do not look at any other directory on this machine, do not use the web, and do not start other agents. The only programs outside $room you may run are $KEST_LUAU and $KEST_LUAU-analyze.
- Stop when you believe $room/$task.lua does what ask.md asks. It is judged afterwards by tests you are not shown.

End with a short report: (1) finished or gave up; (2) how many times you ran luau or luau-analyze; (3) how many of those reported an error in your code, and in a few words what each was about; (4) anything in the task, the language or the tools that confused you; (5) a confirmation that you read nothing outside $room.
EOF
            n=$((n + 1))
        done
    done
    echo "made $(ls "$where" | wc -l | tr -d ' ') room(s) in $where"
    ;;
score)
    for room in "$where"/*-kest-* "$where"/*-luau-*; do
        [ -d "$room" ] || continue
        name=$(basename "$room")
        task=${name%%-*}
        language=$(echo "$name" | cut -d- -f2)
        file="$room/$task.$( [ "$language" = kest ] && echo kest || echo lua )"
        said=$(cd "$here" && sh ai/run.sh "$task" "$language" "$file" 2>&1 |
            head -1)
        printf '%s\t%s\n' "$name" "$said"
    done
    ;;
*)
    echo "usage: ai/blind.sh make <where> [attempts] | score <where>"
    exit 2
    ;;
esac
