#!/bin/sh
# What crossing into an engine costs, and how many bodies fit in a frame: each
# host in `bench/hosts` built against its engine's own library, run five times,
# and the best of the five written to `bench/hosts.tsv` with the date, the
# commit, the machine and how busy it was, for `bench/chart.py` to draw. Not
# part of `make check`, for the reason none of `bench` is.
#
# Where each comparator's source tree is built is said by an environment
# variable; a host whose engine is not given is left out rather than guessed:
#
#   KEST_LUA_SRC     lua-5.4.x, built (`make linux`)
#   KEST_LUAJIT_SRC  LuaJIT, built (`make`)
#   KEST_LUAU_SRC    luau, built with CMake into `build/`
#   KEST_QJS_SRC     quickjs-ng, built with CMake into `build/`
#
# Each line a host writes is an engine, a measure, nanoseconds for one of what
# the measure counts and the answer the work summed to, which has to be the
# same for every engine; a run whose answers differ is refused. See D1272.
set -eu

best=${BEST:-5}
out=${OUT:-bench/hosts.tsv}
room=$(mktemp -d)
trap 'rm -rf "$room"' EXIT

# A machine somebody else is using says what they were doing as much as what
# this was, so wait a while for it to be quiet, and write down how busy it was.
quiet() {
    waited=0
    while [ "$waited" -lt "${QUIET_WAIT:-600}" ]; do
        busy=$(cut -d' ' -f1 /proc/loadavg)
        if awk -v b="$busy" -v q="${QUIET_LOAD:-2}" 'BEGIN { exit !(b < q) }'
        then
            return
        fi
        sleep 10
        waited=$((waited + 10))
    done
}

cc=${CC:-cc}
cxx=${CXX:-c++}
hosts=""
./kest emit --c bench/hosts/bodies.kest >"$room/bodies-native.c"
$cc -O2 -DKEST_NO_MAIN -Iinclude -Ibench/hosts -o "$room/kest" \
    bench/hosts/kest.c "$room/bodies-native.c" libkest.a -lm
hosts="$room/kest"
if [ -n "${KEST_LUA_SRC:-}" ]; then
    $cc -O2 -Ibench/hosts -I"$KEST_LUA_SRC/src" -o "$room/lua" \
        bench/hosts/lua.c "$KEST_LUA_SRC/src/liblua.a" -lm -ldl
    hosts="$hosts|$room/lua;Lua 5.4;bench/hosts/bodies.lua"
fi
if [ -n "${KEST_LUAJIT_SRC:-}" ]; then
    $cc -O2 -DLUAJIT -Ibench/hosts -I"$KEST_LUAJIT_SRC/src" -o "$room/luajit" \
        bench/hosts/lua.c "$KEST_LUAJIT_SRC/src/libluajit.a" -lm -ldl
    hosts="$hosts|$room/luajit;LuaJIT, interpreted;bench/hosts/bodies.lua;off"
    hosts="$hosts|$room/luajit;LuaJIT;bench/hosts/bodies.lua"
fi
if [ -n "${KEST_LUAU_SRC:-}" ]; then
    u=$KEST_LUAU_SRC
    $cxx -O2 -std=c++17 -Ibench/hosts -I"$u/VM/include" \
        -I"$u/Compiler/include" -I"$u/CodeGen/include" -o "$room/luau" \
        bench/hosts/luau.cpp "$u/build/libLuau.CodeGen.a" \
        "$u/build/libLuau.Compiler.a" "$u/build/libLuau.Bytecode.a" \
        "$u/build/libLuau.Ast.a" "$u/build/libLuau.VM.a" \
        "$u/build/libLuau.Common.a" -lm
    hosts="$hosts|$room/luau;Luau;bench/hosts/bodies.lua"
    hosts="$hosts|$room/luau;Luau, native;bench/hosts/bodies.lua;native"
fi
if [ -n "${KEST_QJS_SRC:-}" ]; then
    $cc -O2 -Ibench/hosts -I"$KEST_QJS_SRC" -o "$room/quickjs" \
        bench/hosts/quickjs.c "$KEST_QJS_SRC/build/libqjs.a" -lm -lpthread
    hosts="$hosts|$room/quickjs;bench/hosts/bodies.js"
fi

quiet
{
    printf '# taken\t%s\n' "$(date -u +%Y-%m-%d)"
    printf '# commit\t%s\n' "$(git rev-parse --short HEAD)"
    printf '# machine\t%s\n' \
        "$(awk -F': ' '/^model name/ { print $2; exit }' /proc/cpuinfo)"
    printf '# load\t%s\n' "$(cut -d' ' -f1 /proc/loadavg)"
    printf '# best of\t%s\n' "$best"
    printf 'engine\tmeasure\tns\tanswer\n'
} >"$out"

# Every host once a round, rounds over and over, so a neighbour's burst falls
# on one run of many hosts rather than on every run of one.
round=0
while [ "$round" -lt "$best" ]; do
    printf '%s\n' "$hosts" | tr '|' '\n' | while IFS=';' read -r host a b c; do
        [ -n "$host" ] || continue
        if [ -z "$a" ]; then
            "$host"
        elif [ -z "$b" ]; then
            "$host" "$a"
        elif [ -z "$c" ]; then
            "$host" "$a" "$b"
        else
            "$host" "$a" "$b" "$c"
        fi
    done >>"$room/runs"
    round=$((round + 1))
done

python3 - "$room/runs" "$out" <<'EOF'
import sys
best = {}
answers = {}
order = []
for line in open(sys.argv[1]):
    engine, measure, ns, answer = line.rstrip("\n").split("\t")
    key = (engine, measure)
    if key not in best:
        order.append(key)
        best[key] = float(ns)
    best[key] = min(best[key], float(ns))
    answers.setdefault(measure, set()).add(answer)
wrong = [m for m, a in answers.items() if len(a) > 1]
if wrong:
    sys.exit("the engines answered differently for %s" % ", ".join(wrong))
with open(sys.argv[2], "a") as out:
    for engine, measure in order:
        out.write("%s\t%s\t%.2f\t%s\n" % (engine, measure,
                                          best[(engine, measure)],
                                          next(iter(answers[measure]))))
EOF
python3 bench/chart.py bench/results.tsv bench
echo "wrote $out"
