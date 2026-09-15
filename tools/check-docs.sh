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
import atexit
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

# One room for this run, handed back however this goes out. A check refuses in
# the middle — that is what it is for — and a room taken away on the last line
# is a room the refusing runs leave on the machine. The gate runs every check
# in a broken tree a hundred and twenty-one times over, so those are the runs
# there are most of.
room = tempfile.mkdtemp()
atexit.register(shutil.rmtree, room, ignore_errors=True)

DECLARES = ('module ', 'import ', 'const ', 'struct ', 'enum ', 'fn ',
            'extern fn ', 'flags ')


def split(body):
    """The declarations, and the statements that are not part of one."""
    declarations = []
    statements = []
    depth = 0
    inside = False
    # A blank line belongs to whatever it was written under. Between two
    # declarations it is part of them — the one form puts it there — and
    # sending it to the statements made a block that is written the way this
    # language is written look as though it was not.
    last = statements
    for one in body.splitlines():
        if depth == 0 and not inside:
            inside = one.startswith(DECLARES)
        if not one.strip() and depth == 0 and not inside:
            last.append(one)
            continue
        last = declarations if inside else statements
        last.append(one)
        depth += one.count('{') + one.count('(') + one.count('[')
        depth -= one.count('}') + one.count(')') + one.count(']')
        if depth <= 0:
            depth = 0
            inside = False
    return declarations, statements


failed = 0
# A pattern that stops matching finds nothing, and a document nothing is read
# out of is a document nothing is holding to anything. Every sweep here says
# how many it found, and none of them may find none: a fence written another
# way, a heading renamed, a message printed differently, and this would pass
# without reading a word.
def some(what, found):
    global failed
    if not found:
        print("%s: nothing in these documents is where this reads it from"
              % what)
        failed = 1
    return found


checked = 0
# Whether what is in this file is what the formatter would write, said by
# formatting it and comparing. A block is held to it after it is held to
# parsing, so a block that does not parse says that and not this.
def one_form(path):
    said = subprocess.run(['./kest', 'fmt', path], capture_output=True,
                          text=True)
    return said.returncode == 0 and said.stdout == open(path).read()


# What a block says when it names something the paragraph around it declared
# and this file does not: a name, a type, a module that was never imported, or
# an import that is not a file. A block saying one of those is a fragment, and
# what it says after that is whatever the checker made of an error.
QUOTED = ('K0306', 'K0301', 'K0344', 'K0701')
WRAPPER = 'this function returns nothing, so `return` takes no value'

quoting = 0
standing = 0
made_code = 0
said_it = 0
whole = 0
work = os.path.join(room, 'blocks')
os.mkdir(work)
one = os.path.join(work, 'one.kest')

