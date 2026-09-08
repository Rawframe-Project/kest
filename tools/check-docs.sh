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
# The other thing documentation shows is what the compiler says, and a message
# printed here is a promise that a run says it. One of them was invented: a
# code that means a number written without digits, over a message about an
# unknown function. So every `error[Kxxxx]` and `warning[Kxxxx]` line in these
# documents is held to a message that code is actually raised with.
set -u
exec python3 - "$@" <<'PY'
import glob
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

if not failed:
    print('every documented block parses: %u, and every message shown is one '
          'the compiler says: %u' % (checked, messages))
sys.exit(failed)
PY
