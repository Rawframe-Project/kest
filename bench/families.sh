#!/bin/sh
# Every family of workload this project measures, run through the one
# instrument that keeps its samples. `bench/run.sh` compares languages and
# answers the best of five; this one answers where the time goes inside this
# language, and it says under what conditions it answered.
#
# Four families, which is what the post-v1 performance work is organised
# around:
#
#   micro      one function per thing the machine does, timed on its own
#   meso       the three reference programs, whole
#   boundary   what a frame costs lent, crossed a value at a time, and in C
#   pressure   what a world costs when it is worked on rather than grown
#
# Nothing here is a pass or a fail. It is what to read before changing
# something and what to read after.
set -eu

kest=${KEST:-./kest}
measure=${MEASURE:-./bench/measure}
rounds=${ROUNDS:-100000}
samples=${SAMPLES:-30}

if [ ! -x "$measure" ]; then
    echo "bench/families.sh: $measure is not there; \`make bench/measure\`" >&2
    exit 2
fi
if [ ! -x "$kest" ]; then
    echo "bench/families.sh: $kest is not there; \`make\`" >&2
    exit 2
fi

# What was measured, so that two runs of this can be told apart. Everything
# here is asked of the machine rather than written down.
commit=$(git rev-parse --short HEAD 2>/dev/null || echo "not a repository")
dirty=$(git status --porcelain 2>/dev/null | head -1)
echo "kest $($kest --version)"
echo "commit $commit${dirty:+ (with uncommitted changes)}"
if [ -r /proc/cpuinfo ]; then
    echo "cpu $(sed -n 's/^model name[ \t]*: //p' /proc/cpuinfo | head -1)"
fi
echo "$(uname -srm)"
"$measure" bench/micro.kest --entry micro.intMath --arg 1 --samples 1 \
    --warmup 0 --builds 1 2>/dev/null | sed -n '2p'
echo "$rounds round(s) an entry, $samples sample(s), every sample kept"
echo

# One body at a time. What is printed beside each is nanoseconds a round,
# which is the number to compare against the one beside it rather than against
# another machine.
echo "micro                   p50 ms    p95 ms    p99 ms    max ms    ns/round"
for one in intMath realMath realMath32 branchKnown branchUnknown callDirect \
    callIndirect aggregateSmall aggregateWide fixedRun arrayWalk arrayIndex \
    arrayWrite textLength textSearch textMake textSplit enumMatch optionals \
    storeWalk storeWrite allocates scratches; do
    # The ones that make something every round are given fewer rounds, because
    # a hundred thousand of them is a heap rather than a measurement.
    many=$rounds
    case "$one" in
    textMake | textSplit | allocates | scratches | storeWrite)
        many=$((rounds / 10))
        ;;
    arrayWalk | arrayIndex | arrayWrite | storeWalk)
        many=$((rounds / 100))
        ;;
    esac
    said=$("$measure" bench/micro.kest --entry "micro.$one" --arg "$many" \
        --samples "$samples" --warmup 5 --builds 1 2>/dev/null |
        sed -n 's/^calling *//p')
    if [ -z "$said" ]; then
        echo "$(printf '%-20s' "$one")  would not run"
        continue
    fi
    p50=$(echo "$said" | awk '{print $1}')
    printf '%-20s %9s %9s %9s %9s  %10.1f\n' "$one" \
        "$p50" "$(echo "$said" | awk '{print $2}')" \
        "$(echo "$said" | awk '{print $3}')" \
        "$(echo "$said" | awk '{print $4}')" \
        "$(awk -v ms="$p50" -v n="$many" 'BEGIN { print ms * 1000000 / n }')"
done
echo "a round of arrayWalk, arrayIndex, arrayWrite, storeWalk and storeWrite is"
echo "sixty-four elements; every other round is one."
echo

# The three reference programs, whole, at the scale they are written for.
echo "meso                    p50 ms    p95 ms    p99 ms    max ms    steps"
for one in agents rules; do
    said=$("$measure" "bench/$one.kest" --samples 10 --warmup 2 --builds 1 \
        2>/dev/null)
    calling=$(echo "$said" | sed -n 's/^calling *//p')
    steps=$(echo "$said" | sed -n 's/^a call ran \([0-9]*\) step.*/\1/p')
    printf '%-20s %9s %9s %9s %9s  %s\n' "$one" \
        "$(echo "$calling" | awk '{print $1}')" \
        "$(echo "$calling" | awk '{print $2}')" \
        "$(echo "$calling" | awk '{print $3}')" \
        "$(echo "$calling" | awk '{print $4}')" "$steps"
done
echo

# The boundary, which needs its own host because a host is what crosses one.
if [ -x ./bench/frame ]; then
    echo "boundary"
    ./bench/frame --bodies 20000 --frames 50
else
    echo "boundary                skipped: \`make bench/frame\` first"
fi
echo

# And what a world costs when it is worked on rather than grown: the most the
# heap ever held at once, which is what a host has to find room for, at ten
# times the rounds and at a hundred. A shape that settles answers the same
# figure twice.
echo "pressure             200       2000      20000     (bytes held at most)"
for shape in replace reuse keep turn nest burst; do
    line=$(printf '%-20s' "$shape")
    for many in 200 2000 20000; do
        held=$("$kest" profile --room 2M examples/churn.kest -- "$many" \
            "$shape" 2>&1 >/dev/null |
            sed -n '1s/.*heap and \([0-9]*\) at most.*/\1/p')
        line="$line $(printf '%-9s' "${held:-would not run}")"
    done
    echo "$line"
done
