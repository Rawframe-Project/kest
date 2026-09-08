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
import json
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

# `check.sh` writes a host of its own, compiles it against the public header and
# throws it away, which is how the one thing neither host in this tree does is
# asked. What that host calls is not in any object here, so nothing above holds
# those names to being there — and a header function used only by it would read
# as used to `check.sh` and unused to this.
#
# So the rule is that it may only call what a host in the tree already calls.
# It is not coverage; it is the trap taken away: a name it leans on is a name
# something else here leans on too.
leaned_on = set(re.findall(r'\b(kest_[a-z_0-9]+)\s*\(',
                           open('tools/check.sh').read()))
reached = set()
for name in HOSTS:
    reached |= wanted.get(os.path.join(OBJECTS, name), set())
for name in sorted(leaned_on):
    if name not in reached:
        print("tools/check.sh: `%s` is called by the host it writes and by no "
              "host in the tree" % name)
        failed = 1

# The same rule for the library written in Kest, which no linker reads: a
# function nothing anywhere names is one nothing has ever run, and a library
# with a hole in it is worse than a library without the function.
#
# What counts as naming it is the checker's answer rather than a reader's. A
# name in a comment is a mention and not a use; a name handed around as a value
# — `sort.by(xs, sort.ascending)` — is a use and is not a call; and one of four
# functions called `min` is the one that was meant. `check --json` says `named`
# for each, which is what the checker settled while it resolved the file.
def said_of(path):
    ran = subprocess.run(['./kest', 'check', '--json', path],
                         capture_output=True, text=True,
                         stdin=subprocess.DEVNULL)
    if not ran.stdout.strip():
        print("%s: `check --json` said nothing about it" % path)
        return None
    return json.loads(ran.stdout)


def functions_of(path):
    said = said_of(path)
    return None if said is None else said.get('functions', [])


modules = {os.path.basename(path)[: -len('.kest')]: path
           for path in sorted(glob.glob('lib/std/*.kest'))}

# What the library declares, read out of the compiler rather than out of the
# text: a parameter list has commas inside it — `fn(T, T) -> bool` is one
# parameter — and a reader that splits on commas is a reader that miscounts.
declares = {}
anywhere = set()
for path in sorted(glob.glob('examples/*.kest') + glob.glob('tools/*.kest')
                   + list(modules.values())):
    said = functions_of(path)
    if said is None:
        failed = 1
        continue
    for one in said:
        key = (one['name'], tuple(one['parameters']))
        if one['named']:
            anywhere.add(key)
        module = one['name'].split('.')[0]
        if modules.get(module) == path:
            declares[key] = path

for key, path in sorted(declares.items()):
    if key not in anywhere:
        print("%s: nothing names `%s(%s)`, so nothing has run it"
              % (path, key[0], ', '.join(key[1])))
        failed = 1

# The same for what a library declares beside its functions: a constant nobody
# reads and a shape nobody holds. `check` says these about a program with a
# `main` in it and cannot say them about a library, because a library is named
# by whoever imports it — so the whole tree is the importer and this is where
# they are asked.
#
# A generic is written once and copied per set of types, and the copy is
# named under `Table<i32, i32>`; what is declared is `Table`, so the types are
# read by the name in front of the brackets.
def bare(name):
    return name.split('<')[0]


declared_names = {}
named_names = set()
for path in sorted(glob.glob('examples/*.kest') + glob.glob('tools/*.kest')
                   + list(modules.values())):
    said = said_of(path)
    if said is None:
        failed = 1
        continue
    for what in ('types', 'constants'):
        for one in said.get(what, []):
            name = bare(one['name'])
            if one['named']:
                named_names.add(name)
            if modules.get(name.split('.')[0]) == path:
                declared_names[name] = (path, what)

for name, (path, what) in sorted(declared_names.items()):
    if name not in named_names:
        print("%s: nothing names `%s`, so nothing has ever held one"
              % (path, name) if what == 'types' else
              "%s: nothing reads `%s`, so nothing has ever used it"
              % (path, name))
        failed = 1

if not failed:
    print("every declaration is there and is called: %u, and every library "
          "function, constant and shape is named where the checker can see "
          "it: %u" % (len(declared), len(declares) + len(declared_names)))
sys.exit(failed)
PY
