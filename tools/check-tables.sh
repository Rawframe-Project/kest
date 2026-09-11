#!/bin/sh
# Three lists have to stay in step: the token names beside the token kinds, the
# instruction names beside the opcodes, and the keywords the lexer holds beside
# the ones the reference prints. Nothing in C says so, and two of them have
# drifted once — a name printed for the wrong thing, and an operand read for an
# instruction that has none, which walked off the end of the code. The third is
# the list a reader is told is the whole of it.
set -u
exec python3 - "$@" <<'PY'
import ast
import glob
import os
import re
import shutil
import textwrap
import subprocess
import sys
import tempfile

failed = 0
pythons = 0
shells = 0


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


# What a value is made of, where that can be told from the words: a number, a
# piece of text, a list, a set, a table. `None` where it cannot — what a
# function gives back is its own business, and two names for two answers of an
# unknown kind is not something to complain about.
MADE_BY = {"set": "set", "dict": "dict", "list": "list", "int": "int",
           "len": "int", "str": "str", "sorted": "list", "open": "file",
           "tuple": "tuple", "bool": "bool", "float": "float"}
# What the ones written with a dot in front give back. `json.loads` is left
# out on purpose: what comes back is whatever the JSON held.
MADE_BY_DOTTED = {"subprocess.run": "run", "re.compile": "pattern",
                  "re.findall": "list", "os.path.join": "str"}
# And what a method gives back, where the name says it whatever it was called
# on. `get` is not one of these: what a table holds is the table's business.
MADE_BY_METHOD = {"read": "str", "split": "list", "splitlines": "list",
                  "strip": "str", "rstrip": "str", "lstrip": "str",
                  "join": "str", "lower": "str", "upper": "str",
                  "groups": "tuple"}


def whole_name(node):
    """`os.path.join` out of the three pieces it is written in."""
    pieces = []
    while isinstance(node, ast.Attribute):
        pieces.append(node.attr)
        node = node.value
    if not isinstance(node, ast.Name):
        return None
    pieces.append(node.id)
    return ".".join(reversed(pieces))


def made_of(node):
    if isinstance(node, ast.Constant):
        # Nothing is not a kind. A name set to `None` and then to something is
        # how a thing that is not known yet is written, and it is every other
        # line of a check.
        return (None if node.value is None
                else type(node.value).__name__)
    if isinstance(node, (ast.List, ast.ListComp)):
        return "list"
    if isinstance(node, (ast.Set, ast.SetComp)):
        return "set"
    if isinstance(node, (ast.Dict, ast.DictComp)):
        return "dict"
    if isinstance(node, ast.Tuple):
        return "tuple"
    if isinstance(node, ast.Call):
        if isinstance(node.func, ast.Name):
            # The door every list in these checks goes through hands back what
            # it was given, so what a name is made of is what went in. Without
            # this, everything read through it claims nothing — which is how
            # `named` came to be a list of checks and a piece of text in this
            # very file, with the check for that running and saying nothing.
            if node.func.id == "some" and len(node.args) > 1:
                return made_of(node.args[1])
            return MADE_BY.get(node.func.id)
        dotted = whole_name(node.func)
        if dotted in MADE_BY_DOTTED:
            return MADE_BY_DOTTED[dotted]
        if isinstance(node.func, ast.Attribute):
            return MADE_BY_METHOD.get(node.func.attr)
    return None


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


def report(what, kinds, wording, spell):
    global failed
    if len(kinds) != len(wording):
        print("%s: %u kinds and %u names" % (what, len(kinds), len(wording)))
        failed = 1
        return
    for i, (kind, name) in enumerate(zip(kinds, wording)):
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
printed_words = some("the keywords the reference prints", sorted(
    table('docs/language.md',
          r'## Keywords\n\n```\n(.*?)```').split()))
if held != printed_words:
    only_held = [w for w in held if w not in printed_words]
    only_printed = [w for w in printed_words if w not in held]
    if only_held:
        print("keywords: the lexer holds %s and the reference does not say so"
              % ", ".join("`%s`" % w for w in only_held))
    if only_printed:
        print("keywords: the reference says %s and the lexer does not hold it"
              % ", ".join("`%s`" % w for w in only_printed))
    failed = 1

# What a check writes into its own scratch and then never looks at. A program
# built and not run is a probe that says nothing, and what it looks like from
# outside is a check with one more thing in it — which is the shape D534 found
# in the sweeps, from the other end. The one that quotes the others is left
# out, because what it writes are broken copies rather than work of its own.
# See D535.
for reading in sorted(glob.glob('tools/check-*.sh')):
    if reading.endswith('check-backstops.sh'):
        continue
    said_in = open(reading).read().split('\n')
    written_at = {}
    for at, line in enumerate(said_in):
        for one in re.finditer(r'>\s*"\$(?:work|scratch|sweeps)/'
                               r'([A-Za-z0-9_.-]+)"', line):
            written_at.setdefault(one.group(1), at)
    for made, at in sorted(written_at.items()):
        # By the name without what is after the dot, because a check names a
        # program by its stem where it runs it: `run "$work/$file.kest"`.
        stem = made.rsplit('.', 1)[0]
        named_again = [line for again, line in enumerate(said_in)
                     if again != at and not re.match(r'\s*#', line)
                     and re.search(r'(?<![A-Za-z0-9_.-])%s(?![A-Za-z0-9_-])'
                                   % re.escape(stem), line)]
        if not named_again:
            print("%s: writes `%s` into its own scratch and never names it "
                  "again" % (reading, made))
            failed = 1

# The reasons there is no least, in the three places that know them: the header
# a host reads, the one list of what each is called, and the reference. A reason
# with no name of its own is a host told whatever the last one fell through to,
# and a reason nobody is told about is one nobody writes a branch for — which is
# what `nothing was asked` was four of until D566. `KEST_REACH_KNOWN` is on the
# header's list and on neither of the others on purpose: it is the answer a host
# never reads, because the call that would have written it answered true.
REASON_NAMES = table(
    'src/value.c',
    r'const char \*kest_reach_name\(KestReach reach\) \{(.*?)\n\}')
reasons = some("the reasons the header has", re.findall(
    r'(KEST_REACH_[A-Z_]+),',
    table('include/kest.h', r'typedef enum \{(.*?)\} KestReach;')))
reasons_named = some("the reasons the one list names",
                     re.findall(r'case (KEST_REACH_[A-Z_]+):', REASON_NAMES))
reason_words = some(
    "what the one list calls them",
    re.findall(r'case KEST_REACH_[A-Z_]+:\s+return "([^"]+)";', REASON_NAMES))
if sorted(reasons) != sorted(reasons_named):
    print("reasons: the header has %s and the one list names %s"
          % (", ".join(sorted(reasons)), ", ".join(sorted(reasons_named))))
    failed = 1
for word in sorted(set(w for w in reason_words
                      if reason_words.count(w) > 1)):
    print("reasons: two of them are called `%s`" % word)
    failed = 1
reasons_in_docs = some("the reasons the reference names",
                       set(re.findall(r'KEST_REACH_[A-Z_]+',
                                      open('docs/language.md').read())))
if "KEST_REACH_KNOWN" in reasons_in_docs:
    print("reasons: the reference names `KEST_REACH_KNOWN`, which is the one a "
          "host is never handed")
    failed = 1