heads = set()
for path in sys.argv[1:]:
    text = open(path).read()
    for match in re.finditer(r'```kest\n(.*?)```', text, re.S):
        at = text[:match.start()].count('\n') + 2
        declarations, statements = split(match.group(1))

        # Written the way the one form writes it, because what comes out of
        # this is held to being in the one form as well as to parsing: a blank
        # line between the declarations and the body they were written above,
        # and nothing on a line that holds nothing.
        while statements and not statements[0].strip():
            statements.pop(0)
        while statements and not statements[-1].strip():
            statements.pop()
        while declarations and not declarations[-1].strip():
            declarations.pop()
        wrapped = list(declarations)
        if statements and any(line.strip() for line in statements):
            if wrapped:
                wrapped.append('')
            wrapped.append('fn documented() {')
            wrapped += ['    ' + line if line.strip() else ''
                        for line in statements]
            wrapped.append('}')
        with open(one, 'w') as out:
            out.write('\n'.join(wrapped) + '\n')

        checked += 1
        done = subprocess.run(['./kest', 'parse', one], capture_output=True,
                              text=True)
        # What this block is made of, read off the tree rather than out of the
        # words: the head of every list `parse` writes is a kind of thing a
        # program can be, and an operator is written there under its own name.
        # See D446.
        heads.update(re.findall(r'\(([^\s()]+)', done.stdout))
        if done.returncode != 0:
            print('%s:%u: this block does not parse' % (path, at))
            for line in done.stderr.splitlines()[:6]:
                print('    ' + line)
            failed = 1

        # And the block is written the way this language is written. A
        # document that shows a form the formatter would rewrite is a document
        # a reader cannot copy out of: the reference is where somebody looks
        # to see what the language looks like. See D399.
        elif not one_form(one):
            print('%s:%u: this block is not in the one form' % (path, at))
            failed = 1

        # And a block that stands on its own is held to what the checker says
        # about it, not only to parsing. Most of them do not stand on their
        # own: a fragment names what the paragraph around it declared, and a
        # name or a type that is not here is what that looks like. Those are
        # left alone — everything after an unknown name is whatever the
        # checker made of an error, and holding a block to that would be
        # holding it to the shape of a cascade. The rest have to check.
        # See D400.
        said = subprocess.run(['./kest', 'check', one], capture_output=True,
                              text=True, stdin=subprocess.DEVNULL)
        told = said.stdout + said.stderr
        if any(code in told for code in QUOTED):
            quoting += 1
        else:
            # The one thing the wrapper itself can be wrong about: a fragment
            # that gives a value back was written inside something that does.
            left = [line for line in told.splitlines()
                    if line.startswith('error[') and WRAPPER not in line]
            if left:
                print('%s:%u: this block stands on its own and does not check'
                      % (path, at))
                for line in left[:3]:
                    print('    ' + line)
                failed = 1
            else:
                standing += 1
                # And what checks is compiled. Checking is one half of this
                # compiler and emitting is the other, and the two have
                # disagreed about what a program is before — that is what
                # `K0505` is for. A block the documents show and this compiler
                # cannot make is a block a reader would find out about after
                # typing it. Only one of these declares a `main`, and nothing
                # here needs one: what is asked for is the code, not a run.
                # See D401.
                made = subprocess.run(['./kest', 'emit', one],
                                      capture_output=True, text=True,
                                      stdin=subprocess.DEVNULL)
                if made.returncode != 0:
                    print('%s:%u: this block checks and does not compile'
                          % (path, at))
                    for line in (made.stdout + made.stderr).splitlines()[:3]:
                        print('    ' + line)
                    failed = 1
                else:
                    made_code += 1

        # And nothing calls a `print` this language has not got, which is a
        # thing this document says in one place and did in seven others. It is
        # the one name worth holding on its own: everything else a block calls
        # bare is either a builtin or something the prose beside it declares,
        # and this is neither.
        for line in match.group(1).splitlines():
            if re.search(r'(?<![\w.])print\s*\(', line):
                print('%s:%u: a block calls `print`, which this language has '
                      'not got' % (path, at))
                failed = 1

        # And a block with a `main` in it is a program rather than a piece of
        # one: everything it uses is in it or imported by it, so it is held to
        # compiling and not only to parsing. A fragment is not — what it leans
        # on is in the prose around it — and the difference is what it declares
        # rather than what somebody says about it.
        if re.search(r'\bfn main\(', match.group(1)) is None:
            continue
        whole += 1
        done = subprocess.run(['./kest', 'check', one], capture_output=True,
                              text=True, stdin=subprocess.DEVNULL)
        if done.returncode != 0:
            print('%s:%u: this block is a program and does not compile'
                  % (path, at))
            for line in (done.stdout + done.stderr).splitlines()[:6]:
                print('    ' + line)
            failed = 1
            continue

        # And it is run, and what is written under it is what it wrote. A
        # program shown in a document and never run is a program that compiles
        # and stops at its first line, and a reader finds that out by typing
        # it. What it says it says is the block fenced as nothing under it,
        # is the block under it fenced `text`, which is neither Kest nor
        # nothing: what a program wrote is not a program, and a block fenced
        # as nothing is held to not being Kest — which `hello` is. See D402.
        ran = subprocess.run(['./kest', 'run', one], capture_output=True,
                             text=True, stdin=subprocess.DEVNULL)
        if ran.returncode != 0 or ran.stderr:
            print('%s:%u: this program does not run' % (path, at))
            for line in (ran.stderr or 'it answered %d' % ran.returncode
                         ).splitlines()[:3]:
                print('    ' + line)
            failed = 1
            continue
        after = re.search(r'```(\w*)\n(.*?)```', text[match.end():], re.S)
        if after is None or after.group(1) != 'text':
            print('%s:%u: this program is run and nothing says what it wrote'
                  % (path, at))
            failed = 1
        elif after.group(2) != ran.stdout:
            print('%s:%u: this program wrote %r and under it is %r'
                  % (path, at, ran.stdout, after.group(2)))
            failed = 1
        else:
            said_it += 1

