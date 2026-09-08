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
making = re.findall(r'\nfn ([a-zA-Z]+)\(([^)]*)\) -> text', source)
if not making:
    print("costs: nothing in %s makes text, which cannot be right" % LIBRARY)
    sys.exit(1)

host = open(HOST).read()
failed = 0
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
    written = [one.split(':') for one in params.split(',') if one.strip()]
    called = [one[0].strip() for one in written]
    takes = [one[1].strip() for one in written]
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
    'io': """import std.io

fn work(n: i32) -> i32 {
    for i in 0..n {
        io.write("")
    }
    return n
}
""",
}

proved = 0
driven = 0
work = tempfile.mkdtemp()
try:
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

if not failed:
    print("what the library costs grows the way it should: %u askings of the "
          "text it makes, %u left to the host, %u modules in a loop, %u proved "
          "by `no.alloc`"
          % (asked, len(left_to_the_host), driven, proved))
sys.exit(failed)
PY