reasons_a_host_reads = set(reasons) - {"KEST_REACH_KNOWN"}
if reasons_in_docs != reasons_a_host_reads:
    print("reasons: a host can be told %s and the reference says %s"
          % (", ".join(sorted(reasons_a_host_reads)),
             ", ".join(sorted(reasons_in_docs))))
    failed = 1

# The answers a host is given, and the hosts that read them. Each is a list with
# nothing else in it, so a host that reads one with a `switch` and no `default`
# is told by its own compiler when an answer is added — and a host that compares
# against one value at a time is a host with a branch missing on the day the
# machine says something else, which no compiler can see. The reference tells a
# host writer that both hosts here read every one of them that way, so both are
# held to it: a file that names any of an answer's values names all of them in a
# `case`. Comparing against one beside that is a host saying which it expected,
# which is a different thing from deciding what to do about each.
ANSWERS = {"KestKept": "KEST_KEPT", "KestReach": "KEST_REACH",
           "KestRefusal": "KEST_REFUSED", "KestSlot": "KEST_S"}
for answer in sorted(ANSWERS):
    all_of_them = some("the answers `%s` has" % answer, re.findall(
        r'(%s_[A-Z_]+),' % ANSWERS[answer],
        table('include/kest.h',
              r'typedef enum \{(.*?)\} %s;' % answer)))
    for host in ("src/main.c", "examples/embed.c"):
        read_there = open(host).read()
        if ANSWERS[answer] + "_" not in read_there:
            continue
        cases = set(re.findall(r'case (%s_[A-Z_]+):' % ANSWERS[answer],
                               read_there))
        if cases != set(all_of_them):
            print("answers: %s reads `%s` and names %u of its %u in a `case`"
                  % (host, answer, len(cases), len(all_of_them)))
            failed = 1

# The types the language has of its own, beside the ones the reference says it
# has. A primitive nobody is told about is a type somebody can write and cannot
# look up, which is what `void` was until D519; one the reference names and
# nothing registers is a type a reader would be refused for writing. `void` is
# on neither list on purpose: it is registered because the compiler looks types
# up by name and it is not written, so the reference must not offer it.
registered = some("the primitives the compiler registers", sorted(spelled(
    table('src/types.c',
          r'static bool add_primitives\(KestProgram \*program\) \{(.*?)\n\}'))))
printed_line = table('docs/language.md', r'\nPrimitives: (.*?)\n')
printed_types = some("the primitives the reference prints",
                     sorted(word
                            for group in re.findall(r'`([a-z0-9 ]+)`',
                                                    printed_line)
                            for word in group.split()))
writable = [w for w in registered if w != "void"]
if "void" in printed_types:
    print("primitives: the reference offers `void`, and it is the one type "
          "there is no way to write")
    failed = 1
if writable != [w for w in printed_types if w != "void"]:
    only_held = [w for w in writable if w not in printed_types]
    only_printed = [w for w in printed_types if w not in writable
                    and w != "void"]
    if only_held:
        print("primitives: the compiler registers %s and the reference does "
              "not say so" % ", ".join("`%s`" % w for w in only_held))
    if only_printed:
        print("primitives: the reference says %s and the compiler does not "
              "register it" % ", ".join("`%s`" % w for w in only_printed))
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

# And the options, in the same two places. The rule above reads the commands,
# which are words; this reads what is written with dashes in front of it, and
# it is the same rule for the same reason: an option nothing answers to is a
# mistake in the first place a reader looks, and one that works and is not
# printed is one nobody finds. `-h` and `--help` were the second of those.
said_options = some("the options `help` prints", sorted(set(re.findall(
    r'(?<![\w-])(--?[a-z][a-z-]*)',
    table('src/main.c',
          r'static void help\(FILE \*out\) \{(.*?)\n\}')))))
options = some("the options `main` reads", sorted(set(re.findall(
    r'strcmp\(argv\[[^\]]*\], "(--?[a-z][a-z-]*)"\)', source))))
if said_options != options:
    for one in options:
        if one not in said_options:
            print("commands: `kest %s` does something and `kest help` does not "
                  "say so" % one)
            failed = 1
    for one in said_options:
        if one not in options:
            print("commands: `kest help` prints `%s` and nothing reads it"
                  % one)
            failed = 1

# And the rest of what `help` says. The commands and the options in it are held
# above to being answered; the sentences around them are promises too — what
# `KEST_LIB` does, what an exit status carries, what `4,5,6` lends — and each is
# a thing a reader will do on the strength of having read it. What holds one is
# that something walks it, so every name `help` marks out is a name the check
# that runs the command line names as well. What is marked out is what is in
# backticks and what is written in capitals, less the capitals that are this
# file's own C rather than anything a reader sees.
helped = table('src/main.c',
               r'static void help\(FILE \*out\) \{(.*?)\n\}')
promises = some("what `help` marks out", sorted(
    {one for one in re.findall(r'`([^`\s]+)`', helped)} |
    {one for one in re.findall(r'\b([A-Z][A-Z_]{2,})\b', helped)
     if ('#define ' + one) not in source}))
walks = open('tools/check-commands.sh').read()
for one in promises:
    if one not in walks:
        print("commands: `help` marks out `%s` and nothing in "
              "`check-commands.sh` walks it" % one)
        failed = 1

# And the options, which are held above to being answered and were held to
# nothing about working: one of them was printed, answered, and run by
# nothing at all. Two checks are left out of this: the one that quotes them
# as holes, because a broken copy of a thing is not a run of it, and this
# one, because a rule about a name is written with the name in it. What
# counts is a check that types the option.
elsewhere = "".join(open(one).read() for one in sorted(glob.glob('tools/*.sh'))
                    if not one.endswith(('check-backstops.sh',
                                         'check-tables.sh')))
for one in options:
    if one not in elsewhere:
        print("commands: `kest %s` is answered and nothing runs it" % one)
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
# The stamps a machine hands out are the one of these not written `MAX_`, and
# the one a program runs into rather than is compiled against: a store filled
# and emptied four thousand million times. It is in the table because the
# table's own sentence names `K0630`, which is what it says. See D523.
SPELLED = {'UINT16_MAX': 65535, 'INT32_MAX': 2147483647,
           '0xffffffffu': 4294967295}
