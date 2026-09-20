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
    # What it was handed rather than what it is holding when it comes back.
    # A call that makes a megabyte of text and lets it go holds what a call
    # that made nothing holds, and what this is weighing is the making. See
    # D996.
    return json.loads(ran.stdout)['taken']


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
    if ('"std.text.%s"' % name) not in host:
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
    # A file written and read back, which is the one thing in `std.os` that can
    # cost the heap twice over: the text handed out and the text handed back.
    # Twice the bytes is twice the answer, which is the same rule every other
    # module here is asked.
    'os': """import std.os
import std.text

fn work(n: i32) -> i32 {
    let said = text.repeat("ab", n)
    if !os.write("{HERE}/os.txt", said) {
        return 0 - 1
    }
    if let back = os.read("{HERE}/os.txt") {
        return len(back) - len(back)
    }
    return 0 - 2
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
went = {}
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
        deep_room = int(deep_words[-5])
        deep_went = int(deep_words[-3])
        # And how much of the room asked for the lowering took off the stack
        # after the compiler had reckoned it: an element read straight into
        # the frame never goes through the stack, and the reckoning does not
        # know that. Reducing what is asked for would be wrong, because the
        # deepest moment may be elsewhere, so a body is held to asking for no
        # more than its deepest run used plus this. See D1012.
        deep_fused = int(deep_words[-1])
        deep_name = ' '.join(deep_words[1:-6])
        asked_for += 1
        reached += deep_room
        # The furthest any run of this body went, rather than the furthest one
        # example took it. A body with a branch deeper than the rest is asking
        # for what that branch needs, and an example that never takes it is an
        # example rather than a compiler asking for nothing: `text.fixed`
        # writes a number with no places as two pieces of text joined and
        # everything else it does as one, so the example that prints a price
        # never goes as deep as the one that prints a whole number.
        went[deep_name] = (deep_room,
                           max(deep_went, went.get(deep_name, (0, 0, 0))[1]),
                           deep_path, deep_fused)
for deep_name in sorted(went):
    deep_room, deep_went, deep_path, deep_fused = went[deep_name]
    if deep_room > deep_went + deep_fused:
        loose.append((deep_room - deep_went - deep_fused, deep_name,
                      deep_path))
for deep_slack, deep_name, deep_path in sorted(loose, reverse=True)[:4]:
    print("costs: `%s` asks for %u slot(s) no run of it ever used, and the "
          "deepest was %s" % (deep_name, deep_slack, deep_path))
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
        # A driver that writes a file writes it into this run's own room, so
        # two of these running at once do not write to one name.
        open(driver, 'w').write(DRIVERS[module].replace('{HERE}', work))
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
# `add.i.narrow` and `add.i.narrow.to` are both the one instruction this is
# about: the second is the first with the store after it taken in as well
# (D1014), and a check that counted only the first would be a check that
# stopped counting the day the addition got shorter still.
together = (cuts_together(ADDING, 'add.i.narrow') +
            cuts_together(ADDING, 'add.i.narrow.to'))
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
# And how many questions that build asked of its own compiler on the way. Every
# number an instruction carries is read by something that asks whether it could
# be that number, and the seven turns from D900 to D906 put those questions
# there -- so what the build that checks itself does is this many more things
# than the build that ships. A count rather than a duration, which is why it
# belongs here: the ratio is the same anywhere and the seconds are not. See D907.
def what_it_asked(body):
    where = os.path.join(work, 'running.kest')
    with open(where, 'w') as running:
        running.write(body)
    ran = subprocess.run(['./kest-debug', 'run', where], capture_output=True,
                         text=True, stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib', KEST_DEEP='1'))
    if ran.returncode != 0:
        return None
    for line in ran.stderr.splitlines():
        if line.startswith('guards '):
            return int(line.split()[1])
    return None


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

# And what a frame step costs in work, which is the number every fusion in this
# compiler is about and the one thing about a frame that is not this machine's:
# an instruction count is the same anywhere and a duration is not. Two runs over
# different numbers of rounds, subtracted, the way a turn of a `for` is weighed
# above -- the world is built once either way, so what is left is the step. And
# the same frame with its two helpers written out where they are called beside
# it, because what a call costs is the one thing a program cannot ask for from
# inside the language. See D889.
ENTITIES = 20
ROUNDS = 4

SHAPED = """module walking

struct Npc {
    x: f32
    y: f32
    dx: f32
    dy: f32
    health: i32
}

"""

HELPED = """fn moved(one: Npc, dt: f32) -> Npc no.alloc {
    return Npc(one.x + one.dx * dt, one.y + one.dy * dt, one.dx, one.dy,
               one.health)
}

fn turned(one: Npc) -> Npc no.alloc {
    let dx = if one.x < 0.0 -> 0.0 - one.dx else -> one.dx
    let health = if one.health > 0 -> one.health else -> 1
    return Npc(one.x, one.y, dx, one.dy, health)
}

fn step(world: [Npc], dt: f32) -> i32 no.alloc {
    let alive = 0
    for i in 0..len(world) {
        let one = turned(moved(world[i], dt))
        world[i] = one
        if one.health > 0 {
            alive += 1
        }
    }
    return alive
}