# And the blocks that do not say they are Kest. A fence with nothing after it
# is what a thing that is not a program is written in — a signature on its own,
# a message, what a command printed — and nothing reads one. So a block fenced
# that way that would pass as a `kest` block is a program nothing checks: it
# parses, so it is code, and it says it is not.
# What a fence says it holds. There are four words these documents fence with
# and nothing else, and a fence with anything else after it is one somebody
# meant to close and wrote a sentence on: `` ``` It is also why `` opens a
# block whose word is `It`, and everything from there to the next fence renders
# as code. Two of them were in here. See D536.
FENCED_AS = ('', 'kest', 'json', 'text', 'c')
for path in sys.argv[1:]:
    for at, line in enumerate(open(path).read().split('\n'), 1):
        if not line.startswith('```'):
            continue
        word = line[3:].strip()
        if word not in FENCED_AS:
            print('%s:%u: fences with `%s`, which is a fence that was meant to '
                  'close and did not' % (path, at, word))
            failed = 1

fenced = 0
for path in sys.argv[1:]:
    lines = open(path).read().split('\n')
    at = 0
    while at < len(lines):
        if not lines[at].startswith('```'):
            at += 1
            continue
        fence = lines[at][3:].strip()
        start = at + 1
        at = start
        while at < len(lines) and lines[at].strip() != '```':
            at += 1
        body = lines[start:at]
        at += 1
        if fence != '' or not any(line.strip() for line in body):
            continue
        fenced += 1
        declarations, statements = split('\n'.join(body))
        wrapped = list(declarations)
        if statements and any(line.strip() for line in statements):
            wrapped.append('fn documented() {')
            wrapped += ['    ' + line for line in statements]
            wrapped.append('}')
        with open(one, 'w') as out:
            out.write('\n'.join(wrapped) + '\n')
        done = subprocess.run(['./kest', 'parse', one], capture_output=True,
                              text=True, stdin=subprocess.DEVNULL)
        if done.returncode == 0:
            print('%s:%u: this block reads as Kest and is fenced without it, '
                  'so nothing checks it' % (path, start))
            failed = 1
# And the two kinds of number the reference prints. A count is the program's and
# the same anywhere; a measurement is the machine's, and a reader on another one
# needs to be told which of the two they are reading before they compare. The
# checks that say a machine's numbers say so in the sentence a reader reads, and
# this is the same rule for the document. See D691.
if sys.argv[1].endswith('language.md'):
    said_it = "measured on the machine this was written on" in open(
        sys.argv[1]).read()
    if not said_it:
        print("%s: prints numbers a machine gave it and does not say which of "
              "them are that machine's" % sys.argv[1])
        failed = 1

some("the blocks fenced as nothing", fenced)
# A block that stands on its own is the only kind this holds to checking, so a
# reading that finds none of them holds none of them.
some("the blocks that stand on their own", standing)

shutil.rmtree(work, ignore_errors=True)

LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
HOLE = re.compile(r'%[-#0-9.*+ ]*(?:hh|h|ll|l|z|j|t)?[a-zA-Z]')
SHOWN = re.compile(r'(?:error|warning)\[(K\d{4})\]: (.*?)\s*$')


def written(text):
    """Every string literal in a C file, with the ones C joins joined, and
    what stood between it and the one before it.

    Nothing but space between two of them is C joining them, which is how a
    message longer than a line is written. A colon between two of them is one
    message written as a choice — `close == end ? "not closed" : "empty"` is
    one call and two things it can say — and reading only the first is half of
    what that code says. See D443.
    """
    out = []
    at = 0
    for match in LITERAL.finditer(text):
        piece = (match.group(1).replace('\\"', '"').replace('\\n', '\n')
                 .replace('\\t', '\t').replace('\\\\', '\\'))
        between = text[at:match.start()].strip()
        if out and between == '':
            out[-1] = (out[-1][0] + piece, out[-1][1])
        else:
            out.append((piece, between))
        at = match.end()
    return out


# A code is a literal, and the message it is raised with is the literal after
# it: that is the shape of every call, whether it goes to `kest_diags_add` or
# through one of the wrappers that take a code and a format.
# Every operator this language has, shown in a program somebody can read. What
# the reference says about `^` and `~` is a sentence naming them; what it showed
# was nothing, so a reader looking for what one looks like found a list of names
# and no line of Kest. The three lists are the parser's own -- what binds how
# tightly, what may be assigned with, and what may stand in front of a value --
# and reading them is reading what a program may be written with. See D446.
kinds = re.search(r'typedef enum \{(.*?)\} KestTokenKind;',
                  open(os.path.join('src', 'lexer.h')).read(), re.S)
spellings = re.search(r'TOKEN_NAMES\[\] = \{(.*?)\n\};',
                      open(os.path.join('src', 'lexer.c')).read(), re.S)
parser = open(os.path.join('src', 'parser.c')).read()
if kinds is None or spellings is None:
    print('nothing in the tree is where the token names are read from')
    failed = 1
    spelt = {}
else:
    token_kinds = [one for one in re.findall(r'KEST_TOK_([A-Z_0-9]+)', kinds.group(1))]
    spellings_of = re.findall(r'"((?:[^"\\]|\\.)*)"',
                              spellings.group(1))
    spelt = dict(zip(token_kinds, spellings_of))


def under(what):
    """The token kinds one of the parser's own tables names."""
    body = re.search(what, parser, re.S)
    return set() if body is None else set(
        re.findall(r'KEST_TOK_([A-Z_0-9]+)', body.group(1)))


works = (under(r'static int binary_precedence\(KestTokenKind kind\) \{(.*?)\n\}')
         | under(r'static bool is_assignment\(KestTokenKind kind\) \{(.*?)\n\}')
         | under(r'static KestExpr \*parse_unary\(Parser \*parser\) \{(.*?)\n\}'))
if not works or not spelt:
    print('nothing here reads as the operators a program is written with')
    failed = 1
# And every other kind of thing a program can be made of. An operator is one of
# them; the tree's other heads are the rest -- a `defer`, an `index`, a set of
# bits, a `break`. They are read the same way, off the printer that writes them
# rather than off a list beside it: a head is a string that opens with a
# bracket, and the word after the bracket is what it is. See D447.
shapes = set()
for one in re.finditer(r'"((?:[^"\\]|\\.)*)"',
                       re.sub(r'//[^\n]*', '',
                              open(os.path.join('src', 'ast.c')).read())):
    if not one.group(1).startswith('('):
        continue
    head = re.split(r'[\s)\\]', one.group(1)[1:])[0]
    if head:
        shapes.add(head)
if not shapes:
    print('nothing here reads as the shapes a program is made of')
    failed = 1
for head in sorted(shapes):
    if head not in heads:
        print('%s: no block here holds a `%s`' % (sys.argv[1], head))
        failed = 1

