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


# A pattern that stops matching finds nothing, and nothing agrees with
# everything: two empty lists are in step with each other and with nobody, and
# a loop over none of them checks none of it. Every list this reads out of the
# source is read through here, so a table that moved or was written differently
# is a check that says so rather than a check that passes.
def some(what, found):
    global failed
    if not found:
        print("%s: nothing in the source is where this reads it from" % what)
        failed = 1
    return found


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


ops = some("instructions", names(
    table('src/value.h', r'typedef enum \{(.*?)\} KestOp;'), 'KEST_OP_'))
written = some("instruction names", [m[0] for m in re.findall(
    r'\{"((?:[^"\\]|\\.)*)",\s*(\w+)\}',
    table('src/value.c', r'INSTRUCTIONS\[\] = \{(.*?)\n\};'))])
# `load.n` is spelled for a reader and `KEST_OP_LOADN` for a compiler, so the
# marks between the words are not part of the comparison.
def bare(text):
    return re.sub(r'[^a-z0-9]', '', text.lower())


report("instructions", ops, written, lambda k: bare(k[len('KEST_OP_'):]))

toks = some("token kinds", names(
    table('src/lexer.h', r'typedef enum \{(.*?)\} KestTokenKind;'),
    'KEST_TOK_'))
spellings = some("token names", spelled(
    table('src/lexer.c', r'TOKEN_NAMES\[\] = \{(.*?)\n\};')))
if len(toks) != len(spellings):
    print("tokens: %u kinds and %u names" % (len(toks), len(spellings)))
    failed = 1

# The words a program may not use, beside the words the reference says they
# are. A keyword nobody is told about is a name somebody loses without being
# told why, and a word in that block that the lexer does not hold is a program
# refused for nothing.
held = some("keywords", sorted(spelled(table(
    'src/lexer.c', r'KEYWORDS\[\] = \{(.*?)\n\};'))))
printed = some("the keywords the reference prints", sorted(
    table('docs/language.md',
          r'## Keywords\n\n```\n(.*?)```').split()))
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


checked = some("the builtins the checker asks about",
               words('src/check.c',
                     r'is_builtin\(checker, expr, name, "([a-z]+)"'))
emitted = some("the builtins the compiler emits for",
               words('src/compile.c',
                     r'builtin_named\(compiler, name, length, "([a-z]+)"'))
suggested = some("the builtins a message suggests from", sorted(set(spelled(
    table('src/check.c',
          r'static const char \*const BUILTINS\[\] = \{(.*?)\n\};')))))
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

# What the proof of a `no.alloc` promise knows about each of them. It has an
# opinion per builtin — a reason it reaches the heap, or nothing — and a
# builtin it has never heard of is one it says nothing about: the promise is
# then broken with no line to name, and what catches it is the proof that
# reads the emitted code, which calls it a fault in the compiler when it is
# the program's own mistake.
promised = some("what the promise's proof knows about a builtin",
                sorted(set(re.findall(
                    r'\{"([a-z]+)", (?:NULL|")',
                    table('src/contract.c',
                          r'\} REACHES\[\] = \{(.*?)\n            \};')))))
if promised != checked:
    missing = [w for w in checked if w not in promised]
    extra = [w for w in promised if w not in checked]
    if missing:
        print("builtins: the promise's proof has no opinion about %s"
              % ", ".join("`%s`" % w for w in missing))
    if extra:
        print("builtins: the promise's proof knows %s and the checker does not"
              % ", ".join("`%s`" % w for w in extra))
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
for called, listed_names in some("what a builtin calls what it takes",
                                 re.findall(
        r'\{"([a-z]+)", \{(.*?)\}\}',
        table('src/check.c', r'\} BUILTIN_TAKES\[\] = \{(.*?)\n\};'))):
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
answered = some("the commands `main` answers to", sorted(set(
    re.findall(r'strcmp\(argv\[1\], "([a-z]+)"\)', source))))
offered = some("the commands `help` prints", sorted(set(re.findall(
    r'"  ([a-z]+)[ \\]',
    table('src/main.c',
          r'static void help\(FILE \*out\) \{(.*?)\n\}')))))
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
enforced = set()  # filled below, and held to being filled
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

enforced = some("the numbers the compiler holds a program to", enforced)
printed = some("the numbers the reference prints", set(int(one) for one in
    re.findall(r'\n\| (\d+) \| ',
               table('docs/language.md',
                     r'## What there is a most of(.*?)\n\n```'))))
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

named = some("the escapes a run names", set(re.findall(
    r'\\(\S)', said.partition('known escapes are')[2])))
printed = some("the escapes the reference prints", set(re.findall(
    r'`\\(.)`',
    table('docs/language.md', r'The escapes are\n(.*?)\n\n'))))
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

# And what the gate does itself, which is the half of it that is not a tool: a
# line deleted from the middle of `check.sh` is a check that no longer happens,
# and a run with one fewer line in it reads exactly like the day before. What
# it says for itself is held to what `CLAUDE.md` says it does.
does = some("what `check.sh` says for itself", sorted(set(re.findall(
    r'\n\s*say "([a-z]+)"', open('tools/check.sh').read())) - {'$what'}))
told = some("what `CLAUDE.md` says the gate does", sorted(set(
    line.split()[0] for line in table(
        'CLAUDE.md',
        r'What the gate does itself.*?```\n(.*?)```').splitlines()
    if line and not line.startswith(' '))))