"""

BY_HAND = """fn step(world: [Npc], dt: f32) -> i32 no.alloc {
    let alive = 0
    for i in 0..len(world) {
        let was = world[i]
        let far = Npc(was.x + was.dx * dt, was.y + was.dy * dt, was.dx, was.dy,
                      was.health)
        let dx = if far.x < 0.0 -> 0.0 - far.dx else -> far.dx
        let health = if far.health > 0 -> far.health else -> 1
        let one = Npc(far.x, far.y, dx, far.dy, health)
        world[i] = one
        if one.health > 0 {
            alive += 1
        }
    }
    return alive
}

"""

WALKED = """fn world(count: i32) -> [Npc] {
    let made: [Npc] = array()
    for i in 0..count {
        push(made, Npc(1.0, 2.0, 0.5, 0.5, 100))
    }
    return made
}

fn main() -> i32 {
    let all = world(%u)
    let alive = 0
    for r in 0..%u {
        alive += step(all, 0.016)
    }
    return alive - %u
}
"""


def a_step_of(middle):
    over = []
    for many in (ROUNDS, ROUNDS * 2):
        over.append(what_it_ran(SHAPED + middle +
                                WALKED % (ENTITIES, many, ENTITIES * many)))
    if over[0] is None or over[1] is None:
        return None
    return (over[1] - over[0]) // (ROUNDS * ENTITIES)


# The machine's own list of what it can do, read where it is written: how much
# of it a frame reaches is a fraction and needs both halves.
instruction_names = some("the machine's instructions", re.findall(
    r'\{"((?:[^"\\]|\\.)*)",\s*\w+\}',
    re.search(r'INSTRUCTIONS\[\] = \{(.*?)\n\};',
              open(os.path.join('src', 'value.c')).read(), re.S).group(1)))


# And how much of the machine a frame is: which of its instructions a frame
# reaches at all. Every one of them costs every program that runs (D889), and
# what a frame touches is the list to read before anybody moves one of them --
# it is what said which end of the jump family is the cold end. A count of the
# names a run says it ran, which is the same list anywhere. See D891.
def what_a_frame_reaches(middle):
    body = SHAPED + middle + WALKED % (ENTITIES, ROUNDS, ENTITIES * ROUNDS)
    where = os.path.join(work, 'running.kest')
    with open(where, 'w') as running:
        running.write(body)
    ran = subprocess.run(['./kest-debug', 'run', where], capture_output=True,
                         text=True, stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib', KEST_DEEP='1'))
    if ran.returncode != 0:
        return None
    return len({line.split()[1] for line in ran.stderr.splitlines()
                if line.startswith('ran ')}) or None


def a_step_asks():
    over = []
    for many in (ROUNDS, ROUNDS * 2):
        over.append(what_it_asked(SHAPED + HELPED +
                                  WALKED % (ENTITIES, many, ENTITIES * many)))
    if over[0] is None or over[1] is None:
        return None
    return (over[1] - over[0]) // (ROUNDS * ENTITIES)


# And which instructions those are, one name at a time. A total says a frame
# got dearer between one day and the next; a breakdown says which instruction
# did it, and that is the sentence the reference writes about where a frame's
# time goes -- two in three of them moving a value, six of them doing
# arithmetic. What makes each count a whole number rather than a division that
# rounds is subtracting twice. A world is built once however many rounds there
# are, and a round has a loop of its own however many entities are in it, so
# one difference leaves one of the two behind whichever way it is taken; the
# difference of two differences, over entities and over rounds both, leaves
# neither. See D915.
def what_it_ran_each(body):
    where = os.path.join(work, 'running.kest')
    with open(where, 'w') as running:
        running.write(body)
    ran = subprocess.run(['./kest-debug', 'run', where], capture_output=True,
                         text=True, stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib', KEST_DEEP='1'))
    if ran.returncode != 0:
        return None
    ran_each = {}
    for line in ran.stderr.splitlines():
        if line.startswith('ran '):
            ran_each[line.split()[1]] = int(line.split()[2])
    return ran_each or None


def twice(over_what):
    return ((over_what[3] - over_what[2]) -
            (over_what[1] - over_what[0])) // (ENTITIES * ROUNDS)


def what_a_step_of(over_a):
    ran_over = []
    asked_over = []
    for entities_of, rounds_of in ((ENTITIES, ROUNDS),
                                   (ENTITIES * 2, ROUNDS),
                                   (ENTITIES, ROUNDS * 2),
                                   (ENTITIES * 2, ROUNDS * 2)):
        body = over_a.replace('HOW_MANY', str(entities_of)).replace(
            'HOW_LONG', str(rounds_of))
        ran_over.append(what_it_ran_each(body))
        asked_over.append(what_it_asked(body))
    if (any(each_one is None for each_one in ran_over) or
            any(each_one is None for each_one in asked_over)):
        return None, None
    per_name = {}
    for op_name in set().union(*ran_over):
        tally = twice([each_one.get(op_name, 0) for each_one in ran_over])
        if tally:
            per_name[op_name] = tally
    return per_name or None, twice(asked_over)


# And the instrument itself, which is what that paragraph is about. The
# nanoseconds printed above it came off `tools/frame.kest`, so the instructions
# under them are read off the same file rather than off a shape written here
# that happens to look like it -- the one above is a frame of this check's own
# and is a different frame, forty-eight instructions where this is fifty-nine.
# Its bodies are taken as they are and only its `main` is replaced, because a
# copy of a body is a body that stops being the one the reference measured.
# How many and how long are written in by name rather than with a `%`: the
# bodies have a remainder in them, and a file with a `%` in it is not a format
# string however carefully the rest of it was written.
def bodies_of(named):
    return some("the bodies of `tools/%s`" % named, open(
        os.path.join('tools', named)).read().partition(
            '\nfn main() -> i32 {')[0])


COUNTED = bodies_of('frame.kest') + """
fn main() -> i32 {
    let all = world(HOW_MANY)
    let seen = 0
    for r in 0..HOW_LONG {
        seen += step(all, 0.016)
    }
    if seen < 0 {
        return 1
    }
    return 0
}
"""


# And the other two instruments, counted the same way. The reference quotes a
# duration for each of them and, since this, a count beside it -- and the two
# do not always point the same way: a crossing out runs fewer instructions than
# a call the program makes and takes longer, which is the one thing on that page
# a reader would get backwards from either number alone. Their loop lengths are
# a `const`, so the count is written in where the constant is; the bodies
# themselves are taken as they are, the way the frame's are. See D917.
CROSSED = bodies_of('crossing.kest').replace(
    'const CALLS: i32 = 1000000', 'const CALLS: i32 = HOW_MANY') + """