operators = 0
for kind in sorted(works):
    spelling = spelt.get(kind, '').strip('`')
    if not spelling:
        continue
    operators += 1
    if spelling not in heads:
        print('%s: no block here is written with `%s`' % (sys.argv[1], spelling))
        failed = 1

some("the blocks of Kest the documents show", checked)
says = {}
for path in sorted(glob.glob('src/*.c')):
    pieces = written(open(path).read())
    for i, (piece, _) in enumerate(pieces):
        if not re.fullmatch(r'K\d{4}', piece) or i + 1 >= len(pieces):
            continue
        says.setdefault(piece, []).append(pieces[i + 1][0])
        # And the other arm, where the message is a choice between two.
        if i + 2 < len(pieces) and pieces[i + 2][1] == ':':
            says[piece].append(pieces[i + 2][0])


# And a code said from more than one file, which is written down once and
# named rather than repeated. The pattern above reads a call, and a call that
# passes two names has no literals in it, so this reads the names: `X_CODE`
# beside `X_SAYS`, defined together because they are one thing that happened.
for path in sorted(glob.glob('src/*.h')):
    defined = dict(re.findall(r'#define\s+(\w+)\s*(?:\\\n\s*)?"((?:[^"\\]|\\.)*)"',
                              open(path).read()))
    for name, value in defined.items():
        if not name.endswith('_CODE') or not re.fullmatch(r'K\d{4}', value):
            continue
        beside = defined.get(name[:-len('_CODE')] + '_SAYS')
        if beside is None:
            print('%s: `%s` is a code with no words beside it' % (path, name))
            failed = 1
        else:
            says.setdefault(value, []).append(beside)


def raised(words, form):
    """Whether a message printed in a document could have come from `form`."""
    pattern = ''
    at = 0
    for hole in HOLE.finditer(form):
        pattern += re.escape(form[at:hole.start()]) + '.*'
        at = hole.end()
    return re.fullmatch(pattern + re.escape(form[at:]), words, re.S) is not None


messages = 0
for path in sys.argv[1:]:
    for number, line in enumerate(open(path), 1):
        match = SHOWN.match(line.strip())
        if match is None:
            continue
        code, wording = match.groups()
        messages += 1
        if any(raised(wording, form) for form in says.get(code, [])):
            continue
        print('%s:%u: no run says this' % (path, number))
        print('    ' + line.strip())
        for form in says.get(code, []):
            print('    %s is raised with `%s`' % (code, form))
        if code not in says:
            print('    nothing raises %s' % code)
        failed = 1

some("the messages the documents print", messages)
some("the messages a run of this compiler says", says)

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


work = os.path.join(room, 'names')
os.mkdir(work)
names_written = set()
for name, body in (('whole.kest', WHOLE), ('ticking.kest', TICKING),
                   ('crowded.kest', CROWDED), ('broken.kest', BROKEN)):
    path = os.path.join(work, name)
    with open(path, 'w') as out:
        out.write(body)
    # `call` takes the name of a function as well, and `take` is the one every
    # one of these has nothing of except the crowded one, which has nine.
    for command in ('check', 'emit', 'run', 'fmt', 'lex', 'parse', 'tick',
                    'call'):
        command_line = ['./kest', command, path]
        if command == 'call':
            command_line.append('take')
        command_line.append('--json')
        done = subprocess.run(command_line, capture_output=True, text=True,
                              stdin=subprocess.DEVNULL)
        for line in done.stdout.splitlines():
            if line.strip():
                keys_of(json.loads(line), names_written)
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
            if name not in names_written:
                print('%s:%u: nothing writes `%s` into JSON' % (path, at, name))
                failed = 1

# And the other way round. A field a run writes and nothing shows is a field a
# tool finds by reading output rather than by being told, which is how a name
# gets read once and depended on for a year.
for name in sorted(names_written - printed):
    print('%s: `%s` is written into JSON and nothing shows it'
          % (sys.argv[1], name))
    failed = 1

