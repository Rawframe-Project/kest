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

import shutil
import tempfile

OBJECTS = "build/release"
# And the build that checks itself, which is the same sources with one more
# thing turned on. A name the machine calls only inside `#if KEST_CHECKED` is
# in no release object and is still called: reading one build and calling it
# the tree was what said `kest_op_name` was declared and not there. Both are
# read for what they make and what they ask for; which host calls what is still
# read out of the release ones, because there is one of each there. See D870.
CHECKED = "build/debug"
# The hosts. A tool that quietly skips one is a tool that says the public
# header is used when nothing has looked. Three of them: the command line, the
# one that asks every door, and the engine that drives a world. See D949.
HOSTS = ["main.o", "embed.o", "engine.o"]

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
        print("%s is not built; `make embed engine` first" % host)
        sys.exit(1)

declared = {}
# A body written in a header rather than declared by one. `static inline` is
# folded into whoever calls it, and where a build does not fold it, it is that
# object's own by design — so it is a symbol in no object and the rules below
# read it out of the files that call it instead. See D868.
in_headers = {}
for header in sorted(os.listdir("src")) + ["../include/kest.h"]:
    if not header.endswith(".h"):
        continue
    path = os.path.join("src", header)
    text = open(path).read()
    # A name in a comment is a mention and not a declaration.
    text = re.sub(r"//[^\n]*", "", text)
    for name in re.findall(r"\b(kest_[a-z_0-9]+)\s*\(", text):
        declared.setdefault(name, os.path.normpath(path))
    for name in re.findall(r"static inline [^;{}]*?\b(kest_[a-z_0-9]+)\s*\(",
                           text):
        in_headers[name] = os.path.normpath(path)

# What the files say, for the one question objects cannot answer. Comments are
# taken out for the same reason they are taken out of a header.
calling = {path: re.sub(r"//[^\n]*", "", open(path).read())
           for path in sorted(glob.glob(os.path.join("src", "*.c")))}

made = {}
inside = {}
wanted = {}
built = [os.path.join(OBJECTS, name) for name in sorted(os.listdir(OBJECTS))]
checked = ([os.path.join(CHECKED, name) for name in sorted(os.listdir(CHECKED))]
           if os.path.isdir(CHECKED) else [])
built += checked
for path in built:
    if not path.endswith(".o"):
        continue
    mine, theirs, asked = symbols(path)
    # What a build makes is asked of the release one alone. The one that checks
    # itself is built without optimisation, so every `static` function is still
    # a symbol in it — which is what the compiler was told to do rather than a
    # name written as a public one. What is read out of both is what each asks
    # for, because that is the half a checked-only call is missing from. See
    # D870.
    if path.startswith(OBJECTS):
        for symbol in mine:
            made[symbol] = path
        for symbol in theirs:
            inside[symbol] = path
    wanted[path] = asked

