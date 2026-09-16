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
        found += table.orElse(t, i, 0)
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
# And the other half of the count the machine holds. `K0655` refuses a body
# that goes deeper than it was given; nothing says a body never went that deep
# at all. Room asked for and never used is memory a host is told to find for
# nothing, and `needs_of` carries it up every chain of calls — a body four
# slots wider than it needs makes every caller of it four wider too.
#
# The build that checks itself counts what each body reached and says so when
# `KEST_DEEP` is set, which is the only time it says anything the release build
# does not. Every body every example runs reaches the room it was given. Where
# that stops being true it is usually not the compiler: it is an arm, or a
# branch, or a copy of a generic that nothing here runs, and the answer is a
# program that runs it. See D812.
asked_for = 0
reached = 0
loose = []
deep_runs = []
# What a machine takes when the program has no answer, which `include/kest.h`
# names and this reads rather than writing a second copy of.
KEST_STACK_SLOTS = int(re.search(r"#define KEST_STACK_SLOTS (\d+)",
                                 open(os.path.join("include",
                                                   "kest.h")).read()).group(1))
# The build that checks itself is what counts this, and the gate makes sure it
# is there before anything runs. A tree with only the release build in it --
# which is what a hole that does not ask for the other one leaves -- has
# nothing to read here, and reading nothing is not the same as reading nought.
have_checked = os.path.exists('./kest-debug')
for deep_path in (sorted(glob.glob(os.path.join('examples', '*.kest')))
                  if have_checked else []):
    deep_env = dict(os.environ)
    deep_env['KEST_DEEP'] = '1'
    deep_ran = subprocess.run(['./kest-debug', 'run', deep_path],
                              capture_output=True, text=True,
                              stdin=subprocess.DEVNULL, env=deep_env)
    for deep_line in deep_ran.stderr.split("\n"):
        if deep_line.startswith('run '):
            deep_runs.append((deep_path, deep_line))
            continue
        if not deep_line.startswith('deep '):
            continue
        deep_words = deep_line.split(' ')
        deep_room = int(deep_words[-3])
        deep_went = int(deep_words[-1])
        deep_name = ' '.join(deep_words[1:-4])
        asked_for += 1
        reached += deep_room
        if deep_room > deep_went:
            loose.append((deep_room - deep_went, deep_name, deep_path))
for deep_slack, deep_name, deep_path in sorted(loose, reverse=True)[:4]:
    print("costs: `%s` asks for %u slot(s) it never used, running %s"
          % (deep_name, deep_slack, deep_path))
    failed = 1
if have_checked:
    some("the bodies the examples run", asked_for)

# And the same question of the whole run rather than of one body. What a
# program is given is what `kest_needs` said it would want, worked out by
# adding each body's width to the worst of what it reaches; what it used is
# what the machine reached. Going over is the one that cannot stand -- a
# machine the program outgrows is a refusal in the middle of a frame -- and
# the room left over is worth saying, because it is what a host is asked to
# find and never uses. A program with no answer takes the usual numbers and is
# counted apart: what it wants cannot be worked out, so what it did not use is
# not slack. See D813.
run_asked = 0
run_went = 0
run_frames = 0
run_went_frames = 0
run_sized = 0
for run_path, run_line in deep_runs:
    run_words = run_line.split(' ')
    # A program with no least is not sized by what it needs but bounded by
    # what a frame of it costs, so the room it did not use is a bound being
    # loose rather than a number being wrong. Counted apart. See D815.
    if run_words[1] != 'least':
        continue
    room, frames, went, went_frames = (int(run_words[3]), int(run_words[5]),
                                       int(run_words[8]), int(run_words[9]))
    run_sized += 1
    run_asked += room
    run_went += went
    run_frames += frames
    run_went_frames += went_frames
if have_checked:
    some("the examples a machine could size from the program", run_sized)
# A machine the program outgrows is not what this watches: the machine asks
# whether there is room at every call and refuses rather than running off the
# end, so under-asking is a program that stops and over-asking is a program
# that runs. What is left to watch is the over-asking, and the shape of it is a
# ratio rather than a number, the same as everything else here: a fifth over
# what every example between them reaches is room enough for the paths they do
# not take. Adding a call's arguments to its caller without taking them off
# again -- they are one set of slots, and the machine puts the callee's frame
# on top of them -- was a quarter over on its own. See D813.
if have_checked and (run_asked > run_went * 5 // 4 or
                     run_frames != run_went_frames):
    print("costs: %u example(s) ask for %u slot(s) and %u frame(s) and reach "
          "%u and %u, and what a program is told to find is what it uses"
          % (run_sized, run_asked, run_frames, run_went, run_went_frames))
    failed = 1

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

    # And what the stack a function asks for is counted out of. Every value a
    # program works out is so many slots wide, and the compiler adds that up
    # as it goes to say how much room a body needs. A byte written as a letter
    # and the same byte written as a number are the same one slot, so the two
    # programs below have to ask for the same room -- until D808 the letter was
    # counted twice, because `emit_constant` pushes and four places pushed
    # beside it, and a body full of them asked for twice the stack it uses.
    # Written as two programs rather than as a number, because a number here
    # would be this compiler's arithmetic held to itself. Each byte is added to
    # something the function was handed, because a conversion of a byte written
    # down is a value the compiler works out where it stands since D886 -- and
    # a value worked out is a byte literal this never compiles.
    letters = ["'a'", "'b'", "'c'", "'d'", "'e'"]
    numbers = ["97", "98", "99", "100", "101"]
    counted = {}
    for which, written in (('letters', letters), ('numbers', numbers)):
        spelled = os.path.join(work, which + '.kest')
        open(spelled, 'w').write(
            "fn f(by: u8) -> i32 {\n"
            "    return %s\n"
            "}\n"
            "\n"
            "fn main() -> i32 {\n"
            "    return f(0) - 495\n"
            "}\n" % ' + '.join("i32(%s + by)" % one for one in written))
        asked_ran = subprocess.run(['./kest', 'emit', spelled, '--json'],
                                   capture_output=True, text=True,
                                   stdin=subprocess.DEVNULL)
        # One that did not compile is counted as nought, which no program
        # asks for, so the pair below is what says so: a second message here
        # would be a second thing this can say and one more thing to have
        # watched it say.
        counted[which] = (json.loads(asked_ran.stdout).get('needs', {})
                          .get('slots', 0) if asked_ran.returncode == 0 else 0)
    if counted.get('letters') != counted.get('numbers') or \
            counted.get('letters', 0) == 0:
        print("costs: five bytes written as letters ask for %u slots and the "
              "same five written as numbers ask for %u, and a byte is a byte"
              % (counted.get('letters', 0), counted.get('numbers', 0)))
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