A_HOSTS_OWN = {'MAX_FRAMES'}
enforced = set()  # filled below, and held to being filled
for path in ('src/compile.c', 'src/check.c', 'src/types.c', 'src/vm.c',
             'src/parser.c'):
    for name, value in re.findall(r'#define (MAX_[A-Z]+|MOST_STAMPS)\s+(\S+)',
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
printed_numbers = some("the numbers the reference prints", set(int(one) for one in
    re.findall(r'\n\| (\d+) \| ',
               table('docs/language.md',
                     r'## What there is a most of(.*?)\n\n```'))))
if enforced != printed_numbers:
    for one in sorted(enforced - printed_numbers):
        print("limits: the compiler holds a program to %u and the reference "
              "does not say so" % one)
    for one in sorted(printed_numbers - enforced):
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
    said_back = subprocess.run(['./kest', 'check', probe], capture_output=True,
                          text=True, stdin=subprocess.DEVNULL)
    said = said_back.stdout + said_back.stderr
finally:
    shutil.rmtree(work, ignore_errors=True)

names_back = some("the escapes a run names", set(re.findall(
    r'\\(\S)', said.partition('known escapes are')[2])))
printed_escapes = some("the escapes the reference prints", set(re.findall(
    r'`\\(.)`',
    table('docs/language.md', r'The escapes are\n(.*?)\n\n'))))
if accepted != names_back:
    print("escapes: a run takes %s and names %s"
          % (sorted(accepted), sorted(names_back)))
    failed = 1
if accepted != printed_escapes:
    print("escapes: a run takes %s and the reference prints %s"
          % (sorted(accepted), sorted(printed_escapes)))
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
told_of = some("what `CLAUDE.md` says the gate does", sorted(set(
    line.split()[0] for line in table(
        'CLAUDE.md',
        r'What the gate does itself.*?```\n(.*?)```').splitlines()
    if line and not line.startswith(' '))))
if does != told_of:
    for one in does:
        if one not in told_of:
            print("checks: `check.sh` says `%s` and `CLAUDE.md` does not say "
                  "it does" % one)
            failed = 1
    for one in told_of:
        if one not in does:
            print("checks: `CLAUDE.md` says the gate does `%s` and nothing in "
                  "it says so" % one)
            failed = 1

# What can be written down and what writes it. Two switches say which types a
# value of can be put in a hole: the checker's, which refuses a program that
# asks for one that cannot, and the machine's, which writes the ones that can.
# Each is held to naming every tag by there being no `default` in it, and
# neither was held to the other — a tag moved from one side to the other in one
# of them compiles, and what a program gets then is `<no text>` where it asked
# for a value, or a refusal for something the machine can write perfectly well.
def sides(path, opening):
    body = table(path, opening)
    runs = re.findall(r'((?:\s*case (?:KEST_T_\w+):)+)\s*'
                      r'(?:\*without = type;|break;\n    \})', body)
    if not runs:
        return None, None
    # Every run that ends in one of those and not only the last, because a
    # tag moved out of the list into a `case` of its own is a tag this used to
    # go on counting on the side it left. See D541.
    silent = set(re.findall(r'KEST_T_(\w+)', ''.join(runs)))
    every = set(re.findall(r'case KEST_T_(\w+):', body))
    # What a type is when the checker has already said something about it. The
    # checker says it can be written so that a program already wrong is not
    # told twice, and the machine never meets one because a program with one in
    # it does not run. Neither is about what can be written down.
    return (every - silent) - {'ERROR'}, silent - {'ERROR'}


# And the third switch of the same shape: which types compare. `hash` applies
# to exactly what `==` applies to, and the reference says why — a type that
# compares has one and a type that does not has neither. What parts this list
# from the one above it is a single tag: an optional can be written and cannot
# be compared, because the one way to ask an optional anything is to take what
# it holds out. Held to being that one tag, so a kind that quietly moves in
# either of them is a kind somebody has to have decided about. See D541.
compares, does_not = sides('src/check.c',
                           r'static bool has_equality\([^)]*\) \{(.*?)\n\}')
some("the types the checker compares", compares)

says, refuses = sides('src/types.c',
                      r'bool kest_type_has_text\([^)]*\) \{(.*?)\n\}')
writes, cannot = sides('src/vm.c',
                       r'static size_t format_value\([^;]*?slots\) \{(.*?)\n\}')
# And the pair the machine keeps to itself. `missing_text` is the walk
# `format_value` makes, asked first: a frame nothing has been called with is
# noughts, and a nought where text goes is the absence of a piece rather than
# an empty one. What makes the two one walk is which tags they go down into —
# a tag that carries something is one both have to follow, and a tag one of
# them follows and the other does not is a null read where it is written. See
# D543.
# One arm is the labels written together and the lines under them. What ends an
# arm is the next label rather than the `return` in it: an arm whose body is a
# block returns from inside the block, and a walk that waits for a `return` at
# the top goes on reading the arms under it as though they were this one. That
# is what the first of these read, and it read every arm as recursive because
# one of them was.
def arms(text):
    out, labels, depth, started, labelled = {}, [], 0, False, False
    for line in text.split('\n'):
        if not started:
            started = 'switch (' in line
            continue
        found = re.match(r'\s*case KEST_T_(\w+):', line)
        if found:
            if not labelled:
                labels, depth = [], 0
            labels.append(found.group(1))
            out.setdefault(found.group(1), [])
            labelled = True
            depth += line.count('{') - line.count('}')
            continue
        labelled = False
        depth += line.count('{') - line.count('}')
        for one in labels:
            out[one].append(line)
    return {name: '\n'.join(body) for name, body in out.items()}


def followed(path, opening, calling):
    body = re.search(opening, open(path).read(), re.S)
    if body is None:
        return None
    walk = arms(body.group(1))
    return {name for name, one in walk.items() if calling in one}


writes_down = some("the tags the machine follows to write one", followed(
    'src/vm.c', r'static size_t format_value\([^;]*?slots\) \{(.*?)\n\}',
    'format_value('))
asks_first = some("the tags the machine follows to ask about one", followed(
    'src/vm.c', r'static bool missing_text\([^)]*\) \{(.*?)\n\}',
    'missing_text('))
if writes_down != asks_first:
    for one in sorted(writes_down - asks_first):
        print("text: writing a `%s` goes into what it carries and asking "
              "whether it is there does not" % one.lower())
        failed = 1
    for one in sorted(asks_first - writes_down):
        print("text: asking about a `%s` goes into what it carries and "
              "writing one does not" % one.lower())
        failed = 1

# And the fourth of the same shape: what the machine makes a hash out of. The
# reference says `hash` applies to exactly what `==` applies to, because a type
# that compares has one and a type that does not has neither — so the two lists
# are the same list, said in two places. See D542.
hashes, unhashed = sides('src/vm.c',
                         r'static uint64_t hash_value\([^)]*\) \{(.*?)\n\}')
some("the types the machine hashes", hashes)
# And the third of that family. `values_equal` is what the machine does when
# two values are compared, `hash_value` is the number standing for one, and
# `has_equality` is what the checker lets near either: three lists of the same
# tags, and any two of them disagreeing is two equal values with two hashes or
# a comparison of something the checker refused. See D545.
equals, unequal = sides('src/vm.c',
                        r'static bool values_equal\([^)]*\) \{(.*?)\n\}')
some("the types the machine compares", equals)
for what, does_none, takes in (("hash", "makes no hash of", hashes),
                               ("equal", "cannot compare", equals)):
    if takes is None or compares is None or takes == compares:
        continue
    for lost in sorted(compares - takes):
        print("%s: a `%s` compares and the machine %s one"
              % (what, lost.lower(), does_none))
        failed = 1
    for extra in sorted(takes - compares):
        print("%s: the machine takes a `%s` and the checker says it does not "
              "compare" % (what, extra.lower()))
        failed = 1

some("the types the checker says can be written", says)
some("the types the machine writes", writes)
if says is not None and compares is not None and says - compares != {'OPTIONAL'}:
    for one in sorted((says - compares) - {'OPTIONAL'}):
        print("text: a `%s` can be written and does not compare, and an "
              "optional is the one that is both" % one.lower())
        failed = 1
    for one in sorted(compares - says):
        print("text: a `%s` compares and cannot be written, which nothing here "
              "has an answer for" % one.lower())
        failed = 1
    if 'OPTIONAL' not in says - compares:
        print("text: an optional compares now, and the one way to ask one "
              "anything was to take what it holds out")
        failed = 1
if says != writes:
    for one in sorted((says or set()) - (writes or set())):
        print("text: the checker says a `%s` can be written and the machine "
              "does not write one" % one.lower())
        failed = 1
    for one in sorted((writes or set()) - (says or set())):
        print("text: the machine writes a `%s` and the checker says it cannot "
              "be written" % one.lower())
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
# Where a build says the library will be, against where an install puts it.
# One is a string compiled into every object — the last place a program looks
# for `std` — and the other is a line in a rule, and they are the same path
# said twice. A build installed under one and told the other finds no library
# and says so from a path nobody can fix by moving anything.
told_where = some("what a build says the library will be", {
    where.replace('$(PREFIX)', '').rstrip('/')
    for where in re.findall(r"-DKEST_LIB_DIR='\"([^\"]*)\"'", make)})
puts = some("where an install puts the library", {
    where.rstrip('/')[:-len('/std')]
    for where in re.findall(r'\$\(DESTDIR\)\$\(PREFIX\)(\S*/kest/std\S*)',
                            rule('install'))})
# Every place it puts them and not one of them: a rule that makes a directory
# in one place and copies into another is two paths, and both have to be the
# one the build was told_where.
for where in sorted(puts):
    if {where} != told_where:
        print("Makefile: a build says `std` is under `%s` and an install puts "
              "it under `%s`" % (", ".join(sorted(told_where)), where))
        failed = 1

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
# The name the sanitised build is told by, which is spelt once. One compiler
# defines it and another answers a question about it, so a file that asks for
# it directly is a file that has the checks under one and not under the other —
# and the build with none of them in it runs everything and finds nothing. What
# every other file asks for is `KEST_CHECKED`, which is that question answered
# in one place.
spelt = [where for where in sorted(glob.glob('src/*.c') + glob.glob('src/*.h'))
         if '__SANITIZE_ADDRESS__' in open(where).read()]
if spelt != ['src/mem.h']:
    print("checks: the sanitiser's own name is spelt in %s, and `KEST_CHECKED` "
          "is what says it once" % ", ".join(spelt) if spelt else
          "checks: nothing spells the sanitiser's own name, so `KEST_CHECKED` "
          "answers a question nobody asked")
    failed = 1

for check in tools:
    where = os.path.join('tools', check)
    text_of = open(where).read()
    if not os.access(where, os.X_OK):
        print("%s: is a check and is not something to run" % where)
        failed = 1
    if not text_of.startswith('#!/bin/sh\n'):
        print("%s: does not say what runs it" % where)
        failed = 1
    if '\nset -u\n' not in text_of:
        print("%s: does not stop on a name nobody set" % where)
        failed = 1
    # Except in the one whose contents are quotations of the others: it holds
    # broken copies of every check here on purpose, so a fixed name written in
    # it is a fixed name it is asking about rather than one it writes to.
    if check != 'check-backstops.sh':
        # A name written into the file, quoted or bare. The one this project
        # had was bare — a shell assignment, no quotes around it — and the
        # pattern that only looked inside quotes read past it for as long as
        # it was there. Neither form matches the line below, because what is
        # written there is a pattern rather than a name.
        for fixed in re.findall(r'=\s*/tmp/\S+|["\']/tmp/[^"\']*', text_of):
            print("%s: writes to `%s`, which is a name another run has too"
                  % (where, fixed.lstrip('=\'" ')))
            failed = 1
    # A room this check makes for itself, counted where one is made rather
    # than wherever the words appear: a check that quotes another check quotes
    # the words too.
    rooms = re.findall(r"^\s*(?:\w+=\$\(mktemp -d\)|\w+ = tempfile\.mkdtemp\(\))",
                       text_of, re.M)
    takes = len(re.findall(r"trap 'rm -rf|rmtree|atexit.register", text_of))
    if rooms and takes == 0:
        print("%s: makes somewhere to work and does not take it away" % where)
        failed = 1
    # And one room per check, because the second one is the one that is left:
    # what takes a room away is written once. Everything else a check needs is
    # a directory under the room it already has.
    if len(rooms) > 1:
        print("%s: makes %u places to work, and what takes one away is "
              "written once" % (where, len(rooms)))
        failed = 1
    # A second `trap ... EXIT` replaces the first rather than adding to it.
    # That is how nine hundred directories were left in `/tmp` by a check that
    # reads as though it takes both of its rooms away.
    traps = len(re.findall(r"^trap ", text_of, re.M))
    if traps > 1:
        print("%s: sets %u traps, and the last one is the only one that runs"
              % (where, traps))
        failed = 1

    # And a name in the Python a check is written in stands for one thing. A
    # counter given a name a set further down the same file already had ran
    # every line of the check and then refused with a `TypeError` from Python
    # rather than with anything about what it was checking. What says two
    # things are two things is what they are made of: a number and a set are
    # not the same kind, and a name that is both is a name somebody reused.
    # See D403.
    # Both ways a check carries Python: a heredoc, and a quoted string handed
    # to `python3 -c`. The second is nearly two thirds of it and was read by
    # nothing — a shell string cannot hold the quote that ends it, so what is
    # in one is Python written to avoid a character, which is exactly the kind
    # of writing a reader skims. It comes indented under the shell around it,
    # so the indent comes off before it is read.
    carried = [body for _, body in
               re.findall(r"<<'([A-Za-z_]+)'\n(.*?)\n\1\n", text_of, re.S)]
    carried += re.findall(r"python3 -c '(.*?)'", text_of, re.S)
    for body in carried:
        try:
            tree = ast.parse(textwrap.dedent(body))
        except SyntaxError:
            # A heredoc of something else. Kest, a program, a message.
            continue
        if not any(isinstance(one, (ast.Import, ast.ImportFrom))
                   for one in tree.body):
            continue
        pythons += 1
        stands_for = {}
        # A name given another name is that name's kind. `out = pieces` says
        # what `out` is made of as plainly as `out = []` does, and reading only
        # the line it is written on says nothing about it. Which way round the
        # two are written does not matter, so this goes round until it stops
        # learning anything.
        # Everywhere in the file rather than at the top of it. A name meaning
        # one thing outside a function and another inside one is the same
        # mistake where it is easier to make, and a function is a kind too:
        # `written` was a function, a set, a list and a piece of text in one
        # check, and what it was in the line that read it was whichever had
        # been assigned last.
        assigned = []
        for one in ast.walk(tree):
            if isinstance(one, ast.FunctionDef):
                assigned.append(([one.name], "function", None))
            elif isinstance(one, ast.Assign):
                assigned.append(([target.id for target in one.targets
                               if isinstance(target, ast.Name)],
                              made_of(one.value),
                              one.value.id
                              if isinstance(one.value, ast.Name) else None))
        learning = True
        while learning:
            learning = False
            for called, what, through in assigned:
                if what is None and through is not None:
                    what = stands_for.get(through)
                if what is None:
                    continue
                for name in called:
                    if name not in stands_for:
                        stands_for[name] = what
                        learning = True
        for called, what, through in assigned:
            if what is None and through is not None:
                what = stands_for.get(through)
            if what is None:
                continue
            for name in called:
                if stands_for[name] != what:
                    print("%s: `%s` is a %s and a %s, and one name is one "
                          "thing" % (where, name, stands_for[name], what))
                    failed = 1

for where in sorted(glob.glob('tools/*.sh')):
    text_of = open(where).read()
    # And the same rule over the shell the Python is carried in, which had
    # half the ground the Python had. A name in a shell script is a place or
    # it is not: what has a `/` in it is somewhere, and a count, a word and
    # what a command answered are all text. That is the whole of what shell
    # can be held to, and it is the half that went wrong — a directory's name
    # and the last thing a command said were one name, so every answer a sweep
    # wrote went to a file nothing read and four holes were missed. See D438.
    #
    # Every shell file here, not only the ten checks: the gate is shell too, and
    # the name that stood for two things was in one of each.
    #
    # A heredoc holds something else: Kest, C, a program. Its body is not
    # shell and an assignment inside one is not an assignment here.
    without = []
    lines = text_of.split("\n")
    at = 0
    while at < len(lines):
        without.append((at + 1, lines[at]))
        opened = re.search(r"<<-?'?([A-Za-z_][A-Za-z0-9_]*)'?", lines[at])
        if opened is not None:
            at += 1
            while at < len(lines) and lines[at].strip() != opened.group(1):
                at += 1
        at += 1

    def shell_kind(value):
        one = value.strip()
        # What a command answered, whether or not it closes on this line.
        if one.startswith("$("):
            return "text"
        if one == "":
            return None
        # What a function was handed. Which kind that is is the caller's, and
        # this reads one file rather than following calls.
        if re.fullmatch(r'"?\$\{?[0-9]+\}?"?', one):
            return None
        # A name given another name, or a piece of one, is that name's kind.
        through = re.fullmatch(
            r'"?\$\{?([A-Za-z_][A-Za-z0-9_]*)(?:[%#}][^"]*|\}?)"?', one)
        if through is not None:
            return ("through", through.group(1))
        return "place" if "/" in one else "text"

    given = []
    for number, line in without:
        put = re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)=(.*)$", line)
        if put is None:
            continue
        kind = shell_kind(put.group(2))
        if kind is not None:
            given.append([put.group(1), kind, number])
    shells += 1

    settled = {}
    for name, kind, number in given:
        if isinstance(kind, str):
            settled.setdefault(name, kind)
    learning = True
    while learning:
        learning = False
        for one in given:
            if not isinstance(one[1], str) and one[1][1] in settled:
                one[1] = settled[one[1][1]]
                if one[0] not in settled:
                    settled[one[0]] = one[1]
                learning = True

    stands = {}
    for name, kind, number in given:
        if not isinstance(kind, str):
            continue
        if name in stands and stands[name][0] != kind:
            print("%s: `%s` is a %s at line %u and a %s at line %u, and one "
                  "name is one thing"
                  % (where, name, stands[name][0], stands[name][1], kind,
                     number))
            failed = 1
            stands[name] = (kind, number)
        else:
            stands.setdefault(name, (kind, number))

    # And a name that is a function and a value, which is the same mistake
    # with the two things furthest apart: `said "$said"` reads as a call of
    # one of them on the other and is one of them called on itself.
    for name in sorted(set(re.findall(r"^([A-Za-z_][A-Za-z0-9_]*)\(\)\s*\{",
                                      text_of, re.M)) & set(stands)):
        print("%s: `%s` is a function and a %s, and one name is one thing"
              % (where, name, stands[name][0]))
        failed = 1

    # And a piece of a shell this one is not. Every check here says `/bin/sh`
    # on its first line, and under that shell `$'\\r'` is those four characters
    # and nothing else: a sweep written that way looks for a byte no file has
    # and can never say a word. It is not an error anywhere — the shell reads
    # it, the check runs, and what it holds is nothing. `printf` writes the
    # byte in every shell there is. See D465.
    for number, line in without:
        if re.search(r"\$'", line):
            print("%s: line %u writes `$'...'`, which under `/bin/sh` is those "
                  "characters and not what they stand for"
                  % (where, number))
            failed = 1