fn main() -> i32 {
    let sum: f64 = 0.0
    for r in 0..HOW_LONG {
        sum = WHICH_ONE(sum)
    }
    if sum < 0.0 {
        return 1
    }
    return 0
}
"""
READ_OF = bodies_of('reference.kest').replace(
    'const READS: i32 = 200000', 'const READS: i32 = HOW_MANY') + """
fn main() -> i32 {
    let world: store<Thing> = store()
    let where: [ref<Thing>] = array()
    let items: [Thing] = array()
    for i in 0..READS {
        push(where, add(world, Thing(i % 64, 1.0)))
        push(items, Thing(i % 64, 1.0))
    }
    let seen = 0
    for r in 0..HOW_LONG {
        seen += WHICH_ONE
    }
    if seen < 0 {
        return 1
    }
    return 0
}
"""


# A sentence of the reference read back as it is written. A sentence too long
# for a line is written in as many as it takes, so what is looked for is the
# words with the spacing left open.
def as_written(said):
    return r'\s+'.join(said.split())


# A number the reference writes out the way a reader reads it. Every other
# number held here is a figure on both sides; this sentence is prose, and prose
# spells a number. A word this has no figure for is a sentence nobody can hold,
# which is what answering nothing rather than nought is for.
# `nought` is here because a difference may be nought: an index read costs the
# same as a hop of the loop it is in since D1044, and a sentence that says so
# has to be a sentence this can read.
FIGURES = {"nought": 0,
           "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
           "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11,
           "twelve": 12, "thirteen": 13, "fourteen": 14, "fifteen": 15,
           "sixteen": 16, "seventeen": 17, "eighteen": 18, "nineteen": 19,
           "twenty": 20, "thirty": 30, "forty": 40, "fifty": 50, "sixty": 60,
           "seventy": 70, "eighty": 80, "ninety": 90}


def in_figures(spelled_out):
    tallied = 0
    for half in spelled_out.split('-'):
        if half not in FIGURES:
            return None
        tallied += FIGURES[half]
    return tallied


# And what a frame step costs the program rather than the machine. Everything
# above weighs the compiler's work; this is the other side of the door, and it
# is the number a host budgeting a frame most needs: bytes an entity, a step.
# Two ticks over different numbers of rounds, subtracted, so the world built
# once inside the tick cancels and what is left is the step. A body that
# promises `no.alloc` comes to nought an entity, which is what the promise
# means read from outside it, and one that makes a piece of text a frame pays
# for the text -- both halves, because a measurement that says nought for
# either is a measurement of nothing. See D908.
TICKED = """module ticking

import std.text
import std.table

struct Npc {
    name: text
    health: i32
}

fn made(count: i32) -> [Npc] {
    let all: [Npc] = array()
    for i in 0..count {
        push(all, Npc("npc", 100))
    }
    return all
}

fn quiet(world: [Npc], keep: [Npc], seen: table.Table<i32, i32>) -> i32 no.alloc {
    let found = 0
    for one in world {
        if one.health > 0 {
            found += 1
        }
    }
    return found
}

fn loud(world: [Npc], keep: [Npc], seen: table.Table<i32, i32>) -> i32 {
    let said = 0
    for one in world {
        let line = "{one.name}: {one.health}"
        said += len(line)
    }
    return said
}

fn grown(world: [Npc], keep: [Npc], seen: table.Table<i32, i32>) -> i32 {
    for one in world {
        push(keep, one)
    }
    return len(keep)
}

fn keyed(world: [Npc], keep: [Npc], seen: table.Table<i32, i32>) -> i32 {
    for at, one in world {
        table.set(seen, at + table.count(seen), one.health)
    }
    return table.count(seen)
}

fn housed(world: [Npc], kept: store<Npc>) -> i32 {
    for one in world {
        add(kept, one)
    }
    return 1
}

fn onEvents(events: [i32]) -> i32 {
    let all = made(%u)
%s    let found = 0
    for r in 0..%u {
        found += %s
    }
    return found
}

