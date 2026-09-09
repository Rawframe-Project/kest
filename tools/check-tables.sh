#!/bin/sh
# Three lists have to stay in step: the token names beside the token kinds, the
# instruction names beside the opcodes, and the keywords the lexer holds beside
# the ones the reference prints. Nothing in C says so, and two of them have
# drifted once — a name printed for the wrong thing, and an operand read for an
# instruction that has none, which walked off the end of the code. The third is
# the list a reader is told is the whole of it.
set -u
exec python3 - "$@" <<'PY'
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

failed = 0


def table(path, pattern):
    text = open(path).read()
    found = re.search(pattern, text, re.S)
    if found is None:
        # A list that has moved is not a list that is in step, and a stack
        # trace says so in the one language nobody reading this speaks.
        print("%s: nothing here matches /%s/" % (path, pattern))
        raise SystemExit(1)
    return found.group(1)


def names(block, prefix):
    out = []
    for line in block.splitlines():
        line = line.strip()
        if line.startswith(prefix):
            out.append(line.split(',')[0].split('//')[0].strip())
    return out


# One group, so `findall` gives the strings themselves. Taking the first of
# each was taking the first letter, which nothing noticed while the only thing
# asked of this was how many there were.
def spelled(block):
    return re.findall(r'"((?:[^"\\]|\\.)*)"', block)


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

# The words a program may not use, beside the words the reference says they
# are. A keyword nobody is told about is a name somebody loses without being
# told why, and a word in that block that the lexer does not hold is a program
# refused for nothing.
held = sorted(spelled(table('src/lexer.c',
                           r'KEYWORDS\[\] = \{(.*?)\n\};')))
printed = sorted(
    table('docs/language.md',
          r'## Keywords\n\n```\n(.*?)```').split())
if held != printed:
    only_held = [w for w in held if w not in printed]
    only_printed = [w for w in printed if w not in held]
    if only_held:
        print("keywords: the lexer holds %s and the reference does not say so"
              % ", ".join("`%s`" % w for w in only_held))
    if only_printed:
        print("keywords: the reference says %s and the lexer does not hold it"
              % ", ".join("`%s`" % w for w in only_printed))
    failed = 1

# The names the language answers to on its own, in the three places that know
# them: what the checker asks about, what the compiler emits for, and the list
# a message about methods suggests from. Two of the three are twenty-odd calls
# each, so the list is not written anywhere as a list except in the third.
def words(path, pattern):
    return sorted(set(re.findall(pattern, open(path).read())))


checked = words('src/check.c', r'is_builtin\(checker, expr, name, "([a-z]+)"')
emitted = words('src/compile.c',
                r'builtin_named\(compiler, name, length, "([a-z]+)"')
suggested = sorted(set(spelled(table(
    'src/check.c', r'static const char \*const BUILTINS\[\] = \{(.*?)\n\};'))))
for what, one, two in (("the compiler", checked, emitted),
                       ("the suggestion", checked, suggested)):
    if one != two:
        missing = [w for w in one if w not in two]
        extra = [w for w in two if w not in one]
        if missing:
            print("builtins: %s does not know %s"
                  % (what, ", ".join("`%s`" % w for w in missing)))
        if extra:
            print("builtins: %s knows %s and the checker does not"
                  % (what, ", ".join("`%s`" % w for w in extra)))
        failed = 1

# The pipeline in `CLAUDE.md` is the map of the tree a reader is given, and it
# said what nothing in the tree said back: a module `str` that does not exist,
# no `kest` at all, and `diag` above the `mem` its own header includes. It is
# also the one place the rule about what may include what is written down.
listed = [line.split()[0] for line in table(
    'CLAUDE.md',
    r'Pipeline, in dependency order[^`]*```\n(.*?)```').splitlines()
    if line.strip()]
present = sorted(os.path.basename(path)[:-2] for path in glob.glob('src/*.c'))
if sorted(listed) != present:
    for name in listed:
        if name not in present:
            print("modules: the pipeline names `%s` and `src` has no such file"
                  % name)
    for name in present:
        if name not in listed:
            print("modules: `src/%s.c` is in the tree and not in the pipeline"
                  % name)
    failed = 1