# And the third thing that calls this library, which is this project's other
# backend: a file it writes calls two doors of the machine and nothing else
# in the tree does. So one is written and compiled here, and what it asks for
# is read the way a host's object is -- the day the backend stops writing a
# call to one of those doors, that door is dead and this is what says so. The
# program is written here rather than taken from the tree because it has to
# hold what provokes both of them: a body written whole, and something in it
# that can stop. See D1094.
WRITTEN = """module dividing

import std.io

fn split(a: i32, b: i32) -> i32 no.alloc no.host deterministic {
    return a / b
}

struct Tag {
    name: text
    rank: i32
}

fn weigh(one: Tag) -> i64 no.alloc no.host deterministic {
    return i64(hash(one) % 97) + i64(hash(one.name) % 89)
}

fn same(a: Tag, b: Tag) -> bool no.alloc no.host deterministic {
    return a == b
}

fn first(a: Tag, b: Tag) -> text no.alloc no.host deterministic {
    if a.name < b.name {
        return a.name
    }
    return b.name
}

// Every read into a piece of text that does not make one, in one body: the
// byte at a place, a cut, the rest, a match, a search and a walk.
fn reading(line: text) -> i32 no.alloc no.host deterministic {
    let n = i32(line[0]) + len(slice(line, 1, 2)) + len(rest(line, 1))
    if matches(line, 0, "co") {
        n += 1
    }
    if let at = find(line, "un", 0) {
        n += at
    }
    for b in line {
        n += i32(b) % 3
    }
    return n
}

// And every way of making one, beside the four things a run of elements does
// that nothing above reaches: room made for what is coming, one more on the
// end where the room already is, everything taken out, and the last one off.
fn writing(one: Tag, n: i32, f: f64, yes: bool) -> i32 {
    let said = "{one} {n} {f} {yes}"
    let out: [u8] = array()
    room(out, 32)
    let fitted = if fit(out, said) -> 1 else -> 0
    fitted += if fit(out, 46) -> 1 else -> 0
    push(out, "-tail")
    let whole = text(out)
    clear(out)
    let counted = [1, 2, 3]
    let sum = 0
    while true {
        if let last = pop(counted) {
            sum += last
        } else {
            break
        }
    }
    return len(whole) + len(said) + fitted + sum + len(out) + len(counted)
}

fn walk(counts: [i32]) -> i32 no.alloc no.host deterministic {
    let sum = 0
    for at in 0..len(counts) {
        sum += split(counts[at] * 2, 2)
        counts[at] = sum
    }
    return sum
}

struct Held {
    what: i32
}

// One of a fixed run in the frame, at an index worked out while it runs,
// which is the last of the doors nothing else in the tree reaches. See D1109.
struct Board {
    cells: [i32; 4]
    turn: i32
}

fn played(b: Board, at: i32) -> i32 no.alloc no.host deterministic {
    let one = b
    one.cells[at] = one.cells[at] + 1
    return one.cells[at] + one.turn
}

fn worlds(many: i32) -> i32 {
    let all: store<Held> = store(many)
    let made: [ref<Held>] = array()
    for i in 0..many {
        push(made, add(all, Held(i)))
    }
    let sum = 0
    for r in all {
        if let one = get(all, r) {
            sum += one.what
            let changed = one
            changed.what += 1
            set(all, r, changed)
        }
    }
    if len(made) > 0 {
        remove(all, made[0])
    }
    return sum + len(all)
}

// A call through a function value, which this backend writes by asking the
// machine which function it is. And, below, a crossing into the host, which
// it does not write: what a file it wrote does instead is hand that body to
// the machine, which is the other door nothing else in the tree reaches.
// See D1105 and D1107.
fn through(f: fn(i32) -> i32 no.alloc no.host deterministic,
           n: i32) -> i32 no.alloc no.host deterministic {
    return f(n) + 1
}

fn twice(n: i32) -> i32 no.alloc no.host deterministic {
    return n * 2
}

fn said(what: i32) {
    io.print("counted {what}")
}

// Working memory, which this backend has no C for: a body that opens one is
// a body the machine runs, and a file this backend wrote calls it through a
// door of its own. That door is the one thing here nothing else in the tree
// reaches. See D1105.
fn scratched(rounds: i32) -> i32 no.host deterministic {
    let sum = 0
    for i in 0..rounds {
        scratch {
            let made: [i32] = array(4, i)
            sum += len(made)
        }
    }
    return sum
}

fn main() -> i32 {
    let counts: [i32] = array()
    for i in 0..4 {
        push(counts, split(i * 6, 2))
    }
    let spare = remove(counts, 0)
    push(counts, spare)
    let one = Tag("counting", 3)
    said(walk(counts) + scratched(3) + played(Board([1, 2, 3, 4], 5), 2))
    return (walk(counts) + i32(weigh(one) % 7) + worlds(4) +
            len(first(one, Tag("counted", 4))) + reading("counting") +
            through(twice, 3) +
            writing(one, -7, 0.5, true) +
            (if same(one, Tag("counting", 4)) -> 1 else -> 0)) % 251
}
"""
room = tempfile.mkdtemp()
try:
    program = os.path.join(room, "dividing.kest")
    open(program, "w").write(WRITTEN)
    wrote = os.path.join(room, "wrote.c")
    writing = subprocess.run(["./kest", "emit", "--c", program],
                             capture_output=True, text=True)
    open(wrote, "w").write(writing.stdout)
    object_of_it = os.path.join(room, "wrote.o")
    built_it = subprocess.run([os.environ.get("CC", "cc"), "-O0", "-Iinclude",
                               "-c", "-o", object_of_it, wrote],
                              capture_output=True, text=True)
    if writing.returncode != 0 or built_it.returncode != 0:
        print("what the other backend writes will not compile, so nothing "
              "here says which of the machine's doors it calls")
        failed = 1
    else:
        _, _, asked = symbols(object_of_it)
        wanted[object_of_it] = asked
finally:
    shutil.rmtree(room, ignore_errors=True)

# A pattern that reads a header finds what it finds, and a header it read
# nothing out of is a header nothing here is holding to anything. Every list
# read out of the source goes through this.
def some(what, found):
    global failed
    if not found:
        print("%s: nothing in the tree is where this reads it from" % what)
        failed = 1
    return found


some("the objects of the build that checks itself", checked)
some("the names the headers declare", declared)
some("the files that could call a body written in a header", calling)

for name, header in sorted(declared.items()):
    if name in in_headers:
        # One file calling it is a body written where it does not belong: it
        # belongs in that file, where nothing has to be said about it at all.
        callers = sorted(path for path, text in calling.items()
                         if re.search(r"\b%s\s*\(" % name, text))
        if not callers:
            print("%s: `%s` is declared and is not there" % (header, name))
            failed = 1
        elif len(callers) < 2:
            print("%s: nothing outside %s calls `%s`"
                  % (header, os.path.basename(callers[0]), name))
            failed = 1
        continue
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
engine = (wanted.get(os.path.join(OBJECTS, "embed.o"), set()) |
          wanted.get(os.path.join(OBJECTS, "engine.o"), set()))
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
    if "." in symbol or symbol in in_headers:
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