if does != told:
    for one in does:
        if one not in told:
            print("checks: `check.sh` says `%s` and `CLAUDE.md` does not say "
                  "it does" % one)
            failed = 1
    for one in told:
        if one not in does:
            print("checks: `CLAUDE.md` says the gate does `%s` and nothing in "
                  "it says so" % one)
            failed = 1

# The `Makefile`, which is the file nothing here has ever read. What it says is
# what "it passes" means, what a reader is told to type, and what is left on a
# machine afterwards — and a line taken out of it is the same silence as a line
# taken out of the gate.
make = open('Makefile').read()
targets = some("the targets the `Makefile` has", set(
    re.findall(r'^([A-Za-z][A-Za-z0-9_-]*):', make, re.M)))

# Every `make something` a reader is told to type is something to type. The
# words after `make` in a sentence are not all targets — `make one` and `make
# true` are English — so what is held is the ones that name a target of this
# kind: a word this file also has a rule for, or a word nothing here has, which
# is the mistake.
for asked in sorted(set(re.findall(r'`make ([a-z][a-z-]*)`',
                                   open('CLAUDE.md').read()))):
    if asked not in targets:
        print("CLAUDE.md: says to run `make %s` and the `Makefile` has no such "
              "target" % asked)
        failed = 1


def rule(name):
    found = re.search(r'\n%s:[^\n]*\n((?:\t[^\n]*\n)+)' % name, make)
    return '' if found is None else found.group(1)


# What a build leaves behind, cleaned. The gate builds four things and asks
# each of them whether it answers; a fifth added and not cleaned is rubbish a
# reader finds in a tree they thought was clean.
cleaned = rule('clean')
for built in some("what the gate builds", re.findall(
        r'for built in ([^;\n]*); do', open('tools/check.sh').read())):
    for one in built.split():
        if one.lstrip('./') not in cleaned:
            print("Makefile: `%s` is built and `clean` does not remove it"
                  % one.lstrip('./'))
            failed = 1

# And what an install leaves on a machine, removed. A file copied somewhere and
# never removed is this project's rubbish in somebody else's tree.
put = re.findall(r'\tcp [^\n]* (\$\(DESTDIR\)[^\n]+)', rule('install'))
takes = rule('uninstall')
for where in some("what an install puts on a machine", put):
    if not any(where.startswith(gone) or gone.startswith(where.rsplit('/', 1)[0])
               for gone in re.findall(r'\trm -[rf]+ ([^\n]+)', takes)):
        print("Makefile: `install` puts `%s` where `uninstall` leaves it"
              % where)
        failed = 1

# And the order of it, which is the one thing about the gate that is not a
# list. Everything below the build uses what the build made, and a check that
# runs before it would be asking a binary that is not there — which for a probe
# that passes when a command fails is a pass. So the first thing in that file
# that reaches for what was built comes after the line that says it was.
gate = open('tools/check.sh').read().splitlines()
builds = next((at for at, line in enumerate(gate)
               if line.startswith('if ! make')), None)
reaches = next((at for at, line in enumerate(gate)
                if './kest' in line and not line.lstrip().startswith('#')),
               None)
if builds is None or reaches is None:
    print("checks: `check.sh` does not build, or never reaches for what it "
          "built")
    failed = 1
elif reaches < builds:
    print("checks: `check.sh` reaches for what it built on line %u and builds "
          "it on line %u" % (reaches + 1, builds + 1))
    failed = 1

# A check that is written and never run is no check, and one that is run and
# never named is one a reader does not know is there. Three lists say which
# checks this project makes: the files, what `CLAUDE.md` says, and what
# `check.sh` reaches for.
tools = some("the checks in `tools`", sorted(
    os.path.basename(path) for path in glob.glob('tools/check-*.sh')))
named = some("the checks `CLAUDE.md` names", sorted(set(
    re.findall(r'check-[a-z]+\.sh', open('CLAUDE.md').read()))))
run = some("the checks `check.sh` runs", sorted(set(
    re.findall(r'ask "[a-z]+" tools/(check-[a-z]+\.sh)',
               open('tools/check.sh').read()))))
# And the shape of a check, which is the thing a tenth one would copy from
# whichever it was written beside. Nothing here says what a check is, so this
# does: it runs as a shell script, it stops on a name it never set, it writes
# where nothing else writes, and it takes away what it wrote. The one about
# `/tmp` is the one this project has already got wrong — two checks writing to
# one fixed name is a gate that failed one run in six for no reason anybody
# could see.
for check in tools:
    where = os.path.join('tools', check)
    written = open(where).read()
    if not os.access(where, os.X_OK):
        print("%s: is a check and is not something to run" % where)
        failed = 1
    if not written.startswith('#!/bin/sh\n'):
        print("%s: does not say what runs it" % where)
        failed = 1
    if '\nset -u\n' not in written:
        print("%s: does not stop on a name nobody set" % where)
        failed = 1
    # Except in the one whose contents are quotations of the others: it holds
    # broken copies of every check here on purpose, so a fixed name written in
    # it is a fixed name it is asking about rather than one it writes to.
    if check != 'check-backstops.sh':
        for fixed in re.findall(r'["\']/tmp/[^"\']*', written):
            print("%s: writes to `%s`, which is a name another run has too"
                  % (where, fixed[1:]))
            failed = 1
    makes = len(re.findall(r'mktemp -d|mkdtemp\(\)', written))
    takes = len(re.findall(r"trap 'rm -rf|rmtree", written))
    if makes > 0 and takes == 0:
        print("%s: makes somewhere to work and does not take it away" % where)
        failed = 1

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