some("the JSON the documents show", shown)
some("the JSON names a run writes", names_written)

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
decided = some("the decisions that are written", set(re.findall(
    r'^## (D\d+)', open('docs/decisions.md').read(), re.M)))
# The two documents this used to leave out, which is where the one dangling
# reference in the tree was. A decision names the ones it rests on and the
# worklog names the one each turn recorded, and both were held to nothing:
# `D279` was cited twice in the worklog for a rule that is `D403`, written a
# hundred turns later, and the log has no D278 or D279 at all. See D793.
#
# And the names these two write *about* rather than point at, which is a thing
# only a log does: a number it got wrong and is recording, and a number it never
# had and is saying so. A log that could not name its own mistakes would have to
# stop recording them. Each is named here with why, the way every list in this
# project that excuses something is.
WRITTEN_ABOUT = {
    "D912": "`D012` mistyped, quoted by the turn that found and fixed it",
    "D915": "a check's refusal naming it, quoted by the turn that fixed it",
    "D278": "a number the log never had, which D793 is about",
    "D279": "a number the log never had, cited once in error before D793",
}
# Every document there is, against the table in `CLAUDE.md` that says which
# there are. A document nothing names is a document no rule reaches and no
# check reads: `README.md` was one for the length of this tree, and said that
# structs did not run yet while the library was written in them. See D794.
DOCUMENTS = sorted(re.findall(r"^\| `([A-Za-z0-9_./-]+\.md)` \|",
                              open("CLAUDE.md").read(), re.M))
some("the documents `CLAUDE.md` names", DOCUMENTS)
there = sorted(where for where in
               glob.glob("*.md") + glob.glob("docs/*.md"))
for where in there:
    if where not in DOCUMENTS:
        print("%s: is a document and `CLAUDE.md` does not name it" % where)
        failed = 1
for where in DOCUMENTS:
    if where not in there:
        print("CLAUDE.md: names `%s` and there is no such document" % where)
        failed = 1

# And that the front page points at things that are there. It is the one
# document written for somebody who has read nothing else, so a link in it
# that goes nowhere is the first thing they meet.
for where in re.findall(r"\]\(([A-Za-z0-9_./-]+)\)", open("README.md").read()):
    if not os.path.exists(where):
        print("README.md: points at `%s` and there is no such file" % where)
        failed = 1

# Every check but the one whose contents are broken copies of the others. A
# hole that takes a decision away has to name one the log has not got, the same
# way one that takes a code away names one this compiler has not -- and the
# scan of codes one file over has left it out since it was written, for this
# reason. See D793.
for path in sorted(glob.glob('src/*.c') + glob.glob('src/*.h')
                   + glob.glob('include/*.h') + glob.glob('examples/*.kest')
                   + glob.glob('lib/std/*.kest')
                   + [where for where in glob.glob('tools/*.sh')
                      if not where.endswith('check-backstops.sh')]
                   + ['docs/language.md', 'docs/decisions.md',
                      'docs/worklog.md', 'CLAUDE.md']):
    for name in sorted(set(re.findall(r'\bD\d{3}\b', open(path).read()))):
        if name in WRITTEN_ABOUT:
            continue
        if name not in decided:
            print("%s: names `%s` and no decision is written under it"
                  % (path, name))
            failed = 1

# And what the engine prints for the rules the machine cannot refuse. Those are
# the ones a host has to keep for itself — a pointer carries no stamp — so what
# they are held by is a host doing each of them wrong on purpose and saying
# what happened. The reference quotes the lines; a run of the engine has to say
# them, or the list is a paragraph again.
kept = re.search(r'### What a host has to keep.*?prints what happened:\n\n'
                 r'```\n(.*?)```',
                 open('docs/language.md').read(), re.S)
if kept is None:
    print("docs/language.md: nothing here says what a host has to keep")
    failed = 1
