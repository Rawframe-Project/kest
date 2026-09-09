#!/bin/sh
# The reference and the decisions describe a language, and every `kest` block
# in them is either a run of declarations, a run of statements, or a
# declaration followed by a use of it. Nothing said whether any of them was
# syntax this language has.
#
# The worklog is not held to this. It records what went wrong, which means it
# holds code the parser refuses on purpose, and a block that does not parse is
# sometimes the whole point of the entry. A thing that is not a program, like a
# signature on its own, is fenced without the word `kest`.
#
# Checking that they mean something would need the types around them, which a
# fragment does not carry. Checking that they parse needs nothing, and it
# catches the mistake documentation actually makes: showing a shape the parser
# would refuse.
#
# The third thing is the shape of what a tool reads. A `json` block in the
# reference is a promise that a run writes those names, and a name nobody
# writes is worse than none: a tool is built to read it and finds nothing.
#
# The other thing documentation shows is what the compiler says, and a message
# printed here is a promise that a run says it. One of them was invented: a
# code that means a number written without digits, over a message about an
# unknown function. So every `error[Kxxxx]` and `warning[Kxxxx]` line in these
# documents is held to a message that code is actually raised with.
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

DECLARES = ('module ', 'import ', 'const ', 'struct ', 'enum ', 'fn ',
            'extern fn ', 'flags ')


def split(body):
    """The declarations, and the statements that are not part of one."""
    declarations = []
    statements = []
    depth = 0
    inside = False
    for line in body.splitlines():
        if depth == 0 and not inside:
            inside = line.startswith(DECLARES)
        (declarations if inside else statements).append(line)
        depth += line.count('{') + line.count('(') + line.count('[')
        depth -= line.count('}') + line.count(')') + line.count(']')
        if depth <= 0:
            depth = 0
            inside = False
    return declarations, statements


failed = 0
checked = 0
work = tempfile.mkdtemp()
one = os.path.join(work, 'one.kest')

for path in sys.argv[1:]:
    text = open(path).read()
    for match in re.finditer(r'```kest\n(.*?)```', text, re.S):
        at = text[:match.start()].count('\n') + 2
        declarations, statements = split(match.group(1))

        written = list(declarations)
        if statements and any(line.strip() for line in statements):
            written.append('fn documented() {')
            written += ['    ' + line for line in statements]
            written.append('}')
        with open(one, 'w') as out:
            out.write('\n'.join(written) + '\n')

        checked += 1
        done = subprocess.run(['./kest', 'parse', one], capture_output=True,
                              text=True)
        if done.returncode != 0:
            print('%s:%u: this block does not parse' % (path, at))
            for line in done.stderr.splitlines()[:6]:
                print('    ' + line)
            failed = 1

shutil.rmtree(work, ignore_errors=True)

LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
HOLE = re.compile(r'%[-#0-9.*+ ]*(?:hh|h|ll|l|z|j|t)?[a-zA-Z]')
SHOWN = re.compile(r'(?:error|warning)\[(K\d{4})\]: (.*?)\s*$')


def written(text):
    """Every string literal in a C file, with the ones C joins joined."""
    out = []
    at = 0
    for match in LITERAL.finditer(text):
        piece = (match.group(1).replace('\\"', '"').replace('\\n', '\n')
                 .replace('\\t', '\t').replace('\\\\', '\\'))
        if out and text[at:match.start()].strip() == '':
            out[-1] += piece
        else:
            out.append(piece)
        at = match.end()
    return out


# A code is a literal, and the message it is raised with is the literal after
# it: that is the shape of every call, whether it goes to `kest_diags_add` or
# through one of the wrappers that take a code and a format.
says = {}
for path in sorted(glob.glob('src/*.c')):
    pieces = written(open(path).read())
    for i, piece in enumerate(pieces):
        if re.fullmatch(r'K\d{4}', piece) and i + 1 < len(pieces):
            says.setdefault(piece, []).append(pieces[i + 1])


def raised(shown, form):
    """Whether a message printed in a document could have come from `form`."""
    pattern = ''
    at = 0
    for hole in HOLE.finditer(form):
        pattern += re.escape(form[at:hole.start()]) + '.*'
        at = hole.end()
    return re.fullmatch(pattern + re.escape(form[at:]), shown, re.S) is not None


messages = 0
for path in sys.argv[1:]:
    for number, line in enumerate(open(path), 1):
        match = SHOWN.match(line.strip())
        if match is None:
            continue
        code, shown = match.groups()
        messages += 1
        if any(raised(shown, form) for form in says.get(code, [])):
            continue
        print('%s:%u: no run says this' % (path, number))
        print('    ' + line.strip())
        for form in says.get(code, []):
            print('    %s is raised with `%s`' % (code, form))
        if code not in says:
            print('    nothing raises %s' % code)
        failed = 1

