#!/bin/sh
# Every function a header declares has to be there, and something other than
# the file it lives in has to call it. A declaration for a function nobody
# wrote is a promise the linker keeps quiet about until somebody takes it up,
# and a function nothing calls is read as a thing that is used.
#
# The public header is held to the same rule by the two hosts in this tree:
# what a host cannot be shown using is what nobody has run.
#
# The symbols are read out of the objects rather than the text, because a name
# in a comment is not a call and a name in a string is not a definition.
set -u
exec python3 - "$@" <<'PY'
import os
import re
import subprocess
import sys

OBJECTS = "build/release"
HOSTS = ["build/release/main.o", "examples/embed.o"]

failed = 0


def symbols(path):
    out = subprocess.run(["nm", path], capture_output=True, text=True).stdout
    made = set()
    wanted = set()
    for line in out.splitlines():
        piece = line.split()
        if len(piece) == 3 and piece[1] in "TtDB" and piece[2].startswith("kest_"):
            made.add(piece[2])
        if len(piece) == 2 and piece[0] == "U" and piece[1].startswith("kest_"):
            wanted.add(piece[1])
    return made, wanted


# `examples/embed.c` is compiled straight to a binary, so there is no object
# beside the others to read. One is made here and thrown away.
def host_objects():
    made = []
    for path in HOSTS:
        if os.path.exists(path):
            made.append(path)
    if not os.path.exists("examples/embed.o"):
        built = subprocess.run(
            ["cc", "-std=c11", "-Iinclude", "-c", "-o", "/tmp/kest-embed.o",
             "examples/embed.c"], capture_output=True, text=True)
        if built.returncode == 0:
            made.append("/tmp/kest-embed.o")
    return made


declared = {}
for header in sorted(os.listdir("src")) + ["../include/kest.h"]:
    if not header.endswith(".h"):
        continue
    path = os.path.join("src", header)
    text = open(path).read()
    # A name in a comment is a mention and not a declaration.
    text = re.sub(r"//[^\n]*", "", text)
    for name in re.findall(r"\b(kest_[a-z_0-9]+)\s*\(", text):
        declared.setdefault(name, os.path.normpath(path))

made = {}
wanted = {}
for name in sorted(os.listdir(OBJECTS)):
    if not name.endswith(".o"):
        continue
    path = os.path.join(OBJECTS, name)
    mine, theirs = symbols(path)
    for symbol in mine:
        made[symbol] = path
    wanted[path] = theirs
for path in host_objects():
    _, theirs = symbols(path)
    wanted[path] = wanted.get(path, set()) | theirs

for name, header in sorted(declared.items()):
    if name not in made:
        print("%s: `%s` is declared and is not there" % (header, name))
        failed = 1
        continue
    home = made[name]
    if not any(name in theirs for path, theirs in wanted.items() if path != home):
        print("%s: nothing outside %s calls `%s`"
              % (header, os.path.basename(home), name))
        failed = 1

if not failed:
    print("every declaration is there and is called: %u" % len(declared))
sys.exit(failed)
PY