else:
    shown_by = some("the lines the engine prints for a host's own rules",
                    [line for line in kept.group(1).split('\n') if line.strip()])
    if not os.path.exists('examples/embed'):
        print("docs/language.md: the engine is not built, so what it prints "
              "for a host's own rules is a list nothing reads")
        failed = 1
    else:
        ran = subprocess.run(['examples/embed'], capture_output=True,
                             text=True, stdin=subprocess.DEVNULL)
        for line in shown_by:
            if line not in ran.stdout:
                print("docs/language.md: the engine says nothing about `%s`"
                      % line.strip())
                failed = 1

# And every name the command line hands a program that no module of the library
# declares. A host is a list of bindings and this one is a host: what it
# provides beyond what `std` asks for is between it and the programs that ask,
# which is to say it is written in the reference or it is not written anywhere.
# It said three and there were eight.
provided = some("what the command line provides", sorted(set(re.findall(
    r'kest_host_bind\(host, "([A-Za-z0-9.]+)"', open('src/main.c').read()))))
asked = set()
for where in sorted(glob.glob('lib/std/*.kest')):
    asked |= set(re.findall(r'extern fn ([A-Za-z0-9.]+)\(', open(where).read()))
all_they_say = "".join(open(path).read() for path in sys.argv[1:])
for name in provided:
    if name in asked or ('`%s`' % name) in all_they_say:
        continue
    print("docs/language.md: the command line provides `%s` and no document "
          "says so" % name)
    failed = 1

# And every file of this tree these documents name is one that is there. A path
# that starts with one of this tree's own directories is a reader being sent
# somewhere; anything else is a program somebody is imagining — `x/y/a/b/c.kest`
# in a paragraph about where imports resolve from is not a file and was never
# meant to be one. `CLAUDE.md` is held to the same thing by `check-tables.sh`,
# which is where the layout of this tree is written down.
OURS = ('src/', 'tools/', 'docs/', 'examples/', 'lib/', 'include/')
pointed = 0
for path in sys.argv[1:]:
    for name in sorted(set(re.findall(
            r'`([A-Za-z0-9_./-]+\.(?:c|h|sh|kest|md|a))`', open(path).read()))):
        if not name.startswith(OURS):
            continue
        pointed += 1
        if not os.path.exists(name):
            print("%s: names `%s` and there is no such file" % (path, name))
            failed = 1
some("the files of this tree the documents name", pointed)

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
# Every example with a `main` in it, wherever it sits. The table's own sentence
# is that every example is a program that checks itself, so what belongs in it
# is the files that are programs — and a file under a directory of its own is a
# module somebody imports, which is named by whoever imports it and has nothing
# of its own to run. Read by looking rather than by where the file is: this
# looked in `examples` and not under it, so a program added a directory down
# was one the table did not have to name. See D538.
here = set()
modules = set()
for path in sorted(glob.glob('examples/**/*.kest', recursive=True)):
    if re.search(r'^fn main\(', open(path).read(), re.M):
        here.add(os.path.basename(path))
    else:
        modules.add(os.path.basename(path))
for name in sorted(here - listed):
    print("%s: `%s` is an example and the reference does not say what it runs"
          % (reference, name))
    failed = 1
for name in sorted(modules & listed):
    print("%s: `%s` has no `main`, so it is a module and not a rule that runs"
          % (reference, name))
    failed = 1
for name in sorted(listed - here):
    print("%s: `%s` is listed and is not in `examples`" % (reference, name))
    failed = 1

# And what the blocks call of the library. A block that imports `std.text` and
# calls `text.trimmed(t)` is a reader's next line of code, and nothing held it
# to the library having one: the block only has to parse, and a call to
# something that is not there parses like any other. A block that imports a
# module of its own is left alone, because `math` beside a program is a module
# the program wrote and not this one — which is why the block is read for what
# it imports rather than for what it calls.
called = 0
library = {os.path.basename(where)[:-len('.kest')]: open(where).read()
           for where in sorted(glob.glob('lib/std/*.kest'))}