# Every name a run of this compiler writes into JSON, from a program with
# something of each kind in it and a program with a mistake in it.
WHOLE = """module doc

// A comment, so that what a run says about one is a name this has seen.
struct Point {
    x: i32
    y: i32
}

const LIMIT: i32 = 3

// An enum and a set of bits, because a run says different names about each of
// the three kinds of shape and this is the file every one of them is read
// from.
enum Shape {
    Dot
    Line(i32)
}

flags State: u8 {
    Moving
    Hurt
}

extern fn Clock.now() -> i64

fn hurt(p: Point, amount: i32) -> i32 {
    return p.x - amount + i32(Clock.now())
}

fn main() -> i32 {
    let where = Shape.Line(2)
    let how = State.Moving
    return hurt(Point(3, 4), LIMIT) + u8(how) - 1 + match where {
        Dot -> 0
        Line(n) -> n - 2
    }
}
"""

# A handler, so that `tick` crosses into something and writes what it found.
# Nothing the host has to provide is called from it: the command line binds
# what it binds, and a handler that reaches for anything else does not run.
TICKING = """module doc

fn onEvents(events: [i32]) -> i32 {
    let total = 0
    for one in events {
        total += one
    }
    return total
}

fn onEvent(event: i32) -> i32 {
    return event + 1
}

fn main() -> i32 {
    return 0
}
"""

# A name that is several functions, so that a run has more places to point at
# than a diagnostic has room for. Nothing else in these programs does.
CROWDED = """module doc

fn take(a: i32) -> i32 {
    return 1
}

fn take(a: i64) -> i32 {
    return 2
}

fn take(a: f32) -> i32 {
    return 3
}

fn take(a: f64) -> i32 {
    return 4
}

fn take(a: u8) -> i32 {
    return 5
}

fn take(a: u16) -> i32 {
    return 6
}

fn take(a: u32) -> i32 {
    return 7
}

fn take(a: bool) -> i32 {
    return 8
}

fn take(a: text) -> i32 {
    return 9
}

fn main() -> i32 {
    return take(0) - 1
}
"""

BROKEN = """module doc

// Ten cases and a diagnostic with room for eight of them, so that a run says
// how many places it had no room for.
enum Many {
    One
    Two
    Three
    Four
    Five
    Six
    Seven
    Eight
    Nine
    Ten
}

fn named() -> Many {
    return Many.Zzzzzz
}

fn hurt(who: i32, amount: i32) -> i32 {
    return who - amount
}

fn main() -> i32 {
    return hurt(3)
}
"""


def keys_of(held, into):
    if isinstance(held, dict):
        for name, value in held.items():
            into.add(name)
            keys_of(value, into)
    elif isinstance(held, list):
        for value in held:
            keys_of(value, into)
    return into


work = tempfile.mkdtemp()
written = set()
for name, body in (('whole.kest', WHOLE), ('ticking.kest', TICKING),
                   ('crowded.kest', CROWDED), ('broken.kest', BROKEN)):
    path = os.path.join(work, name)
    with open(path, 'w') as out:
        out.write(body)
    # `call` takes the name of a function as well, and `take` is the one every
    # one of these has nothing of except the crowded one, which has nine.
    for command in ('check', 'emit', 'run', 'fmt', 'lex', 'parse', 'tick',
                    'call'):
        asked = ['./kest', command, path]
        if command == 'call':
            asked.append('take')
        asked.append('--json')
        done = subprocess.run(asked, capture_output=True, text=True,
                              stdin=subprocess.DEVNULL)
        for line in done.stdout.splitlines():
            if line.strip():
                keys_of(json.loads(line), written)
shutil.rmtree(work, ignore_errors=True)

shown = 0
printed = set()
for path in sys.argv[1:]:
    text = open(path).read()
    for match in re.finditer(r'```json\n(.*?)```', text, re.S):
        at = text[:match.start()].count('\n') + 2
        held = json.loads(match.group(1))
        shown += 1
        for name in sorted(keys_of(held, set())):
            printed.add(name)
            if name not in written:
                print('%s:%u: nothing writes `%s` into JSON' % (path, at, name))
                failed = 1

# And the other way round. A field a run writes and nothing shows is a field a
# tool finds by reading output rather than by being told, which is how a name
# gets read once and depended on for a year.
for name in sorted(written - printed):
    print('%s: `%s` is written into JSON and nothing shows it'
          % (sys.argv[1], name))
    failed = 1