else:
    place = {name: i for i, name in enumerate(listed)}
    for name in listed:
        for path in ('src/%s.c' % name, 'src/%s.h' % name,
                     'include/%s.h' % name):
            if not os.path.exists(path):
                continue
            for included in re.findall(r'#include "([a-z]+)\.h"',
                                       open(path).read()):
                if included != name and place[included] > place[name]:
                    print("modules: %s includes `%s`, which is below it"
                          % (path, included))
                    failed = 1

# What the language's own functions call the things they take is written twice:
# in the messages the checker raises, and in the reference a reader learns them
# from. A message that says `from` is only worth more than `this argument`
# because the reader has met `from` on the page.
reference = open('docs/language.md').read()
for called, listed_names in re.findall(
        r'\{"([a-z]+)", \{(.*?)\}\}',
        table('src/check.c', r'\} BUILTIN_TAKES\[\] = \{(.*?)\n\};')):
    in_source = spelled(listed_names)
    # The reference prints a short form and a long one for some of them, and
    # the long one is the whole of what it takes.
    forms = re.findall(r'`%s\(([a-z, ]*)\)`' % called, reference)
    if not forms:
        print("builtins: the reference never writes `%s(...)`" % called)
        failed = 1
        continue
    in_reference = [word.strip()
                    for word in max(forms, key=len).split(',') if word.strip()]
    if in_source != in_reference:
        print("builtins: the checker calls %s's %s and the reference calls "
              "them %s"
              % (called, ", ".join("`%s`" % w for w in in_source),
                 ", ".join("`%s`" % w for w in in_reference)))
        failed = 1

# The commands the command line answers to, in the two places that say which
# they are: what `main` compares the first argument against, and what `help`
# prints. A command that works and is not printed is one nobody finds, and one
# printed and not answered is a mistake in the first place a reader looks.
source = open('src/main.c').read()
answered = sorted(set(re.findall(r'strcmp\(argv\[1\], "([a-z]+)"\)', source)))
offered = sorted(set(re.findall(
    r'"  ([a-z]+)[ \\]',
    table('src/main.c', r'static void help\(FILE \*out\) \{(.*?)\n\}'))))
if answered != offered:
    for one in answered:
        if one not in offered:
            print("commands: `kest %s` runs and `kest help` does not say so"
                  % one)
            failed = 1
    for one in offered:
        if one not in answered:
            print("commands: `kest help` prints `%s` and nothing answers to it"
                  % one)
            failed = 1

# The numbers a program can run into, in the two places that say what they are:
# the compiler that enforces them and the table a reader is given. A number
# changed in one and not the other is a document that lies about what a program
# may hold, and there is no way to find that out by running anything.
# Every `MAX_` the compiler holds a program to, and the one the machine does:
# what the three files that check and compile a program have between them, and
# what `len` can count to, which is not a number the compiler can see coming.
# The command line's own is not one of these, because how many events a run
# makes is not a number written in a program. Neither is how deep the calls go,
# which is a host's to choose and is in `kest.h`; it is named here so that a
# value nobody taught this reader stops it rather than being passed over.
SPELLED = {'UINT16_MAX': 65535, 'INT32_MAX': 2147483647}
A_HOSTS_OWN = {'MAX_FRAMES'}
enforced = set()
for path in ('src/compile.c', 'src/check.c', 'src/types.c', 'src/vm.c'):
    for name, value in re.findall(r'#define (MAX_[A-Z]+)\s+(\S+)',
                                  open(path).read()):
        if name in A_HOSTS_OWN:
            continue
        if value in SPELLED:
            enforced.add(SPELLED[value])
        elif value.isdigit():
            enforced.add(int(value))
        else:
            print("limits: `%s` is %s and this does not know what that is"
                  % (name, value))
            failed = 1

printed = set(int(one) for one in re.findall(
    r'\n\| (\d+) \| ',
    table('docs/language.md',
          r'## What there is a most of(.*?)\n\n```')))
if enforced != printed:
    for one in sorted(enforced - printed):
        print("limits: the compiler holds a program to %u and the reference "
              "does not say so" % one)
    for one in sorted(printed - enforced):
        print("limits: the reference says %u and nothing holds a program to it"
              % one)
    failed = 1

