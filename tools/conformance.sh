#!/bin/sh
# What a platform has to say the same as another platform. Every runnable
# example, in the order the file system gives them, each with what it answered
# and what it wrote, into one file that two machines can be held to byte for
# byte. That is what "supported platform" means here: not that it built, but
# that it said the same thing.
#
# It takes the command to run as its first argument, because on Windows that is
# not `./kest`, and the place to write as its second.
set -eu

kest=${1:?say which kest to run}
out=${2:?say where to write}

: >"$out"
for one in examples/*.kest; do
    if ! grep -q '^fn main(' "$one"; then
        continue
    fi
    said=$("$kest" run "$one" 2>&1 </dev/null) && answer=0 || answer=$?
    printf '== %s\n' "$one" >>"$out"
    printf '%s\n' "$said" >>"$out"
    printf 'answered %s\n' "$answer" >>"$out"
done