# And what a check says when something is wrong, held to having been said. A
# hole names the words it is caught by, so a sentence no hole names is one
# nothing has ever seen a check say -- and a sentence nobody has seen is a
# sentence nobody knows is right. Of the three hundred and twenty-seven a check
# can say, a hundred and forty had never been said the day this was written, so
# what is held here is the checks that are already at nought and the list grows
# by one when one of them is brought to it. See D455.
#
# What is not counted is what a check writes rather than says: a heredoc it
# hands to a file is a program, and a line that program prints reads like a
# complaint. A heredoc it hands to `python3` is the check itself. And the last
# thing a check says is what it says when nothing is wrong, which no hole can
# make it say.
#
# Five checks were on this list and two are, because what put three of them
# there was a reading any words could meet: a sentence that ends in a number
# or a name ends in a blank, and a piece of words that ran into that blank was
# counted as having come out of the sentence. Two hundred and thirty-nine of
# two hundred and sixty holes read as having said one sentence of
# `check-ceilings.sh` that way. What the reading asks now is below, and what it
# leaves is three checks with sentences nothing has been seen making them say.
# Nine of the ten. The tenth is `check-backstops.sh`, which is the one that
# puts the holes out of order: a hole in it would be a hole in the thing that
# says which holes there are, and what would catch one missing is itself. It is
# left off for the reason the gate's own guards are left off.
HELD = ("check-ceilings.sh", "check-commands.sh", "check-costs.sh",
        "check-dead.sh", "check-docs.sh", "check-fmt.sh",
        "check-header.sh", "check-lends.sh", "check-tables.sh")
