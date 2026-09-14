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
    defines = set()
    own = set()
    asks_for = set()
    for line in out.splitlines():
        piece = line.split()
        if len(piece) == 3 and piece[1] in "TtDB" and piece[2].startswith("kest_"):
            defines.add(piece[2])
        # A capital is a name the whole program can see and a small letter is
        # one only this object can. What a header declares has to be the first
        # kind, and a name only one object can see wearing the prefix of the
        # first kind is an internal function written as a public one.
        if len(piece) == 3 and piece[1] in "tdb" and piece[2].startswith("kest_"):
            own.add(piece[2])
        if len(piece) == 2 and piece[0] == "U" and piece[1].startswith("kest_"):
            asks_for.add(piece[1])
    return defines, own, asks_for


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
inside = {}
wanted = {}
for name in sorted(os.listdir(OBJECTS)):
    if not name.endswith(".o"):
        continue
    path = os.path.join(OBJECTS, name)
    mine, theirs, asked = symbols(path)
    for symbol in mine:
        made[symbol] = path
    for symbol in theirs:
        inside[symbol] = path
    wanted[path] = asked
# A pattern that reads a header finds what it finds, and a header it read
# nothing out of is a header nothing here is holding to anything. Every list
# read out of the source goes through this.
def some(what, found):
    global failed
    if not found:
        print("%s: nothing in the tree is where this reads it from" % what)
        failed = 1
    return found


some("the names the headers declare", declared)

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

# The public header is held to something more than the rest: every function in
# it is called by a host, because the header says there is somewhere to look
# for each of them and a claim nothing holds is a claim that goes stale. What
# the library's own modules call each other is not that.
command_line = wanted.get(os.path.join(OBJECTS, "main.o"), set())
engine = wanted.get(os.path.join(OBJECTS, "embed.o"), set())
# Read out of the public header itself rather than out of where a name was
# first seen: `kest_runtime_free` is declared in both, and the file a name is
# attributed to is whichever was read first.
public = some("the names the public header declares", set(re.findall(
    r'\b(kest_[a-z_0-9]+)\s*\(',
    re.sub(r'//[^\n]*', '',
           open(os.path.join('include', 'kest.h')).read()))))

# And the other way round, which is the half no pattern can be wrong about: a
# function this library makes and no header declares. Nothing can call it, so
# nothing above holds it to anything — it reads as a name that is simply not
# there, which is what a declaration written in a way this cannot read looks
# like from here too. The objects say what was made; `nm` is not a pattern.
for symbol, where in sorted(made.items()):
    # A compiler that splits a function into pieces names them after it with a
    # dot, and those are the same function under another name.
    if "." in symbol or symbol in inside:
        continue
    if symbol not in declared:
        print("%s: makes `%s` and no header declares it" % (where, symbol))
        failed = 1

# And a name the prefix says is public on a function only one object can see.
# `CLAUDE.md` says an internal function is plain snake_case, and the reason is
# this: a reader looking for where `kest_something` is declared finds nothing
# and cannot tell a private name from a declaration that went missing.
for symbol, where in sorted(inside.items()):
    if "." in symbol:
        continue
    print("%s: `%s` is this file's own and is named as a public one"
          % (where, symbol))
    failed = 1
for name in sorted(public - command_line - engine):
    print("include/kest.h: `%s` is declared and no host in this tree calls it"
          % name)
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

# And the same rule one level down, for what the machine can do rather than
# what a library declares. An instruction the compiler never writes is a `case`
# in the machine that has never been dispatched to: it is in the reference, it
# reads as tried, and nothing has ever run it. Twenty one of the hundred and
# forty six were in that state until D430 went looking.
#
# `emit` is the compiler's own answer to what it wrote, so this is the chunk
# and not a pattern over the source. Only the examples are asked, because the
# gate runs every one of them — an instruction written into a file nothing runs
# would be a shorter claim than this one wants to make.
table = re.search(r'INSTRUCTIONS\[\] = \{(.*?)\n\};',
                  open(os.path.join('src', 'value.c')).read(), re.S)
instructions = some("the machine's instructions", [] if table is None else [
    m[0] for m in re.findall(r'\{"((?:[^"\\]|\\.)*)",\s*(\w+)\}',
                             table.group(1))])