# The escapes, in the places they are said: what a run accepts, what a run names
# when it meets one it does not know, and what the reference prints. The first
# is asked by asking — every printable character is written after a backslash
# and the answer says whether it is one — because reading the set out of the
# source is reading the same list a second time rather than a different one.
#
# Asked of a byte written on its own rather than of a piece of text, because
# that is where all of them are legal: a nought is an escape and text is the
# one place it may not go, since text ends at its first one.
accepted = set()
work = tempfile.mkdtemp()
try:
    probe = os.path.join(work, 'escape.kest')
    for code in range(0x21, 0x7f):
        one = chr(code)
        open(probe, 'w').write(
            "fn main() -> i32 {\n    let b = '\\%s'\n    return 0\n}\n" % one)
        ran = subprocess.run(['./kest', 'check', probe], capture_output=True,
                             text=True, stdin=subprocess.DEVNULL)
        if ran.returncode == 0:
            accepted.add(one)
    # And what it says about one it does not know, which is where a reader is
    # told what the set is. Which character that is comes from the answer
    # above, so this asks about one the compiler really does not know.
    unknown = sorted(set(chr(code) for code in range(0x61, 0x7b)) - accepted)
    open(probe, 'w').write(
        "fn main() -> i32 {\n    let b = '\\%s'\n    return 0\n}\n"
        % (unknown[0] if unknown else 'e'))
    told = subprocess.run(['./kest', 'check', probe], capture_output=True,
                          text=True, stdin=subprocess.DEVNULL)
    said = told.stdout + told.stderr
finally:
    shutil.rmtree(work, ignore_errors=True)

named = set(re.findall(r'\\(\S)', said.partition('known escapes are')[2]))
printed = set(re.findall(r'`\\(.)`',
                         table('docs/language.md',
                               r'The escapes are\n(.*?)\n\n')))
if accepted != named:
    print("escapes: a run takes %s and names %s"
          % (sorted(accepted), sorted(named)))
    failed = 1
if accepted != printed:
    print("escapes: a run takes %s and the reference prints %s"
          % (sorted(accepted), sorted(printed)))
    failed = 1

# Every file `CLAUDE.md` names is a file. It prints the layout of this tree —
# the modules, the checks, the two hosts, the one measurement — and a name that
# has moved leaves a paragraph describing something that is not there, which is
# worse than no paragraph because it reads like one that is true.
WHERE = ('', 'include', 'src', 'tools', 'examples', 'docs', 'lib/std')
for name in sorted(set(re.findall(r'`([A-Za-z0-9_./-]+\.(?:c|h|sh|kest|md|a))`',
                                  open('CLAUDE.md').read()))):
    if not any(os.path.exists(os.path.join(where, name)) for where in WHERE):
        print("CLAUDE.md: names `%s` and there is no such file" % name)
        failed = 1

# A check that is written and never run is no check, and one that is run and
# never named is one a reader does not know is there. Three lists say which
# checks this project makes: the files, what `CLAUDE.md` says, and what
# `check.sh` reaches for.
tools = sorted(os.path.basename(path) for path in glob.glob('tools/check-*.sh'))
named = sorted(set(re.findall(r'check-[a-z]+\.sh', open('CLAUDE.md').read())))
run = sorted(set(re.findall(r'run "[a-z]+" tools/(check-[a-z]+\.sh)',
                            open('tools/check.sh').read())))
for what, these in (("named in `CLAUDE.md`", named), ("run by `check.sh`", run)):
    for one in tools:
        if one not in these:
            print("checks: `%s` is in `tools` and is not %s" % (one, what))
            failed = 1
    for one in these:
        if one not in tools:
            print("checks: `%s` is %s and is not in `tools`" % (one, what))
            failed = 1

if not failed:
    print("%u escapes, "
          % len(accepted), end="")
    print("%u instructions, %u tokens, %u keywords, %u builtins, %u modules "
          "and %u checks are in step with their names"
          % (len(ops), len(toks), len(held), len(checked), len(listed),
             len(tools)))

sys.exit(failed)
PY