# The sentences nothing can make a check say, each beside the reason. A host
# that will not build is a tree that will not build, and every hole is put in
# a tree that was built before it was broken. And a hole breaks what a file
# says, so a file that is missing is a tree a hole cannot make: a check that
# refuses because what it reads has not been built is a check asking for the
# tree it is already in — two of these are that one. The fourth is a document
# read from the top rather than by name: a worklog with no entry in it is one
# whose every heading was written another way, and a hole breaks one place.
NOT_SAID = (("check-lends.sh", "the host that lends by name does not build"),
            ("check-dead.sh", "%s is not built; `make embed` first"),
            ("check-docs.sh", "docs/language.md: the engine is not built, so "
                              "what it prints for a host's own rules is a "
                              "list nothing reads"),
            ("check-docs.sh", "docs/worklog.md: nothing here is an entry"),
            ("check-header.sh", "the library is not built"),
            ("check-header.sh", "the host the header describes did not run"),
            ("check-ceilings.sh", "ceilings: the tree does not build"),
            # The last thing that check asks, after every part of what it needs
            # has been asked about on its own: a program of two files that
            # works. Every way it can fail to work is a way one of those parts
            # fails, and those are asked first — six breaks were tried and each
            # was caught earlier, and the one that was not never came back.
            ("check-commands.sh",
             "run: a program of two files that works answered $crossing_status"),
            # A rung of the memory ladder that neither ran nor refused. It is
            # what a crash looks like from outside, and a hole that crashes
            # this compiler is caught by the crash rather than by these words:
            # what says them is the compiler being wrong in a way no hole can
            # ask for on purpose. D645 is the one time anything has said it.
            ("check-ceilings.sh",
             "ceilings: $died of $all_rungs rungs were killed rather than running "
             "or refusing"),
            ("check-ceilings.sh",
             "ceilings: with ${level}K of memory a run died rather than "
             "running or refusing: it came back $answered"))

WILD = re.compile(r"%[-+ #0]*[0-9*]*(?:\.[0-9*]+)?(?:hh|h|ll|l|j|z|t|L)?[a-zA-Z]"
                  r"|\$\{[^}]*\}|\$\([^)]*\)|\$[A-Za-z_][A-Za-z0-9_]*|\$[0-9]")