fn main() -> i32 {
    return onEvents(array()) - onEvents(array())
}
"""


def what_a_tick_took(body):
    where = os.path.join(work, 'ticking.kest')
    with open(where, 'w') as ticking:
        ticking.write(body)
    ran = subprocess.run(['./kest', 'tick', '--json', where],
                         capture_output=True, text=True,
                         stdin=subprocess.DEVNULL,
                         env=dict(os.environ, KEST_LIB='lib'))
    if ran.returncode != 0:
        return None
    said = json.loads(ran.stdout)
    # What it was handed rather than what it is holding at the end. A world
    # that makes a name a frame and lets it go holds nothing at the end of it
    # and paid for every one, and it is the paying that a frame budget is
    # about. See D996.
    return None if said.get('errors') else said.get('taken')


# The room a container is told to make is the same in both runs, so what is
# left after subtracting is what was put in it and not the making. One told
# nothing grows into it instead, which is the number beside each.
ROOM = ENTITIES * ROUNDS * 10
NOTHING_MADE = ("    let keep: [Npc] = array()\n"
                "    let seen: table.Table<i32, i32> = table.empty()\n")
TOLD_TABLE = (NOTHING_MADE + "    table.refill(seen, %u)\n" % ROOM)
ROOMY_ARRAY = ('    let keep = array(%u, Npc("npc", 100))\n'
               "    clear(keep)\n"
               "    let seen: table.Table<i32, i32> = table.empty()\n" % ROOM)
NO_STORE = "    let kept: store<Npc> = store()\n"
ROOMY_STORE = "    let kept: store<Npc> = store(%u)\n" % ROOM
# And the same store told afterwards rather than when it was made, which is
# what a program has when the store is inside something it was handed.
TOLD_STORE = NO_STORE + "    room(kept, %u)\n" % ROOM


def a_step_takes(which, made=NOTHING_MADE):
    over = []
    for many in (ROUNDS, ROUNDS * 2):
        over.append(what_a_tick_took(TICKED % (ENTITIES, made, many, which)))
    if over[0] is None or over[1] is None:
        return None
    return (over[1] - over[0]) // (ROUNDS * ENTITIES)


quiet_frame = a_step_takes('quiet(all, keep, seen)')
text_frame = a_step_takes('loud(all, keep, seen)')
grown_frame = a_step_takes('grown(all, keep, seen)')
roomy_frame = a_step_takes('grown(all, keep, seen)', ROOMY_ARRAY)
keyed_frame = a_step_takes('keyed(all, keep, seen)')
told_frame = a_step_takes('keyed(all, keep, seen)', TOLD_TABLE)
stored_frame = a_step_takes('housed(all, kept)', NO_STORE)
housed_frame = a_step_takes('housed(all, kept)', ROOMY_STORE)
roomy_store = a_step_takes('housed(all, kept)', TOLD_STORE)
# The three containers this language has, side by side, and the promise beside
# them. What is held here is the noughts -- for the promise, because that is
# what `no.alloc` means seen from outside, and for a container told how many
# were coming, because that is what being told is worth. The rest of it is held
# to the numbers the reference writes, below. It was held to an order once -- a
# pair in a table above an element in an array above a piece of text -- and an
# order is what you hold when you do not know the number; D912 put seventy-seven
# bytes where fifty-one belonged and every one of those held, because seventy-
# seven is above twenty-five too. The numbers are known and written down now.
# See D909 and D915.
# And the table the reference prints of these, held to them. The numbers are
# measured here and written there, which is two places for one fact -- so the
# one that runs reads the one that is read, and a row somebody edits without
# running anything is a gate that fails. The same rule D886 made for the one
# number the reference quotes from a run, said about four. See D914.
WRITTEN_DOWN = {"a piece of text": text_frame, "an array": grown_frame,
                "a table": keyed_frame, "a store": stored_frame}
printed = dict(re.findall(
    r"^\| (a piece of text|an array|a table|a store) \| (\d+) bytes an entity",
    open(os.path.join('docs', 'language.md')).read(), re.M))
some("the table of what a container costs a frame", printed)
for which in sorted(WRITTEN_DOWN):
    if which not in printed or int(printed[which]) != WRITTEN_DOWN[which]:
        print("costs: the reference says %s costs %s byte(s) an entity and a "
              "run says %s"
              % (which, printed.get(which), WRITTEN_DOWN[which]))
        failed = 1

if (quiet_frame is None or text_frame is None or grown_frame is None or
        keyed_frame is None or told_frame is None or roomy_frame is None or
        stored_frame is None or housed_frame is None or quiet_frame != 0 or
        told_frame != 0 or
        roomy_frame != 0 or housed_frame != 0 or roomy_store is None or
        roomy_store != 0):
    print("costs: a frame step takes %s byte(s) an entity promising "
          "`no.alloc`, %s making a piece of text, %s growing an array and %s "
          "into one made with room, %s putting a pair in a table and %s into "
          "one told how many were coming, %s adding to a store, %s to one "
          "made with room and %s to one told afterwards"
          % (quiet_frame, text_frame, grown_frame, roomy_frame, keyed_frame,
             told_frame, stored_frame, housed_frame, roomy_store))
    failed = 1

a_frame = a_step_of(HELPED) if have_checked else None
by_hand = a_step_of(BY_HAND) if have_checked else None
reaches = what_a_frame_reaches(HELPED) if have_checked else None
asked_of_itself = a_step_asks() if have_checked else None
if have_checked and (a_frame is None or by_hand is None or reaches is None or
                     asked_of_itself is None or asked_of_itself < 1 or
                     a_frame < 1 or by_hand < 1 or a_frame - by_hand < 2):
    print("costs: a frame step is %s instruction(s) an entity and %s with its "
          "helpers written out, and a call and its answer are two of them, "
          "and it reaches %s of the machine's %u, and the build that checks "
          "itself asks %s question(s) of its own compiler over it"
          % (a_frame, by_hand, reaches, len(instruction_names),
             asked_of_itself))
    failed = 1

# And the reference's own paragraph about that step, held to the run it quotes.
# It was the last number in these documents taken from a run and written down
# with nothing comparing it: right the day it was written, and right by luck
# every day after. The figures there are spelled out, because a paragraph is
# prose and prose spells a number, so they are read back through a table of
# words. Which instructions the sentence counts as moving a value and which as
# arithmetic is read out of the sentence rather than kept in a list here: the
# ones it names before `The arithmetic is` and the ones it names after. What is
# held is every figure in it — the total, each instruction it names, both sums
# it draws, and the questions the checked build asks over them — so a number
# that moves is a gate that fails and a line to change on purpose. The rule
# D886 made for the one number the reference quotes from a run and D914 made
# for the table of what a container costs, said about a paragraph. See D915.
def named_in(half):
    at_a_time = {}
    for how_many, op_name in re.findall(
            r'([a-z-]+)\s+(?:are\s+|is\s+)?`([a-z0-9._]+)`', half):
        at_a_time[op_name] = in_figures(how_many)
    return at_a_time


REFERENCE = open(os.path.join('docs', 'language.md')).read()
step_says = re.search(
    r'a frame step an entity is \*\*([a-z-]+) instructions\*\*,\s+of which'
    r'(.*?)—\s+([a-z-]+)\s+of\s+the\s+([a-z-]+),\s+near\s+enough.*?'
    r'The\s+arithmetic\s+is\s+([a-z-]+):\s+(.*?)\.\s+That\s+is\s+what\s+a'
    r'\s+stack\s+machine\s+is.*?asks\s+its\s+own\s+compiler'
    r'\s+\*\*([a-z-]+)\s+questions\*\*',
    REFERENCE, re.S)
some("the reference's paragraph about what a frame step runs", step_says)
ran_it = None
asked_it = None
if step_says is not None and have_checked:
    ran_it, asked_it = what_a_step_of(COUNTED)
    moves_it = named_in(step_says.group(2))
    sums_it = named_in(step_says.group(6))
    at_a_time = dict(moves_it, **sums_it)
    if (ran_it is None or not moves_it or not sums_it or
            in_figures(step_says.group(1)) != sum(ran_it.values()) or
            in_figures(step_says.group(4)) != sum(ran_it.values()) or
            in_figures(step_says.group(3)) != sum(how_many or 0
                                                  for how_many
                                                  in moves_it.values()) or
            in_figures(step_says.group(5)) != sum(how_many or 0
                                                  for how_many
                                                  in sums_it.values()) or
            in_figures(step_says.group(7)) != asked_it or
            any(at_a_time[op_name] != ran_it.get(op_name)
                for op_name in at_a_time)):
        print("costs: the reference says a frame step an entity is %s "
              "instruction(s), %s of them moving a value and %s doing "
              "arithmetic, and that the build that checks itself asks %s "
              "question(s) over them, and a run says %s, %s, %s and %s — the "
              "reference names %s and the run ran %s"
              % (in_figures(step_says.group(1)), in_figures(step_says.group(3)),
                 in_figures(step_says.group(5)), in_figures(step_says.group(7)),
                 None if ran_it is None else sum(ran_it.values()),
                 sum(how_many or 0 for how_many in moves_it.values()),
                 sum(how_many or 0 for how_many in sums_it.values()),
                 asked_it, sorted(at_a_time.items()),
                 sorted((op_name, None if ran_it is None
                         else ran_it.get(op_name))
                        for op_name in at_a_time)))
        failed = 1

# And the two paragraphs beside the other two instruments. The same rule as the
# frame's, over the same door: every figure in them measured on the instrument's
# own bodies rather than on a shape written here. What made these worth writing
# down is that one of them points the other way -- a crossing out is two
# instructions fewer than a call and six nanoseconds more -- so a reader given
# only the count would move work across the boundary to save it. See D917.
crossed_says = re.search(as_written(
    r'a turn of that loop is \*\*([a-z-]+) instructions\*\* when it calls a'
    r' function of the program and \*\*([a-z-]+)\*\* when it crosses out\.'
    r' The dearer one runs ([a-z-]+) fewer'), REFERENCE)
some("the reference's paragraph about what a crossing runs", crossed_says)
ran_here = None
ran_out = None
ran_hop = None
ran_index = None
ran_ref = None
if crossed_says is not None and have_checked:
    ran_here = what_a_step_of(CROSSED.replace('WHICH_ONE', 'ofInside'))[0]
    ran_out = what_a_step_of(CROSSED.replace('WHICH_ONE', 'ofCrossing'))[0]
    if (ran_here is None or ran_out is None or
            in_figures(crossed_says.group(1)) != sum(ran_here.values()) or
            in_figures(crossed_says.group(2)) != sum(ran_out.values()) or
            in_figures(crossed_says.group(3)) != (sum(ran_here.values()) -
                                                  sum(ran_out.values()))):
        print("costs: the reference says a turn of that loop is %s "
              "instruction(s) calling a function of the program and %s "
              "crossing out, %s fewer, and a run says %s and %s"
              % (in_figures(crossed_says.group(1)),
                 in_figures(crossed_says.group(2)),
                 in_figures(crossed_says.group(3)),
                 None if ran_here is None else sum(ran_here.values()),
                 None if ran_out is None else sum(ran_out.values())))
        failed = 1

reading_says = re.search(as_written(
    r'a hop of that loop is \*\*([a-z-]+) instructions\*\*, an index read is'
    r' \*\*([a-z-]+)\*\* and a read through a reference is \*\*([a-z-]+)\*\*'
    r' — \*\*([a-z-]+)\*\* more than the hop for the index and ([a-z-]+) more'
    r' for the reference'), REFERENCE)
some("the reference's paragraph about what a read runs", reading_says)
if reading_says is not None and have_checked:
    ran_hop = what_a_step_of(READ_OF.replace(
        'WHICH_ONE', 'throughNothing(READS)'))[0]
    ran_index = what_a_step_of(READ_OF.replace(
        'WHICH_ONE', 'throughIndexes(items)'))[0]
    ran_ref = what_a_step_of(READ_OF.replace(
        'WHICH_ONE', 'throughReferences(world, where)'))[0]
    if (ran_hop is None or ran_index is None or ran_ref is None or
            in_figures(reading_says.group(1)) != sum(ran_hop.values()) or
            in_figures(reading_says.group(2)) != sum(ran_index.values()) or
            in_figures(reading_says.group(3)) != sum(ran_ref.values()) or
            in_figures(reading_says.group(4)) != (sum(ran_index.values()) -
                                                  sum(ran_hop.values())) or
            in_figures(reading_says.group(5)) != (sum(ran_ref.values()) -
                                                  sum(ran_hop.values()))):
        print("costs: the reference says a hop of that loop is %s "
              "instruction(s), an index read %s and a read through a "
              "reference %s, %s and %s more than the hop, and a run says %s, "
              "%s and %s"
              % (in_figures(reading_says.group(1)),
                 in_figures(reading_says.group(2)),
                 in_figures(reading_says.group(3)),
                 in_figures(reading_says.group(4)),
                 in_figures(reading_says.group(5)),
                 None if ran_hop is None else sum(ran_hop.values()),
                 None if ran_index is None else sum(ran_index.values()),
                 None if ran_ref is None else sum(ran_ref.values())))
        failed = 1

# And the fourth instrument, which is C because the host is what does the
# calling. This is the one the machine cannot count about itself the way it
# counts the other three: what a crossing in costs is mostly the frame the host
# writes, the arguments weighed on the way in and the answer weighed on the way
# back, and none of that is an instruction. So what is counted is the two
# things the machine can say about a call it was handed -- the instructions the
# program runs and the questions the build that checks itself asks -- and the
# distance between those and the duration is the answer. The program is
# `tools/inward.c`'s own, read out of the C string it keeps it in; the host is
# written here, because what has to vary is how many times it calls and that is
# the one thing that instrument fixes. See D918.
INWARD = some("the program `tools/inward.c` measures", ''.join(re.findall(
    r'"((?:[^"\\]|\\.)*)"',
    re.search(r'PROGRAM\s*=\s*((?:\s*"(?:[^"\\]|\\.)*"\s*)+);',
              open(os.path.join('tools', 'inward.c')).read()).group(1)
    )).encode().decode('unicode_escape'))
CALLING = """#include <stdio.h>
#include <stdlib.h>
#include "kest.h"