# Enough calls for a copy to be most of what is paid and few enough to read.
COPIES = 60

# Enough shapes for one copy each to be measurable against the noise, and few
# enough that the program around them is read quickly.
SHAPES = 40

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
# What a constant costs to work out, which is once however many times it is
# read. D674 moved the working out to the declaration and nothing said it had
# arrived: a compiler that folded a constant at every use would print the same
# numbers everywhere else and grow with how often a program named a thing. So a
# run says how many values it worked out, and two programs reading one constant
# a different number of times have to say the same number. See D675.
def folds_in(source):
    at = os.path.join(work, 'folding.kest')
    with open(at, 'w') as writing:
        writing.write(source)
    ran = subprocess.run(['./kest', 'emit', '--json', at],
                         capture_output=True, text=True,
                         stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib'))
    if ran.returncode != 0:
        return None
    return json.loads(ran.stdout).get('folds')


WIDE = "struct Big {\n" + "".join(
    "    f%d: i32\n" % i for i in range(8)) + "}\n\nconst P: Big = Big(" + (
    ", ".join(str(i + 1) for i in range(8))) + ")\n\n"
# Handed to a function rather than bound to a name, because since D887 a name
# nothing writes is itself a value the chunk holds -- which is one value worked
# out per name and has nothing to do with how often the constant was read.
FIRST_OF = "fn first(b: Big) -> i32 {\n    return b.f0\n}\n\n"
read_once = folds_in(WIDE + FIRST_OF +
                     "fn main() -> i32 {\n    return first(P) - 1\n}\n")
read_often = folds_in(WIDE + FIRST_OF +
                      "fn main() -> i32 {\n    let total = 0\n" +
                      "    total += first(P)\n" * 40 +
                      "    return total - total\n}\n")
if read_once is None or read_once < 1 or read_once != read_often:
    print("costs: a constant read once was worked out %s times and read forty "
          "times %s" % (read_once, read_often))
    failed = 1

# And what a copy of a generic costs, which is the other thing a program pays
# for more than once. A copy exists per set of types a generic is called with
# and not per call, so sixty calls of one generic are one copy and sixty calls,
# while sixty generics called once are sixty copies. Two programs of the same
# number of lines, differing only in that. See D742.
one_copy_costs = what_a_program_costs(
    "module reading\n\nfn box<A, B>(x: A, y: B) -> A {\n    return x\n}\n\n"
    "fn main() -> i32 {\n    let t = 0\n" +
    "    t += box(1, 2.0)\n" * COPIES +
    "    return t\n}\n")
many_copies_costs = what_a_program_costs(
    "module reading\n\n" +
    "".join("fn box%u<A, B>(x: A, y: B) -> A {\n    return x\n}\n\n" % i
            for i in range(COPIES)) +
    "fn main() -> i32 {\n    let t = 0\n" +
    "".join("    t += box%u(1, 2.0)\n" % i for i in range(COPIES)) +
    "    return t\n}\n")

# And what one copy is made of. Two programs of the same length, the same
# shapes and the same calls, differing only in how many sets of types the one
# generic is called with: everything that is not the copy is in both, so the
# difference divided by the copies is one copy. Run at two body lengths, and
# under both the stage that checks and the stage that compiles. See D743.
def per_copy(command, lines):
    def shaped(distinct):
        src = ["module reading", ""]
        for k in range(SHAPES):
            src += ["struct Row%u {" % k, "    n: i32", "}", ""]
            src += ["fn touch%u(r: Row%u) -> i32 {" % (k, k), "    return r.n",
                    "}", ""]
        held = ["    let a0 = x"]
        held += ["    let a%u = a%u" % (i, i - 1) for i in range(1, lines)]
        src += ["fn box<A>(x: A) -> A {"] + held
        src += ["    return a%u" % (lines - 1), "}", ""]
        src += ["fn main() -> i32 {", "    let t = 0"]
        for k in range(SHAPES):
            src += ["    t += box(Row%u(1)).n + touch%u(Row%u(1))"
                    % (k if distinct else 0, k, k)]
        return "\n".join(src + ["    return t", "}"]) + "\n"

    where = os.path.join(work, 'reading.kest')
    with open(where, 'w') as out:
        out.write(shaped(True))
    many = what_it_cost(command, where)
    with open(where, 'w') as out:
        out.write(shaped(False))
    one = what_it_cost(command, where)
    if many is None or one is None:
        return None
    return (many - one) // (SHAPES - 1)


flat = per_copy('emit', 1)
# And what the rule costs the tree as it stands, rather than what it costs a
# program written to show it costing something. A copy is what monomorphising
# is: `copies` is how many chunks are one of several compiled from one body,
# `copiedBodies` how many bodies those came from, and `copiedBytes` what the
# ones past the first are in code. The difference between the first two is what
# the rule cost over compiling each body once.
#
# Measured so the two sides can be read against each other. What it costs is
# the bytes; what it buys is that a copy is compiled against real types, which
# is the only way a promise can be proved through one — a generic that boxed
# its argument would allocate, and `no.alloc` through it would be a promise
# nothing could check. So the promises are counted beside the bytes, and a day
# when most of what the rule costs stops carrying one is a day to ask again.
# See D778.
# And what room a token array takes against what is put in it. The array grows
# where it stands, so the factor it grows by is free to be chosen for the room
# it leaves rather than for a copy it is not making, and it is bounded by the
# bytes left to read because the shortest token there is is one byte. Doubling
# left a third of every array never written to. See D783.
# And what a build still holds when it is done, by what asked for it. A total
# is a number with nothing under it: `held` said 63975 for the library and
# nothing said which part of it was the code, which the types and which the
# source it was all cut from. Every number `holds` gives is part of `held`, so
# the sum of them and the source they came from cannot pass it -- one that does
# is a number counted twice. See D784.
held_ran = subprocess.run(['./kest', 'emit', 'lib/std/text.kest', '--json'],
                          capture_output=True, text=True,
                          stdin=subprocess.DEVNULL)
held_said = json.loads(held_ran.stdout) if held_ran.returncode == 0 else {}
held_parts = held_said.get('holds', {})
held_source = sum(one['bytes'] for one in held_said.get('read', []))
held_named = sum(held_parts.values()) + held_source
if not held_parts or held_named > held_said.get('held', 0):
    print("costs: what a build holds is %u and the parts of it named come to "
          "%u, and a part that is not part of the whole is counted twice"
          % (held_said.get('held', 0), held_named))
    failed = 1

room_tokens = 0
room_slots = 0
for room_path in sorted(glob.glob(os.path.join('examples', '*.kest'))):
    room_ran = subprocess.run(['./kest', 'lex', room_path, '--json'],
                              capture_output=True, text=True,
                              stdin=subprocess.DEVNULL)
    if room_ran.returncode != 0:
        continue
    room_said = json.loads(room_ran.stdout)
    room_tokens += len(room_said.get('tokens', []))
    room_slots += room_said.get('tokenRoom', 0)
if room_slots == 0 or room_slots > room_tokens * 5 // 4:
    print("costs: %u tokens were read into room for %u, and an array that "
          "grows where it stands has no copy to be doubling for"
          % (room_tokens, room_slots))
    failed = 1

copied_total = 0
copied_bodies = 0
copied_bytes = 0
copied_code = 0
copied_quiet = 0
for copied_path in sorted(glob.glob(os.path.join('examples', '*.kest'))):
    copied_ran = subprocess.run(['./kest', 'emit', '--json', copied_path],
                                capture_output=True, text=True,
                                stdin=subprocess.DEVNULL,
                                env=dict(os.environ, KEST_LIB='lib'))
    copied_said = (json.loads(copied_ran.stdout)
                   if copied_ran.returncode == 0 else None)
    if copied_said is None:
        continue
    copied_code += sum(one['bytes'] for one in copied_said.get('functions', []))
    copied_total += copied_said.get('copies', 0)
    copied_bodies += copied_said.get('copiedBodies', 0)
    copied_bytes += copied_said.get('copiedBytes', 0)
    copied_where = {}
    for one in copied_said.get('functions', []):
        copied_where.setdefault(
            (one.get('file'), one.get('line'), one.get('wrote')), []).append(one)
    for copied_group in copied_where.values():
        if len(copied_group) > 1:
            copied_quiet += sum(1 for one in copied_group if one.get('noAlloc'))
if (copied_total == 0 or copied_bodies == 0 or copied_bytes == 0 or
        copied_total <= copied_bodies or
        copied_quiet * 2 <= copied_total):
    print("costs: %u copies came from %u bodies and the ones past the first "
          "are %u of %u bytes of code, and %u of them promise `no.alloc` — "
          "which is what a copy per set of types is for"
          % (copied_total, copied_bodies, copied_bytes, copied_code,
             copied_quiet))
    failed = 1

deep = per_copy('emit', 20)
flat_checked = per_copy('check', 1)
deep_checked = per_copy('check', 20)

# What a turn of a walk costs, which is instructions rather than memory and is
# here because it is what `emit` printed rather than what a clock said. The
# count a `for` keeps lives in a slot nothing can name and the name the program
# asked for is a copy of it, so that assigning to the name cannot make the count
# go wrong. Where nothing in the body assigns to it there is nothing to defend
# against and the name is the count itself: a load and a store off every turn of
# the loop this language is written with most. Both halves are read, because a
# copy that is never made is as wrong as one that is always made — one is two
# instructions nobody needed and the other is a walk a body can derail. See
# D866.
def what_emit_printed(body):
    where = os.path.join(work, 'walking.kest')
    with open(where, 'w') as walking:
        walking.write(body)
    ran = subprocess.run(['./kest', 'emit', where], capture_output=True,
                         text=True, stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib'))
    if ran.returncode != 0:
        return None
    printed = {}
    for line in ran.stdout.splitlines():
        found = re.match(r'^  (\d{4})  (\S+)\s*(.*)$', line)
        if found:
            printed[int(found.group(1))] = (found.group(2),
                                            found.group(3).strip())
    return printed


# What a run of one command said about itself, which is how a stage is read
# against the stage before it and how a body is read for what was worked
# out inside it.
def what_it_said(command, where, name):
    ran = subprocess.run(['./kest', command, '--json', where],
                         capture_output=True, text=True,
                         stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib'))
    if ran.returncode != 0:
        return None
    return json.loads(ran.stdout).get(name)


def what_a_turn_is(body):
    printed = what_emit_printed(body)
    if printed is None:
        return None
    step = None
    for at, (op, rest) in printed.items():
        if op.startswith('next.less.'):
            step = (at, rest)
    if step is None:
        return None
    where_from = re.search(r'-> (\d+)$', step[1])
    counts = re.match(r'(\d+)', step[1])
    if where_from is None or counts is None:
        return None
    top = int(where_from.group(1))
    count_slot = counts.group(1)
    turn = sorted(at for at in printed if top <= at <= step[0])
    copies = (len(turn) > 2 and printed[turn[0]] == ('load', count_slot) and
              printed[turn[1]][0] == 'store')
    return len(turn), copies


# Two programs of one loop each, the same in every line but one: the second
# writes the name the walk binds and the first does not.
QUIET_WALK = """module walking

fn main() -> i32 {
    let total = 0
    for i in 0..8 {
        total += i
    }
    return total - 28
}
"""
WRITTEN_WALK = """module walking

fn main() -> i32 {
    let total = 0
    for i in 0..8 {
        i = i + 1
        total += i
    }
    return total - 36
}
"""
# And what a cast and a division cost, which is the same question asked of the
# other instruction a program written over `i32` carries everywhere: `narrow`.
# A result wider than its type is not the answer the type describes and is cut
# back, but two of the places that cut had nothing to cut. A cast to a type that
# already holds every value of what it is cast from cuts nothing, and an
# unsigned division cannot leave the width at all — while a signed one can, for
# the one pair at the end of the range, and `/=` was not cutting it. See D867.
def narrows_in(body):
    printed = what_emit_printed(body)
    if printed is None:
        return None
    return sum(1 for each in printed if printed[each][0] == 'narrow')


def cuts_after_dividing(body, dividing):
    printed = what_emit_printed(body)
    if printed is None:
        return None
    in_order = [printed[each] for each in sorted(printed)]
    places = [i for i, (op, rest) in enumerate(in_order) if op == dividing]
    if len(places) != 1:
        return None
    next_one = places[0] + 1
    return (1 if next_one < len(in_order) and
            in_order[next_one][0] == 'narrow' else 0)


def one_program(lines):
    return "module walking\n\nfn main() -> i32 {\n%s\n}\n" % lines


# And whether the cut arrives with the arithmetic that needed it. Every `+`,
# `-` and `*` on a whole number narrower than a slot used to be two
# instructions: one that adds and one that cuts what it added. They are one
# now, and what says so is that the pair is nowhere in what `emit` prints. See
# D868.
def cuts_apart(body, arithmetic):
    printed = what_emit_printed(body)
    if printed is None:
        return None
    in_order = [printed[each] for each in sorted(printed)]
    return sum(1 for i, (op, rest) in enumerate(in_order)
               if op == arithmetic and i + 1 < len(in_order) and
               in_order[i + 1][0] == 'narrow')


def cuts_together(body, arithmetic):
    printed = what_emit_printed(body)
    if printed is None:
        return None
    return sum(1 for each in printed if printed[each][0] == arithmetic)


# And whether a struct built out of locals is loaded a field at a time. The
# machine has an instruction that takes a run of slots in one go, which is what
# a wide value is loaded with, and the compiler now writes it wherever two loads
# ask for slots that sit next to each other. See D871.
def loads_apart(body):
    printed = what_emit_printed(body)
    if printed is None:
        return None
    in_order = [printed[each] for each in sorted(printed)]
    apart = 0
    for i in range(len(in_order) - 1):
        one, after = in_order[i], in_order[i + 1]
        if (one[0] == 'load' and after[0] == 'load' and
                one[1].isdigit() and after[1].isdigit() and
                int(after[1]) == int(one[1]) + 1):
            apart += 1
    return apart


def runs_loaded(body):
    printed = what_emit_printed(body)
    if printed is None:
        return None
    return sum(1 for each in printed if printed[each][0] == 'load.n')


# Built out of three numbers the compiler cannot work out, because since D887 a
# name nothing writes whose value is worked out where it stands is a value the
# chunk holds -- and a value in the chunk is never loaded out of a frame at
# all, which is not what this is weighing. How long a piece of text is is asked
# while running, so a name holding one is a slot.
BUILT = """module walking

struct Three {
    a: i32
    b: i32
    c: i32
}

fn main() -> i32 {
    let x = len("a")
    let y = len("ab")
    let z = len("abc")
    let made = Three(x, y, z)
    return made.a - 1
}
"""
apart_loads = loads_apart(BUILT)
runs = runs_loaded(BUILT)
if apart_loads != 0 or not runs:
    print("costs: two loads of slots that sit next to each other are one load "
          "of both: %s pair(s) left apart and %s run(s) taken at once"
          % (apart_loads, runs))
    failed = 1

ADDING = one_program(
    "    let total = 0\n    let n: i32 = 3\n    total += n\n    return total")
apart = cuts_apart(ADDING, 'add.i')
together = cuts_together(ADDING, 'add.i.narrow')
if apart != 0 or together != 1:
    print("costs: an `i32` `+` and the cut behind it are one instruction and "
          "not two: %s pair(s) left apart and %s together" % (apart, together))
    failed = 1

widening = narrows_in(one_program(
    "    let n: i16 = 3\n    return i32(n)"))
narrowing = narrows_in(one_program(
    "    let n: i32 = 70000\n    return i32(i16(n))"))
from_truth = narrows_in(one_program(
    "    let yes = true\n    return i32(u8(yes))"))
if (widening != 0 or narrowing != 1 or from_truth != 0):
    print("costs: a cast cuts the width when what it is cast from has values "
          "that width does not hold, and not otherwise: %s cut(s) widening an "
          "`i16` to an `i32`, %s cutting an `i32` to an `i16` and %s turning a "
          "`bool` into a `u8`" % (widening, narrowing, from_truth))
    failed = 1

without_sign = cuts_after_dividing(one_program(
    "    let a: u32 = 100\n    let b: u32 = 7\n    let c: u32 = a / b\n"
    "    return i32(c)"), 'div.u')
with_sign = cuts_after_dividing(one_program(
    "    let a: i32 = 100\n    let b: i32 = 7\n    return a / b"), 'div.i')
into = cuts_after_dividing(one_program(
    "    let a: i32 = 100\n    let b: i32 = 7\n    a /= b\n    return a"),
    'div.i')
if with_sign != 1 or without_sign != 0 or into != 1:
    print("costs: a division cuts the width where it can leave it and not "
          "where it cannot: %s cut(s) after an unsigned `/`, %s after a signed "
          "one and %s after a signed `/=`, and two spellings of one division "
          "emit the one thing"
          % (without_sign, with_sign, into))
    failed = 1

quiet_walk = what_a_turn_is(QUIET_WALK)
written_walk = what_a_turn_is(WRITTEN_WALK)
if quiet_walk is None or quiet_walk[1]:
    print("costs: a `for` whose body never writes the name it binds still "
          "copies the count into that name every turn, and a turn of it is "
          "%s instruction(s)"
          % (None if quiet_walk is None else quiet_walk[0]))
    failed = 1
if written_walk is None or not written_walk[1]:
    print("costs: a `for` whose body writes the name it binds is not given a "
          "copy of the count, so that write is a write to the walk's own "
          "count, and a turn of it is %s instruction(s)"
          % (None if written_walk is None else written_walk[0]))
    failed = 1

# And what the machine actually ran, which is the other half of the same
# question: `emit` says what a turn is written as, and this says what the
# machine did with it. Two loops differing only in how many turns they take, so
# everything that is not the loop is in both and the difference divided by the
# turns is one turn. Only the build that checks itself counts what it ran — the
# release build's numbers stay the release build's — which is why this is asked
# of that one. See D870.
def what_it_ran(body):
    where = os.path.join(work, 'running.kest')
    with open(where, 'w') as running:
        running.write(body)
    ran = subprocess.run(['./kest-debug', 'run', where], capture_output=True,
                         text=True, stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib', KEST_DEEP='1'))
    if ran.returncode != 0:
        return None
    added = 0
    for line in ran.stderr.splitlines():
        if line.startswith('ran '):
            added += int(line.split()[2])
    return added or None


TURNS = 100


def loop_of(turns):
    return one_program("    let total = 0\n    for i in 0..%u {\n"
                       "        total += i\n    }\n    return 0" % turns)


ran_short = what_it_ran(loop_of(TURNS)) if have_checked else None
ran_longer = what_it_ran(loop_of(TURNS * 2)) if have_checked else None
turn_ran = None
if ran_short is not None and ran_longer is not None:
    turn_ran = (ran_longer - ran_short) // TURNS
if have_checked and (turn_ran is None or quiet_walk is None or
                     turn_ran != quiet_walk[0]):
    print("costs: a turn of a `for` is %s instruction(s) where it is written "
          "and the machine ran %s of them"
          % (None if quiet_walk is None else quiet_walk[0], turn_ran))
    failed = 1

# And the three kinds of value the reference names one by one: a case written
# in a body, a hash of a piece of text, and a run of numbers indexed by one --
# the last written both ways, as a constant and inside a body, because where it
# was written decided whether it was a value or work until D887.
# Each is something the folder has always known how to work out, and a call
# written in a body was never offered to it — so a program that hashed a name
# of three letters hashed them again on every frame that went past, while the
# same call written as a constant was a number before the program started. What
# says a value was not paid for is that the instruction which would have done
# the work is nowhere in what `emit` printed. See D886.
def instructions_of(where):
    ran = subprocess.run(['./kest', 'emit', where], capture_output=True,
                         text=True, stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib'))
    if ran.returncode != 0:
        return None
    return {found.group(1)
            for found in re.finditer(r'^  \d{4}  (\S+)', ran.stdout, re.M)}


NOT_PAID_FOR = """module paying

enum Door {
    Shut
    Open
}

const TIERS: [i32; 4] = [0, 90, 250, 1200]

fn tier(at: i32) -> i32 {
    return TIERS[at]
}

fn nearby(at: i32) -> i32 {
    let tiers: [i32; 4] = [0, 90, 250, 1200]
    return tiers[at]
}

fn hashed() -> u64 {
    return hash("sword")
}

fn chosen() -> i32 {
    let door = Door.Open
    return match door {
        Shut -> 0
        Open -> 1
    }
}

fn main() -> i32 {
    return tier(0) + nearby(0) + i32(hashed() % 2) + chosen() - 1
}
"""
paying = os.path.join(work, 'paying.kest')
with open(paying, 'w') as writing:
    writing.write(NOT_PAID_FOR)
there = instructions_of(paying)
paying_bodies = what_it_said('emit', paying, 'functions')
worked_out = (None if paying_bodies is None
              else sum(body['folded'] for body in paying_bodies))
if (there is None or worked_out is None or 'hash.t' in there or
        'const.at' not in there or 'store.n' in there or worked_out < 4):
    print("costs: the three values a frame does not pay for are %s value(s) "
          "worked out, with the hash %s and the run %s"
          % (worked_out,
             "still run" if there is None or 'hash.t' in there else "folded",
             "built" if there is None or 'const.at' not in there or
             'store.n' in there else "read where it stands"))
    failed = 1

shutil.rmtree(work, ignore_errors=True)
if (one_copy_costs is None or many_copies_costs is None or
        many_copies_costs <= one_copy_costs * 2 or
        flat is None or deep is None or flat_checked is None or
        deep_checked is None or
        deep - flat <= (deep_checked - flat_checked) * 4):
    print("costs: %u calls of one generic cost %s and %u generics called once "
          "cost %s, and a copy of a one-line body cost %s to compile and %s "
          "to check against %s and %s for one of twenty lines, and a copy is "
          "what is paid for rather than a call"
          % (COPIES, one_copy_costs, COPIES, many_copies_costs, flat,
             flat_checked, deep, deep_checked))
    failed = 1
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


# And the stages against each other. Working a value out where it is written
# happens in two of them: the checker asks about numbers a program wrote down,
# so that a count below nought is refused where it stands, and the compiler
# works out every constant once. So what `emit` says it worked out is what
# `check` said and more, the way every other number about the stages is. See
# D677.
wider = 0
for reading in ('examples/numbers.kest', 'examples/state.kest',
                'examples/lookup.kest', LIBRARY):
    checked = what_it_said('check', reading, 'folds')
    emitted = what_it_said('emit', reading, 'folds')
    if checked is None or emitted is None or emitted < checked:
        print("costs: `check` worked out %s values of `%s` and `emit` worked "
              "out %s, and a stage does what the one before it did and then "
              "more" % (checked, reading, emitted))
        failed = 1
        continue
    # And where the compiler's share of them went. Every value worked out is
    # one of three things: a number the checker read to refuse a count, a
    # constant worked out at its declaration, or a value written inside a body
    # and put in the chunk rather than built while the function runs. The three
    # add up to all of them, and a value that belongs to none is work nothing
    # can point at. See D678.
    declared = what_it_said('check', reading, 'constants')
    inside = what_it_said('emit', reading, 'functions')
    in_bodies = (None if inside is None
                 else sum(one['folded'] for one in inside))
    if (declared is None or in_bodies is None or
            checked + len(declared) + in_bodies != emitted):
        print("costs: `%s` worked out %s values: %s reading what was written "
              "down, %s constants and %s inside bodies, which do not add up"
              % (reading, emitted, checked,
                 None if declared is None else len(declared), in_bodies))
        failed = 1
        continue
    # And how big those values are, which is what says whether eight of them
    # are eight numbers or eight structs. A value takes a slot at least, and
    # somewhere in this tree one takes more than one — a count that answered
    # the same number twice would be the count of values wearing a second
    # name. See D679.
    for one in inside:
        if one['foldedSlots'] < one['folded']:
            print("costs: `%s` was given %u value(s) taking %u slot(s), and a "
                  "value takes a slot at least"
                  % (one['name'], one['folded'], one['foldedSlots']))
            failed = 1
    wider += sum(1 for one in inside
                 if one['foldedSlots'] > one['folded'])

if wider == 0:
    print("costs: no function here was given a value of more than one slot, "
          "so what those values are made of is a number saying nothing")
    failed = 1

lexing = what_it_cost('lex', LIBRARY)
# And what that paid for, which is the file and the tokens made of it and very
# little else. The array they are read into is the last thing in the arena
# while a file is being read, so it is made bigger where it stands rather than
# taken again: what a run costs to lex is the tokens it kept and not every size
# the array passed through. A quarter over is the room in the last doubling,
# which is a run's worth of tokens that were never reached. What one weighs is
# asked of the run, for the reason D688 gives. See D746.
tokens_read = what_it_said('lex', LIBRARY, 'tokens')
token_bytes = what_it_said('lex', LIBRARY, 'tokenBytes')
held_by_lexing = None
if tokens_read is not None and token_bytes:
    held_by_lexing = os.path.getsize(LIBRARY) + len(tokens_read) * token_bytes
if (lexing is None or held_by_lexing is None or lexing < held_by_lexing or
        lexing > held_by_lexing * 5 // 4):
    print("costs: reading that library as tokens cost %s, and the file and "
          "the %s token(s) it was read into are %s of it"
          % (lexing, None if tokens_read is None else len(tokens_read),
             held_by_lexing))
    failed = 1
parsing = what_it_cost('parse', LIBRARY)
# And what the tree is made of, against what it cost. A node of this compiler is
# fifty-six bytes for an expression, sixty-four for a statement and eighty-eight
# for a declaration, so a tree is at least its nodes and not much more: what is
# beside them is the lists a block and an argument list are. The floor here is
# what the smallest of those is, because a file of nothing but expressions is
# the cheapest tree there is. See D641 and D642.
#
# The ceiling is five thirds of the floor, and what it holds is that a list is
# handed over as exactly what it holds rather than as whatever size it grew to:
# a tree was seventeen tenths of its nodes while lists grew in the arena and is
# fifteen tenths now, so the number between them is the one worth writing down.
# See D745.
nodes = what_it_said('parse', LIBRARY, 'nodes')
# And what checking made, which is types: one for every signature, every
# optional and every run of something, beside the ones that have names. A type
# is a hundred and sixty-eight bytes, so what the stage costs is at least what
# it made — and most of the rest is the symbols and the member lists beside
# them. See D644.
types_made = what_it_said('check', LIBRARY, 'typesMade')
# And what the statements of that tree are, which is what a statement being
# sixty-four bytes rests on. The biggest arm of one is `for` — two names, the
# thing walked, what it walks to and the block — and every statement is as big
# as that whether it is one or not. The decision not to put it out of line
# rests on loops being few; this is that premise, held. See D643.
written = subprocess.run(['./kest', 'parse', LIBRARY], capture_output=True,
                         text=True, stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib')).stdout
loops = len(re.findall(r'\((?:for|while) ', written))
statements = len(re.findall(r'\((?:for|while|let|return|defer|=) ', written))
# And the same loops counted in the file itself, because a tree that says there
# are none is a tree nobody can read this off. What is compared is the two
# counts, which is the one comparison with nothing to guess about.
wrote = open(LIBRARY).read()
in_the_file = len(re.findall(r'(?m)^[ \t]*(?:for|while) ', wrote))
if (statements == 0 or loops != in_the_file or
        loops * 4 > statements):
    print("costs: %u of the %u statements in that library are loops and the "
          "file has %u, and what a statement costs is what the biggest of them "
          "holds" % (loops, statements, in_the_file))
    failed = 1
# What a node weighs is asked of the run rather than written here: it is
# fifty-six bytes on this machine and something else where a pointer is another
# width, and a number about a machine written into a check is a rule that holds
# until somebody builds it elsewhere. D684, D686 and D687 are three of those in
# three days. See D688.
weighs = what_it_said('parse', LIBRARY, 'nodeBytes') or {}
smallest = weighs.get('expression')
if (nodes is None or lexing is None or parsing is None or nodes == 0 or
        smallest is None or parsing - lexing < nodes * smallest or
        parsing - lexing > nodes * smallest * 5 // 3):
    print("costs: a tree of %s nodes cost %s bytes over the tokens it was made "
          "from, and the smallest node of this compiler is %s bytes"
          % (nodes, None if parsing is None or lexing is None
             else parsing - lexing, smallest))
    failed = 1
checking = what_it_cost('check', LIBRARY)
compiling = what_it_cost('emit', LIBRARY)
# What that cost was paid for. A cost on its own has nothing to divide it by: a
# program of four lines that imports the library costs what the library costs,
# and dividing by the file somebody named calls it fifteen times dearer a byte
# than it is. So the compiler says which files it read and how big each of them
# is, and what holds that is the files themselves — a build that says it read
# less than is there is a build whose costs are divided by the wrong number.
# See D656.
# And what the compiler wrote, beside what writing it cost. A run says how many
# bytes each function's code takes, which is the one thing about a compiled
# function a reader could otherwise get only by adding up the instructions and
# knowing how wide each of them is. What holds that number is the instructions
# themselves: the last one listed starts inside it and no further back than the
# widest instruction there is, so a number that is anything else is caught by
# the code it claims to measure. See D744.
WIDEST = 7
compiled = what_it_said('emit', LIBRARY, 'functions') or []
code_bytes = sum(one['bytes'] for one in compiled)
# And the room that code is in. A chunk's arrays double from a floor measured
# against what a body holds, so the room a body ends in is between what it took
# and twice that, plus the floor for a body that never filled one. Held so that
# a floor or a doubling changed without measuring is caught by the room it
# leaves. See D750 and D753.
room_taken = sum(one['room'] for one in compiled)
# And how many constants a body keeps, which is the measurement the constants'
# own floor rests on: four, because the middle body has three.
constants_kept = sum(one['constants'] for one in compiled)
ends_inside = bool(compiled)
for one in compiled:
    if not one['code']:
        ends_inside = ends_inside and one['bytes'] == 0
        continue
    ends_inside = (ends_inside and
                   1 <= one['bytes'] - one['code'][-1]['at'] <= WIDEST)
if (not code_bytes or not ends_inside or compiling is None or
        compiling < code_bytes * 40 or room_taken < code_bytes or
        room_taken > code_bytes * 2 + len(compiled) * 128 or
        constants_kept > code_bytes // 4):
    print("costs: that library compiled to %u byte(s) of code in %u "
          "function(s) held in %u byte(s) of room, keeping %u constant(s), and "
          "cost %s to compile, and what a run says its code takes is where its "
          "instructions end"
          % (code_bytes, len(compiled), room_taken, constants_kept, compiling))
    failed = 1

# And what is given back, which is everything a stage made that the stages after
# it do not read. The tokens a file is read into are dead the moment its tree is
# made — a node holds a span into the source and never a token — and the tree is
# dead when the last copy of every generic has been compiled and the promise has
# been held against what was emitted. What the build says it cost is the same
# number either way, because a ceiling refuses against what was asked for and
# not against what is still held; what moves is what it holds when it is done.
# At least this file's tokens and this file's nodes, and more than that, because
# the library is read with everything it imports and every one of them gives its
# own back. See D747 and D748.
# And how many times it was asked, which tells a stage that keeps a lot from one
# that asks a lot. An arena is not a `malloc`: what it is asked for is a thing
# somebody declared, or an array that doubles, and never an entry at a time. So
# what the number grows with is what the program has in it rather than how big
# any of that is -- twenty askings for every function and every type it made is
# well over what this measures and well under what an array grown an entry at a
# time would say. See D752.
askings = what_it_said('emit', LIBRARY, 'askings')
written = what_it_said('check', LIBRARY, 'functions')
things = None
if written is not None and types_made is not None:
    things = len(written) + types_made
if (askings is None or askings == 0 or things is None or things == 0 or
        askings > things * 20):
    print("costs: reading that library the whole way asked its arena %s "
          "time(s) for the %s thing(s) it declared and made, and what an arena "
          "is asked for is a thing or an array and never an entry at a time"
          % (askings, things))
    failed = 1

# And what a build is still holding, against what it is made of. Everything a
# stage made that no stage after it reads is given back, so what is left is the
# file it read, the types it made, and the module -- and the file and the types
# are two numbers a run says. Two and seven tenths of them, measured; held at
# four, because a build that holds twice what it is made of has started keeping
# something again. See D754.
type_bytes = what_it_said('check', LIBRARY, 'typeBytes')
read_bytes = what_it_said('check', LIBRARY, 'source')
made_of = None
if read_bytes and types_made is not None and type_bytes:
    made_of = read_bytes + types_made * type_bytes

holding = what_it_said('emit', LIBRARY, 'held')
# And what a build that was checked and not compiled holds. It still has its
# trees, because the compiler is one of the two stages that read one, and it has
# given its tokens back all the same — which is the thing held here, because a
# stage that gives something back only when a later stage runs is a stage that
# gives nothing back to whoever stopped early. The two numbers are not
# subtracted from each other: a run that compiles holds a module the other never
# made, so which of them holds more is about the program.
checked_holds = what_it_said('check', LIBRARY, 'held')
given_back = None
if holding is not None and compiling is not None:
    given_back = compiling - holding
if (given_back is None or given_back <= 0 or token_bytes is None or
        tokens_read is None or nodes is None or smallest is None or
        checked_holds is None or checking is None or
        given_back < len(tokens_read) * token_bytes + nodes * smallest or
        checking - checked_holds < len(tokens_read) * token_bytes):
    print("costs: that library cost %s to read the whole way and holds %s of "
          "it, and checking it without compiling holds %s, and what a stage "
          "leaves behind for nobody is given back"
          % (compiling, holding, checked_holds))
    failed = 1

if (made_of is None or holding is None or holding < made_of or
        holding > made_of * 4):
    print("costs: that library holds %s when it is done and is made of %s — "
          "the %s bytes it read and the %s type(s) it made — and what a build "
          "holds is what it is made of and the module it wrote"
          % (holding, made_of, read_bytes, types_made))
    failed = 1

was_read = what_it_said('check', LIBRARY, 'read') or []
source_bytes = what_it_said('check', LIBRARY, 'source')
on_disk = sum(os.path.getsize(one['file']) for one in was_read)
named = sum(one['bytes'] for one in was_read)
if not was_read or not source_bytes or on_disk != source_bytes or named != on_disk:
    print("costs: this compiler says it read %s bytes of source in %u file(s) "
          "it puts at %u, and what it names is %u on disk"
          % (source_bytes, len(was_read), named, on_disk))
    failed = 1
# A signature is a type, so a program is at least as many types as it has
# functions: a count under that is a count of something else.
declared = what_it_said('check', LIBRARY, 'functions')
if (types_made is None or checking is None or parsing is None or
        declared is None or types_made < len(declared) or
        checking - parsing < types_made * 168):
    print("costs: checking that library made %s types for its %s functions "
          "and cost %s bytes over the tree it read, and a type of this "
          "compiler is a hundred and sixty-eight bytes"
          % (types_made, None if declared is None else len(declared),
             None if checking is None or parsing is None
             else checking - parsing))
    failed = 1
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
          "tree of %u nodes of which %u statements are loops, %u to check it "
          "into %u types and %u to compile it — %u bytes of memory for "
          "every hundred of the %u bytes of source it read — "
          "against %u bytes for a program of four lines, %u for one that "
          "prints, %u for one that makes text and %u for one that uses five "
          "of that module rather than one, and its arena was asked %u time(s) "
          "for all of it, and it compiled to %u bytes of "
          "code in %u bytes of room with %u constant(s), and it holds %u of "
          "what it cost "
          "when it is done against %u checked and not compiled, and it "
          "compiled to %u bytes of "
          "code, which is %u bytes of memory for every byte of it, "
          "and what one copy of a generic "
          "is made of, which is %u bytes to compile and %u to check for a "
          "body of one line against %u and %u for one of twenty — a copy is "
          "its body, and its body is paid for when it is compiled — and %u "
          "calls of one generic "
          "against %u generics called once is %u against %u, because a copy "
          "is paid for and a call is not, and what that rule costs the "
          "examples as they stand is %u copies from %u bodies, the ones past "
          "the first being %u of %u bytes of code, of which %u promise "
          "`no.alloc`, and %u bodies run by the examples each reaching "
          "every slot of the %u they were given between them, and %u "
          "example(s) a machine could size from the program itself asking "
          "for %u slot(s) and reaching %u, and a turn of a `for` is %u "
          "instruction(s) where the body never writes the name it binds and "
          "%u where it does, and a cast cuts the width %u time(s) widening "
          "and %u narrowing, and a division %u time(s) with a sign and %u "
          "without, and an `i32` `+` cuts what it added in %u instruction(s), "
          "and the machine ran %s of the turn's instruction(s), and slots "
          "that sit next to each other are taken in %u go(es), all of it "
          "measured on the machine "
          "this ran on"
          % (asked, len(left_to_the_host), driven, proved, kept, len(alone),
             lexing, parsing, nodes, loops, checking, types_made, compiling,
             compiling * 100 // source_bytes, source_bytes,
             alone_costs,
             printing_costs, making_text_costs, using_five_costs,
             askings, code_bytes, room_taken, constants_kept,
             holding, checked_holds, code_bytes, compiling // code_bytes,
             flat, flat_checked, deep, deep_checked,
             COPIES, COPIES, one_copy_costs, many_copies_costs,
             copied_total, copied_bodies, copied_bytes, copied_code,
             copied_quiet, asked_for, reached, run_sized, run_asked,
             run_went, quiet_walk[0], written_walk[0], widening, narrowing,
             with_sign, without_sign, together, turn_ran, runs))
sys.exit(failed)
PY