def quoted(line, at):
    """The shell string that starts at the quote at `at`.

    A double-quoted string may hold a command substitution and that may hold
    quotes of its own, so counting quote marks reads one string as three: a
    sentence written that way stops at the first of them, and every word after
    it is a word nothing here says. Six of what the last check says were being
    read that way.
    """
    out, i, deep = [], at + 1, 0
    while i < len(line):
        if line[i] == "\\" and i + 1 < len(line):
            out.append(line[i:i + 2])
            i += 2
        elif line.startswith("$(", i):
            deep += 1
            out.append("$(")
            i += 2
        elif deep > 0 and line[i] == ")":
            deep -= 1
            out.append(")")
            i += 1
        elif line[i] == '"' and deep == 0:
            return "".join(out), i + 1
        else:
            out.append(line[i])
            i += 1
    return "".join(out), i


def strings_in(line):
    """Every top-level double-quoted string on a shell line."""
    out, i = [], 0
    while True:
        at = line.find('"', i)
        if at < 0:
            return out
        one, i = quoted(line, at)
        out.append(one)


def says(where):
    """Every run of words a check says when something is wrong."""
    out, quiet = [], False
    lines = open(where).read().split("\n")
    # A sentence too long for a line is written on two, which is one sentence.
    # Read as two, the half of it a hole quoted runs off the end of the first
    # and reads as words nothing has ever said.
    joined = []
    for line in lines:
        if joined and joined[-1].endswith("\\"):
            joined[-1] = joined[-1][:-1].rstrip() + " " + line.strip()
        # And in Python it is written as one string after another with nothing
        # between them, over as many lines as it took. Which lines those are is
        # said by the brackets: a `print(` that has not been closed is a
        # sentence that has not been finished.
        elif (joined and re.match(r"\s*print\(", joined[-1])
              and joined[-1].count("(") > joined[-1].count(")")):
            joined[-1] = joined[-1].rstrip() + " " + line.strip()
        else:
            joined.append(line)
    lines = joined
    at = 0
    while at < len(lines):
        line = lines[at]
        opened = re.search(r"<<-?'?([A-Za-z_][A-Za-z0-9_]*)'?", line)
        if opened is not None and "python3" not in line:
            at += 1
            while at < len(lines) and lines[at].strip() != opened.group(1):
                at += 1
            at += 1
            continue
        # And a group of `echo`s written into a file, which is the other way a
        # check writes a program out. What it puts there is the program's, and
        # a program says nothing on a check's behalf.
        if line.strip() == "{":
            shut = at + 1
            while shut < len(lines) and not lines[shut].startswith("}"):
                shut += 1
            if shut < len(lines) and re.search(r"^}\s*>", lines[shut]):
                at = shut + 1
                continue
        if re.match(r"\s*(if not failed:|if \[ \$failed -eq 0 \])", line):
            quiet = True
        # An `echo` given several words prints them with a space between,
        # which is one sentence written in as many pieces as it took to fit.
        if re.match(r"\s*echo ", line):
            spoken = strings_in(line)
            said_here = [" ".join(spoken)] if spoken else []
        elif re.search(r"\bcomplain\s+\"", line):
            said_here = strings_in(line)
        else:
            said_here = []
            # A `print(` where a statement begins. One in the middle of a
            # line is a name being read rather than a call being made, and
            # this file has one: the rule above says what it joins, and
            # reading it as a call made a sentence out of the rule.
            for one in re.finditer(
                    r'^\s*print\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', line):
                said_here.append("".join(
                    re.findall(r'"((?:[^"\\]|\\.)*)"', one.group(1))))
        # And said beside going wrong. A check says two kinds of thing: what
        # is wrong, and what it did. The second is said whether anything is
        # wrong or not, so no hole can be shown to have caused it — and one of
        # them is written in the middle of a check rather than at the end,
        # where stopping at the last line does not reach it.
        # Said where something has to be so for it to be said: under an `if`,
        # a `case` or a loop, or beside setting the failure. A line at the left
        # margin with nothing above it deciding whether to reach it is what a
        # check did rather than what it found, and nothing can be shown to have
        # caused one — the last check here says how many wordings it saw that
        # way, in the middle of itself, where stopping at the last line never
        # reached it.
        marks = "\n".join(lines[at:at + 5])
        wrong_here = (re.search(r"complain\b", line) is not None
                      or re.match(r"\s", line) is not None
                      or re.search(r"(?:^|\n)\s*(?:\w+ = 1|\w+=1|"
                                   r"raise SystemExit\(1\)|sys\.exit\(1\)|"
                                   r"exit 1)",
                                   marks) is not None)
        for words in said_here:
            if words and len(words.strip()) > 8 and not quiet and wrong_here:
                out.append(words.replace("\\`", "`").replace('\\"', '"'))
        at += 1
    return out


def in_pieces(form):
    """A sentence as the runs of words in it, with what fills the rest gone."""
    out, at = [], 0
    for found in WILD.finditer(form):
        out.append(("says", form[at:found.start()]))
        out.append(("value", None))
        at = found.end()
    out.append(("says", form[at:]))
    return [one for one in out if one[0] == "value" or one[1] != ""]


def reads_as(words, pieces, i, s):
    """How much of the sentence's own words these words are, or None."""
    at, first, covered = 0, True, 0
    for k in range(i, len(pieces)):
        kind, said = pieces[k]
        if kind == "says":
            rest = said[s:] if first else said
            if words.startswith(rest, at):
                covered += len(rest)
                at += len(rest)
            elif rest.startswith(words[at:]):
                return covered + len(words) - at
            else:
                return None
        else:
            if at >= len(words):
                return covered
            after = pieces[k + 1][1] if k + 1 < len(pieces) else None
            if after is None:
                return covered
            found = words.find(after, at)
            if found < 0:
                # Or the words stop partway into what comes after the value,
                # which is what a message quoted without the number at the end
                # of it looks like from here.
                for take in range(len(after), 0, -1):
                    if (words.endswith(after[:take])
                            and len(words) - take >= at):
                        return covered + take
                return covered
            at = found
        first = False
    return covered if at == len(words) else None


def ever_said(form, by):
    pieces = in_pieces(form)
    for words in by:
        for i, (kind, said) in enumerate(pieces):
            # A hole quotes a piece of what it saw, and what it saw had the
            # numbers and names in it, so the piece may begin in the middle of
            # one of those: `nesting.kest was not told` begins inside the
            # `$file` the sentence leaves open. What says the words were this
            # sentence's is how much of them is the sentence rather than the
            # blanks in it — more than half, because a piece that is mostly
            # blank is a piece any sentence with a blank in it could have said.
            # Two hundred and thirty-nine of two hundred and sixty holes read
            # as having said one sentence before this was asked.
            starts = range(len(said)) if kind == "says" else [0]
            for s in starts:
                if kind == "says" and said[s] != words[0]:
                    continue
                # And it begins where a word does. `at ` out of `that ` is
                # three letters of somebody else's sentence.
                if kind == "says" and s > 0 and said[s - 1].isalnum():
                    continue
                covered = reads_as(words, pieces, i, s)
                if covered is not None and covered * 2 >= len(words):
                    return True
    return False


by_a_hole = {one.replace("\\`", "`").replace('\\"', '"') for one in
             re.findall(r'"caught": "((?:[^"\\]|\\.)*)"',
                        open("tools/check-backstops.sh").read())}