/* The program above declares one name the host provides, because the
   instrument it belongs to measures a hop out and back in as well as a call
   in. This host is only ever asked for `inside` and `many`, so what it binds
   answers with what it was handed: a program that declares a name nothing
   provides is refused before it runs, and a check that read nothing would say
   a crossing costs None. The answer goes back where the argument came from, so
   a function that leaves the frame alone is one that answered with what it was
   handed. */
static void back(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)frame;
    (void)runtime;
    (void)context;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        return 2;
    }
    long rounds = atol(argv[2]);
    long carried = atol(argv[4]);
    KestBuild *build = kest_build(argv[1], "lib/", stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    if (host != NULL) {
        kest_host_bind(host, "back", back, NULL);
    }
    KestRuntime *runtime = host == NULL ? NULL : kest_start(build, host, NULL);
    kest_host_free(host);
    if (runtime == NULL) {
        return 2;
    }
    int32_t which = kest_entry(runtime, argv[3]);
    if (which < 0) {
        return 2;
    }
    for (long i = 0; i < rounds; i++) {
        KestValue frame[2] = {{0}};
        if (argv[5][0] == 'r') {
            frame[0].real = (double)carried;
        } else {
            frame[0].integer = (int64_t)carried;
        }
        if (!kest_call(runtime, which, frame, 2)) {
            kest_report(runtime, stderr, KEST_FORM_TEXT);
            return 3;
        }
    }
    kest_runtime_free(runtime);
    kest_build_free(build);
    return 0;
}
"""


def a_host_that_calls():
    pieces = [one for one in sorted(glob.glob(
        os.path.join('build', 'debug', '*.o')))
        if os.path.basename(one) != 'main.o']
    if not pieces:
        return None
    calling = os.path.join(work, 'calling')
    with open(calling + '.c', 'w') as writing:
        writing.write(CALLING)
    with open(os.path.join(work, 'inward.kest'), 'w') as writing:
        writing.write(INWARD)
    built = subprocess.run(
        ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O0', '-g',
         '-fsanitize=address,undefined', '-Iinclude', '-o', calling,
         calling + '.c'] + pieces + ['-lm'],
        capture_output=True, text=True, stdin=subprocess.DEVNULL)
    return calling if built.returncode == 0 else None


def what_a_call_in_runs(calling, named, rounds, carried, member):
    ran = subprocess.run(
        [calling, os.path.join(work, 'inward.kest'), str(rounds), named,
         str(carried), member],
        capture_output=True, text=True, stdin=subprocess.DEVNULL,
        env=dict(os.environ, KEST_LIB='lib', KEST_DEEP='1'))
    if ran.returncode != 0:
        return None, None
    ran_added = 0
    asked_said = None
    for line in ran.stderr.splitlines():
        if line.startswith('ran '):
            ran_added += int(line.split()[2])
        if line.startswith('guards '):
            asked_said = int(line.split()[1])
    return (ran_added or None), asked_said


# One call in, and one turn of the loop the same program runs inside one call
# in. Neither is a difference of the other: a crossing in is a frame the host
# wrote, and a turn of a loop is a hop and a call the machine reached. Both are
# taken by subtracting two runs, so what the machine does either side of the
# work does not land in the number. The instrument's own words for the two.
def a_crossing_in(calling, named, member):
    over_what = []
    asked_over = []
    for how_far in (ENTITIES * 50, ENTITIES * 100):
        if named == 'inside':
            ran_over, asked = what_a_call_in_runs(
                calling, named, how_far, 1, member)
        else:
            ran_over, asked = what_a_call_in_runs(
                calling, named, 1, how_far, member)
        if ran_over is None or asked is None:
            return None, None
        over_what.append(ran_over)
        asked_over.append(asked)
    return ((over_what[1] - over_what[0]) // (ENTITIES * 50),
            (asked_over[1] - asked_over[0]) // (ENTITIES * 50))


inward_says = re.search(as_written(
    r'a crossing in runs \*\*([a-z-]+) instructions\*\* of the program and'
    r' \*\*([a-z-]+)\*\* of its questions, against \*\*([a-z-]+)\*\* and'
    r' \*\*([a-z-]+)\*\* for a turn of that loop'), REFERENCE)
some("the reference's paragraph about what a crossing in runs", inward_says)
ran_in = None
ran_turn = None
asked_in = None
asked_turn = None
if inward_says is not None and have_checked:
    calling_host = a_host_that_calls()
    if calling_host is not None:
        ran_in, asked_in = a_crossing_in(calling_host, 'inside', 'real')
        ran_turn, asked_turn = a_crossing_in(calling_host, 'many', 'whole')
    if (ran_in is None or ran_turn is None or
            in_figures(inward_says.group(1)) != ran_in or
            in_figures(inward_says.group(2)) != asked_in or
            in_figures(inward_says.group(3)) != ran_turn or
            in_figures(inward_says.group(4)) != asked_turn):
        print("costs: the reference says a crossing in runs %s instruction(s) "
              "and %s question(s) against %s and %s for a turn of that loop, "
              "and a run says %s, %s, %s and %s"
              % (in_figures(inward_says.group(1)),
                 in_figures(inward_says.group(2)),
                 in_figures(inward_says.group(3)),
                 in_figures(inward_says.group(4)),
                 ran_in, asked_in, ran_turn, asked_turn))
        failed = 1

# And the list of what all that is about, read out of the document rather than
# kept here. Four instruments, four paragraphs, four blocks above -- and the
# reference is already the list: each paragraph names its instrument and writes
# its figures in bold. So the two things a fifth instrument would need are held
# to each other here, and adding one is then a paragraph rather than a block.
#
# Which figures are held is not a list either. A figure this check compares is
# one some pattern above caught, and a figure written in bold is one whose match
# begins right after the two stars -- so the patterns say which of their own
# groups are figures, and nothing has to be written down twice. A bold figure
# added to a paragraph with nothing measuring it is then a gate that fails,
# which is the hole D915 left: every number in those paragraphs was held and
# nothing said the next one would be. See D919.
def bold_in(said_by):
    if said_by is None:
        return []
    return [said_by.group(which_at)
            for which_at in range(1, (said_by.re.groups or 0) + 1)
            if said_by.string[said_by.start(which_at) - 2:
                              said_by.start(which_at)] == '**']


RUNNING = ''.join(piece_of for piece_of in re.split(r'\n(?=## )', REFERENCE)
                  if piece_of.startswith('## What running costs'))
written_bold = some("the figures the reference writes in bold", re.findall(
    r'\*\*([a-z-]+)(?:\s+(?:instructions|questions))?\*\*', RUNNING))
held_bold = (bold_in(step_says) + bold_in(crossed_says) +
             bold_in(reading_says) + bold_in(inward_says))
if sorted(written_bold) != sorted(held_bold):
    print("costs: the reference writes %s in bold where it says what running "
          "costs and this holds %s of them to a run"
          % (sorted(written_bold), sorted(held_bold)))
    failed = 1

# And every instrument in `tools` named there with a figure beside it. An
# instrument is a thing the gate runs and expects a number from, and one whose
# paragraph quotes no count is a duration with nothing under it -- which is what
# all four of them were before D915.
# `fuzz.c` is not one. An instrument is a thing the gate takes a *number* from
# and the reference says what that number was measured over; the fuzzer answers
# with how many inputs ended in a program or a refusal, which is a count of
# what it did rather than a measurement of anything. Written down here rather
# than left out quietly, because a name missing from a list and a name written
# down as not belonging in it read the same from outside. See D984.
NOT_AN_INSTRUMENT = ('tools/fuzz.c',)
INSTRUMENTS = some("the instruments in `tools`", sorted(
    one for one in (glob.glob(os.path.join('tools', '*.kest')) +
                    glob.glob(os.path.join('tools', '*.c')))
    if one not in NOT_AN_INSTRUMENT))
told_of = {}
for piece_of in re.split(r'(?=`tools/)', RUNNING):
    named_of = re.match(r'`tools/([\w.]+)`', piece_of)
    if named_of is not None:
        stem_of = os.path.splitext(named_of.group(1))[0]
        told_of[stem_of] = told_of.get(stem_of, '') + piece_of
for one_of in INSTRUMENTS:
    stem_of = os.path.splitext(os.path.basename(one_of))[0]
    if '**' not in told_of.get(stem_of, ''):
        print("costs: `%s` is an instrument and what running costs says no "
              "figure about it, so its duration has nothing under it" % one_of)
        failed = 1

# A copy is its body, and its body is paid for when it is compiled. Compiling a
# twenty-line copy costs more over a one-line copy than checking one does --
# which is the shape held here. It was held at four times as much until D933
# gave the contract proof a node per copy and the tree is typed again for each:
# checking a copy is now most of what it costs, and the difference between a
# body of one line and one of twenty is a difference in both. The shape is the
# same and the multiple is not, so the multiple is gone rather than tuned.
shutil.rmtree(work, ignore_errors=True)
if (one_copy_costs is None or many_copies_costs is None or
        many_copies_costs <= one_copy_costs * 2 or
        flat is None or deep is None or flat_checked is None or
        deep_checked is None or
        deep - flat <= (deep_checked - flat_checked)):
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

# And what the reference says this compiler's own work costs, which is the four
# numbers this check has been measuring all along and printing in a line nobody
# reads unless something fails. The document quoted them from a run once and
# nothing compared them after: by the time anything did, the file had grown by
# two lines and compiling it had got a third cheaper, so every figure in that
# paragraph was wrong. The same shape D886 found in the one number before it and
# D914 found in the container table. The file is named from the one this check
# weighs rather than written out again, so a paragraph about another file is a
# paragraph about nothing. See D920.
COST_SAYS = re.search(as_written(
    r'For `' + re.escape(LIBRARY) + r'`, which is (\d+) lines: (\d+) bytes as'
    r' tokens, (\d+) as a tree, (\d+) checked and (\d+) compiled'), REFERENCE)
READ_SAYS = re.search(as_written(
    r'For `' + re.escape(LIBRARY) + r'` that is one file and (\d+) bytes,'
    r' against the (\d+) it costs to compile'), REFERENCE)
some("the reference's paragraph about what compiling costs", COST_SAYS)
some("the reference's paragraph about what that cost was paid for", READ_SAYS)
if COST_SAYS is not None and READ_SAYS is not None:
    lines_of = len(open(LIBRARY).read().splitlines())
    said_costs = [int(one_said) for one_said in
                  COST_SAYS.groups() + READ_SAYS.groups()]
    ran_costs = [lines_of, lexing, parsing, checking, compiling,
                 source_bytes, compiling]
    if said_costs != ran_costs:
        print("costs: the reference says `%s` is %s line(s) and %s bytes and "
              "costs %s as tokens, %s as a tree, %s checked and %s compiled, "
              "and a run says %s line(s), %s bytes, %s, %s, %s and %s"
              % ((LIBRARY,) + tuple(said_costs[:1] + said_costs[5:6] +
                                    said_costs[1:5]) +
                 tuple(ran_costs[:1] + ran_costs[5:6] + ran_costs[1:5])))
        failed = 1

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
          "that sit next to each other are taken in %u go(es), and a frame "
          "step is %s instruction(s) an entity and %s with its two helpers "
          "written out, reaching %s of the machine's %u instructions and "
          "answering %s question(s) about itself in the build that checks "
          "itself, against %s instruction(s) and %s question(s) for the "
          "frame the instrument walks, which is the one the reference "
          "writes out instruction by instruction, and a turn of a loop "
          "is %s instruction(s) calling a function of the program "
          "against %s crossing out, and a hop of one is %s, %s reading "
          "through an index and %s through a reference, and a crossing "
          "in from a host is %s instruction(s) and %s question(s) "
          "against %s and %s for a turn of the loop it calls — %u "
          "figure(s) in bold about %u instrument(s), every one of them "
          "held to a run — and one "
          "that promises "
          "`no.alloc` takes %u byte(s) of heap "
          "an entity against %u for one that makes a piece of text, %u for "
          "one that grows an array and %u for one that puts a pair in a "
          "table -- nought where it was told how many were coming, as for an "
          "array or a store that was -- and %u for one that adds to a store "
          "that was not, which "
          "is work rather than time and the same count "
          "anywhere, with the rest of it measured on the machine "
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
             with_sign, without_sign, together, turn_ran, runs,
             a_frame, by_hand, reaches, len(instruction_names),
             asked_of_itself,
             None if ran_it is None else sum(ran_it.values()), asked_it,
             None if ran_here is None else sum(ran_here.values()),
             None if ran_out is None else sum(ran_out.values()),
             None if ran_hop is None else sum(ran_hop.values()),
             None if ran_index is None else sum(ran_index.values()),
             None if ran_ref is None else sum(ran_ref.values()),
             ran_in, asked_in, ran_turn, asked_turn,
             len(written_bold), len(INSTRUMENTS),
             quiet_frame, text_frame, grown_frame,
             keyed_frame, stored_frame))
sys.exit(failed)
PY