# Nothing in the decisions is edited, so an entry that is no longer what this
# project does reads exactly like one that is. What tells them apart is the
# list at the top, and what holds the list is this: a decision whose body says
# it supersedes another has to be named there as the one that replaced it.
decisions = open('docs/decisions.md').read()
replaced = set(re.findall(r'\n\| (D\d+) \| (D\d+) \|', decisions))
for was, now in sorted(replaced):
    for name in (was, now):
        if ('\n## %s' % name) not in decisions:
            print("docs/decisions.md: the list at the top names `%s` and no "
                  "decision is written under it" % name)
            failed = 1
# And one word for it. A decision that says it replaces another in some other
# words is one this cannot pair with the list, so the word is asked for by
# name: `supersedes`. What is caught here is the near miss, which is the one a
# writer makes.
ANOTHER_WAY = (r'replaces D\d+', r'replacing D\d+', r'undoes D\d+',
               r'overrides D\d+', r'in place of D\d+', r'instead of D\d+')
for entry in decisions.split('\n## ')[1:]:
    named = re.match(r'(D\d+)', entry)
    if named is None:
        continue
    for pattern in ANOTHER_WAY:
        said = re.search(pattern, entry)
        if said is not None and 'supersed' not in entry:
            print("docs/decisions.md: `%s` says `%s`; the word this reads is "
                  "`supersedes`" % (named.group(1), said.group(0)))
            failed = 1

for entry in decisions.split('\n## ')[1:]:
    named = re.match(r'(D\d+)', entry)
    if named is None or 'supersed' not in entry:
        continue
    if named.group(1) not in {now for _, now in replaced}:
        print("docs/decisions.md: `%s` says it supersedes something and the "
              "list at the top does not say so" % named.group(1))
        failed = 1

# What an entry in the worklog holds. Every one of them says what was run,
# because an entry that says a thing was done and not what said so is a claim;
# and the last one says what is next, because that line is what the next turn
# reads. The older ones without a `**Next:**` are what the file looked like
# before the loop had one, and are left alone.
work = open('docs/worklog.md').read()
entries = re.split(r'\n## ', work)[1:]
if not entries:
    print("docs/worklog.md: nothing here is an entry")
    failed = 1
for entry in entries:
    if '**Runs:**' not in entry:
        print("docs/worklog.md: `%s` does not say what was run"
              % entry.split('\n')[0])
        failed = 1
if entries and '**Next:**' not in entries[-1]:
    print("docs/worklog.md: the last entry does not say what is next")
    failed = 1

# A decision named where somebody would chase it has to be one that was made.
# `D193` in a comment is a promise that `docs/decisions.md` says something
# under that number, and a wrong digit is a reader sent nowhere.
decided = set(re.findall(r'^## (D\d+)', open('docs/decisions.md').read(),
                         re.M))
for path in sorted(glob.glob('src/*.c') + glob.glob('src/*.h')
                   + glob.glob('include/*.h') + glob.glob('examples/*.kest')
                   + glob.glob('lib/std/*.kest') + glob.glob('tools/*.sh')
                   + ['docs/language.md', 'CLAUDE.md']):
    for name in sorted(set(re.findall(r'\bD\d{3}\b', open(path).read()))):
        if name not in decided:
            print("%s: names `%s` and no decision is written under it"
                  % (path, name))
            failed = 1

# Every example is named where a reader looks for one, and every name there is
# a file. The list is what makes a rule a thing to run rather than a paragraph
# to believe, and a list of files goes stale the day somebody adds one.
reference = 'docs/language.md'
where = re.search(r'## Where each rule is run(.*?)\n## ',
                  open(reference).read(), re.S)
listed = set() if where is None else set(
    re.findall(r'\| `([a-z]+\.kest)`', where.group(1)))
if where is None:
    print("%s: nothing here says where each rule is run" % reference)
    failed = 1
here = {os.path.basename(path) for path in glob.glob('examples/*.kest')}
for name in sorted(here - listed):
    print("%s: `%s` is an example and the reference does not say what it runs"
          % (reference, name))
    failed = 1
for name in sorted(listed - here):
    print("%s: `%s` is listed and is not in `examples`" % (reference, name))
    failed = 1

if not failed:
    print('every documented block parses: %u, every message shown is one the '
          'compiler says: %u, and every JSON name shown is one a run writes: '
          '%u' % (checked, messages, shown))
sys.exit(failed)
PY