some("the words a hole says it is caught by", by_a_hole)
# And what the gate's own guards make a check say. Those have no holes, by the
# rule that what would catch one missing is itself: the gate hands a check a
# document with nothing in it or a list of no files and greps for the words it
# has to answer with. A sentence reached that way has been watched being said,
# which is the whole of what a hole is for.
by_the_gate = some("the words the gate's own guards look for",
                   set(re.findall(r'grep -q "([^"]+)"',
                                  open("tools/check.sh").read())))
seen_said = by_a_hole | by_the_gate
sentences = 0
for name in HELD:
    where = os.path.join("tools", name)
    if not os.path.exists(where):
        print("checks: `%s` is held to what it says and is not there" % name)
        failed = 1
        continue
    said_here = some("what `%s` says" % name, says(where))
    for form in said_here:
        if (name, form) in NOT_SAID:
            continue
        sentences += 1
        if not ever_said(form, seen_said):
            print("%s: says `%s`, and nothing has ever made it" % (where, form))
            failed = 1
for name, form in NOT_SAID:
    if form not in says(os.path.join("tools", name)):
        print("checks: `%s` is written down as one nothing can make `%s` say, "
              "and it does not say it" % (form, name))
        failed = 1

# A check written in shell alone has no Python to read, and a sweep that finds
# none of it holds none of it.
some("the checks written in Python", pythons)
some("the checks written in shell", shells)

# Every refusal a file can meet before it runs is asked for by a check: what
# the lexer and the parser say about what a file is, what the checker says
# about a program that parses and does not mean anything, and what is said
# about a file that cannot be read at all. A message nobody has
# ever seen is a message nobody knows is there, and this compiler could say a
# hundred and thirty-nine things with a third of them named in no document and
# in no check. What is left is what a program meets while it runs, which needs
# a program that runs rather than one that is refused; the number below is what
# says how many. See D415, D416 and D417.
# What nothing can be made to ask for, each written down where it was tried
# rather than left as a number. A count is a thing that goes stale: two is what
# it said the day it was written and nothing refused three.
#
# `K0705` is what a host is told when the very first allocation of a
# build fails, and that succeeds whenever the process started at all — walked
# to the kilobyte where the C library can no longer be mapped and it never
# appears; it is not dead, it is unreachable from here. `K0628` is what is left
# when a name a command line asked for is in the program, takes no types, and
# has no body in the module — which is the compiler having lost a chunk it
# made, and says so in the words a fault says. The one case that reached it was
# a name the host answers, and the machine says that where the `extern` line
# is. See D416, D417, D423, D428, D496 and D497.
NOT_REACHED = ("K0628", "K0705")
# Every refusal this compiler can say, held to being asked for by something
# that makes it happen and reads what it said. A message nobody has ever seen
# is a message nobody knows is there, and this project's first rule is that
# diagnostics are a feature.
reading = some("the refusals this compiler can say", sorted(set(
    code for code in re.findall(r'"(K0[0-9][0-9][0-9])"',
                                "".join(open(where).read() for where in
                                        sorted(glob.glob("src/*.c"))))
    if code not in NOT_REACHED)))
# Every check but the one whose contents are quotations of the others: it holds
# broken copies of these very lines, so a code named in it is a code it is
# asking about rather than one anything asks for.
#
# And the other host, which asks for what only a host can be refused for: a
# lend at no address, a frame said to hold what it does not, a machine freed
# while a program is running. What it names is a code it reads back out of a
# report after asking for the refusal, which is asking.
by_a_check = "".join(open(where).read()
                     for where in sorted(glob.glob("tools/*.sh"))
                     if not where.endswith("check-backstops.sh"))
by_a_check += open("examples/embed.c").read()
# And what a hole says it is caught by: a code somebody made happen on purpose
# and then read. Except this rule's own complaint — the hole that takes a probe
# away is caught by words that name the code, so counting them would let a code
# be asked for by the hole that says nothing asks for it.
caught_by_a_hole = "".join(
    caught for caught in
    re.findall(r'"caught": "(.*?)"', open("tools/check-backstops.sh").read())
    if "asks for it" not in caught)
asked_of = by_a_check + caught_by_a_hole
# Which of them a program cannot be written for. A refusal reached only by
# breaking the thing that says it is one of two things: a guard about this
# compiler being wrong, which is what it should be, or a message a program can
# reach that nobody has written the program for — and the second is what every
# walk of the last several turns found one code at a time by reading. Counted
# here so the number is written down and moves when somebody changes it. See
# D529.
only_a_hole = [code for code in reading
               if code not in by_a_check and code in caught_by_a_hole]
for code in reading:
    if code not in asked_of:
        print("%s: nothing asks for it, and a message nobody has ever seen is "
              "a message nobody knows is there" % code)
        failed = 1

# And the other way round, which is the half that goes stale rather than the
# half that goes missing: a check that names a code names one this compiler
# has. A code retired from the source leaves the check that asked for it
# looking for words nothing says, and what a check looking for words nothing
# says does is pass — the run it reads never has them. `K0507` was withdrawn
# and its asking went with it because somebody remembered; nothing would have
# said so. See D443.
every_code = set(re.findall(r'"(K0[0-9][0-9][0-9])"',
                            "".join(open(where).read() for where in
                                    sorted(glob.glob("src/*.c") +
                                           glob.glob("src/*.h")))))
some("the codes this compiler has", every_code)
# Every check but the one whose contents are quotations of the others, for the
# reason it is left out above: a code in a hole is a check being quoted, and a
# hole that puts a code out of order says one this compiler does not have on
# purpose.
for asking_in in [where for where in sorted(glob.glob("tools/*.sh"))
                  if not where.endswith("check-backstops.sh")] + \
                 ["examples/embed.c"]:
    # A code in a comment is a mention and not an asking, the same way a name
    # in one is not a call: what this holds is what a check looks for.
    reads = re.sub(r'^\s*(?:#|//).*$', '', open(asking_in).read(), flags=re.M)
    for code_named in sorted(set(re.findall(r'K0[0-9][0-9][0-9]', reads))):
        if code_named not in every_code:
            print("%s: asks for `%s`, which nothing in `src` says" %
                  (asking_in, code_named))
            failed = 1

# What a fault says it is, said in one place. A fault is what this project got
# wrong rather than what a program did, and the sentence that says which is
# the one thing every one of them has in common: it was written out eight
# times in five files, each in its own words, so a reader met the same news in
# five voices and a ninth fault could have arrived in a sixth. `kest_diags_fault`
# is the door, and this is what says nobody has gone round it.
saying_fault = {where for where in sorted(glob.glob("src/*.c") +
                                          glob.glob("src/*.h"))
                if "fault in the compiler" in open(where).read()}
some("the words a fault says it is", saying_fault)
if saying_fault - {"src/diag.c"}:
    for where in sorted(saying_fault - {"src/diag.c"}):
        print("%s: says what a fault is in its own words, and there is one "
              "place for that" % where)
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

# A module written in two widths is written in both of them everywhere. Where
# this library is written for numbers it is written twice over: a frame works
# in `f32` and a number is written in `f64`, and widening by hand at every call
# is the module not doing its half. Four functions were missing their other
# half and nothing here could see it — what holds every library function to
# being reached holds the ones that are there, and a half nobody wrote is named
# by nobody, so the check that finds a leftover cannot find a gap.
#
# Which modules this is asked of comes from the library rather than from a name
# written here: a module that declares one name in two widths is a module
# written in widths, and then every function in it that takes one takes both.
# What is declared `extern` is left out, because those are the host's and the
# host provides them in the one width the reference says it does.
PAIRED = {'i32': 'i64', 'i64': 'i32', 'f32': 'f64', 'f64': 'f32'}