library = sorted(glob.glob('lib/std/*.kest'))

# What the library declares, read out of the compiler rather than out of the
# text: a parameter list has commas inside it — `fn(T, T) -> bool` is one
# parameter — and a reader that splits on commas is a reader that miscounts.
#
# A declaration is this file's own where it stands under the module the file
# says it is in. A module is the whole of what a file calls itself, so the
# first piece of a name is `std` for everything in the library and the name
# alone can no longer say which file wrote it. See D1039.
declares = {}
anywhere = set()
for path in sorted(glob.glob('examples/*.kest') + glob.glob('tools/*.kest')
                   + library):
    said = said_of(path)
    if said is None:
        failed = 1
        continue
    for one in said.get('functions', []):
        key = (one['name'], tuple(one['parameters']))
        if one['named']:
            anywhere.add(key)
        if path in library and one['module'] == said['module']:
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
                   + library):
    said = said_of(path)
    if said is None:
        failed = 1
        continue
    for what in ('types', 'constants'):
        for one in said.get(what, []):
            name = bare(one['name'])
            if one['named']:
                named_names.add(name)
            if path in library and one['module'] == said['module']:
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
#
# And run, which is the other half of the same sentence and was a claim rather
# than a check: an instruction written into a chunk and jumped over is a `case`
# nothing has ever dispatched to, the same as one nothing writes. The build
# that checks itself counts what it ran, so this is the machine's own answer
# rather than a reading of the code. What it is worth beyond the claim being
# true is D889: every instruction the machine has costs every program that
# runs, so one nobody has ever run is a cost with nothing on the other side.
# See D890.
emitted = set()
walked = set()
held = set()
counting = os.path.exists('./kest-debug')
for path in sorted(glob.glob('examples/*.kest')):
    if counting:
        went = subprocess.run(['./kest-debug', 'run', path],
                              capture_output=True, text=True,
                              stdin=subprocess.DEVNULL,
                              env=dict(os.environ, KEST_DEEP='1'))
        for line in went.stderr.splitlines():
            if line.startswith('ran '):
                walked.add(line.split()[1])
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

# `stop` is the one instruction nothing compiles to: a debugger writes it over
# an instruction it wants to stop at and puts the byte back afterwards, which is
# what makes a breakpoint cost a machine nothing. It is exercised by the
# debugger rather than by an example, and `check-commands.sh` drives a program
# through it. Written down here rather than left out quietly, because an
# instruction nothing runs and an instruction nothing runs *and nobody
# noticed* look the same from outside. See D991.
WRITTEN_BY_A_DEBUGGER = {"stop"}

for name in instructions:
    if name in WRITTEN_BY_A_DEBUGGER:
        continue
    if name not in emitted:
        print("src/value.h: nothing emits `%s`, so no example has run it"
              % name)
        failed = 1
    elif counting and name not in walked:
        print("src/value.h: an example writes `%s` and no run of one reaches "
              "it, so nothing has seen it work" % name)
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

# And how wide a node is. A tree is half of what compiling a program costs, so
# a union member written out where it could be pointed at is paid for by every
# node of every program: `match` is one expression in three thousand and `for`
# one statement in twenty-five, and between them they were making an expression
# fifty-six bytes and a statement sixty-four. Both are forty-eight now.
#
# Written down rather than worked out, so that a member added wider than the
# widest here has to say so. The numbers are meant to go stale the day one
# legitimately does. See D782.
WIDEST_NODE = {"expression": 48, "statement": 48, "declaration": 88}

node_said = subprocess.run(['./kest', 'parse', 'examples/words.kest', '--json'],
                           capture_output=True, text=True,
                           stdin=subprocess.DEVNULL)
node_bytes = ({} if node_said.returncode != 0
              else json.loads(node_said.stdout).get('nodeBytes', {}))
if True:
    for node_what, node_most in sorted(WIDEST_NODE.items()):
        node_is = node_bytes.get(node_what)
        if node_is != node_most:
            print("src/ast.h: a %s is %s bytes and this says %u, and a node "
                  "widened is every node of every program widened"
                  % (node_what, node_is, node_most))
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
    # Which of the hosts calls what, because the header says there is
    # somewhere to look for each of its functions and this is where that is
    # counted. The two that are not the command line are counted together:
    # both of them are an engine, and what a reader is being told is that a
    # function has a host to look at rather than which file it is in. The internal headers are held to being called from outside the
    # file that has them; the public one is held to a host.
    print("every declaration is there and is called: %u, of which the public "
          "header's %u are called by the command line (%u) and the engine "
          "(%u), and every library function, constant and shape is named "
          "where the checker can see it: %u, and every one of the machine's "
          "%u instructions is written by an example and run by one, holding "
          "every one of "
          "the %u kinds a layout can hold"
          % (len(declared), len(public),
             len(public & command_line), len(public & engine),
             len(declares) + len(declared_names), len(instructions),
             len(kinds)))
sys.exit(failed)
PY
