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
set -u
exec python3 - "$@" <<'PY'
import os
import re
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

if not failed:
    print('every documented block parses: %u' % checked)
sys.exit(failed)
PY
