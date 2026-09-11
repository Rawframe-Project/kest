#!/bin/sh
# The library is written to gather bytes and pay for a piece of text once, and
# every one of those functions could be rewritten out of `slice` tomorrow, pass
# every other check here, and be quadratic. What tells from outside is what two
# sizes cost: twice the work for about twice the bytes is the gathering way,
# and four times is everything copied every time round.
#
# The list of what to ask comes from the library rather than from here, because
# a list written here is one that goes stale the day somebody adds a function.
# What cannot be asked from a command line — anything taking an array — has to
# be measured by the host that can, and `examples/embed.c` is the one that is.
#
# What this weighs is memory, because memory is what a run can be asked for
# without timing it, and this project times one thing in one place on purpose.
# So a rewrite that does more work without allocating more — a table that
# rehashed every time it was written to, say — is not something this catches,
# and nothing here pretends otherwise.
set -u
exec python3 - "$@" <<'PY'
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

LIBRARY = 'lib/std/text.kest'
HOST = 'examples/embed.c'
SMALL = 200
LARGE = 400
# Twice the work costs about twice as much when the bytes are gathered. Three
# is the line: under it is that, over it is everything copied every time.
LIMIT = 3

# What a command line can hand over: a piece of text, a whole number, a float.
# Everything else is a value that has to be built, which is a host's job.
#
# One of them grows and the rest stay where they are, because growing two at
# once is asking a different question: `repeat` of twice as much, twice as
# often, is four times the answer however it is written, and that is the
# function doing what it says rather than doing it badly.
def argument_for(written, size):
    if written == 'text':
        return 'x' * size if size is not None else 'ab'
    if re.fullmatch(r'[iu](8|16|32|64)', written):
        return str(size if size is not None else 3)
    if written in ('f32', 'f64'):
        return '1.5'
    return None


# Which of them a size means anything to. Each is grown on its own and the
# rest stay where they are, because growing two at once asks a different
# question: `repeat` of twice as much twice as often is four times the answer
# however it is written, and that is the function doing what it says.
def can_grow(takes):
    return [at for at, written in enumerate(takes)
            if written == 'text' or re.fullmatch(r'[iu](8|16|32|64)', written)]


source = open(LIBRARY).read()
host = open(HOST).read()
failed = 0


# A pattern that stops matching finds nothing, and a check that asked about
# nothing says the costs are fine because it never asked. Every sweep here goes
# through this: a declaration written differently, a bind that moved, a library
# nothing globs.
def some(what, found):
    global failed
    if not found:
        print("costs: nothing in the tree is where this reads %s from" % what)
        failed = 1
    return found


# Read through that door like the rest of them. It was the one read written
# out on its own, with a sentence of its own saying the same thing the door
# says, and a sentence per reading is a sentence nothing can be made to say.
making = some("the library's functions that make text",
              re.findall(r'\nfn ([a-zA-Z]+)\(([^)]*)\) -> text', source))
asked = 0
left_to_the_host = []


