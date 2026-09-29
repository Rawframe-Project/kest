#!/bin/sh
# What `bench/determinism/sim` answers in every language this machine has,
# one line each: the engine, the platform it ran on, and the answer, written
# to `$OUT`. `.github/workflows/determinism.yml` runs this on every machine CI
# has and puts the lines together into `bench/determinism.tsv`, which says
# whether one language answered the same everywhere. An engine that is not
# given is left out. See D1274.
#
#   PLATFORM=linux-x86_64 KEST=./kest LUA=lua LUAJIT=luajit LUAU=luau \
#       NODE=node OUT=answers.tsv sh bench/determinism.sh
set -eu

out=${OUT:-answers.tsv}
platform=${PLATFORM:?say which platform this is}
: >"$out"

said() {
    printf '%s\t%s\t%s\n' "$1" "$platform" "$2" >>"$out"
}

said Kest "$("${KEST:-./kest}" run bench/determinism/sim.kest | tr -d '\r')"
if [ -n "${LUA:-}" ]; then
    said "Lua 5.4" "$("$LUA" bench/determinism/sim.lua | tr -d '\r')"
fi
if [ -n "${LUAJIT:-}" ]; then
    said LuaJIT "$("$LUAJIT" bench/determinism/sim.lua | tr -d '\r')"
fi
if [ -n "${LUAU:-}" ]; then
    said Luau "$("$LUAU" bench/determinism/sim.lua | tr -d '\r')"
fi
if [ -n "${NODE:-}" ]; then
    said "JavaScript (Node)" "$("$NODE" bench/determinism/sim.js | tr -d '\r')"
fi
cat "$out"