emitted = set()
held = set()
for path in sorted(glob.glob('examples/*.kest')):
    ran = subprocess.run(['./kest', 'emit', path], capture_output=True,
                         text=True, stdin=subprocess.DEVNULL)
    if ran.returncode != 0:
        print("%s: `emit` would not write it out" % path)
        failed = 1
        continue
    # An instruction is an offset and two spaces after it. What a chunk costs
    # is printed above the code as a number and one space, so `34 and 2 for
    # `main`` reads as an instruction called `and` to anything looser.
    for line in ran.stdout.splitlines():
        found = re.match(r'\s+\d{4,}  (\S+)', line)
        if found is not None:
            emitted.add(found.group(1))
        # And what a shape comes to in memory, which is printed above the code
        # for the same reason: it is in the chunk.
        if line.startswith('layout '):
            held |= set(re.findall(r'\+\d+ (\S+)', line))

for name in instructions:
    if name not in emitted:
        print("src/value.h: nothing emits `%s`, so no example has run it"
              % name)
        failed = 1
# The other way round, which is this check reading its own parse: a word it
# took for an instruction that is not one of the names means the pattern above
# is catching something else, and a pattern that catches everything would say
# every instruction is emitted.
for name in sorted(emitted - set(instructions)):
    print("tools/check-dead.sh: read `%s` as an instruction and it is not one"
          % name)
    failed = 1

# And the same rule again for what a layout says. A layout is what a host is
# told about a shape — a byte offset a slot and what is there — and the kinds
# it can say are the widths this language has. A kind no shape in the tree
# holds is a width the machine can lay out and has never laid out beside a C
# compiler doing the same arithmetic, which is the one place it could be wrong
# and nothing would say so. Three of the twelve were in that state (D431).
scalars = re.search(r'SCALARS\[\] = \{(.*?)\};',
                    open(os.path.join('src', 'value.c')).read(), re.S)
kinds = some("the kinds a layout holds",
             [] if scalars is None else re.findall(r'"([^"]+)"',
                                                   scalars.group(1)))
for name in kinds:
    if name not in held:
        print("src/value.c: no shape in an example is laid out holding a `%s`"
              % name)
        failed = 1
for name in sorted(held - set(kinds)):
    print("tools/check-dead.sh: read `%s` as a kind a layout holds and it is "
          "not one" % name)
    failed = 1

# And that every layout says which type it is the layout of. Two that differ
# only in that read as one without it -- every `[T]` is one word whatever `T`
# is, and `u8` and `bool` are both a byte -- so a reader counting what a module
# wrote twice counts what it wrote once, which is a measurement that was wrong
# by sixty per cent until this was printed. The machine reads the same field to
# pack a value across the host boundary. See D779.
of_told = 0
of_untold = 0
for of_path in sorted(glob.glob(os.path.join('examples', '*.kest'))):
    of_ran = subprocess.run(['./kest', 'emit', of_path, '--json'],
                            capture_output=True, text=True,
                            stdin=subprocess.DEVNULL)
    if of_ran.returncode != 0:
        continue
    for of_one in json.loads(of_ran.stdout).get('layouts', []):
        if of_one.get('of'):
            of_told += 1
        else:
            of_untold += 1
if of_told == 0 or of_untold != 0:
    print("src/value.c: %u of %u layouts say which type they are the layout "
          "of, and one that does not is a shape a reader cannot tell from "
          "another shape" % (of_told, of_told + of_untold))
    failed = 1

if not failed:
    # Which of the two hosts calls what, because the header says there is
    # somewhere to look for each of its functions and this is where that is
    # counted. The internal headers are held to being called from outside the
    # file that has them; the public one is held to a host.
    print("every declaration is there and is called: %u, of which the public "
          "header's %u are called by the command line (%u) and the engine "
          "(%u), and every library function, constant and shape is named "
          "where the checker can see it: %u, and every one of the machine's "
          "%u instructions is written by an example, holding every one of "
          "the %u kinds a layout can hold"
          % (len(declared), len(public),
             len(public & command_line), len(public & engine),
             len(declares) + len(declared_names), len(instructions),
             len(kinds)))
sys.exit(failed)
PY