# What one call to one function cost. The library is where the standard one
# lives, so a driver written for a loop says where it is instead.
def cost_of(where, name, arguments):
    ran = subprocess.run(['./kest', 'call', '--json', where, name] + arguments,
                         capture_output=True, text=True,
                         stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib'))
    if ran.returncode != 0:
        said = ran.stdout.strip() or ran.stderr.strip()
        print("costs: `%s` could not be asked: %s" % (name, said[:120]))
        return None
    return json.loads(ran.stdout)['heap']


for name, params in making:
    pairs = [one.split(':') for one in params.split(',') if one.strip()]
    called = [one[0].strip() for one in pairs]
    takes = [one[1].strip() for one in pairs]
    growing = can_grow(takes)
    if not growing or any(argument_for(one, 1) is None for one in takes):
        left_to_the_host.append(name)
        continue
    for which in growing:
        sizes = {}
        for size in (SMALL, LARGE):
            arguments = [argument_for(one, size if at == which else None)
                         for at, one in enumerate(takes)]
            spent = cost_of(LIBRARY, name, arguments)
            if spent is None:
                failed = 1
                break
            sizes[size] = spent
        else:
            asked += 1
            # A function that allocates nothing at either size is the
            # gathering way by not gathering at all, and nought against nought
            # is not a ratio.
            if sizes[SMALL] > 0 and sizes[LARGE] > sizes[SMALL] * LIMIT:
                print("costs: `%s` over `%s` takes %u bytes for %u and %u for "
                      "%u, which is not the gathering way"
                      % (name, called[which], sizes[SMALL], SMALL,
                         sizes[LARGE], LARGE))
                failed = 1

# One that cannot be asked from here is not one that goes unasked: the host
# that lends arrays asks it, and this holds the two lists to each other.
for name in left_to_the_host:
    if ('"text.%s"' % name) not in host:
        print("costs: `%s` cannot be asked from here and `%s` does not ask it"
              % (name, HOST))
        failed = 1

# And the rest of the library, which is not asked function by function because
# what a container costs is what a loop of them costs rather than what one
# does. A module every one of whose functions promises `no.alloc` is not asked
# at all: it cannot reach the heap and the compiler has already proved it,
# which is a better answer than a measurement.
DRIVERS = {
    'table': """import std.table

fn work(n: i32) -> i32 {
    let t: table.Table<i32, i32> = table.empty()
    for i in 0..n {
        table.set(t, i, i * 2)
    }
    let found = 0
    for i in 0..n {
        found += table.get(t, i, 0)
    }
    return found - found
}
""",
}

# And the ones that hand back a run of pieces rather than one. A command line
# cannot print a `[text]`, so these are asked through a wrapper that answers
# with how many there are: what is being weighed is the heap the call took, and
# that is the same number whichever of the two the program says out loud. Each
# is a walk that makes a piece per character or per piece, so twice as much is
# twice the work and nothing here should be more.
RUNS = re.findall(r'\nfn ([a-zA-Z]+)\(([^)]*)\) -> \[text\]', source)
some("the library's functions that hand back a run of text", RUNS)

proved = 0
driven = 0
work = tempfile.mkdtemp()
try:
    for name, takes in RUNS:
        wants = [written.split(': ')[-1].strip()
                 for written in takes.split(',') if written.strip()]
        if wants != ['text'] and wants != ['text', 'text']:
            print("costs: `%s` takes %s, which this does not know how to ask "
                  "for" % (name, ', '.join(wants)))
            failed = 1
            continue
        driver = os.path.join(work, name + '.kest')
        open(driver, 'w').write(
            "import std.text\n"
            "\n"
            "fn work(n: i32) -> i32 {\n"
            "    let subject = text.repeat(\"ab\", n)\n"
            "    return len(text.%s(subject%s))\n"
            "}\n" % (name, ', \"a\"' if len(wants) == 2 else ''))
        sizes = {}
        for size in (SMALL, LARGE):
            spent = cost_of(driver, 'work', [str(size)])
            if spent is None:
                failed = 1
                break
            sizes[size] = spent
        else:
            asked += 1
            if sizes[SMALL] > 0 and sizes[LARGE] > sizes[SMALL] * LIMIT:
                print("costs: `text.%s` takes %u bytes for %u and %u for %u, "
                      "which is not twice for twice the work"
                      % (name, sizes[SMALL], SMALL, sizes[LARGE], LARGE))
                failed = 1

    for path in sorted(glob.glob('lib/std/*.kest')):
        module = os.path.basename(path)[:-len('.kest')]
        if path == LIBRARY:
            continue
        written = open(path).read()
        declared = re.findall(r'\nfn [^\n{]*', written)
        if declared and all('no.alloc' in one for one in declared):
            proved += 1
            continue
        if module not in DRIVERS:
            print("costs: nothing asks `std.%s` what it costs, and not every "
                  "function in it promises `no.alloc`" % module)
            failed = 1
            continue
        driver = os.path.join(work, module + '.kest')
        open(driver, 'w').write(DRIVERS[module])
        sizes = {}
        for size in (SMALL, LARGE):
            spent = cost_of(driver, 'work', [str(size)])
            if spent is None:
                failed = 1
                break
            sizes[size] = spent
        else:
            driven += 1
            if sizes[SMALL] > 0 and sizes[LARGE] > sizes[SMALL] * LIMIT:
                print("costs: `std.%s` takes %u bytes for %u and %u for %u, "
                      "which is not twice for twice the work"
                      % (module, sizes[SMALL], SMALL, sizes[LARGE], LARGE))
                failed = 1
finally:
    shutil.rmtree(work, ignore_errors=True)

some("the library's modules", proved + driven)

# A promise about a host is a claim about the heap like any other, and this is
# where the claims are read. `K0631` holds a host to one while something runs,
# which is the only thing that can hold a host nobody here compiles — but the
# two hosts in this tree are compiled here, and what they do directly can be
# read without running anything. A path nothing runs is then still held.
#
# What a host does by calling back into the program is not read here, because
# what that costs is the program's and the machine already holds it: a promise
# that calls a body which allocates is refused where the call is made.
MAKES = ('kest_text', 'kest_borrow')
HOSTS = ('src/main.c', 'examples/embed.c')

some("the askings of the text the library makes", asked)

promised = set()
for path in glob.glob('examples/*.kest') + glob.glob('lib/std/*.kest') \
        + glob.glob('tools/*.kest'):
    for name in re.findall(r'\nextern fn ([A-Za-z0-9.]+)\([^)]*\)[^\n]*no\.alloc',
                           open(path).read()):
        promised.add(name)
some("the promises a program makes about a host", promised)

kept = 0
provided = set()
for path in HOSTS:
    written = open(path).read()
    # Every one of them, and not most of them: what this reads with a pattern
    # has to be everything the file has, because a bind written across two
    # lines is a promise nothing here would read and nothing would miss.
    binds = re.findall(r'kest_host_bind\(host, "([A-Za-z0-9.]+)", (\w+)', written)
    if len(binds) != written.count('kest_host_bind(host'):
        print("costs: `%s` binds %u and this reads %u of them"
              % (path, written.count('kest_host_bind(host'), len(binds)))
        failed = 1
    bound = some("what `%s` binds" % path, dict(binds))
    for name, function in bound.items():
        if name not in promised:
            continue
        provided.add(name)
        body = re.search(r'\nstatic \w+ %s\([^)]*\) \{(.*?)\n\}'
                         % re.escape(function), written, re.S)
        if body is None:
            print("costs: `%s` binds `%s` and this cannot read what it does"
                  % (path, name))
            failed = 1
            continue
        took = [one for one in MAKES if one + '(' in body.group(1)]
        if took:
            print("costs: `%s` promises `no.alloc` and `%s` in `%s` calls `%s`"
                  % (name, function, path, took[0]))
            failed = 1
        else:
            kept += 1

# And what the compiler's own work cost, which is the one cost this project
# asks of every program it reads and had never said about itself. A run says it
# now, and what holds the number to being the work rather than a number printed
# beside it is that the two commands do different amounts of it: `emit` checks
# the program and then compiles it, on the same arena, so it costs more than
# `check` on the same file. A number that is nought, or the same for both, is a
# number wired to something that is not the work.
def what_it_cost(command, where):
    ran = subprocess.run(['./kest', command, '--json', where],
                         capture_output=True, text=True,
                         stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib'))
    if ran.returncode != 0:
        return None
    return json.loads(ran.stdout).get('cost')


# And what a program pays for what it imports, which is what a host reloading
# one file of its own re-reads every time: a build is its own arena and carries
# nothing from the last one (D573), so importing the library is reading and
# compiling the library again. Three programs of a few lines each, one alone,
# one that prints and one that makes text. See D638.
def what_a_program_costs(body):
    where = os.path.join(work, 'reading.kest')
    with open(where, 'w') as out:
        out.write(body)
    return what_it_cost('emit', where)


os.mkdir(work)
alone_costs = what_a_program_costs("""module reading

fn main() -> i32 {
    return 0
}
""")
printing_costs = what_a_program_costs("""module reading

import std.io

fn main() -> i32 {
    io.print("hello")
    return 0
}
""")
making_text_costs = what_a_program_costs("""module reading

import std.io
import std.text

fn main() -> i32 {
    io.print(text.repeat("a", 2))
    return 0
}
""")
# And the same program using five of that module rather than one of it. What a
# program pays for is the module, because a module is checked and compiled
# whole: what it uses of it is the difference between these two. See D639.
using_five_costs = what_a_program_costs("""module reading

import std.io
import std.text

fn main() -> i32 {
    let parts = text.split("a,b", ",")
    io.print(text.join(parts, "-"))
    io.print(text.upper(text.trim(" x ")))
    io.print(text.repeat("a", 2))
    return len(parts) - 2
}
""")
shutil.rmtree(work, ignore_errors=True)
if (alone_costs is None or printing_costs is None or
        making_text_costs is None or using_five_costs is None or
        printing_costs <= alone_costs or
        making_text_costs <= printing_costs * 4 or
        using_five_costs > making_text_costs + making_text_costs // 8):
    print("costs: a program alone cost %s, one that prints %s, one that makes "
          "text %s and one that uses five of that module %s, and what a "
          "program imports is most of what building it costs"
          % (alone_costs, printing_costs, making_text_costs,
             using_five_costs))
    failed = 1

# The four stages of reading one file, which stop where they stop: `lex` at the
# tokens, `parse` at the tree, `check` at the types and `emit` at the code. Each
# does what the one before it did and then more, so the four numbers grow — and
# what they say is where the work is. See D640.
def what_it_said(command, where, name):
    ran = subprocess.run(['./kest', command, '--json', where],
                         capture_output=True, text=True,
                         stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib'))
    if ran.returncode != 0:
        return None
    return json.loads(ran.stdout).get(name)


lexing = what_it_cost('lex', LIBRARY)
parsing = what_it_cost('parse', LIBRARY)
# And what the tree is made of, against what it cost. A node of this compiler is
# fifty-six bytes for an expression, sixty-four for a statement and eighty-eight
# for a declaration, so a tree is at least its nodes and not much more: what is
# beside them is the lists a block and an argument list are. The floor here is
# what the smallest of those is, because a file of nothing but expressions is
# the cheapest tree there is. See D641 and D642.
nodes = what_it_said('parse', LIBRARY, 'nodes')
if (nodes is None or lexing is None or parsing is None or nodes == 0 or
        parsing - lexing < nodes * 56 or parsing - lexing > nodes * 256):
    print("costs: a tree of %s nodes cost %s bytes over the tokens it was made "
          "from, and a node of this compiler is fifty-six bytes"
          % (nodes, None if parsing is None or lexing is None
             else parsing - lexing))
    failed = 1
checking = what_it_cost('check', LIBRARY)
compiling = what_it_cost('emit', LIBRARY)
if (lexing is None or parsing is None or checking is None or
        compiling is None or parsing <= lexing or checking <= parsing or
        compiling <= checking):
    print("costs: `lex` said %s, `parse` said %s, `check` said %s and `emit` "
          "said %s, and each of them does what the one before it did and then "
          "more" % (lexing, parsing, checking, compiling))
    failed = 1

# A promise nobody here provides is one nothing here can read: no host in this
# tree binds it, so there is no body to look at and no run to hold it. Saying
# how many rather than passing over them is the difference between a check that
# covers something and one that looks as if it does.
alone = sorted(promised - provided)

if not failed:
    print("what the library costs grows the way it should: %u askings of the "
          "text it makes, %u left to the host, %u modules in a loop, %u proved "
          "by `no.alloc`, %u promises about a host kept where they are "
          "written and %u nothing here provides, and what the compiler's own "
          "work costs is %u bytes to read that library as tokens, %u as a "
          "tree of %u nodes, %u to check it and %u to compile it, "
          "against %u bytes for a program of four lines, %u for one that "
          "prints, %u for one that makes text and %u for one that uses five "
          "of that module rather than one"
          % (asked, len(left_to_the_host), driven, proved, kept, len(alone),
             lexing, parsing, nodes, checking, compiling, alone_costs,
             printing_costs, making_text_costs, using_five_costs))
sys.exit(failed)
PY