some("the library the documents can call", library)
for path in sys.argv[1:]:
    for block in re.findall(r'```kest\n(.*?)```', open(path).read(), re.S):
        imported = set(re.findall(r'\bimport std\.([a-z]+)', block))
        for module, name in sorted(set(
                re.findall(r'\b([a-z]+)\.([a-z][A-Za-z0-9_]*)\s*\(', block))):
            if module not in imported or module not in library:
                continue
            called += 1
            if re.search(r'\nfn %s[\s<(]' % re.escape(name),
                         library[module]) is None:
                print("%s: a block calls `%s.%s` and `lib/std/%s.kest` has no "
                      "such function" % (path, module, name, module))
                failed = 1
some("what the documents call of the library", called)

# And the command line, which two documents describe: `help`, held to what
# `main` answers to, and this one, which writes the same commands and options
# in its own words. A command renamed in one of them leaves the two disagreeing
# with nothing to say which of them is the program — so what the documents name
# is held to what the command line does, which is the third side of the same
# triangle.
#
# An option here is `--word` or one letter after a dash, because `-inf` is a
# number this language writes and not something to type at a command line.
line = open('src/main.c').read()
answers = set(re.findall(r'strcmp\(argv\[1\], "([a-z]+)"\)', line))
reads = set(re.findall(r'strcmp\(argv\[[^\]]*\], "(--?[a-z][a-z-]*)"\)', line))
typed = 0
for path in sys.argv[1:]:
    what_it_says = open(path).read()
    for name in sorted(set(re.findall(r'`kest ([a-z]+)', what_it_says))):
        typed += 1
        if name not in answers:
            print("%s: writes `kest %s` and the command line does not answer "
                  "to it" % (path, name))
            failed = 1
    for flag in sorted(set(re.findall(r'`(--[a-z][a-z-]*|-[a-z])`', what_it_says))):
        typed += 1
        if flag not in reads:
            print("%s: writes `%s` and the command line does not read it"
                  % (path, flag))
            failed = 1
some("what the documents type at a command line", typed)

# Where the compiler looks for the library, against where somebody installing
# it is told to look. The loader tries four places in order and the README is
# the one page a person reads before they have a working `kest` — a place it
# does not name is a place nobody knows to put a library, and a place it names
# that the loader does not try is a person putting one somewhere it will never
# be found. Read out of the function rather than from a list beside it. See
# D829.
looking = open(os.path.join("src", "loader.c")).read()
looking = looking[looking.index("const char *kest_library_path"):]
looking = looking[:looking.index("\n}")]
places = set(re.findall(r'"%\.\*s([^"]+)"', looking))
places |= {"$" + one for one in re.findall(r'getenv\("([A-Z_]+)"\)', looking)}
if "KEST_LIB_DIR" in looking:
    places.add("the build it came from was told")
readme = open("README.md").read()
for place in some("the places the compiler looks for the library", places):
    if place not in readme:
        print("README.md: the compiler looks for the library at `%s` and this "
              "page does not say so" % place)
        failed = 1

if not failed:
    print('every documented block parses: %u, is in the one form, and checks '
          'and compiles where it stands on its own: %u of %u, the other %u '
          'naming what the words '
          'around them declared; of them the programs compile, run, and write '
          'what is written under them: %u of %u, and the %u fenced as nothing '
          'are not Kest; every message shown '
          'is one the '
          'compiler says: %u, every JSON name shown is one a run writes: %u, '
          'every command and option written is one there is: %u, and every '
          'library call shown is one there is: %u, and every file of this '
          'tree they name is there: %u, and every one of the %u operators a '
          'program is written with is written in one of them, and every one of '
          'the %u places the compiler looks for the library is named where '
          'somebody installing it reads'
          % (checked, made_code, standing, quoting, said_it, whole, fenced,
             messages, shown, typed, called, pointed, operators,
             len(places)))
sys.exit(failed)
PY