def other_width(params, kind):
    return tuple(PAIRED[kind] if one == kind else one for one in params)


declared = {}
for path in sorted(glob.glob('lib/std/*.kest')):
    asked = subprocess.run(['./kest', 'check', path], capture_output=True,
                           text=True, stdin=subprocess.DEVNULL)
    for line in asked.stdout.splitlines():
        found = re.match(r'^fn ([A-Za-z0-9_.]+)\(([^)]*)\)', line)
        if found is None:
            continue
        params = tuple(one.strip() for one in found.group(2).split(',')
                       if one.strip())
        declared.setdefault(found.group(1).rpartition('.')[0], set()).add(
            (found.group(1), params))
some("the declarations the library makes", declared)

in_widths = []
for module, decls in sorted(declared.items()):
    for name, params in decls:
        if any((name, other_width(params, kind)) in decls
               and other_width(params, kind) != params
               for kind in set(params) & set(PAIRED)):
            in_widths.append(module)
            break
some("a module written in two widths", in_widths)

halves = 0
for module in in_widths:
    for name, params in sorted(declared[module]):
        for kind in sorted(set(params) & set(PAIRED)):
            wanted = other_width(params, kind)
            if (name, wanted) in declared[module]:
                halves += 1
                continue
            print("widths: `%s` takes (%s) and nothing takes (%s), in a "
                  "module written in both" % (name, ', '.join(params),
                                              ', '.join(wanted)))
            failed = 1

# What a mark is made of. `kest_module_mark` folds a module into one number, and
# nothing said it folds all of one: a field added to a chunk tomorrow is a field
# the mark leaves out, and two programs differing only in it mark alike. So every
# field of the four shapes it walks is either folded or written down here beside
# the reason it is not, which is the same rule this file holds the other complete
# lists to. See D661.
LEFT_OUT = {
    ("KestValue", "real"): "the same bytes as the whole number beside it, "
                           "which is what a constant is folded through",
    ("KestValue", "object"): "a constant is a number, a piece of text or "
                             "nothing, and never a thing on the heap",
    ("KestChunk", "wrote"): "the name with what tells one copy of a generic "
                            "from another taken off, which the name it was "
                            "taken from already says",
    ("KestChunk", "source"): "where a chunk was written, which is not what runs",
    ("KestChunk", "declared"): "the same, for the declaration it came from",
    ("KestChunk", "origins"): "where each instruction was written",
    ("KestChunk", "code_capacity"): "how much room the array has, not what is in it",
    ("KestChunk", "constant_capacity"): "the same, for the constants",
    ("KestExtern", "span"): "where the declaration is written",
    ("KestExtern", "source"): "the file it is written in",
    ("KestModule", "arena"): "where the module is kept, which is this run's",
    ("KestModule", "capacity"): "room rather than what is in it",
    ("KestModule", "extern_capacity"): "the same, for the externs",
    ("KestModule", "layout_capacity"): "the same, for the layouts",
    ("KestModule", "stamps"): "what a running world has handed out",
    ("KestModule", "machines"): "how many machines stand on it",
    ("KestModule", "layout_types"): "the types behind the layouts, which a host "
                                    "cannot tell apart from the layouts",
    ("KestLayout", "type"): "the same, said to a host as a handle",
}


def fields_of(shape):
    """What a struct declares: the type and the name of each field."""
    for where in (open(os.path.join("src", "value.h")).read(),
                  open(os.path.join("include", "kest.h")).read()):
        ends = where.find("\n} " + shape)
        if ends < 0:
            continue
        # A union as well as a struct: what a constant is has one name and
        # several shapes, and each of them is either folded or written down
        # like any other field.
        opens = max(where.rfind("struct {", 0, ends),
                    where.rfind("union {", 0, ends))
        if opens < 0:
            continue
        fields = []
        for line in where[opens:ends].split("\n"):
            if line.strip().startswith("//") or not line.startswith("    "):
                continue
            written = re.match(r"\s+(?:const\s+)?([A-Za-z_][A-Za-z_0-9]*)"
                               r"\s*\**\s*([a-z_][A-Za-z_0-9]*)"
                               r"\s*(?:\[\d*\])?;", line)
            if written is not None:
                fields.append((written.group(1), written.group(2)))
        return fields
    return []


folds = re.search(
    r"uint64_t kest_module_mark\(const (Kest[A-Za-z]+) \*module\) \{(.*?)\n\}",
    open(os.path.join("src", "value.c")).read(), re.S)
if some("what a mark is folded from", [folds] if folds else []):
    folded = folds.group(2)
    # Which shapes the mark walks comes from the mark rather than from a list
    # beside it: it starts at what it is handed and follows every field it
    # folds that is a shape of this compiler's own. A list written here would
    # be one more thing to keep in step, and the shape it would miss is the one
    # reached through a field rather than named — which is what happened to
    # `KestPiece`. See D662.
    walked = [folds.group(1)]
    at = 0
    while at < len(walked):
        shape = walked[at]
        at += 1
        for kind, field in some("the fields of `%s`" % shape,
                                fields_of(shape)):
            reached = re.search(r"(?:->|\.)%s\b" % field, folded) is not None
            if reached and kind.startswith("Kest") and kind not in walked:
                walked.append(kind)
            if reached or (shape, field) in LEFT_OUT:
                continue
            print("marks: `%s.%s` is folded into no mark and no reason is "
                  "written for leaving it out" % (shape, field))
            failed = 1
    # And a reason written for a field the mark folds after all, which is a
    # reason nobody can act on: it reads as something left out and is not.
    for shape, field in sorted(LEFT_OUT):
        if re.search(r"(?:->|\.)%s\b" % field, folded) is not None:
            print("marks: `%s.%s` is written down as left out of the mark and "
                  "the mark folds it" % (shape, field))
            failed = 1
    # And a reason written for a shape the mark does not walk at all, which is
    # a line nothing reads: the shape was taken out of the fold and the reason
    # for one of its fields stayed behind.
    for shape in sorted({shape for shape, field in LEFT_OUT}):
        if shape not in walked:
            print("marks: `%s` is written down as partly left out of the mark "
                  "and the mark never walks it" % shape)
            failed = 1

if not failed:
    print("%u escapes, "
          % len(accepted), end="")
    print("%u instructions, %u tokens, %u keywords, %u builtins, "
          "%u primitives, %u reasons, %u modules "
          "and %u checks are in step with their names, holding %u pieces of "
          "Python and %u of shell where a name stands for one thing, %u "
          "refusals asked for, %u of them by a hole and nothing else, "
          "and %u nothing can be made to ask for, every one of the %u codes a "
          "check names being one this compiler has, every one of the %u things "
          "%u check(s) say when something is wrong having been watched being "
          "said, "
          "and %u pairs of widths "
          "in %u module(s) written in both, and %u answers a host is given "
          "read by every host that reads one"
          % (len(ops), len(toks), len(held), len(checked), len(writable),
             len(reasons), len(listed),
             len(tools), pythons, shells, len(reading), len(only_a_hole),
             len(NOT_REACHED),
             len(every_code), sentences, len(HELD), halves // 2,
             len(in_widths), len(ANSWERS)))

sys.exit(failed)
PY
