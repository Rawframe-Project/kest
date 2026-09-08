#!/bin/sh
# Every function a header declares has to be there, and something other than
# the file it lives in has to call it. A declaration for a function nobody
# wrote is a promise the linker keeps quiet about until somebody takes it up,
# and a function nothing calls is read as a thing that is used.
#
# The public header is held to the same rule by the two hosts in this tree:
# what a host cannot be shown using is what nobody has run. Their objects sit
# beside the library's, which is why `examples/embed.c` is compiled to one.
#
# The symbols are read out of the objects rather than the text, because a name
# in a comment is not a call and a name in a string is not a definition.
set -u
exec python3 - "$@" <<'PY'
import glob
import os
import re
import subprocess
import sys

OBJECTS = "build/release"
# The two hosts. A tool that quietly skips one is a tool that says the public
# header is used when nothing has looked.
HOSTS = ["main.o", "embed.o"]

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


for host in HOSTS:
    if not os.path.exists(os.path.join(OBJECTS, host)):
        print("%s is not built; `make embed` first" % host)
        sys.exit(1)

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

# The same rule for the library written in Kest, which no linker reads: a
# function nothing anywhere names is one nothing has ever run, and a library
# with a hole in it is worse than a library without the function. What counts
# is a mention rather than a call, because `sort.by(xs, sort.ascending)` uses
# `ascending` without calling it, and inside its own module a name stands on
# its own.
library = {}
for path in sorted(glob.glob('lib/std/*.kest')):
    module = os.path.basename(path)[: -len('.kest')]
    for name in re.findall(r'\nfn ([a-zA-Z][a-zA-Z0-9]*)', open(path).read()):
        library[(module, name)] = path

written = [(path, open(path).read())
           for path in sorted(glob.glob('examples/*.kest')
                              + glob.glob('lib/std/*.kest')
                              + glob.glob('tools/*.kest'))]
for (module, name), path in sorted(library.items()):
    named = 0
    for where, text in written:
        if where == path:
            # Its own declaration is not a use of it, and everything else in
            # the file that says the name is.
            text = re.sub(r'\nfn %s\b' % name, '\n', text)
            named += len(re.findall(r'(?<![.\w])%s(?![\w])' % name, text))
        named += len(re.findall(r'%s\.%s(?![\w])' % (module, name), text))
    if named == 0:
        print("%s: nothing names `%s.%s`, so nothing has run it"
              % (path, module, name))
        failed = 1

if not failed:
    print("every declaration is there and is called: %u, and every library "
          "function is named: %u" % (len(declared), len(library)))
sys.exit(failed)
PY
