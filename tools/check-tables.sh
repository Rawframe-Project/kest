#!/bin/sh
# Two arrays in two files have to stay in step: the token names beside the
# token kinds, and the instruction names beside the opcodes. Nothing in C says
# so, and both have drifted once — a name printed for the wrong thing, and an
# operand read for an instruction that has none, which walked off the end of
# the code.
set -u
exec python3 - "$@" <<'PY'
import re
import sys

failed = 0


def table(path, pattern):
    text = open(path).read()
    return re.search(pattern, text, re.S).group(1)


def names(block, prefix):
    out = []
    for line in block.splitlines():
        line = line.strip()
        if line.startswith(prefix):
            out.append(line.split(',')[0].split('//')[0].strip())
    return out


def spelled(block):
    return [m[0] for m in re.findall(r'"((?:[^"\\]|\\.)*)"', block)]


def report(what, kinds, written, spell):
    global failed
    if len(kinds) != len(written):
        print("%s: %u kinds and %u names" % (what, len(kinds), len(written)))
        failed = 1
        return
    for i, (kind, name) in enumerate(zip(kinds, written)):
        if spell(kind) != bare(name):
            print("%s: %u is %s and is called %s" % (what, i, kind, name))
            failed = 1
            return


ops = names(table('src/value.h', r'typedef enum \{(.*?)\} KestOp;'), 'KEST_OP_')
written = [m[0] for m in re.findall(
    r'\{"((?:[^"\\]|\\.)*)",\s*(\w+)\}',
    table('src/value.c', r'INSTRUCTIONS\[\] = \{(.*?)\n\};'))]
# `load.n` is spelled for a reader and `KEST_OP_LOADN` for a compiler, so the
# marks between the words are not part of the comparison.
def bare(text):
    return re.sub(r'[^a-z0-9]', '', text.lower())


report("instructions", ops, written, lambda k: bare(k[len('KEST_OP_'):]))

toks = names(table('src/lexer.h', r'typedef enum \{(.*?)\} KestTokenKind;'),
             'KEST_TOK_')
spellings = spelled(table('src/lexer.c', r'TOKEN_NAMES\[\] = \{(.*?)\n\};'))
if len(toks) != len(spellings):
    print("tokens: %u kinds and %u names" % (len(toks), len(spellings)))
    failed = 1

if not failed:
    print("%u instructions and %u tokens are in step with their names"
          % (len(ops), len(toks)))

sys.exit(failed)
PY
