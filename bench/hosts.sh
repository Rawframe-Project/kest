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
# Every host linked the way a game that cares what it ships links: what
# nothing reaches is left out, for every engine alike. The library here is
# built in sections so that there is something to leave out. See D1282.
linked=-Wl,--gc-sections
cxx=${CXX:-c++}
# The Tetris clone as it was written in Lua, which the Lua hosts reload the way
# Kest's host rebuilds `examples/tetromino.kest`. Luau cannot read it: it is
# written with `goto`.
game=bench/hosts/tetromino.lua
churn=bench/hosts/churn.lua
hosts=""
./kest emit --c bench/hosts/bodies.kest >"$room/bodies-native.c"
$cc -O2 $linked -DKEST_NO_MAIN -Iinclude -Ibench/hosts -o "$room/kest" \
    bench/hosts/kest.c "$room/bodies-native.c" libkest.a -lm
hosts="$room/kest"
if [ -n "${KEST_LUA_SRC:-}" ]; then
    $cc -O2 $linked -Ibench/hosts -I"$KEST_LUA_SRC/src" -o "$room/lua" \
        bench/hosts/lua.c "$KEST_LUA_SRC/src/liblua.a" -lm -ldl
    hosts="$hosts|$room/lua;Lua 5.4;bench/hosts/bodies.lua;--game;$game"
    hosts="$hosts;--churn;$churn"
    hosts="$hosts|$room/lua;Lua 5.4, generational;bench/hosts/bodies.lua"
    hosts="$hosts;--generational;--churn;$churn"
fi
if [ -n "${KEST_LUAJIT_SRC:-}" ]; then
    $cc -O2 $linked -DLUAJIT -Ibench/hosts -I"$KEST_LUAJIT_SRC/src" -o "$room/luajit" \
        bench/hosts/lua.c "$KEST_LUAJIT_SRC/src/libluajit.a" -lm -ldl
    hosts="$hosts|$room/luajit;LuaJIT, interpreted;bench/hosts/bodies.lua"
    hosts="$hosts;--off;--game;$game;--churn;$churn"
    hosts="$hosts|$room/luajit;LuaJIT;bench/hosts/bodies.lua;--game;$game"
    hosts="$hosts;--churn;$churn"
fi
if [ -n "${KEST_LUAU_SRC:-}" ]; then
    u=$KEST_LUAU_SRC
    luau="-O2 -std=c++17 -Ibench/hosts -I$u/VM/include -I$u/Compiler/include"
    luau_libs="$u/build/libLuau.Compiler.a $u/build/libLuau.Bytecode.a"
    luau_libs="$luau_libs $u/build/libLuau.Ast.a $u/build/libLuau.VM.a"
    luau_libs="$luau_libs $u/build/libLuau.Common.a -lm"
    $cxx $luau $linked -o "$room/luau" bench/hosts/luau.cpp $luau_libs
    $cxx $luau $linked -DNATIVE -I"$u/CodeGen/include" -o "$room/luau-native" \
        bench/hosts/luau.cpp "$u/build/libLuau.CodeGen.a" $luau_libs
    hosts="$hosts|$room/luau;Luau;bench/hosts/bodies.lua;--churn;$churn"
    hosts="$hosts|$room/luau-native;Luau, native;bench/hosts/bodies.lua"
    hosts="$hosts;--native;--churn;$churn"
fi
if [ -n "${KEST_QJS_SRC:-}" ]; then
    $cc -O2 $linked -Ibench/hosts -I"$KEST_QJS_SRC" -o "$room/quickjs" \
        bench/hosts/quickjs.c "$KEST_QJS_SRC/build/libqjs.a" -lm -lpthread
    hosts="$hosts|$room/quickjs;bench/hosts/bodies.js;bench/hosts/churn.js"
fi

# What each engine adds to a game's executable: its host stripped, less the
# same host with no engine in it. Every host is its engine's library linked the
# way a game links it, and the bench code in each is the same few kilobytes the
# floor has too.
cat >"$room/floor.c" <<'EOF'
#include "common.h"
int main(void) {
    floor_frames("C");
    return 0;
}
EOF
$cc -O2 $linked -Ibench/hosts -o "$room/floor" "$room/floor.c"
weigh() {
    strip -o "$room/weighed" "$1"
    echo $(($(wc -c <"$room/weighed") - $(wc -c <"$room/floor.stripped")))
}
strip -o "$room/floor.stripped" "$room/floor"
{
    printf 'Kest\tsize\t%s\t0\n' "$(weigh "$room/kest")"
    [ -x "$room/lua" ] &&
        printf 'Lua 5.4\tsize\t%s\t0\n' "$(weigh "$room/lua")"
    [ -x "$room/luajit" ] &&
        printf 'LuaJIT\tsize\t%s\t0\n' "$(weigh "$room/luajit")"
    [ -x "$room/luau" ] &&
        printf 'Luau\tsize\t%s\t0\n' "$(weigh "$room/luau")"
    [ -x "$room/luau-native" ] &&
        printf 'Luau, native\tsize\t%s\t0\n' "$(weigh "$room/luau-native")"
    [ -x "$room/quickjs" ] &&
        printf 'QuickJS\tsize\t%s\t0\n' "$(weigh "$room/quickjs")"
} >"$room/runs"

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
    printf '%s\n' "$hosts" | tr '|' '\n' | while IFS= read -r line; do
        [ -n "$line" ] || continue
        # One host and its words, split on `;` and nowhere else: an engine
        # name has spaces in it.
        old=$IFS
        IFS=';'
        set -- $line
        IFS=$old
        "$@"
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
