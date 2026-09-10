#!/bin/sh
# This project checks its own work in places nobody looks: that a `no.alloc`
# promise is kept by the code emitted for it and by the body a value call
# enters, that every chunk can be walked instruction by instruction, that no
# `return` gives back more than the declaration a host reads the width from,
# that a handle is what the instruction following it thinks it is, that the
# formatter leaves a file it cannot read alone, that a header declares what
# is there and nothing nothing calls, that a message the reference quotes is one
# a run of this compiler says, that no module includes one below it, that
# every check this project makes is one it runs, that no command answers a file
# with silence, that a refusal between compiling and running is one somebody
# can read, that a formatter keeps every word somebody wrote, and that two
# functions are never compiled under one name, and that two copies of a shape
# are never one type, that asking whether a file is in the one form does not
# write it, that a host lays its own memory where the compiler says a type's
# pieces are, that a number a program can run into is where a reader finds it,
# that a program is told when it has as much of something as it can be told it
# has, that nothing reads memory past the end of it, whether it is the host's
# or a block the arena handed out, that a heap that ran out is still there to
# be asked about, that a call which promises to allocate nothing leaves the heap
# where it found it, that the library is written the way the reference says to
# write it, that a host keeps a promise made on its behalf, that every function
# in the library is named by something that runs, that what a command says to a
# tool is what it says to a reader, and that a machine keeps nothing of the
# host it was started with. Every check this project makes now has a hole of
# its own, which is what makes the list a list rather than a habit. Every one
# of them only fires when this project is wrong.
#
# A net nobody has seen catch anything is indistinguishable from no net. So
# each one is put out of order on purpose, in a copy of the tree, and has to
# be caught. The copy is why this cannot leave the repository broken.
set -u
exec python3 - "$@" <<'PY'
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys
import tempfile

# Each of these is a hole this project has actually had, or the exact shape of
# one. Nothing here is a mutation for its own sake. A hole names what to break
# and either a program to run, which is the compiler catching itself, or a tool
# to run, which is a check catching the tree.
# Two copies of one generic, told apart by type names that agree until the end
# of them. Nothing in this tree is written that way; a generated program is.
LONG = "A" * 40
SHARED_NAME = """struct %sOne {
    n: i32
}

struct %sTwo {
    n: f32
    m: f32
}

fn held<T>(v: T) -> T {
    return v
}

fn main() -> i32 {
    let a = held(%sOne(3))
    let b = held(%sTwo(1.5, 2.5))
    if a.n != 3 {
        return 1
    }
    if b.m != 2.5 {
        return 2
    }
    return 0
}
""" % (LONG, LONG, LONG, LONG)

# Two copies of one shape, told apart by type names that agree until the end.
SHARED_SHAPE = """struct %sOne {
    n: i32
}

struct %sTwo {
    x: f32
    y: f32
}

struct Box<T> {
    held: T
    tag: i32
}

fn main() -> i32 {
    let a: Box<%sOne> = Box(%sOne(3), 1)
    let b: Box<%sTwo> = Box(%sTwo(1.5, 2.5), 2)
    if a.held.n != 3 {
        return 1
    }
    if b.held.y != 2.5 {
        return 2
    }
    return 0
}
""" % (LONG, LONG, LONG, LONG, LONG, LONG)

# How long a broken tree is given to say what is wrong with it. Every hole here
# answers in a moment; the number is a wall to stop a run that will not, not a
# measurement of anything.
A_WHILE = 600

BREAKS = [
    {
        "what": "a tree walk that does not look inside an `if`",
        "file": "src/contract.c",
        "from": """    case KEST_EXPR_IF:
        walk_expr(graph, function, expr->branch->condition);""",
        "to": """    case KEST_EXPR_IF:
        break;
        walk_expr(graph, function, expr->branch->condition);""",
        "program": "no-alloc.kest",
        "source": """fn quiet(n: i32) -> i32 no.alloc {
    if n > 0 {
        let made: [i32] = array()
        push(made, n)
        return len(made)
    }
    return 0
}

fn main() -> i32 {
    return quiet(1) - 1
}
""",
        "caught": "K0405",
    },
    {
        "what": "a jump that says it is a different width",
        "file": "src/value.c",
        "from": """    case U16:
    case JUMP:
    case BACK:
        // A jump carries how far as one number, printed as a place to make it
        // readable. It is the same two bytes.
        return 3;""",
        "to": """    case U16:
        return 3;
    case JUMP:
    case BACK:
        return 5;""",
        "program": "jumping.kest",
        # Enough branching that a walk stepping wrongly cannot land back on
        # the end by luck, which a four instruction program can.
        "source": """fn main() -> i32 {
    let n = 0
    let i = 0
    while i < 3 {
        if i == 1 {
            n += 2
        } else {
            n += 1
        }
        i += 1
    }
    return n - 4
}
""",
        "caught": "K0406",
    },
    {
        "what": "a `return` wider than the function gives back",
        "file": "src/compile.c",
        "from": """        emit(compiler, KEST_OP_RETURN, stmt->span);
        emit_u16(compiler, size, stmt->span);""",
        "to": """        emit(compiler, KEST_OP_RETURN, stmt->span);
        emit_u16(compiler, size + 1, stmt->span);""",
        "program": "returning.kest",
        # A host sizes its frame from the declaration, so a wider return is
        # read back past the end of what the host has.
        "source": """fn twice(n: i32) -> i32 {
    return n * 2
}

fn main() -> i32 {
    return twice(2) - 4
}
""",
        "caught": "K0407",
    },
    {
        # A refusal the machine makes for a host, made without a word. Only a
        # host can be refused for these, and a host in a frame loop reads the
        # answer rather than the words — so a refusal that stops saying which
        # one it is reads exactly like one that still does.
        "what": "a refusal for a host that says nothing",
        "file": "src/vm.c",
        "from": """        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0644", missing,""",
        "to": """        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K9999", missing,""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "refused without saying `K0644`",
    },
    {
        # A function value that stands for a function that is not there. What
        # a name bound to a function becomes is the place it was compiled to,
        # and a place one past the end is a call into whatever is after the
        # last function — so the machine asks, and nothing had ever seen it
        # ask.
        "what": "a function value that stands for nothing",
        "file": "src/compile.c",
        "from": """    KestValue which = {0};
    which.integer = index;""",
        "to": """    KestValue which = {0};
    which.integer = index + 1000;""",
        "make": ["kest"],
        "program": "calling.kest",
        "source": """fn one(n: i32) -> i32 {
    return n
}

fn through(f: fn(i32) -> i32, n: i32) -> i32 {
    return f(n)
}

fn main() -> i32 {
    return through(one, 1) - 1
}
""",
        "caught": "K0609",
    },
    {
        # A constant this compiler cannot work out where it is written. Every
        # constant in this tree folds, so the refusal for one that does not
        # had never been seen — a fault with no net under it, which is the one
        # thing this project says a check may not be.
        "what": "a constant that cannot be worked out, refused by nobody",
        "file": "src/compile.c",
        "from": """        if (kest_fold_const(compiler->program, symbol->value, values, slots,
                            &why) != slots) {""",
        "to": """        if (kest_fold_const(compiler->program, symbol->value, values, slots,
                            &why) != (uint32_t)(slots + 1)) {""",
        "make": ["kest"],
        "program": "constant.kest",
        "source": """const LIMIT: i32 = 10

fn main() -> i32 {
    return LIMIT - 10
}
""",
        "caught": "K0504",
    },
    {
        # A host's own string put in a frame and taken as the program's. Text
        # a program holds is on the heap the machine keeps, and a pointer into
        # the host's own memory outlives nothing the machine knows about — so
        # a frame is refused for it, and this is what says which refusal that
        # is.
        "what": "a host's own string in a frame that says nothing",
        "file": "src/vm.c",
        "from": """            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                           nowhere,
                           "`%s` takes text in slot %u and this did not come "
                           "from this machine",""",
        "to": """            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K9997",
                           nowhere,
                           "`%s` takes text in slot %u and this did not come "
                           "from this machine",""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "refused without saying `K0636`",
    },
    {
        # A check that gains a sentence nothing has ever made it say. What a
        # check says when something is wrong is a claim about this tree, and a
        # claim nobody has seen made is one nobody knows is right.
        "what": "a check that says something no hole has made it say",
        "file": "tools/check-header.sh",
        "from": """# The header must not reach into the implementation""",
        "to": """if [ -n "${KEST_NOWHERE:-}" ]; then
    echo "header: something nothing has ever seen this say"
    exit 1
fi

# The header must not reach into the implementation""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "and nothing has ever made it",
    },
    {
        # The pipeline in `CLAUDE.md` is the list of what this compiler is made
        # of and the order the modules may include each other in, and it is
        # held to `src` both ways. A name written there that is not a file is a
        # reader sent to a module that does not exist.
        "what": "a pipeline naming a module that is not there",
        "file": "CLAUDE.md",
        "from": r"""lexer    source -> tokens""",
        "to": r"""lexer    source -> tokens
tokens   what a token is and what it carries""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "the pipeline names `tokens` and `src` has no such file",
    },
    {
        # And the other way: a module in the tree that the pipeline does not
        # name. Then nothing says what it may include, and the one rule that
        # keeps this compiler from growing a cycle is a rule it is not under.
        "what": "a module the pipeline does not name",
        "file": "CLAUDE.md",
        "from": r"""ast      syntax tree node definitions
""",
        "to": "",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "`src/ast.c` is in the tree and not in the pipeline",
    },
    {
        # A command the command line answers to and `help` does not print. What
        # a reader is told this program does is `help`, so a command missing
        # from it is one nobody will ever type, and it is held to what `main`
        # compares against because two lists is what this is.
        "what": "a command `help` stopped printing",
        "file": "src/main.c",
        "from": r"""            "  parse <file>...   print the syntax tree\n"
""",
        "to": "",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "runs and `kest help` does not say so",
    },
    {
        # And the other way: `help` printing a command nothing answers to. A
        # reader types it and is told there is no such command by the program
        # that just offered it.
        "what": "a command `help` prints that nothing answers to",
        "file": "src/main.c",
        "from": r"""            "  parse <file>...   print the syntax tree\n"
""",
        "to": r"""            "  parse <file>...   print the syntax tree\n"
            "  dump <file>...    print whatever there is\n"
""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "prints `dump` and nothing answers to it",
    },
    {
        # The same for an option. `help` is what says which of them there are,
        # and one printed that nothing reads is an option a reader hands over
        # and a program ignores without a word.
        "what": "an option `help` prints that nothing reads",
        "file": "src/main.c",
        "from": r"""            "  -w                fmt writes each file it is given\n"
""",
        "to": r"""            "  -w                fmt writes each file it is given\n"
            "  --quiet           say less about what happened\n"
""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "prints `--quiet` and nothing reads it",
    },
    {
        # An option the command line reads that nothing here ever hands it.
        # Every option this program has is used by a check somewhere, which is
        # what makes the list a thing that has been run rather than a list that
        # has been read; one nothing runs is a path nobody has walked.
        "what": "an option nothing in this tree ever hands the command line",
        "file": "src/main.c",
        "from": r"""    bool json = false;""",
        "to": r"""    bool json = false;
    bool quiet = false;""",
        "also": ["src/main.c", r"""        if (strcmp(argv[i], "--json") == 0) {""",
                 r"""        if (strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
            (void)quiet;
        }
        if (strcmp(argv[i], "--json") == 0) {"""],
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "`kest --quiet` is answered and nothing runs it",
    },
    {
        # A target this file tells a reader to type that the `Makefile` has
        # not got. Everything here is meant to be run, and a document that says
        # how and is wrong is worse than one that says nothing: the reader
        # believes it.
        "what": "a document naming a target the Makefile has not got",
        "file": "CLAUDE.md",
        "from": r"""`frame.kest` is the one measurement, run by `make time`.""",
        "to": r"""`frame.kest` is the one measurement, run by `make timing`.""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "says to run `make timing` and the `Makefile` has no such",
    },
    {
        # A check added to `tools` that nobody made runnable. A file written
        # there is written with the rights a new file gets, and the gate asks
        # it by running it: one that cannot be run is a check that is never
        # asked and never missed, because what asks it is a list of files and
        # the file is there.
        "what": "a check in `tools` that nothing can run",
        "file": "tools/check-tables.sh",
        "from": r"""tools = some("the checks in `tools`", sorted(""",
        "to": r"""tools = some("the checks in `tools`", sorted(""",
        "program": "tools/check-nothing.sh",
        "source": "#!/bin/sh\nset -u\nexit 0\n",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "is a check and is not something to run",
    },
    {
        # A check that says a shell nobody here runs it by. Every one of them
        # begins `#!/bin/sh`, which is what says the shell it is written in;
        # written another way it still runs on this machine and is a check
        # written in whatever the machine happens to have.
        "what": "a check that says another shell runs it",
        "file": "tools/check-lends.sh",
        "from": "#!/bin/sh\n",
        "to": "#!/usr/bin/env sh\n",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "does not say what runs it",
    },
    {
        # A check that carries on with a name nobody set. `set -u` is what
        # turns a misspelt name into a stop rather than an empty string, and a
        # sweep over an empty path is a sweep over everything or over nothing,
        # either of which passes.
        "what": "a check that carries on with a name nobody set",
        "file": "tools/check-header.sh",
        "from": "\nset -u\n",
        "to": "\n",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "does not stop on a name nobody set",
    },
    {
        # A check that makes somewhere to work and leaves it there. Nine
        # hundred of those filled a machine once and the gate stopped at `No
        # space left on device`, which is a thing nobody sees until it happens
        # all at once.
        "what": "a check that leaves the room it took",
        "file": "tools/check-header.sh",
        "from": "trap 'rm -rf \"$work\"' EXIT\n",
        "to": "",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "makes somewhere to work and does not take it away",
    },
    {
        # And a check with two rooms and one `trap`. A second `trap ... EXIT`
        # replaces the first rather than adding to it, so a check that reads as
        # though it hands back both hands back one — which is why what takes a
        # room away is written once and everything else is a directory under
        # the room already taken.
        "what": "a check that takes a second room",
        "file": "tools/check-lends.sh",
        "from": "scratch=$(mktemp -d)\n",
        "to": "scratch=$(mktemp -d)\naside=$(mktemp -d)\n",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "makes 2 places to work, and what takes one away is ",
    },
    {
        # A check `CLAUDE.md` names that is not in `tools`. Three lists say
        # which checks this project makes — the files, what this file says, and
        # what the gate reaches for — and a name in one that is in neither of
        # the others is a check a reader will go looking for.
        "what": "a check the documents name that is not there",
        "file": "CLAUDE.md",
        "from": r"""`check-lends.sh` holds what a host says when it lends: a""",
        "to": r"""`check-lending.sh` holds what a host says when it lends: a""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "is not in `tools`",
    },
    {
        # A guard the gate makes about itself that this file does not say it
        # makes. Those guards have no holes, so what stands for one is the
        # list: a line deleted from the middle of the gate is a check that no
        # longer happens, and the run reads the same as it did the day before.
        "what": "a guard the gate makes that the documents do not say it does",
        "file": "CLAUDE.md",
        "from": r"""returns      files written on the spot: line endings, noughts inside text,
             and a promise around a `defer`
""",
        "to": "",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "`CLAUDE.md` does not say it does",
    },
    {
        # The gate building before it reaches for what it built. A probe that
        # asks a binary that is not there is a probe that passes when the
        # command fails, and the order is the one thing about the gate that is
        # not a list — so what says the order is right is finding both lines
        # and finding them the right way round.
        "what": "a gate whose build cannot be found",
        "file": "tools/check.sh",
        "from": "if ! make >/dev/null 2>\"$scratch\"/check-why; then",
        "to": "if ! ${MAKE:-make} >/dev/null 2>\"$scratch\"/check-why; then",
        "also": ["tools/check.sh",
                 "if ! make debug embed embed-debug >/dev/null",
                 "if ! ${MAKE:-make} debug embed embed-debug >/dev/null"],
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "does not build, or never reaches for what it built",
    },
    {
        # A builtin nothing suggests. The list a message suggests from is what
        # a reader who typed a name nearly right is offered, so one missing
        # from it is a name the compiler has and nobody is ever told about —
        # and the checker still answers to it, so nothing else here notices.
        "what": "a builtin no message suggests",
        "file": "src/check.c",
        "from": r"""    "add", "array", "clear", "find",  "get",   "hash", "len", "matches",""",
        "to": r"""    "add", "array", "clear", "find",  "get",   "len", "matches",""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "does not know `hash`",
    },
    {
        # And the other way: a name suggested that is not a builtin. Then a
        # reader who typed something nearly right is told to write a name the
        # checker will refuse, which is a suggestion that costs more than
        # saying nothing.
        "what": "a message suggesting a name that is not a builtin",
        "file": "src/check.c",
        "from": r"""    "pop", "push",  "remove", "rest", "set",   "slice", "store",""",
        "to": r"""    "pop", "push",  "remove", "rest", "set",   "slice", "store", "take",""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "the suggestion knows `take` and the checker does not",
    },
    {
        # And the proof of a `no.alloc` promise, which has an opinion per
        # builtin: a reason it reaches the heap, or nothing. One it knows that
        # the checker does not is a row nothing will ever be asked about, and
        # the pair of lists is the only thing that says so.
        "what": "a promise's proof with an opinion about nothing",
        "file": "src/contract.c",
        "from": r"""                {"add", "`add` grows what it is given"},""",
        "to": r"""                {"add", "`add` grows what it is given"},
                {"append", "`append` grows what it is given"},""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "the promise's proof knows `append` and the checker does",
    },
    {
        # A builtin the reference never writes. What each of them takes is
        # named in the checker's messages and learned by a reader from the
        # page, so a builtin with no `name(...)` written anywhere is one whose
        # message says `from` to somebody who has never met `from`.
        "what": "a builtin the reference never writes out",
        "file": "docs/language.md",
        "from": r"""`slice(t, from, count)` makes a new piece of text""",
        "to": r"""Slicing makes a new piece of text""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "the reference never writes `slice(...)`",
    },
    {
        # And the names disagreeing. A message that says `from` is worth more
        # than one that says `this argument` only because the reader has met
        # `from` on the page, so the two lists are one list written twice.
        "what": "a builtin whose parts the reference calls something else",
        "file": "docs/language.md",
        "from": r"""`rest(t, at)` is what is left of `t` from `at`""",
        "to": r"""`rest(t, start)` is what is left of `t` from `start`""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "and the reference calls them",
    },
    {
        # A table read with a pattern that stops matching. The names of the
        # tokens are read out of the one place they are written, and a brace on
        # the next line is the same C and a list nothing here can find — which
        # would be a check holding every one of no token names to its kind.
        "what": "a table of names a check can no longer find",
        "file": "src/lexer.c",
        "from": r"""static const char *const TOKEN_NAMES[] = {""",
        "to": r"""static const char *const TOKEN_NAMES[] =
{""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "nothing here matches /",
    },
    {
        # A name out of step with the kind it belongs to. The count is held
        # while the tree is built, so two names swapped is a list of the right
        # length in the wrong order — every message about one of those two
        # instructions names the other, which reads as a compiler that emitted
        # something else.
        "what": "two instruction names in each other's places",
        "file": "src/value.c",
        "from": r"""    {"call", U16_U16},     {"call.value", U16},""",
        "to": r"""    {"call.value", U16_U16},     {"call", U16},""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": " is KEST_OP_CALL and is called ",
    },
    {
        # A name for an instruction that is not there. How many there are is
        # held while the tree is built, and this is the same question asked
        # again by the check that holds which — so what says the second reading
        # is worth having is the first one taken away and a name put in.
        "what": "a name for an instruction there is not, with nothing counting",
        "file": "src/value.c",
        "from": r"""_Static_assert(sizeof(INSTRUCTIONS) / sizeof(INSTRUCTIONS[0]) ==
                   KEST_OP_RETURN + 1,
               "every instruction has a name and nothing else does");""",
        "to": "",
        "also": ["src/value.c", r"""    {"return", U16},""",
                 r"""    {"return", U16}, {"return.none", U16},"""],
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "instructions: 146 kinds and 147 names",
    },
    {
        # And the same for the tokens, which is the other list this rule was
        # written for: a name for a kind of token there is not, with the count
        # that would have stopped the build taken out from under it.
        "what": "a name for a token there is not, with nothing counting",
        "file": "src/lexer.c",
        "from": r"""_Static_assert(sizeof(TOKEN_NAMES) / sizeof(TOKEN_NAMES[0]) ==
                   KEST_TOK_ERROR + 1,
               "every token kind has a name and nothing else does");""",
        "to": "",
        "also": ["src/lexer.c", r"""    "end of file", "end of line", "identifier", "integer",  "float",""",
                 r"""    "end of file", "end of line", "start of file", "identifier",
    "integer",  "float","""],
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "tokens: 67 kinds and 68 names",
    },
    {
        # A check named as one that says nothing unwatched, under a name it
        # does not have. The list is what says which checks are at nought, so a
        # name in it that is not a file is a check nothing reads and a rule
        # nothing holds — and the rest of the list still passes, which is what
        # makes it quiet.
        "what": "a check held to what it says under a name it has not got",
        "file": "tools/check-tables.sh",
        "from": r"""HELD = ("check-ceilings.sh", "check-commands.sh", "check-costs.sh",""",
        "to": r"""HELD = ("check-ceiling.sh", "check-commands.sh", "check-costs.sh",""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "is held to what it says and is not there",
    },
    {
        # And a sentence written down as one nothing can make a check say, that
        # the check no longer says. Those are the way out of the rule, so one
        # that has gone stale is a hole in it: the sentence it stood for may
        # have come back under other words and nothing would ask about it.
        "what": "a sentence written down as unreachable that is not said",
        "file": "tools/check-tables.sh",
        "from": r"""NOT_SAID = (("check-lends.sh", "the host that lends by name does not build"),""",
        "to": r"""NOT_SAID = (("check-lends.sh", "the host that lends by name will not build"),""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "is written down as one nothing can make ",
    },
    {
        # A ceiling written as something this cannot read. Every number a
        # program can run into is read out of the `#define` that holds it and
        # held to the table the reference prints; one written as a sum is a
        # number the reference is no longer held to, and the row for it becomes
        # a row nothing enforces without either of them changing.
        "what": "a ceiling written as a sum",
        "file": "src/compile.c",
        "from": r"""#define MAX_LOCALS 256""",
        "to": r"""#define MAX_LOCALS (255 + 1)""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "and this does not know what that is",
    },
    {
        # A number the reference says there is a most of that nothing holds a
        # program to. A reader is told a program may have so many of something
        # and finds out otherwise by writing one, which is the direction that
        # costs the reader rather than the compiler.
        "what": "a ceiling the reference says that nothing holds",
        "file": "docs/language.md",
        "from": r"""| 256 | names in a function, counting its parameters |""",
        "to": r"""| 256 | names in a function, counting its parameters |
| 48 | modules one program may import |""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "and nothing holds a program to it",
    },
    {
        # A keyword the reference prints that the lexer does not hold. A word
        # is a keyword only when a program that used it as a name would be
        # ambiguous, and the cost of one is paid by everybody who wanted the
        # name — so a word written on that page and not in the lexer is a name
        # taken from a reader by a document.
        "what": "a keyword the reference prints and the lexer has not got",
        "file": "docs/language.md",
        "from": r"""match   module  none      return  struct  true    while""",
        "to": r"""match   module  none      return  struct  true    while
yield""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "and the lexer does not hold it",
    },
    {
        # A type the machine writes that the checker says has no text. The two
        # switches are one list: what the checker lets into a hole is what the
        # machine has to be able to write, and a case that writes one the
        # checker refuses is a path nothing can reach, written to look like
        # something a reader could use.
        "what": "a type the machine writes and the checker refuses",
        "file": "src/vm.c",
        "from": r"""    case KEST_T_STRUCT:
    case KEST_T_ARRAY:
    case KEST_T_FIXED:
    case KEST_T_REF:
    case KEST_T_STORE:
    case KEST_T_FN:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        break;
    }
    // Nothing reaches this""",
        "to": r"""    case KEST_T_STRUCT:
    case KEST_T_ARRAY:
        return put_text(out, room, "[...]");
    case KEST_T_FIXED:
    case KEST_T_REF:
    case KEST_T_STORE:
    case KEST_T_FN:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        break;
    }
    // Nothing reaches this""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "and the checker says it cannot ",
    },
    {
        # The same thing one layer down: a body that breaks a promise in three
        # places, reported as one. The chain through calls is a chain and stays
        # one; what a body does is a list. See D509.
        "what": "a promise broken in several places, reported once",
        "file": "src/contract.c",
        "from": r"""    if (function->site_count == MORE_SITES) {""",
        "to": r"""    if (function->site_count == 0) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0401 said",
    },
    {
        # A ceiling met in a tree with the number lowered, so what a program is
        # refused at there says nothing about what the table prints. The table
        # could print anything for either of these two. See D522.
        "what": "a lowered ceiling whose real number is written nowhere held",
        "file": "docs/language.md",
        "from": r"""| 65536 | names a program asks the host for |""",
        "to": r"""| 32768 | names a program asks the host for |""",
        "make": [],
        "tool": "tools/check-ceilings.sh",
        "arguments": [],
        "caught": "limits: the table says 32768 names",
    },
    {
        # And the row taken out altogether, which the walk above passes over
        # because a lowered ceiling is not one it can run into.
        "what": "a lowered ceiling the table stops naming",
        "file": "docs/language.md",
        "from": r"""| 65536 | names a program asks the host for |
""",
        "to": "",
        "make": [],
        "tool": "tools/check-ceilings.sh",
        "arguments": [],
        "caught": "and one of the two is not there",
    },
    {
        # The number a message has to say, taken from this list instead of
        # from the table it is meant to hold. The comment above it said the
        # define, the table and the words were kept in step; the table could
        # say anything. See D521.
        "what": "a ceiling written in the table and nowhere held",
        "file": "docs/language.md",
        "from": r"""| 32 | `defer`s in a function |""",
        "to": r"""| 48 | `defer`s in a function |""",
        "make": [],
        "tool": "tools/check-ceilings.sh",
        "arguments": [],
        "caught": "with 48 in it",
    },
    {
        # And a probe looking for a row that is not there, which used to run
        # against a number of its own and pass.
        "what": "a probe for a ceiling the table does not name",
        "file": "tools/check-ceilings.sh",
        "from": r"""    ("`defer`s in a function", defers, "K0502"),""",
        "to": r"""    ("`defer`s in a body", defers, "K0502"),""",
        "make": [],
        "tool": "tools/check-ceilings.sh",
        "arguments": [],
        "caught": "so there is no number to hold its message to",
    },
    {
        # A type the compiler has and the reference does not offer. `void` was
        # one until D519, and what it cost was a reader writing a type nothing
        # could tell them about. See D520.
        "what": "a primitive in the table and in no document",
        "file": "src/types.c",
        "from": r"""           add_primitive(program, "f64", KEST_T_FLOAT, 64, false);""",
        "to": r"""           add_primitive(program, "f64", KEST_T_FLOAT, 64, false) &&
           add_primitive(program, "f16", KEST_T_FLOAT, 16, false);""",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "arguments": [],
        "caught": "the compiler registers `f16` and the reference does not",
    },
    {
        # And the other way: a type the reference offers and nothing has, which
        # is a reader refused for writing what they were told to write.
        "what": "a primitive in a document and in no table",
        "file": "docs/language.md",
        "from": r"""Primitives: `i8 i16 i32 i64`, `u8 u16 u32 u64`, `f32 f64`, `bool`, `text`.""",
        "to": r"""Primitives: `i8 i16 i32 i64`, `u8 u16 u32 u64`, `f32 f64 f128`, `bool`, `text`.""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "arguments": [],
        "caught": "the reference says `f128` and the compiler does not register",
    },
    {
        # And the one that is registered on purpose and written by nobody. A
        # reference that offers it is a reference offering the second spelling
        # D519 took away.
        "what": "the one type there is no way to write, offered",
        "file": "docs/language.md",
        "from": r"""Primitives: `i8 i16 i32 i64`, `u8 u16 u32 u64`, `f32 f64`, `bool`, `text`.""",
        "to": r"""Primitives: `i8 i16 i32 i64`, `u8 u16 u32 u64`, `f32 f64`, `bool`, `text`, `void`.""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "arguments": [],
        "caught": "the reference offers `void`, and it is the one type there",
    },
    {
        # A second spelling of writing nothing, which compiled. `fn f() ->
        # void` and `fn f()` were one function written two ways, in a language
        # that refuses `if (x < 3)` for exactly that. See D519.
        "what": "a second spelling of giving nothing back",
        "file": "src/types.c",
        "from": r"""    if (type != NULL && type->tag == KEST_T_VOID) {""",
        "to": r"""    if (type != NULL && type->tag == KEST_T_VOID && false) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0357 said",
    },
    {
        # The one place a value that is not one went in without a word: an
        # array held them, laid them out, and counted them. See D518.
        "what": "an array holding what gives nothing",
        "file": "src/check.c",
        "from": r"""            report(checker, word_of(expr->array.items[i]), "K0356",
                   "this gives nothing back, and an array holds values");""",
        "to": r"""            (void)0;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0356 said",
    },
    {
        # A name bound to what gives nothing, which was accepted in silence
        # and refused wherever the name was read, with a message about `void`
        # and a caret nowhere near the `let`. See D517.
        "what": "a name bound to nothing, taken quietly",
        "file": "src/check.c",
        "from": r"""            report(checker, word_of(stmt->let.value), "K0356",
                   "this gives nothing back, and a `let` names a value");""",
        "to": r"""            (void)0;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0356 said",
    },
    {
        # Arms written as blocks where values were meant, which is one mistake
        # and used to be one message per arm, none of them about the arms.
        # See D516.
        "what": "arms of a block shape, said once for each",
        "file": "src/check.c",
        "from": r"""            report(checker, word_of(expr), "K0345",
                   "this `if` gives nothing, and both its arms end in a "
                   "value");""",
        "to": r"""            (void)0;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0345 said",
    },
    {
        # A pointer where a type was wanted, met by the token and not by what
        # this language has instead of one. See D515.
        "what": "a type refused without what there is instead",
        "file": "src/parser.c",
        "from": r"""            kest_diags_suggest(parser->diags,
                               "there are no pointers here: what names a slot "
                               "in a store is `ref<T>`");""",
        "to": r"""            (void)0;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0203 said",
    },
    {
        # A block where a value was wanted, which is a rule about what a block
        # is and was a message about a brace. See D515.
        "what": "a block refused without what a block is",
        "file": "src/parser.c",
        "from": r"""            kest_diags_suggest(parser->diags,
                               "a block is not a value: an `if` gives one "
                               "with `->`");""",
        "to": r"""            (void)0;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0204 said",
    },
    {
        # A field written the way another language writes one, met by the
        # token this wanted and not by which of the two orders is right.
        # See D514.
        "what": "a field refused without the shape of one",
        "file": "src/parser.c",
        "from": r"""        kest_diags_suggest(parser->diags,
                           "a field is written `name: type`");""",
        "to": r"""        (void)0;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0201 said",
    },
    {
        # `flags` that neither arm can read, which fell to the end of the
        # function and was told a file holds `flags` with the caret on the
        # word `flags`. See D514.
        "what": "a flag set pointed at from the wrong end",
        "file": "src/parser.c",
        "from": r"""    if (is_word(parser, 0, "flags")) {
        advance(parser);
        if (expect(parser, KEST_TOK_IDENT)) {""",
        "to": r"""    if (is_word(parser, 0, "flags") && false) {
        advance(parser);
        if (expect(parser, KEST_TOK_IDENT)) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0201 said",
    },
    {
        # The same shape a fourth time: the one place a binding is written,
        # met by a message about the token that was not a name. See D513.
        "what": "a binding refused without the rule behind it",
        "file": "src/parser.c",
        "from": r"""        kest_diags_suggest(parser->diags,
                           "`%s let` names what is held rather than comparing "
                           "with it", what);""",
        "to": r"""        (void)what;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0201 said",
    },
    {
        # And the walk, which writes a name in the same place for the same
        # reason and had the same nothing to say about it.
        "what": "a walk refused without the rule behind it",
        "file": "src/parser.c",
        "from": r"""            kest_diags_suggest(parser->diags,
                               "a `for` names what it walks over: "
                               "`for one in ...`");""",
        "to": r"""            (void)0;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0201 said",
    },
    {
        # A rule met by a message about a token, which is the shape D506
        # found twice. The parser refuses the arm before the checker reads
        # the subject, so K0331 never gets a chance to say what a `match`
        # chooses between. See D512.
        "what": "a `match` arm refused without the rule behind it",
        "file": "src/parser.c",
        "from": r"""                    kest_diags_suggest(parser->diags,
                                       "a `match` arm names a case of an "
                                       "enum, and `else` answers the rest");""",
        "to": r"""                    (void)0;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0201 said",
    },
    {
        # A refusal that names the first thing wrong and stops. The checker
        # knows every unanswered combination before it says anything, so a
        # list cut to one is a reader compiling once per case. See D507.
        "what": "a list of what is unanswered, cut to its first",
        "file": "src/check.c",
        "from": r"""                if (named < NAMED_AT_MOST &&""",
        "to": r"""                if (named < 1 &&""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0333 said",
    },
    {
        # A version that answers a number nobody can use. What a build system
        # does with `--version` is read what it says and look at the status,
        # and a status of 1 with the right words in it reads as a program that
        # is not there.
        "what": "a version that says its name and refuses",
        "file": "src/main.c",
        "from": r"""        printf("kest %s%s\n", kest_version(), KEST_CHECKED ? " checked" : "");
        return 0;""",
        "to": r"""        printf("kest %s%s\n", kest_version(), KEST_CHECKED ? " checked" : "");
        return 1;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "came back with something to say and a number",
    },
    {
        # The three ways of asking for help, saying three things. A reader who
        # typed one of them has read the other two nowhere, so `-h` printing
        # less than `help` is a reader who never finds out what the rest of it
        # says.
        "what": "one way of asking for help that says less than another",
        "file": "src/main.c",
        "from": r"""    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0) {
        help(stdout);""",
        "to": r"""    if (strcmp(argv[1], "-h") == 0) {
        printf("kest <command> <file>...\n");
        return 0;
    }
    if (strcmp(argv[1], "help") == 0 ||
        strcmp(argv[1], "--help") == 0) {
        help(stdout);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "does not say what `help` says",
    },
    {
        # An option the command line answers to that `help` does not print.
        # `--version` is the one a build system reaches for first, and a reader
        # who cannot find it in `help` has no reason to think it is there.
        "what": "the version option missing from what `help` prints",
        "file": "src/main.c",
        "from": r"""            "  --version         print the version\n"
""",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "does not say the command line answers `--version`",
    },
    {
        # Notes left out of the JSON. A diagnostic about more than one place
        # carries a note per place, each with its own line, and a tool reading
        # the JSON is told about one place — the rest of what was wrong is in
        # the words and nowhere a tool can reach.
        "what": "a diagnostic whose notes the JSON leaves out",
        "file": "src/diag.c",
        "from": r"""        if (diag->note_count > 0) {
            fputs(",\"notes\":[", out);""",
        "to": r"""        if (false) {
            fputs(",\"notes\":[", out);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "has a note that names anything",
    },
    {
        # A diagnostic that gives the file its first note is in. A promise
        # made in one module and broken in another is one diagnostic about two
        # files, and this makes the two read as one: a tool is told the fault
        # is where the promise was written rather than where it was broken,
        # and every note then looks like a note about its own file.
        "what": "a diagnostic that says it is in the file its note is in",
        "file": "src/diag.c",
        "from": r"""            fputs(",\"file\":", out);
            kest_json_text(source->path, out);""",
        "to": r"""            fputs(",\"file\":", out);
            kest_json_text(diag->note_count > 0 &&
                                   diag->notes[0].source != NULL
                               ? diag->notes[0].source->path
                               : source->path,
                           out);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "has a note about a file other than its own",
    },
    {
        # A refusal a file meets before it means anything, reworded. The table
        # names the code and some of the words, so a message written another
        # way is a refusal nothing has been seen saying — and the code is still
        # right, which is what makes it look like nothing happened.
        "what": "a refusal reworded under the table that names its words",
        "file": "src/lexer.c",
        "from": r"""                           "string is not terminated");""",
        "to": r"""                           "this string has no end");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0101 said `",
    },
    {
        # And the same for a refusal a program meets while it runs. Those are
        # the ones a reader meets last and reads in a hurry, and the table that
        # names them is the only thing that has ever seen most of them.
        "what": "a running refusal reworded under the table that names it",
        "file": "src/vm.c",
        "from": r"""        case KEST_OP_MOD_I: {
            KestValue right = *--top;
            KestValue left = *--top;
            if (right.integer == 0) {
                fail(vmp, frame, instruction, "K0601", "division by zero");""",
        "to": r"""        case KEST_OP_MOD_I: {
            KestValue right = *--top;
            KestValue left = *--top;
            if (right.integer == 0) {
                fail(vmp, frame, instruction, "K0601", "divided by nought");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0601 said `",
    },
    {
        # `call` with no function named. The command line has refusals of its
        # own for what it was asked rather than for what a file says, and this
        # is the one a reader meets by typing half a command.
        "what": "a command line that says nothing about a call with no name",
        "file": "src/main.c",
        "from": r"""                               nowhere, "`call` was given no function to call");""",
        "to": r"""                               nowhere, "nothing was named to call");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "`call` with no function said `",
    },
    {
        # And `kest` with nothing after it, which is what somebody types first.
        # A refusal there is the first thing this program says to anybody, and
        # nothing but this had ever read it.
        "what": "a command line that says nothing about being typed alone",
        "file": "src/main.c",
        "from": r"""                             "there is no command in what was typed");""",
        "to": r"""                             "nothing was typed");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "with nothing after it said `",
    },
    {
        # A command nobody has. `K0649` says seven things and a check that
        # reads only the code reads none of them, so the words are asked for
        # by name — and a reader who misspells a command is met by whichever of
        # the seven this is.
        "what": "a refusal for a command nobody has, reworded",
        "file": "src/main.c",
        "from": r"""    refused_at_the_words(json, "K0649", "unknown command `%s`", argv[1]);""",
        "to": r"""    refused_at_the_words(json, "K0649", "no command called `%s`", argv[1]);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "`kest nonsense` said `",
    },
    {
        # A condition inside brackets, taken. `if (x < 3) { }` means what
        # `if x < 3 { }` means and the formatter takes the brackets away, so a
        # parser that takes both is a language with two spellings of one thing
        # — and the second is the one every file here would be rewritten out
        # of, silently, by the tool a reader runs to tidy it.
        "what": "a condition inside brackets, taken",
        "file": "src/parser.c",
        "from": r"""    if (wrapped_whole(parser)) {
        refuse_wrapped(parser, "if");
        return NULL;
    }""",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0213 said `",
    },
    {
        # The compound assignments this language has not got, taken. Four are
        # written and the rest are not, because a fifth that appears once in a
        # file is written out — and a reader who reaches for one gets the
        # parser meeting an `=` where a value belongs, which says where it
        # stopped and not what is wrong.
        "what": "a compound assignment this language has not got, taken",
        "file": "src/parser.c",
        "from": r"""    const char *compound = no_compound(parser, &compound_at);
    if (compound != NULL) {""",
        "to": r"""    const char *compound = no_compound(parser, &compound_at);
    if (false) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "K0214 said `",
    },
    {
        # A value-giving `if` written where a statement belongs, measured
        # against nothing. It is a `return` with the word left off, and the
        # function is told exactly that — but the arms are then held to
        # whatever they can be on their own, so `none` has nothing to become
        # and the reader is sent to a third message about a place the mistake
        # is not.
        "what": "a value where a statement belongs, measured against nothing",
        "file": "src/check.c",
        "from": r"""            !does_something && stmt->kind == KEST_STMT_EXPR ? checker->result
                                                            : NULL);""",
        "to": r"""            false ? checker->result : NULL);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a value where a statement belongs was told about none",
    },
    {
        # A copy of a generic struct that nothing is said to name. A copy is
        # made because something asked for it — `Box<i32>` on a `let`, in a
        # signature, or built by naming the shape — so a copy nobody named is
        # a copy that would not exist. Without that, every program declaring a
        # generic struct of its own is warned about the copy it just made, and
        # nothing here could show it: the one generic struct in this tree is
        # the library's, and that warning is about the file that was named.
        "what": "a copy of a generic said to be named by nothing",
        "file": "src/types.c",
        "from": r"""    made->named = true;""",
        "to": r"""    made->named = false;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a copy of a generic was said to be named by nothing",
    },
    {
        # A name from a module the file never asked for. It is the one refusal
        # that says a program is reaching past what it imported, and the words
        # are what tell a reader that importing is the fix rather than
        # spelling.
        "what": "a name from a module nobody imported, refused in other words",
        "file": "src/check.c",
        "from": r"""    report(checker, name, "K0325", "this file does not import `%.*s`",""",
        "to": r"""    report(checker, name, "K0325", "`%.*s` was not asked for here",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "did not ask for said `",
    },
    {
        # A file with no module line. Its names have nowhere to live, and what
        # says so is the one refusal that is about where a file sits rather
        # than about what is in it.
        "what": "a file that names no module, refused in other words",
        "file": "src/loader.c",
        "from": r"""                       "`%s` names no module, so its names have nowhere to "
                       "live",""",
        "to": r"""                       "`%s` has no module line",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a file with no module line said `",
    },
    {
        # Two modules under one name, allowed. Where a module's names go is the
        # program's rather than the file's, so two of them ending in the same
        # word share a namespace and a file importing one finds the other's
        # names without asking — which is what happened while this was a
        # question about one file rather than about the program.
        "what": "two modules that put their names in one place, allowed",
        "file": "src/types.c",
        "from": r"""            if (strcmp(units->items[i].alias, units->items[j].alias) != 0) {
                continue;
            }""",
        "to": r"""            if (units->items[i].alias[0] != units->items[j].alias[0]) {
                continue;
            }
            if (units->items[i].from_library == units->items[j].from_library) {
                continue;
            }
            if (true) {
                continue;
            }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "two modules under one name said `",
    },
    {
        # The tables of refusals read under names they no longer have. Every
        # sweep here refuses to find nothing, because a reading that finds none
        # of a list holds none of it — and this one would then say that every
        # one of no refusals has been seen, which is the shape of a check that
        # passes without asking anything.
        "what": "the tables of refusals read under other names",
        "file": "tools/check-commands.sh",
        "from": r"""for kind in ('REFUSED', 'RUNNING'):""",
        "to": r"""for kind in ('REFUSALS', 'RUNS'):""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "nothing here reads as a table of refusals",
    },
    {
        # A token the printed form leaves out. The two forms of `lex` are one
        # answer said twice, and the one a person reads is the one nobody
        # compares against anything: a token missing from it is a reader shown
        # a file with a piece of it gone, while every tool sees the whole.
        "what": "a token the printed form of `lex` leaves out",
        "file": "src/main.c",
        "from": r"""static void dump_tokens(const KestToken *tokens, uint32_t count,
                        const KestSource *source) {
    for (uint32_t i = 0; i < count; i++) {""",
        "to": r"""static void dump_tokens(const KestToken *tokens, uint32_t count,
                        const KestSource *source) {
    for (uint32_t i = 0; i + 1 < count; i++) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "tokens printed, ",
    },
    {
        # A constant in what `check` prints and not in what it writes for a
        # tool. The two forms are one answer said twice, and this is the half a
        # tool reads: a name missing from it is a program that reads as smaller
        # than it is to everything that is not a person.
        "what": "a constant `check` prints and the JSON leaves out",
        "file": "src/types.c",
        "from": r"""    fputs("],\"constants\":[", out);
    first = true;
    for (uint32_t i = 0; i < program->global_count; i++) {""",
        "to": r"""    fputs("],\"constants\":[", out);
    first = true;
    for (uint32_t i = program->global_count; i < program->global_count; i++) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/lookup.kest"],
        "caught": "printed and not in the JSON: ",
    },
    {
        # And the other half of the same comparison: a walk of a chunk that
        # stops short in the JSON. The two forms are one walk said twice, so a
        # tool is handed a function that ends where it does not while the
        # listing a person reads goes on to the end.
        "what": "a chunk the JSON stops short of",
        "file": "src/value.c",
        "from": r"""        uint32_t offset = 0;
        bool first = true;
        while (offset < chunk->code_count) {
            uint8_t op = chunk->code[offset];
            fputs(first ? "" : ",", out);""",
        "to": r"""        uint32_t offset = 0;
        bool first = true;
        while (offset + 3 < chunk->code_count) {
            uint8_t op = chunk->code[offset];
            fputs(first ? "" : ",", out);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/lookup.kest"],
        "caught": "instructions printed, ",
    },
    {
        # And the other direction of the first pair: a constant in the JSON and
        # not in what `check` prints. That is the half a person reads, so a
        # name missing from it is a reader told a program declares less than it
        # does while every tool sees the whole of it.
        "what": "a constant the JSON has and `check` does not print",
        "file": "src/types.c",
        "from": r"""        if (type->tag != KEST_T_FN) {
            said++;
            fprintf(out, "const %s: %s\n", symbol->name,
                    kest_type_name(arena, type));
            continue;
        }""",
        "to": r"""        if (type->tag != KEST_T_FN) {
            said++;
            continue;
        }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/lookup.kest"],
        "caught": "in the JSON and not printed: ",
    },
    {
        # What `emit` says about a file with nothing to run, reworded. Every
        # command is asked of a file that holds nothing and held to answering
        # with something a reader can act on: the sentence is the answer, and
        # a command that says something else about an empty file is a command
        # nothing here would notice had changed its mind.
        "what": "what `emit` says about a file with nothing in it, reworded",
        "file": "src/value.c",
        "from": r"""        fputs("nothing to run: nothing here has a body, and a function that "
              "takes types only gets one where it is called\n",""",
        "to": r"""        fputs("there is nothing here to run, and a function that "
              "takes types only gets one where it is called\n",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "printed nothing matching /",
    },
    {
        # A formatter that reads a file with nothing in it as one it could not
        # write. The one form of nothing is nothing, and a file somebody made
        # and left empty is a file: what comes back is a refusal about a
        # program that did not parse, which is the wrong news about the wrong
        # thing.
        "what": "a formatter that refuses a file with nothing in it",
        "file": "src/fmt.c",
        "from": r"""    if (printer.buffer == NULL) {
        *length = 0;
        return "";
    }""",
        "to": r"""    if (printer.buffer == NULL) {
        return NULL;
    }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a file that holds nothing formatted to something",
    },
    {
        # The refusal a file with nothing to run gets, under another code. Two
        # commands are asked for it and each is held to the code it answers
        # with, because a refusal read by its words alone is a refusal that can
        # change what it is about without saying so.
        "what": "a file with nothing to run refused under another code",
        "file": "src/main.c",
        "from": r"""                                           "K0603", nowhere,
                                           "this file declares nothing, so "
                                           "there is nothing to run");""",
        "to": r"""                                           "K0604", nowhere,
                                           "this file declares nothing, so "
                                           "there is nothing to run");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "not the refusal a file with nothing to run gets",
    },
    {
        # A program the host cannot run, refused without naming what it wanted.
        # A host is a list of bindings and a program asks for names from it, so
        # a refusal that does not say which name is a reader told their program
        # needs something and not what.
        "what": "a host that refuses without naming what was wanted",
        "file": "src/vm.c",
        "from": r"""                           "the host does not provide `%s`",""",
        "to": r"""                           "no such binding: `%s`",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "refused without naming what it wanted",
    },
    {
        # A file that cannot be read, refused without saying so. A directory
        # opens and measures nought, so a reader who typed the wrong path is
        # told their program declares nothing unless this says otherwise.
        "what": "a file that cannot be read, refused without saying so",
        "file": "src/loader.c",
        "from": r"""                   blamed_in == NULL ? nowhere : blame, "cannot read `%s`",""",
        "to": r"""                   blamed_in == NULL ? nowhere : blame, "no such file `%s`",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "refused without saying it could not read it",
    },
    {
        # A reader that refuses what cannot say how long it is. A program
        # handed over a pipe is a stream, and asking its length is asking a
        # question it has no answer to — so a reader that takes the answer as a
        # refusal reads every file that arrives that way as one that is not
        # there.
        "what": "a reader that refuses what cannot say how long it is",
        "file": "src/loader.c",
        "from": r"""    if (size < 0) {""",
        "to": r"""    if (size < 0 && false) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a program read from a stream said nothing",
    },
    {
        # A file with nothing in it, run. There is no `main` to call, so what
        # this answers with is a refusal — and answering nought instead is a
        # program that did nothing and a shell that was told it worked.
        "what": "a file with nothing in it that runs",
        "file": "src/main.c",
        "from": r"""                                           "K0603", nowhere,
                                           "this file declares nothing, so "
                                           "there is nothing to run");
                        }
                        kest_diags_suggest(&build->diags, "add `fn main() { }`");""",
        "to": r"""                                           "K0603", nowhere,
                                           "this file declares nothing, so "
                                           "there is nothing to run");
                            build->diags.error_count = 0;
                        }
                        kest_diags_suggest(&build->diags, "add `fn main() { }`");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a file that holds nothing ran",
    },
    {
        # And the same refusal saying which of the two reasons it is. A file
        # with no `main` and a file with nothing in it are refused by the same
        # code, and the words are the only thing that says which — a reader
        # with the wrong one of the two goes looking for a function they never
        # wrote.
        "what": "a refusal that does not say which of the two reasons it is",
        "file": "src/main.c",
        "from": r"""                                           "this file declares nothing, so "
                                           "there is nothing to run");""",
        "to": r"""                                           "there is nothing to run here");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "refused without saying the file holds nothing",
    },
    {
        # What a program prints, going where what went wrong goes. A shell
        # reading a program's answer gets the diagnostics mixed into it, and
        # every pipe anybody writes around this reads a refusal as an answer.
        # The two streams are the one thing a command line is for.
        "what": "a program's writing sent where its refusals go",
        "file": "src/main.c",
        "from": r"""            KestHost *host = make_host(json || ticking ? stderr : stdout);""",
        "to": r"""            KestHost *host = make_host(stderr);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "is not what its answer stream held",
    },
    {
        # A host that writes what a program said and stops. Ten thousand lines
        # is more than anything else here writes, and a host that keeps only
        # what fits in one buffer loses the rest without a word — the program
        # ran, the status is right, and the answer is short.
        "what": "a host that writes what a program said and stops",
        "file": "src/main.c",
        "from": r"""static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    fputs(frame[0].text, (FILE *)context);
}""",
        "to": r"""static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    static uint32_t written = 0;
    (void)runtime;
    if (written++ < 4096) {
        fputs(frame[0].text, (FILE *)context);
    }
}""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "lost some of them",
    },
    {
        # A run that answers with whether its writing arrived rather than with
        # what the program said. Both are numbers and one of them is nought
        # whenever the other is anything, so a program that printed and then
        # answered seven comes back as a program that worked.
        "what": "a run that answers with whether its writing arrived",
        "file": "src/main.c",
        "from": r"""    int status = build->diags.error_count > 0 || failed_to_choose
                     ? 1
                     : (int)(exit_code & 0xff);""",
        "to": r"""    int status = build->diags.error_count > 0 || failed_to_choose
                     ? 1
                     : fflush(stdout);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "printed and then answered did not answer",
    },
    {
        # An answer a status cannot carry, refused under other words. Eight
        # bits is what a process answers in and 300 cut down is 44, so the one
        # thing this may not do is cut it — and what says it did not is the
        # code, which is the thing a tool reads.
        "what": "an answer too big for a status refused under another code",
        "file": "src/main.c",
        "from": r"""                                           "K0618", nowhere,""",
        "to": r"""                                           "K0619", nowhere,""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "an answer a status cannot carry was not a message",
    },
    {
        # A standard input that would not be read, refused under other words.
        # `Io.read` gives back text and has no way to say a read went wrong, so
        # a closed stream and an empty one look the same to the program: the
        # host is the only thing that knows, and this is what it says.
        "what": "a stream that would not be read, refused under another code",
        "file": "src/main.c",
        "from": r"""        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0642", nowhere,
                       "what the program asked to read could not be read");""",
        "to": r"""        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0643", nowhere,
                       "what the program asked to read could not be read");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a standard input that would not be read said nothing",
    },
    {
        # A program whose writing went nowhere, said nothing about. A shell
        # that redirects a run into a full disk gets a program that looks as
        # though it worked, and every line it wrote is gone.
        "what": "writing that went nowhere, refused under another code",
        "file": "src/main.c",
        "from": r"""        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0641", nowhere,""",
        "to": r"""        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0644", nowhere,""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a program whose writing went nowhere said nothing",
    },
    {
        # And the other way: a program whose writing arrived, told it had not.
        # What says the writing went wrong is the stream itself, and there are
        # two questions to ask it that read alike — a host that asks the other
        # one refuses every run that wrote anything at all.
        "what": "a host that says writing failed when it did not",
        "file": "src/main.c",
        "from": r"""    if (program_wrote_to != NULL &&
        (fflush(program_wrote_to) == EOF || ferror(program_wrote_to))) {""",
        "to": r"""    if (program_wrote_to != NULL &&
        (fflush(program_wrote_to) == EOF || feof(program_wrote_to) == 0)) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a program whose writing arrived was told it had not",
    },
    {
        # An import of a file that is not there, refused under another code. It
        # is the one refusal a reader meets by writing a name wrong in an
        # `import` line, and the code is what a tool reads to tell it from
        # everything else a file can be refused for.
        "what": "a file that cannot be read refused under another code",
        "file": "src/loader.c",
        "from": r"""    kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0701",""",
        "to": r"""    kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0703",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "an import of a file that is not there said nothing",
    },
    {
        # A library named by a host and quietly gone round when it is not
        # there. A host that says where the library is has said where it is,
        # and looking somewhere else instead means a program built against one
        # library runs against another without a word.
        "what": "a library named by a host that is gone round when it is "
                "missing",
        "file": "src/loader.c",
        "from": r"""    const char *given = getenv("KEST_LIB");
    if (given != NULL && given[0] != '\0') {""",
        "to": r"""    const char *given = getenv("KEST_LIB");
    if (given != NULL && given[0] != '\0' && library_is_at(given)) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a library that is not where it was said to be was read",
    },
    {
        # And a host's name read under another name, so nothing is ever given.
        # Then the library beside the program wins every time, and a host that
        # said where to look is a host that was not listened to.
        "what": "a host's name for the library read under another name",
        "file": "src/loader.c",
        "from": r"""    const char *given = getenv("KEST_LIB");""",
        "to": r"""    const char *given = getenv("KEST_LIBS");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a library named by a host did not win",
    },
    {
        # An install that writes where nothing made a directory. What holds the
        # `Makefile` here is not the lines being right but the files arriving:
        # this installs into somewhere of its own, runs what it put there, and
        # takes it away again.
        "what": "an install that writes where nothing made a directory",
        "file": "Makefile",
        "from": "\tmkdir -p $(DESTDIR)$(PREFIX)/lib/kest/std",
        "to": "\tmkdir -p $(DESTDIR)$(PREFIX)/lib/kest",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "install: this does not install",
    },
    {
        # And an uninstall that leaves something behind. Every file an install
        # puts on a machine is one an uninstall takes away, and the one it
        # forgets is the one that was there before the next install and is read
        # instead of what arrives.
        "what": "an uninstall that leaves the header behind",
        "file": "Makefile",
        "from": "\trm -f $(DESTDIR)$(PREFIX)/include/kest.h\n",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "is still there after removing it",
    },
    {
        # What is wrong with a program, written where its answer goes. A shell
        # reading what `check` says a program holds gets the diagnostics mixed
        # into it, which is the same fault as a run printing its refusals into
        # its own writing and is the command a build system runs most.
        "what": "what is wrong with a program written where its answer goes",
        "file": "src/main.c",
        "from": r"""        kest_build_report(build, stderr, KEST_FORM_TEXT);""",
        "to": r"""        kest_build_report(build, stdout, KEST_FORM_TEXT);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "was written where its answer goes",
    },
    {
        # And the other side of it: nothing written where errors go. Then a
        # build system that reads the error stream is told a program is fine
        # and the status says otherwise, which is the one disagreement nobody
        # can act on.
        "what": "what is wrong with a program written nowhere",
        "file": "src/main.c",
        "from": r"""        kest_build_report(build, stderr, KEST_FORM_TEXT);
    }
""",
        "to": r"""    }
""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "was not written where errors go",
    },
    {
        # What a program holds, written where the errors go. It is the answer
        # `check` is for, and a shell that captures it gets nothing while
        # everything arrives on the stream it was told to ignore.
        "what": "what a program holds written where the errors go",
        "file": "src/main.c",
        "from": r"""                kest_program_dump(build->program, build->arena,
                                  build->units.items[0].alias, stdout);""",
        "to": r"""                kest_program_dump(build->program, build->arena,
                                  build->units.items[0].alias, stderr);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "was not written where an answer goes",
    },
    {
        # A program that did not check, saying nothing was wrong. The count is
        # what a tool reads first, and nought with diagnostics beside it is a
        # tool told to carry on.
        "what": "a count of what was wrong that is always nought",
        "file": "src/diag.c",
        "from": r"""    fprintf(out, "],\"errors\":%u", diags->error_count);""",
        "to": r"""    fprintf(out, "],\"errors\":%u", 0);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "said nothing was wrong",
    },
    {
        # And a program that did not check, saying it holds nothing. What a
        # file declares is true whether or not a body in it is wrong, and a
        # tool told nothing about it cannot say what to do next.
        "what": "a tool told nothing about a program that did not check",
        "file": "src/main.c",
        "from": r"""        if (checking && build->program != NULL) {""",
        "to": r"""        if (checking && build->program != NULL &&
            build->diags.error_count == 0) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "said it holds nothing",
    },
    {
        # A list of what a file declares with the ones nothing calls left out.
        # What a file declares is what it declares, whether anything reaches it
        # or not, and a tool given the shorter list is told a program holds
        # less than it does — for the file whose functions are all unreached
        # because it did not check, it is told the program holds nothing.
        "what": "a list of what a file declares with the unreached left out",
        "file": "src/types.c",
        "from": r"""    fputs("],\"functions\":[", out);
    first = true;
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        if (symbol->type->tag != KEST_T_FN) {""",
        "to": r"""    fputs("],\"functions\":[", out);
    first = true;
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        if (symbol->type->tag != KEST_T_FN || !symbol->named) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "what a tool is given about a program that did not check",
    },
    {
        # A diagnostic that says where it starts and not how far it goes. The
        # four things a diagnostic carries are the code, the place, the fix and
        # the notes, and the place is four numbers: a tool that draws the caret
        # has nothing to draw it under.
        "what": "a diagnostic that does not say how far it goes",
        "file": "src/diag.c",
        "from": r"""                        ",\"line\":%u,\"column\":%u,\"offset\":%u,"
                        "\"length\":%u",""",
        "to": r"""                        ",\"line\":%u,\"column\":%u,\"offset\":%u,"
                        "\"len\":%u",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a diagnostic with everything in it wrote no ",
    },
    {
        # A tick that says nothing about what it cost. It is the one
        # measurement a host can ask for, and a run that crosses the boundary
        # and says nothing about it is a frame budget nobody can be held to.
        "what": "a tick that says nothing about what it cost",
        "file": "src/main.c",
        "from": r"""                    if (!json) {
                        // What it was run over, before what that cost: two""",
        "to": r"""                    if (false) {
                        // What it was run over, before what that cost: two""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a tick said nothing about what it cost",
    },
    {
        # A crossing counted once for a run rather than once for an event. As
        # many crossings as there were events is the whole of what this
        # measures: a number that does not move with the work is a measurement
        # of nothing, and it reads like a measurement.
        "what": "a crossing counted once for a run of events",
        "file": "src/main.c",
        "from": r"""        out->crossings = count;""",
        "to": r"""        out->crossings = 1;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": " events crossed ",
    },
    {
        # And the other way round: a batch counted as though every event in it
        # were a crossing. A host that hands the whole list over crosses once,
        # which is the reason the language has `onEvents` at all — a count that
        # says otherwise says the two ways cost the same.
        "what": "a batch counted as one crossing an event",
        "file": "src/main.c",
        "from": r"""                fputs(",\"onEvents\":{\"crossings\":1,\"gave\":", stdout);""",
        "to": r"""                fprintf(stdout, ",\"onEvents\":{\"crossings\":%d,\"gave\":",
                        ticked.count);""",
        "also": ["src/main.c",
                 r"""                                printf("onEvents  1 crossing   returned %lld\n",
                                       (long long)ticked.bulk_gave);""",
                 r"""                                printf("onEvents  %d crossings  returned %lld\n",
                                       ticked.count,
                                       (long long)ticked.bulk_gave);"""],
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a batch of events crossed ",
    },
    {
        # A heap thrown away when nobody asked. `--reset` is what says the heap
        # goes between events, and a run without it holds what the program made
        # — a host measuring how much a frame keeps is measuring nothing if it
        # is emptied underneath.
        "what": "a heap thrown away when nobody asked",
        "file": "src/main.c",
        "from": r"""            if (reset) {
                if (!kest_heap_reset(runtime)) {
                    return;
                }
                out->thrown++;
            }""",
        "to": r"""            if (reset || true) {
                if (!kest_heap_reset(runtime)) {
                    return;
                }
                out->thrown++;
            }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a heap nobody threw away is not still there",
    },
    {
        # A call that says nothing about what it cost. `call` is the one
        # command whose answer is a value, and the heap beside it is how a host
        # finds out what asking for that value took — a field that is not
        # there is a measurement nobody can make.
        "what": "a call that says nothing about what it cost",
        "file": "src/main.c",
        "from": r"""            fprintf(stdout, ",\"heap\":%zu", called_heap);""",
        "to": r"""            (void)called_heap;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a cut said nothing about what it cost",
    },
    {
        # A name that is not there, said back without the module it was asked
        # for under. `shapes.nope` is what somebody typed and `nope` is what
        # they would find if they went looking, so the refusal has to carry the
        # whole of it or it points at a file that has no such name either.
        "what": "a name that is not there said back without its module",
        "file": "src/main.c",
        "from": r"""        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0624", nowhere,
                       "no `%s` takes what was typed", name);""",
        "to": r"""        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0624", nowhere,
                       "no `%s` takes what was typed",
                       strrchr(name, '.') != NULL ? strrchr(name, '.') + 1
                                                  : name);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "was said back as something else",
    },
    {
        # A function a shell cannot hand anything to, refused without saying
        # what it was about the arguments. A reader who typed two numbers at a
        # function taking a function needs to be told that a function is not a
        # word, and the words are the only place that is said.
        "what": "a value a shell cannot write, refused without saying so",
        "file": "src/value.c",
        "from": r"""        *why = "cannot be written as a word";""",
        "to": r"""        *why = "is not something this takes";""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "was refused without saying which functions there were",
    },
    {
        # What a program says while `call` runs it, written where the answer
        # goes. The one command whose answer is a value is the one where that
        # matters most: a shell reading the value gets the program's writing
        # above it and nothing to say which line is which.
        "what": "what a program says under `call` written where the answer "
                "goes",
        "file": "src/main.c",
        "from": r"""                KestHost *host = make_host(stderr);""",
        "to": r"""                KestHost *host = make_host(stdout);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "while it ran is not beside the ",
    },
    {
        # A value written in words the reader cannot take back. What `call`
        # prints for a value and what it will read for one are the same
        # language, and a number that is not a number is the one answer where
        # that is easy to forget: written any other way it goes out of one
        # command and is refused by the next.
        "what": "a value written in words the reader cannot take back",
        "file": "src/value.c",
        "from": r"""        return snprintf(buffer, size, "nan");""",
        "to": r"""        return snprintf(buffer, size, "not a number");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": " did not read back as what it is",
    },
    {
        # Every name said to be reached. `named` is what the checker settled
        # while it resolved the file, and it is the one field a tool uses to
        # tell a declaration nothing calls from one everything does — always
        # true is a flag that answers the same whatever is so.
        "what": "a flag that says every name is reached",
        "file": "src/types.c",
        "from": r"""        fprintf(out, ",\"noAlloc\":%s,\"foreign\":%s,\"named\":%s",
                symbol->type->no_alloc ? "true" : "false",
                symbol->type->is_foreign ? "true" : "false",
                symbol->named ? "true" : "false");""",
        "to": r"""        fprintf(out, ",\"noAlloc\":%s,\"foreign\":%s,\"named\":%s",
                symbol->type->no_alloc ? "true" : "false",
                symbol->type->is_foreign ? "true" : "false",
                "true");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": " says named is ",
    },
    {
        # And every function said to be the program's own. What a host has to
        # provide is the one thing a reader of this list has to act on, and a
        # field that says none of them are is a host told it has nothing to do.
        "what": "a flag that says no function comes from a host",
        "file": "src/types.c",
        "from": r"""                symbol->type->is_foreign ? "true" : "false",
                symbol->named ? "true" : "false");""",
        "to": r"""                "false",
                symbol->named ? "true" : "false");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "says the host provides it: ",
    },
    {
        # A part of a shape said to be reached. A `flags` type is a set of bits
        # and each of them is a name somebody wrote: one nothing reaches is a
        # bit nobody has ever set, and the whole of what says so is this field.
        "what": "a flag that says every part of a shape is reached",
        "file": "src/types.c",
        "from": r"""                fprintf(out, ",\"bit\":%u,\"named\":%s}", c,
                        type->cases[c].named ? "true" : "false");""",
        "to": r"""                fprintf(out, ",\"bit\":%u,\"named\":%s}", c, "true");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "what a run says about a declaration is not what is so",
    },
    {
        # The shapes left out of what a run says a file declares. A tool
        # reading this list is told what there is to work with, and a file with
        # its structs and its enums missing reads as a file of functions and
        # nothing for them to take.
        "what": "the shapes left out of what a run says a file declares",
        "file": "src/types.c",
        "from": r"""    fputs("\"types\":[", out);""",
        "to": r"""    fputs("\"types\":[", out);
    if (program != NULL) {
        fputs("],\"nothing\":[", out);
    }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "of the fourteen things this asks about",
    },
    {
        # An object with something written after it. Every comparison of the
        # two forms of an answer begins by reading the JSON, and one that is
        # not JSON is a stack trace in a language nobody reading a check
        # speaks — so the reading is asked plainly and this is what it says.
        # A comma after the closing brace is the shape a hand-written writer
        # gets wrong, and every tool anywhere refuses the whole answer for it.
        "what": "an object with something written after it",
        "file": "src/main.c",
        "from": r"""        fputs("}\n", stdout);
    } else {""",
        "to": r"""        fputs("},\n", stdout);
    } else {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "what was said as JSON is not JSON",
    },
    {
        # The other modules left out of what `check` prints. A program of two
        # files declares things in both, and the summary of what the rest of
        # them hold is the only place the words say so — without it a reader is
        # shown one file and told nothing about where the names it uses come
        # from.
        "what": "the other modules left out of what `check` prints",
        "file": "src/types.c",
        "from": r"""    for (uint32_t i = 0; i < elsewhere; i++) {
        const Held *one = &held[i];
        fprintf(out, "%.*s ", (int)one->length, one->name);""",
        "to": r"""    for (uint32_t i = elsewhere; i < elsewhere; i++) {
        const Held *one = &held[i];
        fprintf(out, "%.*s ", (int)one->length, one->name);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "the words wrote out ",
    },
    {
        # A name written without the module it is in. What a tool does with
        # this list is sort it by module — that is the whole of what a name
        # with a dot in it is for — and a bare name puts every function of
        # every file under one heading that is not a module at all.
        "what": "a name written without the module it is in",
        "file": "src/types.c",
        "from": r"""        fputs("{\"name\":", out);
        kest_json_text(symbol->name, out);
        fputs(",\"parameters\":[", out);""",
        "to": r"""        fputs("{\"name\":", out);
        kest_json_text(strrchr(symbol->name, '.') != NULL
                           ? strrchr(symbol->name, '.') + 1
                           : symbol->name,
                       out);
        fputs(",\"parameters\":[", out);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": " written out and ",
    },
    {
        # One more function than there is, in the summary of what another
        # module holds. The words say how many and the JSON says which, so a
        # count that is one out is the two forms of one answer disagreeing
        # about a file the reader cannot see.
        "what": "one more function than there is in another module",
        "file": "src/types.c",
        "from": r"""            fprintf(out, "%s%u function%s", between, one->functions,
                    one->functions == 1 ? "" : "s");""",
        "to": r"""            fprintf(out, "%s%u function%s", between, one->functions + 1,
                    one->functions == 1 ? "" : "s");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "says one thing in words and another in JSON",
    },
    {
        # A place one column out in the JSON. The carets a reader sees and the
        # numbers a tool reads are the same place said twice, and one of them
        # counting from a different end is a tool that underlines the character
        # after the one that is wrong.
        "what": "a place one column out in the JSON",
        "file": "src/diag.c",
        "from": r"""                fprintf(out,
                        ",\"line\":%u,\"column\":%u,\"offset\":%u,"
                        "\"length\":%u",
                        line, column, diag->span.offset, diag->span.length);""",
        "to": r"""                fprintf(out,
                        ",\"line\":%u,\"column\":%u,\"offset\":%u,"
                        "\"length\":%u",
                        line, column + 1, diag->span.offset,
                        diag->span.length);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a diagnostic says one thing in words and another in JSON",
    },
    {
        # A heap thrown away one more time in the JSON than in the words. What
        # a frame cost is one measurement, and a host reading the number a tool
        # is given and a reader reading the number beside it have to be reading
        # the same one.
        "what": "a heap thrown away once more in the JSON than in the words",
        "file": "src/main.c",
        "from": r"""            fprintf(stdout, ",\"heap\":%zu,\"thrown\":%d", ticked.heap,
                    ticked.thrown);""",
        "to": r"""            fprintf(stdout, ",\"heap\":%zu,\"thrown\":%d", ticked.heap,
                    ticked.thrown + 1);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "what a frame cost is one thing in words and another in JSON",
    },
    {
        # A chunk that does not carry the promise its declaration made. What
        # the machine reads at a call the promise's second proof cannot see
        # through is the chunk, so a chunk that says it promises nothing is a
        # promise nothing keeps — and the declaration still says it, which is
        # what a reader and every other check are reading.
        "what": "a chunk that does not carry the promise it was declared with",
        "file": "src/value.c",
        "from": r"""                chunk->param_slots, chunk->slot_count, chunk->stack_needed,
                chunk->no_alloc ? "true" : "false");""",
        "to": r"""                chunk->param_slots, chunk->slot_count, chunk->stack_needed,
                "false");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a chunk carries what its declaration does not",
    },
    {
        # A stream that is an object and a listing at once, which is neither.
        # `--json` is one object a line and nothing else, so a command that
        # writes what a person reads beside it hands a tool a file it cannot
        # begin — and the object is still there, further down, which is how
        # this went unnoticed the first time.
        "what": "a listing written beside the object a tool reads",
        "file": "src/main.c",
        "from": r"""            if (kest_build_emit(build) && !json) {
                kest_module_disassemble(&build->module, EVERY_CALL, stdout);
            }""",
        "to": r"""            if (kest_build_emit(build)) {
                kest_module_disassemble(&build->module, EVERY_CALL, stdout);
            }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "not one object a line",
    },
    {
        # A reference read in whatever store it is handed to. A place is
        # stamped when it is handed out and the stamp is what says which store
        # it came from, so a read that asks only for the index gives back
        # whatever the other store happens to keep there.
        "what": "a reference read in whatever store it is handed to",
        "file": "src/vm.c",
        "from": r"""    if (index >= store->used || !store->live[index] ||
        store->generations[index] != generation) {""",
        "to": r"""    (void)generation;
    if (index >= store->used || !store->live[index]) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a reference used with another store named somebody else",
    },
    {
        # One more event than there was. What a tick measures is a crossing an
        # event, so the two numbers are one number said twice — and a count
        # that is one out is a measurement that says the boundary was crossed
        # for something that never happened.
        "what": "one more event than there was",
        "file": "src/main.c",
        "from": r"""            fprintf(stdout, "%d,\"lent\":", ticked.count);""",
        "to": r"""            fprintf(stdout, "%d,\"lent\":", ticked.count + 1);""",
        "also": ["src/main.c",
                 r"""                            printf("events    %d, counted up from nought\n",
                                   ticked.count);""",
                 r"""                            printf("events    %d, counted up from nought\n",
                                   ticked.count + 1);"""],
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "it ran over ",
    },
    {
        # A path said by its last piece. An import of `std.io` that is not
        # there is an import that was looked for somewhere, and where it looked
        # is the whole of what a reader needs: `io.kest` is what every program
        # writes and says nothing about which of the places this looked in came
        # up empty.
        "what": "a path that cannot be read said by its last piece",
        "file": "src/loader.c",
        "from": r"""                   blamed_in == NULL ? nowhere : blame, "cannot read `%s`",
                   path);""",
        "to": r"""                   blamed_in == NULL ? nowhere : blame, "cannot read `%s`",
                   strrchr(path, '/') != NULL ? strrchr(path, '/') + 1 : path);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "did not say where it looked",
    },
    {
        # A read that stops one short. What a program asked to read is what was
        # there to read, and a piece of it handed over as the whole is the
        # quiet truncation this project refuses everywhere else — the program
        # runs, the text is text, and one byte of somebody's input is gone.
        "what": "a read that stops one byte short",
        "file": "src/main.c",
        "from": r"""    frame[0] = kest_text(runtime, bytes, (uint32_t)held);
    free(bytes);""",
        "to": r"""    frame[0] = kest_text(runtime, bytes, (uint32_t)(held > 0 ? held - 1 : 0));
    free(bytes);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "three bytes on the standard input were not three",
    },
    {
        # A stream with nothing on it read as one that would not be read. They
        # are the same to the program — `Io.read` gives back text and has no
        # way to say a read went wrong — so the host is the one that tells them
        # apart, and a host that cannot refuses every program run without
        # anything on its input.
        "what": "an empty stream read as one that would not be read",
        "file": "src/main.c",
        "from": r"""    if (ferror(stdin)) {
        program_could_not_read = true;
        held = 0;
    }""",
        "to": r"""    if (ferror(stdin) || held == 0) {
        program_could_not_read = true;
        held = 0;
    }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "an empty standard input was read as something wrong",
    },
    {
        # A comment refused wherever it is written. One inside a hole is kept
        # by nothing and read by nobody, so it is refused and the reader is
        # told to write it above the line — and a lexer that has lost track of
        # where it is refuses the line above too, which is the one place there
        # was left to put it.
        "what": "a comment refused wherever it is written",
        "file": "src/lexer.c",
        "from": r"""            if (lexer->in_hole) {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0111",""",
        "to": r"""            if (true) {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0111",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "the same comment above the line was refused too",
    },
    {
        # A run that writes the number it answers with into what the program
        # said. What `run` answers with is the whole of what it says when it
        # works, so a line of its own in there is a shell reading a program's
        # writing with something nobody wrote at the end of it.
        "what": "a run that writes its own answer into the program's writing",
        "file": "src/main.c",
        "from": r"""                    } else if (kest_call(runtime, at, frame, 1)) {
                        exit_code = frame[0].integer;""",
        "to": r"""                    } else if (kest_call(runtime, at, frame, 1)) {
                        exit_code = frame[0].integer;
                        printf("answered %lld\n", (long long)exit_code);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "what a program said is not what this answered",
    },
    {
        # A cut that stops sooner handed back as a piece of what it was given.
        # A cut that ends where the text already ends is a place inside it —
        # the nought after it is the one that was there — and one that stops
        # sooner is not: what comes back reads on past where it was cut, to the
        # end of the text it came out of. It costs nothing, which is how it
        # would be found: the one thing said about a cut is that stopping
        # sooner costs more than measuring.
        "what": "a cut that stops sooner handed back without being copied",
        "file": "src/vm.c",
        "from": r"""            if (text[want] == '\0') {
                (top++)->text = text + from;
                break;
            }""",
        "to": r"""            if (true) {
                (top++)->text = text + from;
                break;
            }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a cut that stops sooner cost ",
    },
    {
        # A promise held to a heap that did not move. What the machine holds a
        # host to is that a call under `no.alloc` leaves the heap where it
        # found it, and a test that asks whether it is not less than it was
        # refuses every host that keeps the promise as well as every one that
        # breaks it — which reads, from a program's side, as a language that
        # cannot be told what a host does.
        "what": "a promise refused for a heap that did not move",
        "file": "src/vm.c",
        "from": r"""            if (promised && kest_heap_used(rt) != held) {""",
        "to": r"""            if (promised && kest_heap_used(rt) >= held) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "keeps `no.alloc` and was refused",
    },
    {
        # A note shown without the place it is about. A diagnostic about more
        # than one place carries a note per place, each with its own line and
        # caret, and the words are where a reader meets them: a note printed as
        # a line of prose is the thing `CLAUDE.md` says prose naming a line
        # number is not, and the JSON still carries the place.
        "what": "a note shown without the place it is about",
        "file": "src/diag.c",
        "from": r"""            render_frame(diag->notes[n].source, diag->notes[n].span,
                         diag->notes[n].label, gutter, out);""",
        "to": r"""            fprintf(out, "      %s\n", diag->notes[n].label);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": " said after the first and ",
    },
    {
        # A check written in a shell it is not run by. Every one here says
        # `/bin/sh` on its first line, and under that shell a dollar-quote is
        # the characters between the quotes: a sweep for a carriage return
        # written that way looks for four bytes no file has. Nothing refuses
        # it — the shell reads it, the check runs, and the sentence under it is
        # one nothing can make it say. That is how it was found.
        "what": "a check written in a shell it is not run by",
        "file": "tools/check-fmt.sh",
        "from": r"""    returned=$(printf '\r')""",
        "to": r"""    returned=$'\r'""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "which under `/bin/sh` is those characters and not what "
                  "they stand for",
    },
    {
        # A lent array the program grows, and nothing said which refusal it
        # was. What a host lends is as long as the host said, and a program
        # that pushes to one would move the elements somewhere the host does
        # not know about — four ways to ask for that and one sentence for it.
        "what": "a lent array that grows without saying which refusal it is",
        "file": "src/vm.c",
        "from": """                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot grow");""",
        "to": """                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, and it may not");""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "refused without saying `K0608`",
    },
    {
        # A name the program asks the host for, answered as something to call.
        # The two directions of this boundary are separate specifications, and
        # a host that reads one as the other is the mistake the whole crossing
        # is shaped to say — so it is said, and this is what says it is.
        "what": "a name asked the wrong way round that answers anyway",
        "file": "src/vm.c",
        "from": """        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0614",""",
        "to": """        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K9998",""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "refused without saying `K0614`",
    },
    {
        # A refusal a file can meet before it means anything, with nothing
        # asking for it. A message nobody has ever seen is a message nobody
        # knows is there — and these are the ones a reader meets first, where
        # a file is refused for what it is rather than for what it says.
        "what": "a refusal a file can meet that nothing asks for",
        "file": "tools/check-commands.sh",
        "from": r"""K0104|fn main() -> i32 {\n    let a = 0x\n    return a\n}|literal has no digits
""",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "K0104: nothing asks for it",
    },
    {
        # The one conversion this language does, made without asking what it
        # is converting. A value standing where an optional is wanted becomes
        # one, and what makes that safe is that it has to be a value of what
        # the optional holds. Without that a piece of text stands where a
        # number is wanted and comes back as where the text was.
        "what": "a value that becomes an optional it does not fit",
        "file": "src/check.c",
        "from": """        kest_type_equal(type, expected->element)) {""",
        "to": """        true) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "text stood where a number that may be nothing was wanted",
    },
    {
        # What is inside an array, a store, a reference or an optional, no
        # longer looked at. One line answers for all four, and an array of one
        # thing standing where an array of another is wanted is the kind of
        # mistake a type system is entirely for.
        "what": "a shape that holds something else",
        "file": "src/types.c",
        "from": """    case KEST_T_OPTIONAL:
        return kest_type_equal(a->element, b->element);""",
        "to": """    case KEST_T_OPTIONAL:
        return true;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "an array of one thing for one of another was taken",
    },
    {
        # And how many a fixed one holds. `[f32; 4]` is four of them where it
        # stands, so a shape that says eight and gets four is a struct read
        # past its own end.
        "what": "a fixed shape that holds a different number",
        "file": "src/types.c",
        "from": """        return a->count == b->count && kest_type_equal(a->element, b->element);""",
        "to": """        return kest_type_equal(a->element, b->element);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "an array of eight for one of four was taken",
    },
    {
        # A function value handed over with the wrong number of things to
        # take. What a shape is is what it takes, what it gives back and what
        # it promises, and nothing in this tree ever hands one of the wrong
        # shape — a program that did would not compile, and every program here
        # compiles. So all three of these were refusals nothing had ever asked
        # for. This one is not even a refusal when it is gone: the machine
        # reads arguments that were never pushed and the process dies.
        "what": "a shape that takes a different number of things",
        "file": "src/types.c",
        "from": """        if (a->param_count != b->param_count ||""",
        "to": """        if (false ||""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "a function that takes two was taken for one that does",
    },
    {
        # And the same for what it gives back.
        "what": "a shape that gives back something else",
        "file": "src/types.c",
        "from": """            !kest_type_equal(a->result, b->result)) {""",
        "to": """            false) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "a function that gives a number back was taken for one",
    },
    {
        # And for what it takes, one thing at a time.
        "what": "a shape that takes something else",
        "file": "src/types.c",
        "from": """            if (!kest_type_equal(a->params[i], b->params[i])) {""",
        "to": """            if (false) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "a function that takes a number was taken for one",
    },
    {
        # A promise refused where none was asked for. A promise is something a
        # caller may rely on and never something it has to have, so a body that
        # promises may stand where one that does not is wanted. Nothing in this
        # tree had ever done it, so tightening the rule the other way passed
        # every check here — the half of it that was held is the half that
        # refuses, and a rule half held is a rule.
        "what": "a promise refused where none was wanted",
        "file": "src/types.c",
        "from": """        return a->no_alloc || !b->no_alloc;""",
        "to": """        return a->no_alloc == b->no_alloc;""",
        "program": "wanted.kest",
        "source": """fn long(word: text) -> bool no.alloc {
    return len(word) > 4
}

fn howMany(items: [text], keep: fn(text) -> bool) -> i32 {
    let found = 0
    for one in items {
        if keep(one) {
            found += 1
        }
    }
    return found
}

fn main() -> i32 {
    let words: [text] = array()
    push(words, "herald")
    return howMany(words, long) - 1
}
""",
        "caught": "found `fn(text) -> bool no.alloc`",
    },
    {
        "what": "a promise that does not survive being handed over",
        "file": "src/types.c",
        "from": """        return a->no_alloc || !b->no_alloc;""",
        "to": """        return true;""",
        "program": "handed.kest",
        # The one call the second proof cannot follow: which chunk it enters
        # is not known until it runs, so the machine is what catches this.
        "source": """fn grows(n: i32) -> i32 {
    let made: [i32] = array()
    push(made, n)
    return len(made)
}

fn careful(f: fn(i32) -> i32 no.alloc, n: i32) -> i32 no.alloc {
    return f(n)
}

fn main() -> i32 {
    return careful(grows, 1) - 1
}
""",
        "caught": "K0623",
    },
    {
        "what": "a handle used as something it is not",
        "file": "src/types.c",
        "from": """    if (a->tag != b->tag) {
        return false;
    }""",
        "to": """    if (a->tag != b->tag) {
        return (a->tag == KEST_T_ARRAY && b->tag == KEST_T_STORE) ||
               (a->tag == KEST_T_STORE && b->tag == KEST_T_ARRAY);
    }""",
        "program": "handles.kest",
        # The machine keeps a tag on every handle it hands out, and reads it
        # before it follows one. Nothing a program can write reaches that
        # check: this is the compiler having agreed that an array is a store.
        "source": """struct Npc {
    n: i32
}

fn count(world: store<Npc>) -> i32 no.alloc {
    return len(world)
}

fn main() -> i32 {
    let xs: [Npc] = array()
    push(xs, Npc(1))
    return count(xs)
}
""",
        "caught": "K0612",
    },
    {
        "what": "a checker that lets through what the compiler cannot emit",
        "file": "src/check.c",
        "from": r"""            if (!walks(sequence)) {
                report(checker, stmt->each.sequence->span, "K0317",
                       "`for` walks an array, text, a store or a set of bits, "
                       "found `%s`",
                       type_name(checker, sequence));
                if (sequence->element != NULL && walks(sequence->element)) {
                    say_if_let(checker, sequence, NULL);
                }
            } else if""",
        "to": r"""            if (!walks(sequence)) {
                element = sequence;
            } else if""",
        "program": "walking.kest",
        # The compiler's own guards, which only fire when the two halves of it
        # disagree about what a program is.
        "source": """fn main() -> i32 {
    let n = 3
    for x in n {
        return 1
    }
    return 0
}
""",
        "caught": "K0505",
    },
    {
        # And a kind of thing a program can be made of that no document shows.
        # A bare block is how a name is given a life shorter than the function
        # it is in, and the reference showed none.
        "what": "a shape no document holds",
        "file": "docs/language.md",
        "from": """{
    let held = costOf(world)
    defer release(world)
    total += held
}""",
        "to": """let held = costOf(world)
defer release(world)
total += held""",
        "make": ["kest", "embed"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "no block here holds a `block`",
    },
    {
        # An answer a process cannot carry, cut down to fit. Eight bits is what
        # a status is, and 256 cut down is nought, which is the one answer that
        # means nothing went wrong. Saying so rather than cutting is a rule
        # this project wrote down, and nothing had ever seen it kept.
        "what": "an answer too big for a status, cut down to fit",
        "file": "src/main.c",
        "from": """                        if (exit_code < 0 || exit_code > 255) {""",
        "to": """                        if (false) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "cut down to fit",
    },
    {
        # A directory read as a file. It opens, measures nought and refuses
        # to be read, and reading nought bytes of one fails at nothing -- so
        # one byte is asked for. Without that, a path that is a directory is a
        # file with nothing in it, and `kest check` says it declares nothing.
        "what": "a directory read as a file",
        "file": "src/loader.c",
        "from": """    if (size == 0) {
        fgetc(file);
    }""",
        "to": """    if (false) {
        fgetc(file);
    }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "read a directory as a file",
    },
    {
        # A formatter that writes a file some other way than the one form. It
        # is the first thing that check says about every file it is given, and
        # nothing had ever made it say it: what has been watched is the
        # formatter losing something, not the formatter writing something else.
        "what": "a formatter that writes a file another way",
        "file": "src/fmt.c",
        "from": """    if (!broken) {
        for (uint32_t i = 0; i < count; i++) {
            put(printer, i > 0 ? ", " : "");""",
        "to": """    if (!broken) {
        for (uint32_t i = 0; i < count; i++) {
            put(printer, i > 0 ? ",  " : "");""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "not in the one form",
    },
    {
        # A formatter that writes over a file it could not read. What `fmt`
        # prints goes back over somebody's source, so a file it did not
        # understand is a file it must leave alone -- and the sentence that
        # says so had never been said.
        "what": "a formatter that writes over what it could not read",
        "file": "src/main.c",
        "from": """        bool read = loaded && diags.error_count == 0;""",
        "to": """        bool read = loaded;""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "formatted a file that does not parse",
    },
    {
        # A table a check reads with a pattern that stops matching. The list is
        # still there and still right; what moved is the shape the reading
        # leans on, and a reading that finds nothing holds nothing. Four checks
        # guard themselves against that, and one of the four had ever been seen
        # doing it.
        "what": "a table `check-dead.sh` reads with a pattern that stops "
                "matching",
        "file": "src/value.c",
        "from": """static const Instruction INSTRUCTIONS[] = {""",
        "to": """static const Instruction INSTRUCTIONS[] =
    {""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "nothing in the tree is where this reads it from",
    },
    {
        # A library module that reaches the heap and nothing measures. What a
        # module costs is proved by `no.alloc` where every function promises
        # one and asked by a program where they do not, so a module that stops
        # being all promises and gains no program is one nothing weighs.
        "what": "a library module that reaches the heap and nothing weighs",
        "file": "lib/std/vec.kest",
        "from": """fn length(v: Vec2) -> f32 no.alloc {""",
        "to": """fn spare(n: i32) -> [i32] {
    let out: [i32] = array()
    push(out, n)
    return out
}

fn length(v: Vec2) -> f32 no.alloc {""",
        "make": ["kest"],
        "tool": "tools/check-costs.sh",
        "caught": "what it costs, and not every",
    },
    {
        # A host keeping a promise made on its behalf is read rather than run,
        # because the machine refuses a promise that calls a body which
        # allocates and cannot see what a host does on its own side of the
        # boundary. So the promise is kept by reading the body, and what says
        # that reading works is a body that breaks it.
        "what": "a host that makes text under a promise not to",
        "file": "examples/embed.c",
        "from": """    Decider *decider = context;
    if (decider->meddles) {""",
        "to": """    Decider *decider = context;
    KestValue said = kest_text(runtime, "deciding", 8);
    (void)said;
    if (decider->meddles) {""",
        "make": ["kest"],
        "tool": "tools/check-costs.sh",
        "caught": "promises `no.alloc` and `engine_decide` in",
    },
    {
        # And the body itself, read with a pattern. A brace on the next line is
        # the same C and a promise nothing reads, which is the shape every
        # sweep here guards against — and the guard beside this one, about the
        # binds, had been watched while this one had not.
        "what": "a bound body written where a check cannot read it",
        "file": "examples/embed.c",
        "from": """static void engine_decide(KestValue *frame, KestRuntime *runtime,
                          void *context) {""",
        "to": """static void engine_decide(KestValue *frame, KestRuntime *runtime,
                          void *context)
{""",
        "make": ["kest"],
        "tool": "tools/check-costs.sh",
        "caught": "and this cannot read what it does",
    },
    {
        # A library function a command line cannot hand an array to is asked by
        # the host that can, and the two lists are held to each other. A host
        # that stops asking about one leaves it weighed by nothing, and nothing
        # else here would say so: the function still runs, still has a caller,
        # and still costs whatever it costs.
        "what": "a library function the host that can weigh it stopped asking "
                "about",
        "file": "examples/embed.c",
        "from": """{"text.repeat", "text.join"}""",
        "to": """{"text.repeat", "text.joined"}""",
        "make": ["kest"],
        "tool": "tools/check-costs.sh",
        "caught": "cannot be asked from here and",
    },
    {
        # A function that hands back a run of pieces is driven by a program
        # written for it, and what that program can be written for is what the
        # check knows how to hand over. One of a shape it does not know is one
        # nothing weighs, and passing over it in silence is how a walk that
        # grows with the square of its input would get in.
        "what": "a library function of a shape nothing knows how to weigh",
        "file": "lib/std/text.kest",
        "from": """fn charsOf(subject: text) -> [text] {""",
        "to": """fn pieces(count: i32) -> [text] {
    let made: [text] = array()
    let at = 0
    while at < count {
        push(made, "piece")
        at = at + 1
    }
    return made
}

fn charsOf(subject: text) -> [text] {""",
        "make": ["kest"],
        "tool": "tools/check-costs.sh",
        "caught": "which this does not know how to ask for",
    },
    {
        # And the library not checking at all. Every cost here is a run, so a
        # library the compiler refuses is a library nothing can be asked about
        # — and a check that read a refusal as a cost of nought would say the
        # costs are fine about a tree that does not compile.
        "what": "a library nothing can be asked what it costs",
        "file": "lib/std/text.kest",
        "from": """fn repeat(subject: text, times: i32) -> text {""",
        "to": """fn repeat(subject: text, times: i32) -> text {
    let unknown = nowhere(subject)""",
        "make": ["kest"],
        "tool": "tools/check-costs.sh",
        "caught": "could not be asked",
    },
    {
        # A library function nothing anywhere names, which is the other half of
        # the rule beside it: the one below adds a constant nobody reads, and
        # the loop that holds the functions had never been seen catching one.
        "what": "a library function nothing has ever reached",
        "file": "lib/std/math.kest",
        "from": """fn clamp(value: i32, low: i32, high: i32) -> i32 no.alloc {""",
        "to": """fn nobody(value: i32) -> i32 no.alloc {
    return value
}

fn clamp(value: i32, low: i32, high: i32) -> i32 no.alloc {""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "so nothing has run it",
    },
    {
        # A name the prefix says is public on a function only one object can
        # see. A reader looking for where `kest_something` is declared finds
        # nothing and cannot tell a name somebody kept private from a
        # declaration that went missing, which is the other thing this check
        # is for and the half of it nothing had watched.
        "what": "an internal function wearing the public prefix",
        "file": "src/check.c",
        "from": """static KestType *check_match(Checker *checker, KestExpr *expr,""",
        "to": """static KestType *kest_check_match(Checker *checker, KestExpr *expr,""",
        "also": ["src/check.c",
                 """        return check_match(checker, expr, expected);""",
                 """        return kest_check_match(checker, expr, expected);"""],
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "is this file's own and is named as a public one",
    },
    {
        # A public function no host in this tree calls. The header says there
        # is somewhere to look for an example of each of its functions, and
        # what makes that true is the two hosts here calling all of them. One
        # that nothing calls is a promise in the header with nothing behind it,
        # and it still compiles, still links and still works.
        "what": "a public function no host in this tree calls",
        "file": "examples/embed.c",
        "from": """    size_t wanted = kest_heap_wanted(engine->runtime);""",
        "to": """    size_t wanted = 0;""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "is declared and no host in this tree calls it",
    },
    {
        # The ten-line host `check.sh` writes is compiled and thrown away, so
        # nothing here would hold a name it was the only user of: it would read
        # as used to that check and as unused to this one. The rule is that it
        # may only call what a host in the tree already calls, and reaching
        # past the public header into the library's own names is the shape that
        # breaks it.
        "what": "a throwaway host leaning on a name nothing else here leans on",
        "file": "tools/check.sh",
        "from": """    kest_report(runtime, stdout, KEST_FORM_TEXT);""",
        "to": """    kest_report(runtime, stdout, KEST_FORM_TEXT);
    kest_lexer_next(NULL);""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "is called by the host it writes and by no host in the tree",
    },
    {
        # A shape in the library nothing anywhere names, which is the third of
        # the three the checker answers for: a function nothing calls and a
        # constant nothing reads were watched, and a shape nothing holds was
        # not.
        "what": "a library shape nothing has ever held",
        "file": "lib/std/vec.kest",
        "from": """struct Vec2 {""",
        "to": """struct Spare {
    n: i32
}

struct Vec2 {""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "so nothing has ever held one",
    },
    {
        # And this check reading its own parse of the code, which is the twin
        # of the layout walk beside it. An instruction is an offset and two
        # spaces after it; a pattern that keeps the offset finds a set of
        # numbers, and every instruction is missing from it — which would read
        # as the compiler emitting none of them rather than as the walk being
        # wrong. Both are said, and the second is the one that is true.
        "what": "a walk of the code that keeps where rather than what",
        "file": "tools/check-dead.sh",
        "from": r"""        found = re.match(r'\s+\d{4,}  (\S+)', line)""",
        "to": r"""        found = re.match(r'\s+(\d{4,})  \S+', line)""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "as an instruction and it is not one",
    },
    {
        # The table of which instructions reach the heap is one of the lists
        # that has to be complete, and the missing `default` catches an
        # instruction nobody answered for. An instruction answered wrongly is
        # the other half: a call reaches the heap only if what it calls does,
        # and saying it always does refuses every `no.alloc` function that
        # calls anything. What sees that is not the proof — the proof is what
        # is wrong — but a command that cannot write out a program this tree
        # compiles.
        "what": "an instruction the proof says reaches the heap and does not",
        "file": "src/value.c",
        "from": """    case KEST_OP_CALL:
    case KEST_OP_CALL_VALUE:
    case KEST_OP_CALL_HOST:
    case KEST_OP_RETURN:
        return false;""",
        "to": """    case KEST_OP_CALL:
        return true;
    case KEST_OP_CALL_VALUE:
    case KEST_OP_CALL_HOST:
    case KEST_OP_RETURN:
        return false;""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "`emit` would not write it out",
    },
    {
        # A command that answers a tool with nothing when nothing is wrong.
        # The object carries what the file holds, and writing it only where
        # something was reported leaves every file that checks with an empty
        # stream. What reads it cannot tell that from a file that declares
        # nothing, so a library nothing names reads as a library with nothing
        # in it and every rule about what is reached passes over it.
        "what": "a command that says nothing to a tool when nothing is wrong",
        "file": "src/main.c",
        "from": """    if (json) {
        // One object, with whatever the command has to add beside what it
        // found wrong.""",
        "to": """    if (json && build->diags.count > 0) {
        // One object, with whatever the command has to add beside what it
        // found wrong.""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "said nothing about it",
    },
    {
        # A token the printed form of `lex` shows and the JSON leaves out. What
        # reads the JSON reads it twice and compares the two, so a token
        # missing from both readings is a token neither notices; the one thing
        # that would is the other form of the same answer.
        "what": "a token in one form of `lex` and not the other",
        "file": "src/main.c",
        "from": """    fputs(",\\"tokens\\":[", out);
    for (uint32_t i = 0; i < count; i++) {""",
        "to": """    fputs(",\\"tokens\\":[", out);
    for (uint32_t i = 0; i + 1 < count; i++) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "lex examples/math.kest: the two forms disagree",
    },
    {
        # A walk of a chunk that says which instruction and not what it
        # carries. Two forms of one answer are compared to hold each to the
        # other, and the numbers an instruction is written with were in
        # neither comparison: a slot said wrong reads as the same instruction
        # doing something else.
        "what": "a walk that says which instruction and not what it carries",
        "file": "src/value.c",
        "from": """                fprintf(out, "%s%u", k == 0 ? "" : ",",
                        read_u16(chunk, offset + 1 + k * 2));""",
        "to": """                fprintf(out, "%s%u", k == 0 ? "" : ",", 0u);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "the two forms disagree",
    },
    {
        # A run that says everything is named. What reads that flag holds a
        # library to naming everything it declares, so a compiler answering
        # `true` for all of them turns that rule into one that cannot fail --
        # and a flag that is always true reads the same as a flag that is
        # right until something is false.
        "what": "a run that says everything is named",
        "file": "src/types.c",
        "from": """        fprintf(out, ",\\"named\\":%s", symbol->named ? "true" : "false");""",
        "to": """        fprintf(out, ",\\"named\\":%s", symbol->named ? "true" : "true");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "named.UNREAD says named is True",
    },
    {
        # And what a function takes, which the same reader keys on: two
        # functions of one name are told apart by it, so a run that said they
        # take the same thing would be a library with two of a name and a rule
        # that could not see it.
        "what": "a run that says a function takes something else",
        "file": "src/types.c",
        "from": """            kest_json_text(kest_type_name(arena, symbol->type->params[p]), out);""",
        "to": """            kest_json_text("i32", out);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "says it takes",
    },
    {
        # A comment reported at a place it is not. Everything that says a
        # formatter kept what somebody wrote works out where a comment sits
        # from the line and the column it is reported at, so a walk that
        # reported one place for every one of them would be a comparison of
        # nothing against nothing.
        "what": "a comment reported where it is not",
        "file": "src/main.c",
        "from": """        fprintf(out, "%s{\\"line\\":%u,\\"column\\":%u,\\"text\\":", i > 0 ? "," : "",
                line, column);""",
        "to": """        fprintf(out, "%s{\\"line\\":%u,\\"column\\":%u,\\"text\\":", i > 0 ? "," : "",
                line, 0u);""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "a comment says it is at",
    },
    {
        # A walk over a file that does not step over what a string wrote
        # itself. A quote a string holds does not end it, and neither do the
        # quotes inside a hole, so a walk that stops at the first of them
        # reads the rest of the line as a comment.
        "what": "a quote a string wrote itself taken as the end of it",
        "file": "src/lexer.c",
        "from": """                if (text[i] == '\\\\') {
                    i++;""",
        "to": """                if (false) {
                    i++;""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "the file with comments in it does not format",
    },
    {
        # A tree that stops saying what a file reads. Every head of the tree
        # carries something beside itself, and a formatter could drop any one
        # of them and be called faithful unless a pair differs in that one
        # thing and no other.
        "what": "a tree that stops saying what a file reads",
        "file": "src/ast.c",
        "from": """    case KEST_DECL_IMPORT:
        fputs("(import ", out);
        print_span(source, decl->name, out);""",
        "to": """    case KEST_DECL_IMPORT:
        fputs("(import ", out);""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "differing in what a file reads have one tree",
    },
    {
        # A tree that says how much is inside an `if` rather than what. The
        # formatter is held to meaning the same by this tree, so an arm whose
        # contents the tree does not carry is an arm a formatter could rewrite
        # and be called faithful.
        "what": "a tree that says the size of an arm and not its shape",
        "file": "src/ast.c",
        "from": """            fputc('\\n', out);
            print_block(&branch->then_body, source, depth + 1, out);
            indent(out, depth);""",
        "to": """            fprintf(out, " %u statement%s", branch->then_body.count,
                    branch->then_body.count == 1 ? "" : "s");""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "differing in what an `if` does have one tree",
    },
    {
        # An operator a program is written with and no document shows. What the
        # reference said about `^` and `~` was a sentence naming them, and what
        # it showed was nothing: a reader looking for what one looks like found
        # a list of names and no line of Kest.
        "what": "an operator no document is written with",
        "file": "docs/language.md",
        "from": """let apart = flags ^ wanted
""",
        "to": "",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "no block here is written with `^`",
    },
    {
        # Half of what a code says. A message written as a choice between two
        # is one call and two things it can say, and a reader that takes the
        # literal after the code takes the first arm: the second is a message
        # no document could quote, and nothing would say why.
        "what": "a message read as the first of the two it may be",
        "file": "tools/check-docs.sh",
        "from": """        if i + 2 < len(pieces) and pieces[i + 2][1] == ':':
            says[piece].append(pieces[i + 2][0])""",
        "to": """        if False:
            says[piece].append(pieces[i + 2][0])""",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "no run says this",
    },
    {
        # The list at the top of `docs/decisions.md` is what tells a decision
        # this project still keeps from one it replaced, because nothing there
        # is edited and the two read alike. A row naming a number nothing is
        # written under is a reader sent to a decision that was never made, and
        # a heading that stops saying which number it is makes one of those out
        # of a row nobody touched.
        "what": "a superseding list naming a decision nobody wrote",
        "file": "docs/decisions.md",
        "from": """## D115 — a count may be the name of a constant, superseding D064""",
        "to": """## A count may be the name of a constant, superseding D064""",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "the list at the top names `D115` and no decision is "
                  "written under it",
    },
    {
        # And the other way: a decision whose body says it supersedes another
        # and no row saying which. The body is the claim and the list is what
        # can be read, so a claim with no row is a replacement nothing above
        # can be held to.
        "what": "a decision that supersedes something the list does not carry",
        "file": "docs/decisions.md",
        "from": """| D064 | D115 | a count may be the name of a constant, not only a number |
""",
        "to": "",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "says it supersedes something and the list at the top "
                  "does not say so",
    },
    {
        # One word for it, asked for by name. A decision that says it replaces
        # another in some other words is one nothing can pair with the list,
        # and the near miss is the one a writer makes: every one of these reads
        # to a person exactly like the word this wants.
        "what": "a decision that says it replaces rather than supersedes",
        "file": "docs/decisions.md",
        "from": """This supersedes D182, which said the file was checked and not run.""",
        "to": """This replaces D182, which said the file was checked and not run.""",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "the word this reads is `supersedes`",
    },
    {
        # An entry that says what was done and not what said so. A worklog is a
        # record of what this project believes about itself, and an entry with
        # no `**Runs:**` line is a claim: it says a thing was built and leaves
        # out what was run to believe it. Written after the file rather than
        # into it, for the same reason the entry with no `**Next:**` is.
        "what": "an entry that says what is next and not what was run",
        "file": "docs/worklog.md",
        "end": """
## A turn that said what comes next and not what said so

Something was done and written down here, and what was run to believe it was
left off.

**Next:** whatever comes after a turn nobody can check.
""",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "does not say what was run",
    },
    {
        # A decision named where somebody would chase it. `D436` in a comment
        # is a promise that `docs/decisions.md` says something under that
        # number, and a heading that no longer says the number is a reader sent
        # nowhere — which reads exactly like a decision that was made and is a
        # decision that is not there. The break is on the document's side
        # rather than the comment's because a wrong number written down here
        # would be a wrong number in this tree, and this file is one of the
        # ones that rule reads.
        "what": "a comment naming a decision nobody wrote",
        "file": "docs/decisions.md",
        "from": """## D436: a number that means no says which no""",
        "to": """## A number that means no says which no""",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "names `D436` and no decision is written under it",
    },
    {
        # The reference's list of what each example runs, held to the tree both
        # ways. An example missing from the list is a rule nobody can find the
        # run for, and the list is what makes a rule a thing to run rather than
        # a paragraph to believe.
        "what": "an example the reference stopped saying what it runs",
        "file": "docs/language.md",
        "from": """| `ants.kest` | a frame that walks an array of value structs and moves each one |
""",
        "to": "",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "is an example and the reference does not say what it runs",
    },
    {
        # And the other way round: a row for a file that is not there, which is
        # a reader told to go and run something that does not exist. A list of
        # files goes stale the day somebody moves one, and this is the day.
        "what": "a reference row for an example that is not there",
        "file": "docs/language.md",
        "from": """| `ants.kest` | a frame that walks an array of value structs and moves each one |
""",
        "to": """| `ants.kest` | a frame that walks an array of value structs and moves each one |
| `swarm.kest` | the same frame with a thousand of them |
""",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "is listed and is not in `examples`",
    },
    {
        # An option the documents write and the command line does not read.
        # A reader copies what is written here into a shell, and what comes
        # back is a refusal about a name nobody typed on purpose. The commands
        # were held to this and the options beside them had not been watched.
        "what": "an option the documents write and nothing reads",
        "file": "docs/language.md",
        "from": """not be there. `--json` says both, because a tool reading a file somebody is""",
        "to": """not be there. `--as-json` says both, because a tool reading a file somebody is""",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "writes `--as-json` and the command line does not read it",
    },
    {
        # A turn that wrote down what it did and not what comes next. The
        # `**Next:**` line on the last entry is the one line in these
        # documents that is read by something other than a person: it is what
        # the next turn is given, so a document missing it is work that stops
        # rather than a document that reads badly. Nothing in the entry there
        # today is there tomorrow, so what this breaks is written after the
        # file rather than quoted out of it.
        "what": "an entry that says what was run and not what is next",
        "file": "docs/worklog.md",
        "end": """
## A turn that said what it did and not what comes next

Something was done and written down here, and the line the next turn reads was
left off.

**Runs:** `make check`, everything passing.
""",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "the last entry does not say what is next",
    },
    {
        # A section renamed, which is a document a check can no longer read.
        # The rules a host has to keep for itself are held by a host doing each
        # of them wrong on purpose, and what pairs the two is a heading. Rename
        # it and the document still reads perfectly to a person while nothing
        # holds a word of it, which is the shape every sweep here guards
        # against and none had been watched guarding against in a document
        # somebody wrote.
        "what": "a section a check reads under a name it no longer has",
        "file": "docs/language.md",
        "from": """### What a host has to keep""",
        "to": """### Rules a host has to keep""",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "nothing here says what a host has to keep",
    },
    {
        # The same again over the other section this reads by name: the table
        # saying which rule each example runs. That one is what makes the list
        # of examples a thing to run rather than a paragraph to believe, and a
        # heading it is looked up under is the whole of the join.
        "what": "the table of what the examples run read under a name it no "
                "longer has",
        "file": "docs/language.md",
        "from": """## Where each rule is run""",
        "to": """## Where the rules are run""",
        "make": [],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "nothing here says where each rule is run",
    },
    {
        # A way of wording a refusal that nothing has ever made happen. A code
        # is a name the compiler chooses and `K0310` says fourteen different
        # things under it; a check that asks for one of them leaves the other
        # thirteen said by nothing, which is how thirteen of them were.
        "what": "a wording of a refusal that nothing has seen",
        "file": "tools/check-commands.sh",
        # Three rows, because three say it: a field asked of an integer, of
        # the reference a store's walk gives, and of an optional. The form is
        # `%s` has no fields, so any one left behind still shows it.
        "from": r"""K0307|fn main() -> i32 {\n    let n = 1\n    return n.x\n}|`i32` has no fields
K0307|struct Thing {\n    n: i32\n}\n\nfn main() -> i32 {\n    let s: store<Thing> = store()\n    let a = add(s, Thing(1))\n    for t in s {\n        return t.n\n    }\n    return 0\n}|read what it names with `get` and take `n` off that
K0307|struct P {\n    x: i32\n}\n\nfn main() -> i32 {\n    let p: P? = P(1)\n    return p.x\n}|take what it holds out with `if let`
""",
        "to": "",
        "make": [],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "and nothing here has made it",
    },
    {
        # A check looking for a code this compiler no longer has. What a check
        # looking for words nothing says does is pass: the run it reads never
        # has them, so a code retired from the source takes its own asking with
        # it and nothing says so.
        "what": "a check asking for a code that was retired",
        "file": "tools/check-commands.sh",
        "from": r"""K0104|fn main() -> i32 {\n    let a = 0x\n    return a\n}|literal has no digits""",
        "to": r"""K0114|fn main() -> i32 {\n    let a = 0x\n    return a\n}|literal has no digits""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "which nothing in `src` says",
    },
    {
        # The reference shows what the compiler says, and what it showed once
        # was invented. The drift is the same either way round: the message
        # moves and the document keeps the old one.
        "what": "a message the reference quotes and nothing says",
        "file": "src/check.c",
        "from": """        report(checker, expr->field.name, "K0307", "`%s` has no field `%.*s`",""",
        "to": """        report(checker, expr->field.name, "K0307", "`%s` holds no field `%.*s`",""",
        "make": ["kest"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "no run says this",
    },
    {
        # The pipeline says a module includes only what is above it, and
        # nothing but this said so: the parser reaching down for the types
        # would have compiled, and the rule would have been a sentence in a
        # document.
        "what": "a module that includes one below it",
        "file": "src/lexer.c",
        "from": '#include "lexer.h"',
        "to": '#include "lexer.h"\n\n#include "types.h"',
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "which is below it",
    },
    {
        # A check that writes where another run of it writes. This is the one
        # this project has already made: two runs on one fixed name under
        # `/tmp` was a gate that failed one run in six for no reason anybody
        # could see, and what fixed it was every check making somewhere of its
        # own. Nothing said it had to until now.
        "what": "a check that writes to a name another run has too",
        "file": "tools/check-header.sh",
        "from": "work=$(mktemp -d)",
        "to": 'work="/tmp/kest-header"; mkdir -p "$work"',
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "which is a name another run has too",
    },
    {
        # A build that says the library will be in one place and an install
        # that puts it in another. The first is a string in every object — the
        # last place a program looks for `std` — and the second is a line in a
        # rule, and a build installed under one and told the other finds no
        # library and says so from a path nobody can fix by moving anything.
        "what": "a build told one place and installed to another",
        "file": "Makefile",
        "from": "\tcp lib/std/*.kest $(DESTDIR)$(PREFIX)/lib/kest/std/",
        "to": "\tcp lib/std/*.kest $(DESTDIR)$(PREFIX)/share/kest/std/",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "and an install puts it under",
    },
    {
        # A thing this project builds and does not clean, which is rubbish left
        # in a tree somebody thought was clean — and the `Makefile` is the file
        # nothing here has ever read.
        "what": "a build that leaves something behind",
        "file": "Makefile",
        "from": '''\trm -rf build kest kest-debug libkest.a examples/embed \\
\t    examples/embed-debug''',
        "to": "\trm -rf build kest kest-debug libkest.a examples/embed",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "is built and `clean` does not remove it",
    },
    {
        # A file this project puts on somebody else's machine and does not take
        # away again. An install and an uninstall are one thing said twice, and
        # the second is the half nobody runs until it matters.
        "what": "an install that leaves a file behind",
        "file": "Makefile",
        "from": "\trm -f $(DESTDIR)$(PREFIX)/include/kest.h\n",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "where `uninstall` leaves it",
    },
    {
        # No memory to read a program with, and a command line that carries on
        # anyway. This is the only failure here that is not about the program
        # or the host: the machine underneath had nothing to give, and what
        # every path in this project does about it is say so and stop.
        "what": "a command line with no memory that says nothing",
        "file": "src/main.c",
        "from": """            kest_diags_say_one(json ? stdout : stderr, json,
                               KEST_STARVED_CODE, KEST_STARVED_SAYS);""",
        "to": "",
        "also": ("src/mem.c",
                 "KestArena *kest_arena_new(void) {\n"
                 "    KestArena *arena = calloc(1, sizeof(KestArena));",
                 "KestArena *kest_arena_new(void) {\n"
                 "    KestArena *arena = NULL;"),
        "make": ["kest"],
        "program": "unread.kest",
        "source": "fn main() -> i32 {\n    return 0\n}\n",
        "caught": "not enough memory to finish",
    },
    {
        # A machine that cannot be made and says nothing about why. A host with
        # a number too big for the machine it is on and a host with a program
        # that would not compile got the same nothing back, and only one of
        # those is about the program.
        "what": "a machine that cannot be made and says nothing",
        "file": "src/vm.c",
        "from": '''            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0638", nowhere,
                           "this host asked for %u slots of stack and this "
                           "machine cannot have that much",
                           rt->stack_slots);''',
        "to": "            (void)nowhere;",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "asked for more stack than there is and was told",
    },
    {
        # A ceiling lowered in a copy of the tree is lowered by reading the
        # line it is written on. The same number in brackets is the same
        # number to a compiler and another line to a reader, and a ceiling that
        # quietly stops being lowered is three refusals nothing reaches and a
        # check that says it reached them.
        "what": "a ceiling written another way than the one that is read",
        "file": "src/vm.c",
        "from": """#define MAX_COUNTED INT32_MAX""",
        "to": """#define MAX_COUNTED (INT32_MAX)""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "so the ceiling this lowers has moved",
    },
    {
        # An arena whose first block is bigger than the memory a small machine
        # has. Every program here still runs on a machine with a gigabyte, so
        # nothing else would say a word; what says it is the ladder, which
        # starts at four megabytes and doubles to sixty-four looking for a rung
        # the compiler runs on, and finds none.
        "what": "a compiler that will not start on a machine somebody has",
        "file": "src/mem.c",
        "from": """#define BLOCK_SIZE (64 * 1024)""",
        "to": """#define BLOCK_SIZE (64 * 1024 * 1024)""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "there is no amount of memory this program runs in",
    },
    {
        # A row in the reference's table of what there is a most of that
        # nothing runs into. The table is what a reader is sent to, and a
        # number written there with no program that reaches it is a message
        # nobody has seen — which is the whole of what this check is for and
        # the half of it nothing had watched.
        "what": "a ceiling the reference writes that nothing runs into",
        "file": "docs/language.md",
        "from": """| 256 | names in a function, counting its parameters |""",
        "to": """| 256 | names in a function, counting its parameters |
| 64 | fields a flags type may hold |""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "so its message is one nobody has seen",
    },
    {
        # And the refusal at a ceiling saying which ceiling it was. A program
        # with one too many in it is refused either way; what the reader needs
        # is the number, and a message that leaves it out is a refusal that
        # says something went wrong and not what the most is.
        "what": "a refusal at a ceiling that does not say what the most is",
        "file": "src/types.c",
        "from": """                               "between one and %u, and `[T]` for one that "
                               "grows",
                               MAX_ELEMENTS);""",
        "to": """                               "a number that fits, and `[T]` for one "
                               "that grows");""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "is not `K0326` with 65535 in it",
    },
    {
        # A host written against the public header that stops compiling. The
        # two hosts in this tree are built by the gate and would say so; the
        # three this check writes are built by this check, and a name that
        # moved under one of them is a refusal nothing reaches and a check that
        # says it reached it.
        "what": "a public name that moved under the hosts a check writes",
        "file": "include/kest.h",
        "from": """    KEST_REFUSED_NOTHING,""",
        "to": """    KEST_REFUSED_NOBODY,""",
        "also": ["src/vm.c", """        return KEST_REFUSED_NOTHING;""",
                 """        return KEST_REFUSED_NOBODY;"""],
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "the host that spends a heap does not build",
    },
    {
        # A ladder whose step is bigger than the ladder walks one rung and
        # stops. It still starts where the program runs and still ends where
        # the C library cannot be mapped, and every rung it walked ran — which
        # is a ladder that never reached the memory the program refuses in and
        # says so rather than reporting the one rung as a ladder.
        "what": "a ladder that steps over everything it was walked for",
        "file": "tools/check-ceilings.sh",
        "from": """        level=$((level - 100))""",
        "to": """        level=$((level - 100000))""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "so the line between them was never crossed",
    },
    {
        # A ceiling something else is written to depend on. This project holds
        # counts with `_Static_assert`, and one written about a number a
        # program can run into is a ceiling that cannot be lowered — so the
        # copy this check makes to reach three refusals does not build, and
        # the tree it was made from does.
        "what": "a ceiling that cannot be lowered",
        "file": "src/compile.c",
        "from": """#define MAX_EXTERNS 65536""",
        "to": """#define MAX_EXTERNS 65536
_Static_assert(MAX_EXTERNS > 1024, "a program may ask for plenty of names");""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "the tree with a lower ceiling does not build",
    },
    {
        # A heap a host said was all there is, spent without a word. The
        # ceiling is a host's number and the only thing that reads it is the
        # allocator, so a program that walks past it is a frame budget that was
        # never a budget.
        "what": "a heap ceiling nothing is held to",
        "file": "src/mem.c",
        "from": '''    if (arena->ceiling != 0 && arena->handed + taking > arena->ceiling) {
        arena->refused = taking;
        arena->refused_by_ceiling = true;
        return NULL;
    }
    if (fresh) {''',
        "to": "    if (fresh) {",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "was spent in silence",
    },
    {
        # A machine that lets calls nest deeper than a host allowed. The
        # number is the host's and the check is one comparison; what it stands
        # between is a program that stops and a stack this project does not
        # own being walked off the end of.
        "what": "calls that nest deeper than they may",
        "file": "src/vm.c",
        # The line above it as well, because the same sentence is written
        # twice in the machine -- once in front of a call by name and once in
        # front of a call through a value -- and a hole that quotes it alone
        # breaks whichever is written first. Which one a hole is about is a
        # thing a hole has to say.
        "from": """            const KestChunk *callee = module->functions[index];

            if (rt->frame_count == rt->call_depth) {""",
        "to": """            const KestChunk *callee = module->functions[index];

            if (false) {""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "nesting.kest was not told what the machine has",
    },
    {
        # And the stack, which is the same pair one line further down: the
        # machine says it is out of stack in front of a call by name and again
        # in front of a call through a value, and only the first had ever been
        # reached. The whole of the arm above it comes with the quotation,
        # because the two are the same three lines.
        "what": "a stack that runs out under a call through a value",
        "file": "src/vm.c",
        "from": """                     promised, entered);
                kest_diags_fault(vmp->diags,
                                 "the shape it was held in promises and the "
                                 "body does not");
                return false;
            }

            if (rt->frame_count == rt->call_depth) {
                fail(vmp, frame, instruction, "K0602",
                     "calls nest more than %u deep", rt->call_depth);
                return false;
            }
            KestValue *base = top - argument_slots;
            if (base + callee->slot_count + callee->stack_needed > rt->limit) {
                fail(vmp, frame, instruction, "K0602", "out of stack");""",
        "to": """                     promised, entered);
                kest_diags_fault(vmp->diags,
                                 "the shape it was held in promises and the "
                                 "body does not");
                return false;
            }

            if (rt->frame_count == rt->call_depth) {
                fail(vmp, frame, instruction, "K0602",
                     "calls nest more than %u deep", rt->call_depth);
                return false;
            }
            KestValue *base = top - argument_slots;
            if (false) {
                fail(vmp, frame, instruction, "K0602", "out of stack");""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "holding-through.kest was not told what the machine has",
    },
    {
        # And the other copy, in front of a call through a function value,
        # which nothing had ever gone through: taking this one out let a
        # program run the machine off its own stack and answer with a signal.
        "what": "calls through a value that nest deeper than they may",
        "file": "src/vm.c",
        "from": """                                 "body does not");
                return false;
            }

            if (rt->frame_count == rt->call_depth) {""",
        "to": """                                 "body does not");
                return false;
            }

            if (false) {""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "through.kest was not told what the machine has",
    },
    {
        # A check that runs before the thing it checks has been built. For a
        # probe that passes when a command fails, a binary that is not there
        # yet is a pass: it fails for the wrong reason and nothing says which
        # reason it was.
        "what": "a check that runs before the build",
        "file": "tools/check.sh",
        "from": "# Built twice, because the two are different programs:",
        "to": '''if ./kest run examples/math.kest >/dev/null 2>&1; then
    :
fi

# Built twice, because the two are different programs:''',
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "reaches for what it built on line",
    },
    {
        # A check taken out of the middle of the gate. The gate is a run of
        # things done one after another, and a run with one fewer line in it
        # reads exactly like the day before: nothing counts them, and what was
        # deleted is a check that no longer happens.
        "what": "a check taken out of the middle of the gate",
        "file": "tools/check.sh",
        "from": 'say "modules" "every file is where its \\`module\\` line says it is"\n',
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "and nothing in it says so",
    },
    {
        # A function this library makes that no header declares. Nothing can
        # call it, so every check about what is declared passes over it in
        # silence — and a declaration written in a way a pattern cannot read
        # looks exactly the same from here, which is what this is really for.
        "what": "a function no header declares",
        "file": "src/value.c",
        "from": "const char *kest_scalar_name(uint8_t kind) {",
        "to": """uint32_t kest_value_nobody_declared(void) {
    return 0;
}

const char *kest_scalar_name(uint8_t kind) {""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "and no header declares it",
    },
    {
        # A host whose binds a check reads with a pattern that no longer
        # matches. What it holds is the promises made about a host — that a
        # bound function under an `extern ... no.alloc` makes no text and lends
        # no array — and a host it reads nothing out of is a check saying every
        # promise is kept because it never found one.
        "what": "a host whose binds a check can no longer read",
        "file": "examples/embed.c",
        "from": '''    if (host == NULL || !kest_host_bind(host, "Io.write", io_write, stdout) ||
        !kest_host_bind(host, "Engine.decide", engine_decide, &decider) ||
        !kest_host_bind(host, "Engine.name", engine_name, &decider)) {''',
        "to": '''    if (host == NULL ||
        !kest_host_bind(host,
                        "Io.write", io_write, stdout) ||
        !kest_host_bind(host,
                        "Engine.decide", engine_decide, &decider) ||
        !kest_host_bind(host,
                        "Engine.name", engine_name, &decider)) {''',
        "make": ["kest"],
        "tool": "tools/check-costs.sh",
        "caught": "and this reads 1 of them",
    },
    {
        # A table written differently, which is a check that reads it with a
        # pattern finding nothing. Nothing is what agrees with everything: two
        # empty lists are in step with each other, and a loop over none of them
        # checks none of it. This is the shape of every check in `tools` that
        # reads the source rather than running it.
        "what": "a table a check reads with a pattern that stops matching",
        "file": "src/check.c",
        "from": '''} BUILTIN_TAKES[] = {
    {"find", {"t", "needle", "from"}},
    {"matches", {"t", "at", "needle"}},
    {"rest", {"t", "at", NULL}},
    {"slice", {"t", "from", "count"}},
};''',
        "to": '''} BUILTIN_TAKES[] = {
    { "find", { "t", "needle", "from" } },
    { "matches", { "t", "at", "needle" } },
    { "rest", { "t", "at", NULL } },
    { "slice", { "t", "from", "count" } },
};''',
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "nothing in the source is where this reads it from",
    },
    {
        # A scalar a layout can hold with no name of its own. A host reads a
        # layout piece by piece and a message says what a slot holds, and both
        # of them read this list — one name short and every kind after it
        # answers to the name of the one before.
        "what": "a scalar a layout holds with no name",
        "file": "src/value.c",
        "from": '"f32", "f64", "word", "payload"};',
        "to": '"f32", "f64", "word"};',
        "make": ["build/release/value.o"],
        "in_build": True,
        "caught": "every scalar a layout holds has a name",
    },
    {
        # A token kind with no name of its own. Every message about what was
        # found where something else was wanted reads this list, so one name
        # short is every kind after it answering to the name before it.
        "what": "a token kind with no name",
        "file": "src/lexer.c",
        "from": '''    "string",      "byte",        "`break`",     "`const`",    "`continue`",''',
        "to": '''    "string",      "byte",        "`break`",     "`const`",''',
        "make": ["build/release/lexer.o"],
        "in_build": True,
        "caught": "every token kind has a name",
    },
    {
        # An instruction with no name of its own, which is a disassembly that
        # says one thing and runs another from there on.
        "what": "an instruction with no name",
        "file": "src/value.c",
        "from": '''    {"array", U16_U16},    {"make.array", U16},   {"push", U16},''',
        "to": '''    {"array", U16_U16},    {"make.array", U16},''',
        "make": ["build/release/value.o"],
        "in_build": True,
        "caught": "every instruction has a name",
    },
    {
        # A kind of type nothing has an opinion about. What a value can be
        # written as is a list in two files, held by there being no `default`
        # in either: a tag added to the language stops the build until somebody
        # says what a value of it looks like written down. Nothing had ever
        # seen it stop.
        "what": "a kind of type nothing says how to write",
        "file": "src/types.h",
        "from": "} KestTypeTag;",
        "to": "    KEST_T_INVENTED,\n} KestTypeTag;",
        "make": ["build/release/types.o"],
        "in_build": True,
        "caught": "KEST_T_INVENTED",
    },
    {
        # A kind of token nothing says whether a line may end after. Where a
        # statement ends is decided by what the last token was, so a token kind
        # added to the language and not answered for is a line that ends
        # somewhere nobody chose.
        "what": "a kind of token nothing says a line may end after",
        "file": "src/lexer.h",
        "from": "} KestTokenKind;",
        "to": "    KEST_TOK_INVENTED,\n} KestTokenKind;",
        "make": ["build/release/lexer.o"],
        "in_build": True,
        "caught": "KEST_TOK_INVENTED",
    },
    {
        # An instruction the proof that reads emitted code has never heard of.
        # It is the second of the two proofs of a `no.alloc` promise and the
        # one that says the first was wrong, so a list of instructions it
        # quietly does not know about is a promise kept by not looking.
        "what": "an instruction the promise's second proof does not know",
        "file": "src/value.h",
        "from": "} KestOp;",
        "to": "    KEST_OP_INVENTED,\n} KestOp;",
        "make": ["build/release/value.o"],
        "in_build": True,
        "caught": "KEST_OP_INVENTED",
    },
    {
        # A message whose words disagree with the numbers put in them. A `%u`
        # given an `i64` prints a number nobody wrote, and the message is a
        # sentence either way: reading it does not show it. What says so is the
        # compiler, so what catches this is a build that stops.
        "what": "a message that says an `i64` through a `%u`",
        "file": "src/vm.c",
        "from": '''                                   "it was making an array of %lld of %u bytes "
                                   "each",
                                   (long long)count, layout->size);''',
        "to": '''                                   "it was making an array of %u of %u bytes "
                                   "each",
                                   count, layout->size);''',
        "make": ["build/release/vm.o"],
        "in_build": True,
        "caught": "expects argument of type",
    },
    {
        # A heap that ran out where nothing was growing, and said only that it
        # had. A program asking for a million of something at once and one
        # appending to a list are the same message otherwise, and the first is
        # a number in the program while the second is a ceiling.
        "what": "a heap that ran out without saying what was being made",
        "file": "src/vm.c",
        "from": """                kest_diags_suggest(vmp->diags,
                                   "it was making an array of %lld of %u bytes "
                                   "each",
                                   (long long)count, layout->size);""",
        "to": "",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "did not say what it was making",
    },
    {
        # A heap that ran out and said only that it had. What a host raises a
        # ceiling by is not what the last allocation asked for: a thing that
        # doubles asks for the double again, so what it was growing and how far
        # along it was is what decides anything.
        "what": "a heap that ran out without saying what was growing",
        "file": "src/vm.c",
        "from": """    kest_diags_suggest(vm->diags,
                       "it was %s holding %u of %zu bytes each, growing to %u",
                       what, held, each, growing_to);""",
        "to": "    (void)what, (void)held, (void)each, (void)growing_to;",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "did not say what was growing",
    },
    {
        # A ceiling that stops a program and says nothing about what stopped
        # it. What a host reads after one is a total that stopped short of what
        # it allowed, and the difference is what it was reaching for: a frame
        # that missed by eight bytes and one that missed by a megabyte are the
        # same message otherwise, and they are not the same problem.
        "what": "a refusal that does not say what it refused",
        "file": "src/mem.c",
        "from": """    if (arena->ceiling != 0 && arena->handed + taking > arena->ceiling) {
        arena->refused = taking;
        arena->refused_by_ceiling = true;
        return NULL;
    }
    if (fresh) {""",
        "to": """    if (arena->ceiling != 0 && arena->handed + taking > arena->ceiling) {
        arena->refused_by_ceiling = true;
        return NULL;
    }
    if (fresh) {""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "said it was reaching for",
    },
    {
        # A running total of what an arena has handed out that stops being the
        # sum of what it handed out. It is kept rather than counted so that a
        # ceiling costs nothing to ask about, and a ceiling is what reads it: a
        # number that has drifted is a program stopped early or let past what a
        # host allowed it, and neither says a word about where it came from.
        "what": "a total of what was handed out that is not the sum of it",
        "file": "src/mem.c",
        "from": """    if (offset + want + KEPT_BACK <= block->capacity) {
        block->used = offset + want + KEPT_BACK;
        arena->handed += taking;""",
        "to": """    if (offset + want + KEPT_BACK <= block->capacity) {
        block->used = offset + want + KEPT_BACK;""",
        "make": ["debug"],
        "binary": "kest-debug",
        "program": "grew.kest",
        "source": """module grew

fn main() -> i32 {
    let many: [i32] = array()
    for i in 0..40000 {
        push(many, i)
    }
    return len(many) - 40000
}
""",
        "caught": "its blocks gave away",
    },
    {
        # An allocation that arrives holding what was there before. Everything
        # above this file reads one expecting nought: a header whose unwritten
        # fields are noughts, a length nobody has set, a slot nobody has stored
        # to. A heap thrown away and handed out again is where that promise is
        # easiest to drop and hardest to see.
        "what": "an allocation that arrives holding what was there before",
        "file": "src/mem.c",
        "from": """    memset(first->data, 0,
           first->used < first->capacity ? first->used : first->capacity);""",
        "to": "",
        "make": ["embed-debug"],
        "host": "examples/embed-debug",
        "caught": "was not nought",
    },
    {
        # An arena that stops agreeing with what it keeps. The four shortcuts
        # it holds — the block it started with, the one that answered last, and
        # what they all sit between — are read at a crossing and at a reset,
        # and a program behaves exactly the same whether they are true or not.
        # Only the sanitised build says so, which is where this arena already
        # does its saying.
        "what": "an arena whose blocks fall outside what it says they do",
        "file": "src/mem.c",
        "from": """        if (block->data + block->capacity > arena->high) {
            arena->high = block->data + block->capacity;
        }
""",
        "to": "",
        "make": ["debug"],
        "binary": "kest-debug",
        "program": "grew.kest",
        "source": """module grew

fn main() -> i32 {
    let many: [i32] = array()
    for i in 0..40000 {
        push(many, i)
    }
    return len(many) - 40000
}
""",
        "caught": "sits outside what the arena says",
    },
    {
        # A machine that says it still has what it threw away. A host keeping
        # a piece of text between frames has nothing of its own to check
        # against: the pointer does not change when the heap under it goes.
        "what": "a machine that still has what it threw away",
        "file": "src/vm.c",
        "from": """    return kest_arena_holds(runtime->heap, kept.object) ||
           kest_arena_holds(runtime->module->arena, kept.object);""",
        "to": "    return true;",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "still had text it had thrown away",
    },
    {
        # A host's own string, taken as the program's. Nothing about a pointer
        # says where it came from, and text has no header to say it either, so
        # the only thing that can tell is the machine asking whether it handed
        # that address out.
        "what": "a host's own string taken as the program's text",
        "file": "src/vm.c",
        "from": "        if (type != NULL && type->tag == KEST_T_TEXT &&",
        "to": "        if (false &&",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "own string was taken",
    },
    {
        # A lend that costs the heap a header every time it is made. The block
        # is the host's, so what a lend leaves behind is the header — and a
        # host lending a batch a frame is a frame budget that grows for a
        # program doing the same thing every frame.
        "what": "a lend that leaves its header on the heap",
        "file": "src/vm.c",
        "from": "        runtime->spare_lends = one;",
        "to": "",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "frames of lending grew the heap",
    },
    {
        # A lend the host took back and the program read anyway. The block is
        # the host's and the header is the machine's, so what says a lend is
        # over is the header saying it: a host that ends one and a program that
        # keeps reading is memory the host has moved on from.
        "what": "a lend the host took back and can still be read",
        "file": "src/vm.c",
        "from": "        one->what = KEST_WAS_LENT;",
        "to": "",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "took back was read",
    },
    {
        # A handle that is a real handle and belongs to another machine. The
        # tag at its front reads exactly right, because it is the tag: what is
        # wrong with it is which heap it lives on, and nothing but asking the
        # heap can say so.
        "what": "a handle another machine made",
        "file": "src/vm.c",
        "from": "            !kest_arena_holds(runtime->heap, frame[at].object)) {",
        "to": "            false) {",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "another machine made was taken",
    },
    {
        # A word read as a number whatever it says. What a host hands over as
        # words is read the way the language reads one, and a reader that takes
        # anything hands a program a number nobody typed — which is what this
        # was before the command line refused it.
        "what": "a word that is not a number read as one",
        "file": "src/value.c",
        "from": """        double value = strtod(text, &end);
        if (end == text || *end != '\\0') {""",
        "to": """        double value = strtod(text, &end);
        if (false) {""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "was read as one",
    },
    {
        # A shape asked for as words, answered with minus one and nothing. A
        # number that means no cannot say which of three things it was — an
        # index that is no function, a function that gives nothing back, or a
        # result the language has no text of its own for — and a host given
        # the number and an empty report has to guess between them.
        "what": "a result with no words that says nothing about why",
        "file": "src/vm.c",
        "from": """        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0646", nothing,
                       "`%s` gives back `%s`, which has no text of its own",""",
        "to": """        return -1;
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0646", nothing,
                       "`%s` gives back `%s`, which has no text of its own",""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "without saying `K0646` and `no text of its own`",
    },
    {
        # And the other half of the same silence, which is the half a host is
        # likeliest to meet: it asked what a function said and the function
        # gives nothing back.
        "what": "a function with nothing to give that says nothing about it",
        "file": "src/vm.c",
        "from": """        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0646", nothing,
                       "`%s` gives nothing back, so there is nothing to write",""",
        "to": """        return -1;
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0646", nothing,
                       "`%s` gives nothing back, so there is nothing to write",""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "without saying `K0646` and `gives nothing back`",
    },
    {
        # How wide a frame is and what is in it are two walks of the same
        # thing: a width counted in slots while the function was compiled, and
        # a run of layouts registered beside it whose pieces are those slots.
        # A width that counted the arguments rather than their slots is right
        # for every function taking scalars and wrong for every one taking a
        # shape, which is the half of a boundary a host cannot see.
        "what": "a frame as wide as the arguments are many",
        "file": "src/compile.c",
        "from": """            compiler.chunk->param_slots = compiler.next_slot;
            remember_takes(&compiler, symbol == NULL ? NULL : symbol->type);""",
        "to": """            compiler.chunk->param_slots =
                (uint16_t)decl->function.param_count;
            remember_takes(&compiler, symbol == NULL ? NULL : symbol->type);""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "needs a frame 1 wide and what it takes (3)",
    },
    {
        # And the walk to where an argument starts, which is the number a host
        # is told so that it does not count the fields of the one before it. A
        # step of one a value is right for a frame of scalars and puts the
        # second `Point` over the first one's second float. What notices first
        # is the end of the same walk: where a result written over the
        # arguments would start is what they come to.
        "what": "a walk to where an argument starts that steps one a value",
        "file": "src/vm.c",
        "from": """        at += runtime->module->layouts[chunk->takes[i]].count;""",
        "to": """        at += 1;""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "says a result starts at",
    },
    {
        # A frame agreed to whatever a host said it holds. What a host writes
        # into a slot carries nothing that says what it is, so the only place
        # this can be caught is where the host says what it is about to write
        # and the program says what it takes.
        "what": "a frame that agrees with whatever a host says is in it",
        "file": "src/vm.c",
        "from": "            if (kinds[at] != layout->pieces[p].kind) {",
        "to": "            if (false) {",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "a frame it does not take",
    },
    {
        # A measurement of where the machine calls into the host that is short
        # of what it turns out to be. A host sizes a stack from that number and
        # calls back in from there, so being wrong about it is a host running
        # out of room somewhere it was told it would not — which is the shape
        # of a fault nothing in a program can cause.
        "what": "a measurement of where a host is called from that is short",
        "file": "src/value.c",
        "from": "    host_depth[which] = reaches_host ? host_deepest + 1 : 0;",
        "to": "    host_depth[which] = 0;",
        "make": ["kest"],
        "program": "reaches.kest",
        "source": """module reaches

import std.io

fn main() -> i32 {
    io.print("into the host and back")
    return 0
}
""",
        "caught": "K0633",
    },
    {
        # A copy of a generic that carries no promise where the generic made
        # one. Nothing runs differently for it — the machine only reads what a
        # chunk carries at the one call it checks — so what would say so is a
        # command being asked what the chunk is, beside the declaration.
        "what": "a chunk that carries less than its declaration promised",
        "file": "src/compile.c",
        "from": "        chunk->no_alloc = instance->type->no_alloc;",
        "to": "        chunk->no_alloc = false;",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/shapes.kest"],
        "caught": "the chunk carries",
    },
    {
        # A builtin the proof of a `no.alloc` promise has never heard of. It
        # would say nothing about it, the promise would be broken with no line
        # to name, and the proof that reads the emitted code would catch it
        # and call it a fault in the compiler — which is the one message that
        # blames this project for what a program did.
        "what": "a builtin the promise's proof has no opinion about",
        "file": "src/contract.c",
        "from": '                {"push", "`push` grows what it is given"},\n',
        "to": '',
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "has no opinion about `push`",
    },
    {
        # The one that would hide all the others: a check that is written,
        # named, and never reached for. Nothing else in `make check` says a
        # word about a check that does not run.
        "what": "a check that is written and never run",
        "file": "tools/check.sh",
        "from": 'ask "header" tools/check-header.sh\n',
        "to": '',
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "is not run by",
    },
    {
        # Text made out of a lend that points at the lend. What a program makes
        # out of a host's block is the program's own, copied onto the heap
        # where everything else it holds lives — and text that points at the
        # block instead is a program holding memory its owner has taken back.
        "what": "text made out of a lend that points at the lend",
        "file": "src/vm.c",
        "from": """            memcpy(text, bytes->bytes, bytes->length);
            text[bytes->length] = '\\0';
            (top++)->text = text;""",
        "to": """            memcpy(text, bytes->bytes, bytes->length);
            text[bytes->length] = '\\0';
            (top++)->text = (const char *)bytes->bytes;""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "after the lend was taken back",
    },
    {
        # A shape holding a pointer into the machine, lent anyway. The bytes
        # are the host's, so the machine did not put the pointer there and
        # cannot take it back when the lend ends: a program reading a name out
        # of one and keeping it holds the host's memory after the host has
        # moved on, which is a read of freed memory that nothing in the
        # program is wrong about.
        "what": "a lend of a shape holding the machine's own",
        "file": "src/vm.c",
        "from": """    if (layout->type != NULL && kest_type_holds_own(layout->type, &own)) {""",
        "to": """    if (false && kest_type_holds_own(layout->type, &own)) {""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "a shape holding a name was lent",
    },
    {
        # A name that is two types, lent as whichever was found first. A host
        # writes what the program calls the type, and two modules may each
        # declare a `Row`: taking the first is a lend of a shape the host never
        # named, laid out the way somebody else's declaration says.
        "what": "a lend of a name that means two types",
        "file": "src/vm.c",
        "from": "    if (named > 1) {",
        "to": "    if (false) {",
        "make": ["kest"],
        "tool": "tools/check-lends.sh",
        "caught": "a name that is two types is not two types to a lend",
    },
    {
        # A lend at an address a value of that type may not sit at. Where a
        # lend starts is the host's word and almost nothing about it can be
        # weighed — the block is the host's and what its bytes mean is the
        # host's too — but an address a type may not sit at is a program
        # reading a field across a word boundary the C standard has no answer
        # for, and that much is arithmetic.
        "what": "a lend at an address the type may not sit at",
        "file": "src/vm.c",
        "from": "    uintptr_t past = (uintptr_t)data % align;",
        "to": "    uintptr_t past = 0;",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "at a crooked address was allowed",
    },
    {
        # A lend of more than a host has. The count is the host's word and a
        # build that ships cannot weigh it — the block is the host's and its
        # end is written down nowhere the library can read — but the sanitised
        # build is told where every block ends, and a program given a longer
        # run than there is walks off the end of somebody else's memory.
        "what": "a lend of more than a host has",
        "file": "src/vm.c",
        "from": """    if (length > 0 &&
        __asan_region_is_poisoned(data, (size_t)length * stride) != NULL) {""",
        "to": "    if (false) {",
        "make": ["embed-debug"],
        "host": "examples/embed-debug",
        "caught": "out of two was taken",
    },
    {
        # A lend taken back by address rather than by run. A host lending the
        # tail of a block on its own has two runs that share their ends, and
        # what it takes back is memory: a handle over the tail of a block whose
        # owner has finished with it is reading what somebody else has now.
        "what": "a lend taken back from one address only",
        "file": "src/vm.c",
        "from": "        if (one != array && (to <= block || from >= block_end)) {",
        "to": "        if (one != array && from != block && to <= block_end) {",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "the tail of it was read",
    },
    {
        # A lend taken back from one handle and left alive under another. A
        # host that lends the same block twice has two handles and one block,
        # and what it takes back is the block: a handle still reading memory
        # its owner has moved on from is what ending a lend is for.
        "what": "a lend taken back from one handle only",
        "file": "src/vm.c",
        "from": "        if (one != array && (to <= block || from >= block_end)) {",
        "to": "        if (one != array && (to > block && from < block_end)) {",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "and the other was read",
    },
    {
        # A number written in a way a host cannot read whole. What the machine
        # writes is the shortest spelling that reads back as the same number,
        # and the reader that promise is about is a host's — this project reads
        # its own numbers back with its own reader, which agrees with itself
        # whatever it does.
        "what": "a number a host cannot read whole",
        "file": "src/value.c",
        "from": '    int written = snprintf(buffer, size, "%.*g", chosen, value);',
        "to": '    int written = snprintf(buffer, size, "%.*g,0", chosen, value);',
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "read 9 out of it",
    },
    {
        # Two machines counting places on their own. Two machines from one
        # build are two worlds of one program, and a host running both holds
        # references from each: if each counts from one, the first place of one
        # world is stamped like the first place of the other, and a reference
        # from over there names whoever is standing here.
        "what": "two machines that stamp their places alike",
        "file": "src/vm.c",
        "from": "    rt->stamps = &stamped->stamps;",
        "to": "    rt->stamps = &rt->own_stamps;",
        "also": ("src/vm.c", "    uint32_t *stamps;\n",
                 "    uint32_t *stamps;\n    uint32_t own_stamps;\n"),
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "from another machine named something here",
    },
    {
        # Two stores stamping their places alike. A reference is a place and a
        # stamp and nothing else, so what keeps a reference from naming
        # somebody in another store of the same shape is that no two places
        # anywhere are stamped the same — which is true because the machine
        # hands the stamps out and not the store.
        "what": "two stores that stamp their places alike",
        "file": "src/vm.c",
        "from": "            store->generations[index] = ++*rt->stamps;",
        "to": "            store->generations[index] = index + 1;",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "from another store named something here",
    },
    {
        # A reference followed whatever it names. A reference is a slot and how
        # many times that slot has been used, and the count is the whole of
        # what tells a reference to something dropped from a reference to
        # whoever is in that slot now — a host keeping one across three calls
        # is exactly the shape that finds out.
        "what": "a reference followed whatever it names",
        "file": "src/vm.c",
        "from": """    if (index >= store->used || !store->live[index] ||
        store->generations[index] != generation) {
        return NULL;
    }""",
        "to": """    if (index >= store->used) {
        return NULL;
    }
    (void)generation;""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "to something dropped still named it",
    },
    {
        # A write into a lend that goes somewhere else. A lend is the host's
        # memory and everything a program does with one but making text of it
        # reads and writes that memory: what the program wrote is what the host
        # has, and a write that lands anywhere else is a program and a host
        # holding two different things and neither of them told.
        "what": "a write into a lend that goes somewhere else",
        "file": "src/vm.c",
        "from": """            unsigned char *at = (--top)->object;
            pack(at + offset, layout, value);""",
        "to": """            unsigned char *at = (--top)->object;
            unsigned char aside[64];
            pack((at == NULL ? aside : aside) + offset, layout, value);""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "into this host's bytes",
    },
    {
        # Bytes with a nought among them taken as text. Text ends at its first
        # nought, so what a program would hold is shorter than what the host
        # handed over and nobody would be told: a name cut in half, a line that
        # says less than it holds. It is the third of the three ways a nought
        # gets into text and the one nothing had ever asked about.
        "what": "bytes with a nought among them taken as text",
        "file": "src/vm.c",
        "from": """    for (uint32_t i = 0; i < length; i++) {
        if (bytes[i] == 0) {
            KestSpan nowhere = {0, 0};
            kest_diags_in(runtime->diags, NULL);
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0611",
                           nowhere,
                           "byte %u of what the host handed over is zero, and "
                           "text ends at a zero byte",
                           i);
            return value;
        }
    }""",
        "to": "    (void)0;",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "were taken as text",
    },
    {
        # A mistake in the words at a command line, said to a person and not to
        # whatever asked. Every other refusal this project makes carries a code
        # and is written in the form the run asked for; these were sentences on
        # the standard error, so a tool driving this got a status and an empty
        # stream, which is the one answer nothing can act on.
        "what": "a mistake in the words said in one form only",
        "file": "src/main.c",
        "from": """    kest_diags_say_one(json ? stdout : stderr, json, code, said);""",
        "to": """    (void)json;
    kest_diags_say_one(stderr, false, code, said);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "kest nonsense --json",
    },
    {
        # And the form found after the word that was wrong rather than before
        # it. `--json` is one of the words, and a count that is not a number is
        # refused while they are being read: whichever came first decided how
        # the other was answered.
        "what": "a form read after the word it was needed for",
        "file": "src/main.c",
        "from": """    bool json = false;
    for (int i = 2; i < argc; i++) {
        json = json || strcmp(argv[i], "--json") == 0;
    }""",
        "to": """    bool json = false;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "2x --json` wrote",
    },
    {
        # A file the one form could not be written into, answered with the
        # object that says it is not in the one form. Both are true and only
        # one of them is what happened, and `-w` again would not fix it.
        "what": "a file that could not be written, said as a file in the wrong form",
        "file": "src/main.c",
        "from": """                refused_at_the_words(json, "K0706",
                                     "`%s` could not be written", paths[i]);
                status = 1;""",
        "to": """                status = 1;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "where it cannot write said",
    },
    {
        # A failure written before the lines that led to it. The two streams
        # are kept apart and a shell puts them back together, where what a
        # program printed waits in a buffer until the run ends and what went
        # wrong does not — so the machine that watched both happen tells them
        # in the wrong order.
        "what": "a failure written before what a program printed",
        "file": "src/diag.c",
        "from": """    if (out != stdout) {
        fflush(stdout);
    }
    for (uint32_t i = 0; i < diags->count; i++) {""",
        "to": "    for (uint32_t i = 0; i < diags->count; i++) {",
        # And the other thing that empties that buffer: the command line asks
        # the stream whether what the program said arrived, which it cannot do
        # without flushing it first. Two ways of emptying it and one order to
        # get wrong, so a hole about the order has to take both away.
        "also": ("src/main.c",
                 """        (fflush(program_wrote_to) == EOF || ferror(program_wrote_to))) {""",
                 """        ferror(program_wrote_to)) {"""),
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "came after what went wrong",
    },
    {
        # A tool given something on the stream it does not read. In JSON
        # everything is on one stream, because a tool reads one thing and an
        # object split over two is neither — and a message on the other stream
        # is a message nobody sees, in the one form written to be read by
        # something that cannot look.
        "what": "a tool given something on the stream it does not read",
        "file": "src/main.c",
        "from": "        kest_diags_write_json(&build->diags, stdout);",
        "to": """        kest_diags_write_json(&build->diags, stdout);
        kest_build_report(build, stderr, KEST_FORM_TEXT);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "on the stream it does not read",
    },
    {
        # A program that did not check, written out to a reader anyway. What a
        # half-worked-out program holds is a listing of things that may not be
        # there: a reader asked what is wrong and is shown a program instead.
        # A tool is given both on purpose, because an editor greys out what it
        # cannot see yet rather than forgetting it.
        "what": "a program that did not check written out anyway",
        "file": "src/main.c",
        "from": "            if (kest_build_check(build) && !json) {",
        "to": "            kest_build_check(build);\n            if (!json) {",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "was written out anyway",
    },
    {
        # A program of two files answered in full to a reader and by halves to
        # a tool. The words write out one module and count the rest because a
        # reader asked about one; the JSON writes every function there is
        # because a tool wants all of them, and a tool given the reader's half
        # cannot see the rest of the program.
        "what": "a program of two files said by halves to a tool",
        "file": "src/types.c",
        "from": """        if (symbol->type->tag != KEST_T_FN) {
            continue;
        }
        fputs(first ? "" : ",", out);""",
        "to": """        if (symbol->type->tag != KEST_T_FN) {
            continue;
        }
        if (i > 0 && symbol->source != program->globals[0].source) {
            continue;
        }
        fputs(first ? "" : ",", out);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "counted and 0 in the JSON",
    },
    {
        # Every module written out in full rather than the one that was asked
        # about. `check` says what the first file named declares and counts
        # what everything else holds; a project of thirty files answered in
        # full is an answer nobody reads.
        "what": "every module written out rather than the one asked about",
        "file": "src/types.c",
        "from": """        if (held != NULL && !same_module(symbol->name, root, root_length)) {
            Held *one = held_of(held, &elsewhere, symbol->name);
            if (type->tag == KEST_T_FN) {
                one->functions++;""",
        "to": """        if (false) {
            Held *one = held_of(held, &elsewhere, symbol->name);
            if (type->tag == KEST_T_FN) {
                one->functions++;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "was not the one written out",
    },
    {
        # A tick of a file with nothing to tick, run rather than refused. It
        # says it crossed a thousand times into a program that has no handler
        # to cross into, which is a measurement of nothing reported as a
        # measurement.
        "what": "a tick of a file with nothing to tick",
        "file": "src/main.c",
        "from": """                        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                       "K0621", nowhere,
                                       "nothing here takes events");""",
        "to": "                        (void)nowhere;",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a file with nothing to run ran",
    },
    {
        # A file with no declarations, written as nothing. Every rule the
        # formatter is held to is about what a declaration looks like, so a
        # file that has none has none of them to be true of: writing nothing at
        # all for one parses the same, means the same, and comes out the same
        # twice. What it loses is what somebody wrote.
        "what": "a file of nothing but a comment written as nothing",
        "file": "src/fmt.c",
        "from": """    // Anything written after the last declaration is still the author's.
    flush_comments(&printer, (uint32_t)source->length);""",
        "to": """    // Anything written after the last declaration is still the author's.
    if (unit->count > 0) {
        flush_comments(&printer, (uint32_t)source->length);
    }""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "came back without it",
    },
    {
        # A formatter that leaves a list flat when it cannot fit anyway. What a
        # line holds may be longer than a line — a name is one thing and
        # breaking it in half makes a different name — and what a formatter
        # does then is break what can break. Nothing in this tree has a name
        # that long, so the file is written by the check.
        "what": "a list left flat because its line could not fit",
        "file": "src/fmt.c",
        "from": """    if (printer->counting || printer->flat || count < 2) {
        return true;
    }""",
        "to": """    if (printer->counting || printer->flat || count < 2 ||
        measure(printer, expr) > LINE_LIMIT) {
        return true;
    }""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "was left on one line",
    },
    {
        # A name too long to be near anything. Names are compared qualified, so
        # a real one is longer than anybody expects, and a name past the table
        # the distance is measured in was answered for with nothing and no word
        # about why.
        "what": "a name too long to be near anything",
        "file": "src/diag.c",
        "from": "#define FAR_ENOUGH 256",
        "to": "#define FAR_ENOUGH 64",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "of seventy letters was near nothing",
    },
    {
        # Two letters the other way round counted as two mistakes. It is the
        # commonest way to write a name wrong and the one an edit count gets
        # wrong: `pirnt` is two edits from `print` by insertions and removals
        # and one by any reader's reckoning.
        "what": "two letters the other way round counted as two",
        "file": "src/diag.c",
        "from": """            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] &&
                a[i - 2] == b[j - 1]) {""",
        "to": "            if (false) {",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "the other way round were not one mistake",
    },
    {
        # A name of two letters left unanswered. Everything short is one edit
        # from everything else, which is why nothing under three was ever
        # suggested for — and what made that necessary is gone, because two
        # names equally near are both said and three are said as nothing.
        "what": "a name of two letters left unanswered",
        "file": "src/check.c",
        "from": "    if (length < 2) {\n        return NULL;\n    }\n    uint32_t limit = length <= 5 ? 1 : (uint32_t)length / 3;",
        "to": "    if (length < 3) {\n        return NULL;\n    }\n    uint32_t limit = length <= 5 ? 1 : (uint32_t)length / 3;",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "of two letters was not answered for",
    },
    {
        # Two names equally near and one of them said. What a suggestion says
        # is what this knows, and knowing two and saying one is choosing for a
        # reader without telling them there was a choice.
        "what": "two names equally near said as one",
        "file": "src/check.c",
        "from": "    found->level++;\n    if (found->second == NULL) {\n        found->second = candidate;\n    }",
        "to": "    found->level++;",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "equally near were said as one",
    },
    {
        # A module written nearly right and not named back. What a file writes
        # as often as anything else is the name in front of the dot, and a
        # module is a file rather than a declaration — so it was the one kind
        # of name nothing was ever suggested for.
        "what": "a module written nearly right and not named back",
        "file": "src/check.c",
        "from": "    if (written_plain) {\n        for (uint32_t i = 0; i < checker->program->global_count; i++) {\n            const char *whole = checker->program->globals[i].name;\n            if (kest_needs_import(checker->program, whole, strlen(whole))) {\n                continue;\n            }\n            const char *dot = strrchr(whole, '.');",
        "to": "    if (false) {\n        for (uint32_t i = 0; i < checker->program->global_count; i++) {\n            const char *whole = checker->program->globals[i].name;\n            if (kest_needs_import(checker->program, whole, strlen(whole))) {\n                continue;\n            }\n            const char *dot = strrchr(whole, '.');",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "was not named back",
    },
    {
        # A name a module does not have, said without which module was read. A
        # program read with a library that is not the one it was written
        # against asks for something that is not there, and the file it is not
        # in is the whole of what a reader needs — there is no version on a
        # library here, so where it came from is the only question to ask.
        "what": "a name a module does not have, without which module",
        "file": "src/check.c",
        "from": "            if (read != NULL && read->source != NULL) {",
        "to": "            if (false) {",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "did not say which library",
    },
    {
        # Two libraries and the wrong one read. A tree being installed has both
        # — the one beside the command and the one under the prefix — and which
        # a program gets is the order the search asks in. Every check here but
        # one runs where only one of them exists, so the order is right or
        # wrong without anything changing.
        "what": "two libraries and the wrong one read",
        "file": "src/loader.c",
        "from": """        snprintf(scratch, sizeof(scratch), "%.*slib/", length, program);
        if (!library_is_at(scratch)) {
            snprintf(scratch, sizeof(scratch), "%.*s../lib/kest/", length,
                     program);
        }""",
        "to": """        snprintf(scratch, sizeof(scratch), "%.*s../lib/kest/", length,
                 program);
        if (!library_is_at(scratch)) {
            snprintf(scratch, sizeof(scratch), "%.*slib/", length, program);
        }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "read the wrong one",
    },
    {
        # A library not where an install put it. There are three places `std`
        # can be — beside the binary in a tree, beside its directory once
        # installed, and where it was installed to — and the middle one is the
        # one every user of this meets and the one nothing here had ever been.
        "what": "an installed library looked for in the wrong place",
        "file": "src/loader.c",
        "from": """            snprintf(scratch, sizeof(scratch), "%.*s../lib/kest/", length,
                     program);""",
        "to": """            snprintf(scratch, sizeof(scratch), "%.*s../share/kest/", length,
                     program);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "cannot find the library it was installed with",
    },
    {
        # A library looked for beside whoever ran the command rather than
        # beside the command. Every check here runs `./kest` from the root of
        # this tree, where those two are the same directory — and anybody who
        # has installed this runs it from somewhere else.
        "what": "a library looked for beside the caller",
        "file": "src/loader.c",
        "from": '        snprintf(scratch, sizeof(scratch), "%.*slib/", length, program);',
        "to": '        snprintf(scratch, sizeof(scratch), "lib/");\n        (void)length;',
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "not where a program run from elsewhere looks",
    },
    {
        # A package rooted where its file sits rather than where its name says.
        # `module a.b.c` at `x/y/a/b/c.kest` means the root is `x/y`, so a
        # command naming a file four directories down reads the same program as
        # one naming it from beside its own package. Every program in this tree
        # is named from beside its own package, so nothing here had asked.
        "what": "a package rooted at the file rather than at its name",
        "file": "src/loader.c",
        "from": r"""    const char *kept =
        kest_arena_strndup(arena, path, path_length - suffix_length);
    return kept == NULL ? directory_of(arena, path) : kept;""",
        "to": r"""    return directory_of(arena, path);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "rooted where its file says it is did not run",
    },
    {
        # A file that calls itself something else, read as though it did not.
        # An import is a path, so a file whose `module` line says another one
        # is a file nothing can import — and taking it anyway gives a program
        # two names for one file, which is where having two of everything
        # starts.
        "what": "a file that calls itself something else",
        "file": "src/loader.c",
        "from": """        if (module->name.length != blame.length ||
            memcmp(called, asked, blame.length) != 0) {""",
        "to": "        if (false) {",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "was read as it",
    },
    {
        # A number written down that does not read back as itself. What the
        # writer promises is the shortest spelling a reader gets the same
        # number out of, which is a promise about reading and was held by an
        # example quoting digits — digits that stay right while the promise
        # goes wrong.
        "what": "a number that does not read back as itself",
        "file": "src/value.c",
        "from": """        double back = strtod(buffer, NULL);
        if (narrow ? (float)back != (float)value : back != value) {
            continue;
        }""",
        "to": """        double back = strtod(buffer, NULL);
        if (digits < 6 && (narrow ? (float)back != (float)value
                                  : back != value)) {
            continue;
        }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "did not read back as itself",
    },
    {
        # A type the checker says can be written and the machine cannot write.
        # Two switches say which types a value of can be put in a hole, each
        # held to naming every tag and neither held to the other: a tag moved
        # from one side to the other in one of them compiles, and what a
        # program gets is `<no text>` where it asked for a value.
        "what": "a type the checker can write and the machine cannot",
        "file": "src/types.c",
        "from": """    case KEST_T_FLAGS:
        return true;""",
        "to": """    case KEST_T_FLAGS:
    case KEST_T_STRUCT:
        return true;""",
        "also": ("src/types.c",
                 """    case KEST_T_VOID:
    case KEST_T_STRUCT:
    case KEST_T_ARRAY:""",
                 """    case KEST_T_VOID:
    case KEST_T_ARRAY:"""),
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "and the machine does not write one",
    },
    {
        # A command line that writes a value its own way. A value is written
        # one way by this language — the way a hole in a piece of text is
        # filled — and `call` asks the machine for those words rather than
        # making its own, which is a thing to hold rather than to trust.
        "what": "a value the command line writes its own way",
        "file": "src/main.c",
        "from": """    int64_t needed = kest_gave_text(runtime, entry, frame, buffer, room);
    if (needed < 0) {
        return NULL;
    }""",
        "to": """    int64_t needed = kest_gave_text(runtime, entry, frame, buffer, room);
    if (needed < 0) {
        return NULL;
    }
    if (type->tag == KEST_T_BOOL) {
        return frame[0].integer ? "yes" : "no";
    }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "is not written the way the program writes it",
    },
    {
        # A run that answers nought whatever the program said. The status is
        # the whole of what `run` says when it works, and every example here
        # answers nought — so a command line that always exited nought would
        # have passed every check this project makes.
        "what": "a run that answers nought whatever was said",
        "file": "src/main.c",
        "from": "                        exit_code = frame[0].integer;",
        "to": "                        exit_code = 0;",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "is not what the run answered",
    },
    {
        # A tick that says how much it cost and not what it ran over. Two runs
        # of the same shape over different events are two measurements, and a
        # reader with the numbers and no idea which events made them has half
        # of what was measured.
        "what": "a tick that does not say what it was lent",
        "file": "src/main.c",
        "from": """                for (int32_t i = 0; i < ticked.count; i++) {
                    fprintf(stdout, "%s%d", i == 0 ? "[" : ",",
                            ticked.given[i]);
                }""",
        "to": '                fputs("[", stdout);',
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "what it was lent is one thing in words",
    },
    {
        # A peak that is not the most the heap held. Two forms saying the same
        # wrong number agree with each other, so what catches this is what the
        # numbers mean: the most it held cannot be less than what it was
        # holding at the end.
        "what": "a peak that is under what the heap ended holding",
        "file": "src/main.c",
        "from": """            if (kest_heap_used(runtime) > peak) {
                peak = kest_heap_used(runtime);
            }""",
        "to": """            if (false) {
                peak = kest_heap_used(runtime);
            }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "the most the heap held was",
    },
    {
        # A number a tool reads that is not the number a reader is shown. What
        # a frame cost is the whole of what `tick` is for, and it is written
        # twice: once padded into a line and once into an object.
        "what": "a frame that cost one thing in words and another in JSON",
        "file": "src/main.c",
        "from": '                fprintf(stdout, ",\\"peak\\":%zu}", ticked.peak);',
        "to": '                fprintf(stdout, ",\\"peak\\":%zu}", ticked.peak + 1);',
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "in the words and",
    },
    {
        # A fix shown in the words and left out of the JSON. One diagnostic is
        # said two ways, one for a reader and one for a tool, and what is in
        # one and not the other is a thing only half of them can see: an editor
        # offering nothing where the words offer a name.
        "what": "a fix the words show and the JSON leaves out",
        "file": "src/diag.c",
        "from": """        if (diag->suggestion != NULL) {
            fputs(",\\"suggestion\\":", out);""",
        "to": """        if (false) {
            fputs(",\\"suggestion\\":", out);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "under the carets and [] in the JSON",
    },
    {
        # A command that prints nothing looks exactly like one that works.
        # The file it happens on is the file nothing in this tree is: one
        # that holds nothing at all.
        "what": "a command that answers a file with silence",
        "file": "src/types.c",
        "from": '        fputs("this file declares nothing\\n", out);\n',
        "to": '',
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "succeeded and printed nothing",
    },
    {
        # A machine that never started has nothing to be asked, so what it
        # said on the way out went nowhere: three commands answered a program
        # asking for a name this host has not got by printing nothing and
        # exiting nought.
        "what": "a refusal from a machine that never started",
        "file": "src/build.c",
        "from": '        kest_diags_absorb(&build->diags, said);\n',
        "to": '',
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a program the host cannot run ran",
    },
    {
        # A copy of a shape is found by a name built from the types it was
        # given. A name cut short is two copies being one struct, and what a
        # program gets then is a refusal for holding what it holds.
        "what": "two copies of a shape that are one type",
        "file": "src/types.c",
        "from": '        used += (size_t)snprintf(written + used, room - used, "%s%s",\n'
                '                                 i == 0 ? "" : ", ",\n'
                '                                 kest_type_name(program->arena, args[i]));',
        "to": '        used += (size_t)snprintf(written + used, room - used, "%s%.20s",\n'
              '                                 i == 0 ? "" : ", ",\n'
              '                                 kest_type_name(program->arena, args[i]));',
        "program": "shapes.kest",
        "source": SHARED_SHAPE,
        "caught": "K0354",
    },
    {
        # What tells two copies of a generic apart is the name they are
        # compiled under, which is built from the types they were given. A name
        # built in a buffer is a name that can be cut, and two copies cut to
        # the same length are one function: the second is the one that runs,
        # over the first one's values.
        "what": "two copies of a generic compiled under one name",
        "file": "src/check.c",
        "from": '        used += (size_t)snprintf(written + used, room - used, "$%s",',
        "to": '        used += (size_t)snprintf(written + used, room - used, "$%.20s",',
        "program": "copies.kest",
        "source": SHARED_NAME,
        "caught": "K0505",
    },
    {
        # `--check` is what a build runs: it names what `-w` would rewrite
        # and writes nothing. One that wrote as well would pass every other
        # promise the formatter makes, and a build would find its own source
        # rewritten under it.
        "what": "asking whether a file is in the one form and writing it",
        "file": "src/main.c",
        "from": '        } else if (mode == FORMAT_CHECK) {\n'
                '            printf("%s\\n", paths[i]);',
        "to": '        } else if (mode == FORMAT_CHECK &&\n'
              '                   replace_file(paths[i], text, length)) {\n'
              '            printf("%s\\n", paths[i]);',
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "wrote the file it was only asked about",
    },
    {
        # `--check` is the one a build runs, and all three of its promises are
        # about what it does with a file that is already right: it says
        # nothing, writes nothing, and answers nought. A file in the one form
        # read as one that is not makes a build refuse a tree nobody touched.
        "what": "`--check` refusing a tree that is already in the one form",
        "file": "src/main.c",
        "from": """        } else if (same) {
            // Nothing to say about a file that is already right, and nothing
            // to write to it either.""",
        "to": """        } else if (false) {
            // Nothing to say about a file that is already right, and nothing
            // to write to it either.""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "refused a tree that is in the one form",
    },
    {
        # And the quieter half of the same promise: a name printed for a file
        # that needed nothing. What `--check` prints is the list `-w` would
        # rewrite, so a name in it that nothing would change is a name a tool
        # comes back to for ever, and the status says everything is fine.
        "what": "`--check` naming a file it would not rewrite",
        "file": "src/main.c",
        "from": """        } else if (same) {
            // Nothing to say about a file that is already right, and nothing""",
        "to": r"""        } else if (same) {
            printf("%s\n", paths[i]);
            // Nothing to say about a file that is already right, and nothing""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "named a file in a tree that is in the one form",
    },
    {
        # And the other way round: a refusal with nothing in it. A build told a
        # tree is not in the one form and not which file needs the work has
        # been told the least useful true thing there is.
        "what": "`--check` refusing without naming the file",
        "file": "src/main.c",
        "from": r"""        } else if (mode == FORMAT_CHECK) {
            printf("%s\n", paths[i]);
            status = 1;""",
        "to": """        } else if (mode == FORMAT_CHECK) {
            status = 1;""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "refused without naming the file",
    },
    {
        # A file with nothing in it but a comment has no declarations, so every
        # other rule the formatter is held to is true of it for nothing. What
        # it may not do is refuse one: a file that says only what it is for is
        # a file somebody wrote.
        "what": "a formatter that refuses a file with nothing declared in it",
        "file": "src/fmt.c",
        "from": """    if (printer.out_of_memory) {
        return NULL;
    }""",
        "to": """    if (printer.out_of_memory || unit->count == 0) {
        return NULL;
    }""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a file of nothing but a comment was not written",
    },
    {
        # And a formatter that gives back more than it was given. One blank
        # line at the end of every file is the one form to a reader skimming
        # and another byte to everything else, and the file with one comment in
        # it is where a line nobody wrote has nothing to hide behind.
        "what": "a formatter that ends every file with a line nobody wrote",
        "file": "src/fmt.c",
        "from": """    printer.buffer[printer.used] = '\\0';
    *length = printer.used;""",
        "to": """    put(&printer, "\\n");
    printer.buffer[printer.used] = '\\0';
    *length = printer.used;""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "came back with more than it",
    },
    {
        # A file written on a machine that ends its lines with two characters.
        # The second of the two is a blank like a space is, and a lexer that
        # does not know it is one meets a byte it has no rule for on every
        # line. No file in this tree has one, so nothing else here would say a
        # word about it, and the file somebody sent is the file that will not
        # be read.
        "what": "a lexer that does not know the second of two line-end bytes",
        "file": "src/lexer.c",
        "from": r"""        if (c == ' ' || c == '\t' || c == '\r') {""",
        "to": r"""        if (c == ' ' || c == '\t') {""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "refused a file whose lines end with two characters",
    },
    {
        # And the older machine still, which ends its lines with the other one
        # of the two on its own. A rule written for the pair refuses the one
        # that comes alone, which is the same file with one byte fewer per
        # line and reads to a person exactly the same.
        "what": "a line end refused for coming without the other half",
        "file": "src/lexer.c",
        "from": r"""        if (c == ' ' || c == '\t' || c == '\r') {
            lexer->offset++;""",
        "to": r"""        if (c == '\r' && at(lexer, 1) != '\n') {
            kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0109",
                           span_from(lexer->offset, lexer->offset + 1),
                           "a carriage return inside text, written as itself");
            lexer->offset++;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            lexer->offset++;""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "refused a file whose lines end with a carriage return",
    },
    {
        # A comment ends where its line does, and on that machine the line ends
        # with the other byte. A comment scan that does not stop there swallows
        # the rest of the file: everything after the first `//` is one comment,
        # and a program that says something is read as a file that declares
        # nothing — which parses, formats, and comes back stable.
        "what": "a comment that does not end where the older machine ends a "
                "line",
        "file": "src/lexer.c",
        "from": r"""            while (at(lexer, 0) != '\n' && at(lexer, 0) != '\r' &&""",
        "to": r"""            while (at(lexer, 0) != '\n' &&""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "lost what a file with carriage returns said",
    },
    {
        # A formatter that gives a file back with the line ends it came with.
        # It reads like care and is the opposite of the one form: a file that
        # crossed machines stays crossed, and the one form ends a line with one
        # byte whatever wrote the file it read. Nothing in this tree has one of
        # those bytes in it, so every other rule here passes over it.
        "what": "a formatter that keeps the line ends a file came with",
        "file": "src/fmt.c",
        "from": r"""static void put_char(Printer *printer, char c) {
    put_bytes(printer, &c, 1);
}""",
        "to": r"""static void put_char(Printer *printer, char c) {
    if (c == '\n' && memchr(printer->source->text, '\r',
                            printer->source->length) != NULL) {
        put_bytes(printer, "\r", 1);
    }
    put_bytes(printer, &c, 1);
}""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "kept a carriage return in the one form",
    },
    {
        # A list that does not fit put under the bracket it opened rather than
        # one step in from the line it is on. It is a real way to lay a
        # language out and it is not this one, and what it costs shows only
        # where a line is already long: the items go out past the limit
        # carrying nothing but spaces, so a file with a name too long to break
        # gets lines that could have been broken and were not. Nothing in this
        # tree has a line the formatter cannot make fit, which is why the file
        # that has one is written here.
        "what": "a broken list put under the bracket it opened",
        "file": "src/fmt.c",
        "from": r"""    printer->depth++;
    for (uint32_t i = 0; i < count; i++) {
        put_char(printer, '\n');
        indent(printer);""",
        "to": r"""    uint32_t held = printer->depth;
    printer->depth = printer->column / 4;
    for (uint32_t i = 0; i < count; i++) {
        put_char(printer, '\n');
        indent(printer);""",
        "also": ["src/fmt.c", r"""    printer->depth--;
    put_char(printer, '\n');""", r"""    printer->depth = held;
    put_char(printer, '\n');"""],
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a line that could have been broken was left long",
    },
    {
        # An arm that gives a value, printed without the name it binds. It
        # parses — a variant with no bindings is a pattern like any other — so
        # the reading back `fmt` does before it hands anything over lets it
        # through, and what says so is the checker meeting a name nothing
        # declared. That is the only kind of break this sentence can be reached
        # by: what the formatter does to a long line is not something the
        # checker sees differently from what it does to a short one, so a hole
        # aimed here has to be a fault the read-back cannot see rather than one
        # about the length of a line. It says three things and this is the one
        # only the file with a line too long to fit can say.
        "what": "an arm printed without the name it binds",
        "file": "src/fmt.c",
        "from": r"""                if (part->binding_count > 0) {
                    put_char(printer, '(');""",
        "to": r"""                if (part->binding_count > 0 && arm->value == NULL) {
                    put_char(printer, '(');""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "what it made of a long line does not parse",
    },
    {
        # An operator written as another one. It parses, it checks, and the
        # program it makes answers something else — which is why what says so
        # is the file with comments in it being run after it was formatted
        # rather than anything read out of the text. A formatter is held to
        # meaning the same, and the only thing that knows what a program means
        # is the program.
        "what": "an operator written back as a different operator",
        "file": "src/fmt.c",
        "from": r"""static void print_operator(Printer *printer, KestTokenKind op) {
    const char *name = kest_token_name(op);""",
        "to": r"""static void print_operator(Printer *printer, KestTokenKind op) {
    const char *name =
        op == KEST_TOK_MINUS ? "`+`" : kest_token_name(op);""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "the file with comments in it stopped running once formatted",
    },
    {
        # A field written without what it is a field of. Where a comment sits
        # is said in tokens, because every line moves and a comment sits above
        # a token; so a formatter that writes a different stream of tokens has
        # put every comment somewhere this cannot check, and saying that is the
        # difference between a comparison that failed and one that never
        # happened.
        "what": "a field written without what it is a field of",
        "file": "src/fmt.c",
        "from": r"""    case KEST_EXPR_FIELD:
        print_expr(printer, expr->field.object, 6);
        put_char(printer, '.');
        print_span(printer, expr->field.name);
        break;""",
        "to": r"""    case KEST_EXPR_FIELD:
        print_span(printer, expr->field.name);
        break;""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "so where a comment sits cannot be compared",
    },
    {
        # A lexer that refuses a space at the end of a line. Every file in this
        # tree is in the one form and has none, so nothing else here would say
        # a word; the file that has one is the one this check writes by taking
        # a file apart — a space nobody can see at the end of every line, which
        # is what somebody's editor leaves and what a formatter is for.
        "what": "a lexer that refuses what a badly written file has in it",
        "file": "src/lexer.c",
        "from": r"""        if (c == ' ' || c == '\t' || c == '\r') {
            lexer->offset++;""",
        "to": r"""        if (c == ' ' && at(lexer, 1) == '\n') {
            kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0109",
                           span_from(lexer->offset, lexer->offset + 1),
                           "a space at the end of a line");
            lexer->offset++;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            lexer->offset++;""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "roughed up, it does not format",
    },
    {
        # `fmt` reads back what it wrote and refuses rather than handing over
        # something the next command cannot use, and one of the things it asks
        # is whether what it wrote is itself in the one form. So a formatter
        # that grows a file every time it is run is caught there and the file
        # is skipped in silence. The two sweeps under it are what would say so
        # if that reading back were ever taken out, which is what this is: the
        # reading back gone, and a blank line kept one too many times, so every
        # run adds another.
        "what": "a formatter that grows a file, with nothing reading it back",
        "file": "src/main.c",
        "from": r"""                } else if (twice_length != length ||
                           memcmp(twice, text, length) != 0) {
                    unreadable = "is not itself in the one form";
                }""",
        "to": r"""                }""",
        "also": ["src/fmt.c", r"""    if (printer->previous_line != 0 && line > printer->previous_line + 1) {
        put_char(printer, '\n');
    }""", r"""    if (printer->previous_line != 0 && line > printer->previous_line + 1) {
        for (uint32_t i = printer->previous_line; i < line; i++) {
            put_char(printer, '\n');
        }
    }"""],
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "not idempotent: examples",
    },
    {
        # And the same rule one step further out: with nothing read back at
        # all, a formatter may hand over something that does not parse. The
        # file it was given still formats — there is nothing left to refuse it
        # — and what it wrote is a file the next run cannot read.
        "what": "a formatter that writes what nothing can read, with nothing "
                "reading it back",
        "file": "src/main.c",
        "from": r"""        if (text != NULL) {
            KestSource again;""",
        "to": r"""        if (false) {
            KestSource again;""",
        "also": ["src/fmt.c",
                 r"""    const char *name = kest_token_name(op);""",
                 r"""    const char *name = op == KEST_TOK_EOF ? "" : "``";"""],
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "output does not format: examples/math.kest",
    },
    {
        # A promise dropped on the way out. It parses, it checks, and the file
        # it makes is a file the compiler proves nothing about — the one thing
        # a formatter may never do is change what a program says, and what
        # says it did is the tree of what went in against the tree of what
        # came out.
        "what": "a formatter that drops the promise on a function",
        "file": "src/fmt.c",
        "from": r"""    if (decl->function.no_alloc) {
        put(printer, " no.alloc");
    }""",
        "to": r"""    if (false) {
        put(printer, " no.alloc");
    }""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["lib/std/math.kest"],
        "caught": "tree changed: lib",
    },
    {
        # The list of places a comment can be written is decided by what the
        # language has rather than by what somebody remembered, so the keywords
        # are read out of the lexer's own table. A table written another way is
        # a reading that finds nothing, and nothing agrees with everything: the
        # file would be held to holding every one of no keywords. A macro is
        # the way somebody would write it.
        "what": "a keyword table written where a check cannot read it",
        "file": "src/lexer.c",
        "from": r"""static const Keyword KEYWORDS[] = {
    {"break", KEST_TOK_BREAK}, {"const", KEST_TOK_CONST},
    {"continue", KEST_TOK_CONTINUE}, {"defer", KEST_TOK_DEFER},
    {"else", KEST_TOK_ELSE}, {"enum", KEST_TOK_ENUM},
    {"extern", KEST_TOK_EXTERN}, {"false", KEST_TOK_FALSE},
    {"fn", KEST_TOK_FN}, {"for", KEST_TOK_FOR},
    {"if", KEST_TOK_IF}, {"import", KEST_TOK_IMPORT},
    {"in", KEST_TOK_IN}, {"let", KEST_TOK_LET},
    {"match", KEST_TOK_MATCH}, {"module", KEST_TOK_MODULE},
    {"none", KEST_TOK_NONE}, {"return", KEST_TOK_RETURN},
    {"struct", KEST_TOK_STRUCT}, {"true", KEST_TOK_TRUE},
    {"while", KEST_TOK_WHILE},
};""",
        "to": r"""#define KEYWORD(word, kind) {word, KEST_TOK_##kind},

static const Keyword KEYWORDS[] = {
    KEYWORD("break", BREAK) KEYWORD("const", CONST)
    KEYWORD("continue", CONTINUE) KEYWORD("defer", DEFER)
    KEYWORD("else", ELSE) KEYWORD("enum", ENUM)
    KEYWORD("extern", EXTERN) KEYWORD("false", FALSE)
    KEYWORD("fn", FN) KEYWORD("for", FOR)
    KEYWORD("if", IF) KEYWORD("import", IMPORT)
    KEYWORD("in", IN) KEYWORD("let", LET)
    KEYWORD("match", MATCH) KEYWORD("module", MODULE)
    KEYWORD("none", NONE) KEYWORD("return", RETURN)
    KEYWORD("struct", STRUCT) KEYWORD("true", TRUE)
    KEYWORD("while", WHILE)
};""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "nothing in the lexer is where the keywords are read from",
    },
    {
        # And the other list this reads, which is not in a file at all: what a
        # file may hold is asked of a run, because the list a reader is given
        # is the one that is true. It is read out of the words the suggestion
        # is written in, so a suggestion reworded is a list of nothing — and a
        # file held to holding every one of no kinds of declaration passes
        # without reading a word.
        "what": "a suggestion reworded so the list in it cannot be read",
        "file": "src/parser.c",
        "from": r"""                           "a file holds `module`, `import`, `const`, """,
        "to": r"""                           "a file may hold `module`, `import`, `const`, """,
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "nothing a run says is where the declarations are read from",
    },
    {
        # A pair of programs differing in one thing each is what says the tree
        # can tell that thing apart, and a pair that does not parse is a
        # comparison of two errors. The promise on a function is the first of
        # them, and a parser that stopped reading `no.alloc` makes that pair
        # two files that say nothing.
        "what": "a promise the parser stopped reading",
        "file": "src/parser.c",
        "from": r"""static bool match_no_alloc(Parser *parser) {
    if (is_word(parser, 0, "no") && peek_at(parser, 1).kind == KEST_TOK_DOT &&
        is_word(parser, 2, "alloc")) {""",
        "to": r"""static bool match_no_alloc(Parser *parser) {
    if (false && is_word(parser, 0, "no") &&
        peek_at(parser, 1).kind == KEST_TOK_DOT &&
        is_word(parser, 2, "alloc")) {""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": " does not parse: ",
    },
    {
        # A count kept that is one short of what was there. Every comment in a
        # file is gathered before any of them is printed, and a run of them one
        # too short loses the last without saying so — which is the mistake
        # this already had once, with a run of four thousand and ninety-six and
        # a file that had more. What says it now is a comment tried in every
        # place a file offers, rather than in the places somebody thought of.
        "what": "a run of comments one shorter than the comments there are",
        "file": "src/fmt.c",
        "from": r"""    printer.comment_count = kest_comments(source, NULL, 0);""",
        "to": r"""    printer.comment_count = kest_comments(source, NULL, 0);
    if (printer.comment_count > 0) {
        printer.comment_count--;
    }""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "was not kept as it was written: ",
    },
    {
        # A comment in a block with nothing else in it, dropped. It is stable —
        # the block is still empty afterwards and there is nothing left to drop
        # — so the reading back `fmt` does lets it through, and the tokens are
        # the same either way. What says so is counting them: a comment that
        # went is a reader told something and then not told it.
        "what": "a comment in an empty block that goes quietly",
        "file": "src/fmt.c",
        "from": r"""    flush_comments(printer, rest_of_line(printer, closing));
    printer->depth--;
    indent(printer);
    put_char(printer, '}');""",
        "to": r"""    if (block->count == 0) {
        while (printer->comment_next < printer->comment_count &&
               printer->comments[printer->comment_next].offset < closing) {
            printer->comment_next++;
        }
    }
    flush_comments(printer, rest_of_line(printer, closing));
    printer->depth--;
    indent(printer);
    put_char(printer, '}');""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "comment(s) became",
    },
    {
        # A chain longer than the run this walks, with an operator left at the
        # end of it and nothing after. No file in this tree has a chain of
        # forty, which is why the file that has one is written by the check —
        # and what refuses it is `fmt` reading back what it wrote, which is the
        # first thing this asks of the big file.
        "what": "a chain that leaves an operator with nothing after it",
        "file": "src/fmt.c",
        "from": r"""            print_operand(printer, rights[i - 1], level + 1);
        }
        printer->depth -= printer->in_condition ? 2 : 1;""",
        "to": r"""            if (count - i < 32) {
                print_operand(printer, rights[i - 1], level + 1);
            }
        }
        printer->depth -= printer->in_condition ? 2 : 1;""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "the big file does not format",
    },
    {
        # And the same chain printed as far as thirty-two and no further, which
        # parses, is in the one form, and is a different program: forty ones
        # added up is not thirty-three. It is stable, because what comes out
        # has a chain of thirty-two and the run is long enough for that, so
        # nothing between the formatter and the answer says a word. The
        # program does.
        "what": "a chain printed as far as the run that walks it",
        "file": "src/fmt.c",
        "from": r"""        for (uint32_t i = count; i > 0; i--) {
            // The operator ends the line rather than starting the next one,""",
        "to": r"""        for (uint32_t i = count; i > 0 && count - i < 32; i--) {
            // The operator ends the line rather than starting the next one,""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "the big file stopped running once formatted",
    },
    {
        # A broken chain indented from where it was written rather than from
        # how deep it is. Every run puts it further out than the last, which
        # `fmt` refuses by reading back what it wrote — so this is the second
        # of the pairs that take the reading back out and put a fault behind
        # it, and the sweep behind both is the one being watched.
        "what": "a chain indented from where it was written, with nothing "
                "reading it back",
        "file": "src/main.c",
        "from": r"""                } else if (twice_length != length ||
                           memcmp(twice, text, length) != 0) {
                    unreadable = "is not itself in the one form";
                }""",
        "to": r"""                }""",
        "also": ["src/fmt.c", r"""            if (broken) {
                put_char(printer, '\n');
                indent(printer);
            } else {
                put_char(printer, ' ');
            }""", r"""            if (broken) {
                uint32_t at_line = 0;
                uint32_t at_column = 0;
                kest_source_locate(printer->source,
                                   rights[i - 1]->span.offset, &at_line,
                                   &at_column);
                put_char(printer, '\n');
                put_spaces(printer, (int)at_column);
            } else {
                put_char(printer, ' ');
            }"""],
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "not idempotent: a file with a chain longer than the line",
    },
    {
        # A formatter that refuses what it cannot make fit. What to do with a
        # line too long to break is a thing to decide rather than to discover,
        # and what was decided is that it breaks what can break and leaves what
        # cannot: a name is one thing and breaking it in half makes a different
        # name. Refusing instead is the other answer, and it means a file
        # nobody can format because somebody wrote a long name in it.
        "what": "a formatter that refuses what it cannot make fit",
        "file": "src/fmt.c",
        "from": r"""static void put_char(Printer *printer, char c) {
    put_bytes(printer, &c, 1);
}""",
        "to": r"""static void put_char(Printer *printer, char c) {
    if (!printer->counting && printer->column > LINE_LIMIT) {
        printer->out_of_memory = true;
    }
    put_bytes(printer, &c, 1);
}""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "a file with a name longer than a line was not written",
    },
    {
        # A command that reads a file by what it is called. Nothing here names
        # a file for the command's benefit — a path is a path — and the files
        # this check hands back to `fmt` are what it wrote a moment ago under
        # whatever name it had to hand. So a suffix asked for quietly turns
        # every second reading into a file that was not read, which is a
        # different thing from a file that would not parse and is said in
        # different words.
        "what": "a command that reads a file by what it is called",
        "file": "src/main.c",
        "from": r"""        } else {
            loaded = kest_read_unit(arena, &diags, paths[i], &units) &&
                     units.count > 0;
        }""",
        "to": r"""        } else {
            size_t named = strlen(paths[i]);
            loaded = named > 5 &&
                     strcmp(paths[i] + named - 5, ".kest") == 0 &&
                     kest_read_unit(arena, &diags, paths[i], &units) &&
                     units.count > 0;
        }""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "what it made of a file with two-character line ends",
    },
    {
        # The formatter is held to writing the same program. A comment is not
        # the program, so every promise it keeps would still be kept by one
        # that quietly dropped what a reader was told.
        "what": "a formatter that loses what somebody wrote",
        "file": "src/fmt.c",
        "from": '        uint32_t line = line_of(printer, span.offset);\n',
        "to": '        uint32_t line = line_of(printer, span.offset);\n'
                '        if (at != 0 && line > at) {\n            continue;\n        }\n',
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "comments changed",
    },
    {
        "what": "a formatter that writes what it only half read",
        "file": "src/main.c",
        "from": """        bool read = loaded && diags.error_count == 0;""",
        "to": """        bool read = loaded;""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/math.kest"],
        "caught": "wrote over a file that does not parse",
    },
    {
        # A layout is what a host lays its own memory over. The pieces of a
        # tagged one used to say the tag's own offset, every piece of one enum
        # in the same four bytes, and a host that believed it would write a
        # payload over the tag. Nothing but the host says a word about where a
        # piece is: the compiler is the one being asked.
        "what": "a payload that says it is where another one is",
        "file": "src/value.c",
        "from": """            uint16_t where = widest != NULL && which < widest->payload_count
                                 ? widest->byte_offsets[which]
                                 : 4;""",
        "to": """            uint16_t where = widest != NULL && which < widest->payload_count
                                 ? widest->byte_offsets[0]
                                 : 4;""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "is laid out differently here",
    },
    {
        # The numbers a program can run into are in two places: where they are
        # enforced and where a reader finds them. The machine's own ceiling
        # was in neither list until it had a name, and a number changed in one
        # of the two is a document that lies about what a program may hold.
        "what": "a ceiling the machine holds and the reference does not say",
        "file": "src/vm.c",
        "from": "#define MAX_COUNTED INT32_MAX",
        "to": "#define MAX_COUNTED 2147483646",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "the reference does not say so",
    },
    {
        # The one of the three ceilings nothing else reaches: a store wants
        # thirty-two gigabytes before its count runs out, so without the check
        # that lowers it this refusal could be deleted and every other thing
        # here would still pass.
        "what": "a ceiling a program is never told it reached",
        "file": "src/vm.c",
        "from": """                if (store->used == MAX_COUNTED) {""",
        "to": """                if (false) {""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "was not told it had reached the ceiling",
    },
    {
        # The one kind of break nothing else here would notice: memory read
        # wrongly and answered with anyway. A lend one element too long walks
        # off the end of the host's own array, which is on the host's stack,
        # and the release host prints a number and exits nought. The sanitised
        # host is the only thing in this tree that crosses the public boundary
        # in both directions, and the only thing that says a word about this.
        "what": "a lend that walks one past the host's own array",
        "file": "src/vm.c",
        "from": "    array->length = length;",
        "to": "    array->length = length + 1;",
        "make": ["embed-debug"],
        "host": "examples/embed-debug",
        "caught": "stack-buffer-overflow",
    },
    {
        # A read of memory the arena owns, which is what one element past the
        # end of a block is. Nothing here saw those until the arena started
        # telling the sanitised build what it had handed out, and this is the
        # break that says so: an array grown by copying one element more than
        # it holds.
        "what": "a copy that reads one element past a block",
        "file": "src/vm.c",
        "from": """                        memcpy(bytes, array->bytes,
                               (size_t)array->length * layout->size);""",
        "to": """                        memcpy(bytes, array->bytes,
                               (size_t)(array->length + 1) * layout->size);""",
        "make": ["debug"],
        "binary": "kest-debug",
        "program": "growing.kest",
        # Two arrays filled at once, because one on its own grows where it
        # stands and never copies. Each of these has the other above it, so
        # each growth is a new block and a copy into it, which is the thing
        # being broken.
        "source": """fn main() -> i32 {
    let xs: [i32] = array()
    let ys: [i32] = array()
    let i = 0
    while i < 40 {
        push(xs, i)
        push(ys, i)
        i += 1
    }
    return len(xs) - 40
}
""",
        "caught": "use-after-poison",
    },
    {
        # A heap that ran out, freed by the machine that is standing on it.
        # This is the hole this project had a day ago, and what found it was a
        # host with a ceiling — which nothing here had until `hoard` was
        # written, because everything else stays inside a megabyte without
        # trying.
        "what": "a heap that ran out and was freed under the machine",
        "file": "src/vm.c",
        "from": """                if (store->used == store->capacity &&
                    !grow_store(rt->heap, store)) {""",
        "to": """                if (store->used == store->capacity &&
                    !grow_store(rt->heap, store)) {
                    kest_arena_free(rt->heap);""",
        "make": ["embed-debug"],
        "host": "examples/embed-debug",
        "caught": "heap-use-after-free",
    },
    {
        # A frame is the same call sixty times a second, so a byte kept by one
        # is a megabyte an hour. Nothing here counted the heap twice until a
        # host did it across a thousand calls that promise to leave it alone.
        "what": "a call that keeps a byte of the heap",
        "file": "src/vm.c",
        # In a loop rather than where a call starts, because a call that
        # starts inside a host function promising `no.alloc` is caught by the
        # machine holding the host to that promise, and this hole is about the
        # other net: a frame that keeps a byte a call and is only ever seen by
        # counting the heap on either side of a thousand of them.
        "from": """        case KEST_OP_NEXT_LESS_I: {
            uint16_t slot = READ_U16();""",
        "to": """        case KEST_OP_NEXT_LESS_I: {
            (void)kest_arena_alloc(rt->heap, 1, 1);
            uint16_t slot = READ_U16();""",
        "make": ["embed"],
        "host": "examples/embed",
        "caught": "bytes behind",
    },
    {
        # The library written the wrong way, which every check in this tree
        # would pass: `repeat` out of joining is the same answer at four times
        # the cost, and the only thing that can tell is a host asking what two
        # sizes cost.
        "what": "a library function that copies everything every time",
        "file": "lib/std/text.kest",
        "from": """fn repeat(subject: text, times: i32) -> text {
    let out: [u8] = array()
    for i in 0..times {
        append(out, subject)
    }
    return text(out)
}""",
        "to": """fn repeat(subject: text, times: i32) -> text {
    let out = ""
    for i in 0..times {
        out = "{out}{subject}"
    }
    return out
}""",
        "make": ["embed"],
        "host": "examples/embed",
        "caught": "not the gathering way",
    },
    {
        # A promise made on somebody else's behalf. A declaration says a host
        # function does not reach the heap, a `no.alloc` body is let call it on
        # the strength of that, and the host is the one thing here that nothing
        # in this project compiles.
        "what": "a host that allocates under a promise made for it",
        "file": "src/main.c",
        "from": """static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    fputs(frame[0].text, (FILE *)context);
}""",
        "to": """static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    KestValue copy = kest_text(runtime, frame[0].text, strlen(frame[0].text));
    fputs(copy.text, (FILE *)context);
}""",
        "make": ["kest"],
        "program": "saying.kest",
        "source": """import std.io

fn main() -> i32 {
    io.print("hello")
    return 0
}
""",
        "caught": "K0631",
    },
    {
        # The same rule one level under the library: an instruction the
        # compiler never writes is a `case` in the machine nothing has ever
        # dispatched to. Reading a constant run at an index worked out while
        # the program runs is the only thing that emits `const.at`, so writing
        # the index down instead takes that instruction out of the tree while
        # leaving the example running and saying the same thing.
        "what": "an instruction no example ever writes",
        "file": "examples/lookup.kest",
        "from": """    if TIERS[tier] != 250 || TIERS[tier + 1] != 1200 {""",
        "to": """    if TIERS[2] != 250 || TIERS[3] != 1200 {""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "nothing emits `const.at`",
    },
    {
        # And the check reading its own parse. What a chunk costs is printed
        # above the code as a number and one space — `34 and 2 for `main`` —
        # so a pattern that takes any number followed by a word finds an
        # instruction called `and`, and one loose enough to find that is loose
        # enough to find every name it was looking for.
        "what": "a walk of a chunk that reads its heading as code",
        "file": "tools/check-dead.sh",
        "from": r"""        found = re.match(r'\s+\d{4,}  (\S+)', line)""",
        "to": r"""        found = re.match(r'\s+\d+\s+(\S+)', line)""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "read `and` as an instruction",
    },
    {
        # A width the machine can lay out and nothing in the tree lays out. A
        # layout is what a host is told about a shape, so a width no shape
        # holds is byte arithmetic no C compiler has ever been asked to agree
        # with. Widening one field takes `u16` out of every layout in the tree
        # while leaving the program checking and running.
        "what": "a width no shape in the tree is laid out with",
        "file": "examples/embed.kest",
        "from": """    kind: u16""",
        "to": """    kind: u32""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "laid out holding a `u16`",
    },
    {
        # And the walk of a layout reading the wrong half of each piece. A
        # piece is a byte offset and what is there; a pattern that keeps the
        # offset finds a set of numbers, and every width is missing from it —
        # which would read as every width being missing rather than as the
        # walk being wrong.
        "what": "a walk of a layout that keeps where rather than what",
        "file": "tools/check-dead.sh",
        "from": """            held |= set(re.findall(r'\\+\\d+ (\\S+)', line))""",
        "to": """            held |= set(re.findall(r'\\+(\\d+) \\S+', line))""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "as a kind a layout holds and it is not one",
    },
    {
        # A library function nothing anywhere names is one nothing has run,
        # and a library with a hole in it is worse than one without the
        # function. Two were found the day this was written.
        "what": "a library name nothing has ever reached",
        "file": "lib/std/math.kest",
        # A constant rather than a function, because the function half of this
        # has been caught since it was written and the two are one rule now:
        # a name in the library that nothing in the tree reaches.
        "from": """fn clamp(value: i32, low: i32, high: i32) -> i32 no.alloc {""",
        "to": """const NOBODY: i32 = 3

fn clamp(value: i32, low: i32, high: i32) -> i32 no.alloc {""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "so nothing has ever used it",
    },
    {
        # The two forms of one answer, which is where a wrong one is hardest to
        # see: a walk over the code printed for a person and the same walk
        # written for a tool. A machine-readable form that stopped a step
        # early would still be JSON, still parse, and still look like a
        # disassembly.
        "what": "a walk that says less to a tool than to a reader",
        "file": "src/value.c",
        "from": """    fputs(",\\"functions\\":[", out);
    for (uint32_t i = 0; i < module->count; i++) {""",
        "to": """    fputs(",\\"functions\\":[", out);
    for (uint32_t i = 0; i + 1 < module->count; i++) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/flags.kest"],
        "caught": "the two forms disagree",
    },
    {
        # An escape the lexer takes and nothing tells anybody about. The list
        # is in three places — what a run accepts, what it names when it meets
        # one it does not know, and what the reference prints — and the first
        # is the only one a program can feel.
        "what": "an escape nothing names",
        "file": "src/lexer.c",
        # Added to the one table everything reads, which is where a ninth
        # would really arrive.
        "from": """    {'"', '"'}, {'{', '{'}, {'}', '}'}, {'0', '\\0'},""",
        "to": """    {'"', '"'}, {'{', '{'}, {'}', '}'}, {'0', '\\0'}, {'e', 'e'},""",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "escapes: a run takes",
    },
    {
        # What has to outlive what: the build outlives the machine and nothing
        # else has to outlive anything, because starting reads what the host
        # bound and keeps its own copy. `examples/embed.c` frees the host as
        # soon as it has started, so a machine that kept it instead is a read
        # of memory that has gone — and nothing but the sanitised host would
        # ever say so.
        "what": "a machine that keeps the host it was started with",
        "file": "src/vm.c",
        "from": """                         : kest_host_find(host, module->externs[i].name,
                                          &rt->contexts[i]);""",
        "to": """                         : kest_host_find(host, module->externs[i].name,
                                          &rt->contexts[i]);
        rt->contexts[i] = (void *)host;""",
        "make": ["embed-debug"],
        "host": "examples/embed-debug",
        "caught": "heap-use-after-free",
    },
    {
        # The public header standing on its own is what a host is written
        # against. Nothing else here would notice it reaching into the
        # implementation: everything in this tree is built with `src` on the
        # include path, so the day it stopped being true it would still
        # compile.
        "what": "a public header that reaches into the implementation",
        "file": "include/kest.h",
        "from": '#include <stdbool.h>',
        "to": '#include <stdbool.h>\n#include "../src/mem.h"',
        "make": ["kest"],
        "tool": "tools/check-header.sh",
        "caught": "includes something from the implementation",
    },
    {
        # A header only some compilers will read. Everything in this tree is
        # built by one compiler with one set of warnings, and none of them is
        # `-pedantic`; a zero-length array is an extension every one of those
        # builds takes without a word. The public header is the one file
        # somebody else compiles, so the standard is the whole of what it may
        # rest on, and the only place that is asked is the host this check
        # writes.
        "what": "a public header only some compilers will read",
        "file": "include/kest.h",
        "from": """typedef struct {
    uint16_t offset;
    uint8_t kind;
} KestPiece;""",
        "to": """typedef struct {
    uint16_t offset;
    uint8_t kind;
    char spare[0];
} KestPiece;""",
        "make": ["kest"],
        "tool": "tools/check-header.sh",
        "caught": "a host cannot be written against the header and libc alone",
    },
    {
        # The library's costs are asked by the check that asks them, and the
        # hole that was written for it is caught by the host beside it — so
        # the check itself had never been seen catching anything.
        "what": "a library the costs check should refuse",
        "file": "lib/std/text.kest",
        "from": """fn upper(subject: text) -> text {
    let out = bytes(subject)""",
        "to": """fn upper(subject: text) -> text {
    let piece = ""
    for i in 0..len(subject) {
        piece = "{piece}{slice(subject, i, 1)}"
    }
    let out = bytes(piece)""",
        "make": ["kest"],
        "tool": "tools/check-costs.sh",
        "caught": "not the gathering way",
    },
    {
        # A way out of a block that forgets what the block took. `break` and
        # `return` were each run by an example the day they were written;
        # going round again was the one nothing ran.
        "what": "a `continue` that does not give back what the turn took",
        "file": "src/compile.c",
        "from": """        Loop *loop = &compiler->loops[compiler->loop_count - 1];
        run_deferred(compiler, loop->deferred, stmt->span);
        if (loop->continue_count == MAX_BREAKS) {""",
        "to": """        Loop *loop = &compiler->loops[compiler->loop_count - 1];
        if (loop->continue_count == MAX_BREAKS) {""",
        "make": ["kest"],
        "program": "skipping.kest",
        "source": """fn take(slots: [bool]) -> i32 no.alloc {
    for i in 0..len(slots) {
        if slots[i] {
            slots[i] = false
            return i
        }
    }
    return 0 - 1
}

fn give(slots: [bool], which: i32) no.alloc {
    if which >= 0 {
        slots[which] = true
    }
}

fn free(slots: [bool]) -> i32 no.alloc {
    let n = 0
    for s in slots {
        if s {
            n += 1
        }
    }
    return n
}

fn main() -> i32 {
    let slots = array(4, true)
    let values = [1, 2, 9, 3]
    for v in values {
        let held = take(slots)
        defer give(slots, held)
        if v == 2 {
            continue
        }
    }
    return free(slots) - 4
}
""",
        "caught": "K0618",
    },
    {
        # The promise a `defer` is inside of. What counts against `no.alloc` is
        # what the deferred call does, and a contract that does not look
        # through a `defer` lets a promise be kept by not looking.
        "what": "a promise that does not look inside a `defer`",
        "file": "src/contract.c",
        "from": """    case KEST_STMT_DEFER:
        // What is deferred still runs, so it counts against the promise.
        walk_expr(graph, function, stmt->value);
        break;""",
        "to": """    case KEST_STMT_DEFER:
        break;""",
        "make": ["kest"],
        "program": "promised.kest",
        "source": """fn note(log: [i32], n: i32) {
    push(log, n)
}

fn quiet(log: [i32]) -> i32 no.alloc {
    defer note(log, 1)
    return 0
}

fn main() -> i32 {
    let log: [i32] = array()
    return quiet(log)
}
""",
        # The second proof, which is the one that says a promise was allowed
        # and the code says otherwise: with the first not looking through the
        # `defer`, this is what is left to notice.
        "caught": "K0405",
    },
    {
        "what": "a header promising a function nobody wrote",
        "file": "src/loader.h",
        "from": """// The source and the tree it makes, following nothing it imports.""",
        "to": """bool kest_never(KestArena *arena);

// The source and the tree it makes, following nothing it imports.""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "is declared and is not there",
    },
    {
        "what": "a function in a header that nothing outside its file calls",
        "file": "src/loader.c",
        "from": """bool kest_read_unit(KestArena *arena, KestDiags *diags, const char *path,""",
        "to": """void kest_alone_here(void) {
}

bool kest_read_unit(KestArena *arena, KestDiags *diags, const char *path,""",
        "also": ("src/loader.h", """// The source and the tree it makes, following nothing it imports.""",
                 """void kest_alone_here(void);

// The source and the tree it makes, following nothing it imports."""),
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "nothing outside",
    },
    {
        # A machine started from the host that started the first one. Two hosts
        # in one process share nothing, and what makes that true is that a
        # machine reads the list it was handed and nobody remembers it
        # afterwards. A machine that remembered would answer a program with
        # somebody else's context, which reads as one program running under a
        # host it was never given.
        "what": "a machine started from the first host anybody used",
        "file": "src/vm.c",
        "from": """    bool unbound = false;
    for (uint32_t i = 0; i < module->extern_count; i++) {""",
        "to": """    static const KestHost *ever = NULL;
    if (ever == NULL) {
        ever = host;
    }
    host = ever;
    bool unbound = false;
    for (uint32_t i = 0; i < module->extern_count; i++) {""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "two hosts answered the same",
    },
    {
        # A check that hands back one of its two rooms. A second `trap ...
        # EXIT` replaces the first rather than adding to it, so this reads as
        # though both are taken away and one of them stays on the machine the
        # check ran on. The tree had this: nine hundred directories under
        # `/tmp` from one check, and a gate that stopped for want of room.
        "what": "a check whose second trap replaces its first",
        "file": "tools/check-ceilings.sh",
        "from": """trap 'rm -rf "$scratch"' EXIT""",
        "to": """trap 'rm -rf "$scratch"' EXIT
trap 'rm -rf "$scratch"/work' EXIT""",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "the last one is the only one that runs",
    },
    {
        # A run with no memory left, saying nothing. A diagnostic is written
        # into the arena that has just refused, so the one thing a run in this
        # state has to say is the one thing it cannot write down. Dropping it
        # in silence is what this compiler did: the run recorded nothing,
        # counted no errors, and came back nought, which reads from outside
        # like a program that ran and printed nothing.
        #
        # Broken where it is recorded rather than at one of the ten places
        # that record it. Taking out one of ten left the other nine to say it,
        # so the hole was a hole in nothing and had stopped proving anything;
        # what a check is held by has to be the one thing all of it goes
        # through. See D377.
        "what": "a run with no memory left that says nothing",
        "file": "src/diag.c",
        "from": """    diags->starved = true;
    diags->error_count++;
}""",
        "to": """}""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "of memory a run came back",
    },
    {
        # A program that ran the machine out of memory and was told `out of
        # memory`. That is the one sentence a reader already knew before they
        # read it: what they do about it depends on whether the program wants
        # a gigabyte or the machine has a megabyte left, and those are the two
        # numbers this used to leave out.
        "what": "a machine with no memory left that says only that",
        "file": "src/vm.c",
        "from": """    fail(vm, frame, instruction, "K0605",
         "the program has used %zu bytes and this asked for %zu more, which "
         "this machine has not got",
         kest_heap_used(rt), kest_arena_refused(rt->heap));""",
        "to": """    (void)rt;
    fail(vm, frame, instruction, "K0605", "out of memory");""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "out of memory a bit at a time and was told",
    },
    {
        # An arena refused a block of its own by the host, saying nothing about
        # what it had been asked for. The ceiling's refusal wrote that number
        # down and the host's did not, so a program made in one go that ran the
        # machine out said it had asked for nought more.
        "what": "an arena refused a block that says nothing about what for",
        "file": "src/mem.c",
        "from": """            arena->refused = taking;
            arena->refused_by_ceiling = false;
            return NULL;
        }""",
        "to": """            arena->refused_by_ceiling = false;
            return NULL;
        }""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "out of memory in one go and was told",
    },
    {
        # A machine with nothing left, read back as the ceiling a host set. The
        # number is the same either way and what a host does about it is not: a
        # ceiling it set is a number it can raise, and this is a host raising it
        # forever on a machine that has nothing to give.
        "what": "a machine with nothing left that answers as a ceiling",
        "file": "src/mem.c",
        "from": """            arena->refused = taking;
            arena->refused_by_ceiling = false;""",
        "to": """            arena->refused = taking;
            arena->refused_by_ceiling = true;""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "was read as a host's own",
    },
    {
        # And the other way: a ceiling this machine kept, handed back as the
        # machine underneath. A host told that raises nothing and gives up on a
        # program that was inside a number it chose.
        "what": "a ceiling kept that answers as the machine underneath",
        "file": "src/mem.c",
        "from": """    if (arena->ceiling != 0 && arena->handed + taking > arena->ceiling) {
        arena->refused = taking;
        arena->refused_by_ceiling = true;
        return NULL;
    }
    if (fresh) {""",
        "to": """    if (arena->ceiling != 0 && arena->handed + taking > arena->ceiling) {
        arena->refused = taking;
        arena->refused_by_ceiling = false;
        return NULL;
    }
    if (fresh) {""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "was blamed on the machine",
    },
    {
        # A heap thrown away while the program is standing on it. What the
        # machine does next is read what it gave back, and what a host does
        # next is nothing, because it was told this worked.
        "what": "a heap thrown away from inside a call",
        "file": "src/vm.c",
        "from": """bool kest_heap_reset(KestRuntime *runtime) {
    if (is_running(runtime)) {""",
        "to": """bool kest_heap_reset(KestRuntime *runtime) {
    if (false) {""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "thrown away while the program was running",
    },
    {
        # And the machine itself, freed from inside a call it is in the middle
        # of. The stack the program is standing on goes with it, and the host
        # that asked is told nothing.
        "what": "a machine freed from inside a call",
        "file": "src/vm.c",
        "from": """bool kest_runtime_free(KestRuntime *runtime) {
    if (runtime == NULL) {
        // Nothing to free is not a refusal: what a host asked for is that
        // there be no machine, and there is none.
        return true;
    }
    if (is_running(runtime)) {""",
        "to": """bool kest_runtime_free(KestRuntime *runtime) {
    if (runtime == NULL) {
        return true;
    }
    if (false) {""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "and was told about",
    },
    {
        # A refusal that answers like the thing it refused. A host in a frame
        # loop does not read reports; what it reads is the answer, and one that
        # says the machine is gone leaves it holding a machine it believes it
        # gave away.
        "what": "a machine that says it was freed and was not",
        "file": "src/vm.c",
        "from": """        kest_diags_suggest(runtime->diags,
                           "free it after the call it was made for returns");
        return false;""",
        "to": """        kest_diags_suggest(runtime->diags,
                           "free it after the call it was made for returns");
        return true;""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "said it was freed while the program was running",
    },
    {
        # A build freed with machines standing on it. What they run is on it,
        # and so is the text their diagnostics point at, so the instruction
        # after this one is a machine reading memory that has been given back.
        "what": "a build freed out from under its machines",
        "file": "src/build.c",
        "from": """    if (build->module.machines > 0) {""",
        "to": """    if (false) {""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "was freed with machines standing on it",
    },
    {
        # A build that counts what is standing on it and never counts one up,
        # which is the same as not counting: the refusal is there, the number
        # it reads is nought, and every host is told its machines are gone.
        "what": "a build that never counts a machine it made",
        "file": "src/vm.c",
        "from": """    ++*rt->standing;
    return rt;""",
        "to": """    return rt;""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "did not say how many were standing on it",
    },
    {
        # A machine that reads its context out of the host's list rather than
        # copying it. What a host hands over at binding is its own, and the
        # list it hands it over in is the caller's: every host here frees the
        # list as soon as its machines have started, so a machine pointing into
        # one is a machine reading what somebody else has since been given.
        "what": "a machine that points into the list it was started from",
        "file": "src/kest.c",
        "from": """                *context = host->items[i].context;""",
        "to": """                *context = (void *)&host->items[i];""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "two hosts answered the same",
    },
    {
        # A machine that never started, counted as one standing on the build.
        # A host that hands over no host at all is told what the program wanted
        # and has no machine; a build that counted that one could never be
        # freed by anybody.
        "what": "a build that counts a machine that never started",
        "file": "src/vm.c",
        "from": """    if (unbound) {
        kest_arena_free(rt->heap);
        return NULL;
    }""",
        "to": """    if (unbound) {
        kest_arena_free(rt->heap);
        ++*rt->standing;
        return NULL;
    }""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "did not say how many were standing on it",
    },
    {
        # A program that asks the host for one name more than an instruction
        # can name. The call carries the extern in two bytes, so the one past
        # the last is called as whichever one that number wraps to: another of
        # the host's own functions, handed this call's arguments, and nothing
        # said about any of it.
        "what": "a program asking for more names than a call can name",
        "file": "src/compile.c",
        "from": """    if (slot >= MAX_EXTERNS) {""",
        "to": """    if (false) {""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "asking for one name too many was not refused",
    },
    {
        # A ceiling raised past what the instruction under it can hold. The
        # number is what it is because a call names an extern in two bytes, and
        # a build that lets the two disagree is a wrap nobody sees.
        "what": "a ceiling on names raised past what names them",
        "file": "src/compile.c",
        "from": "#define MAX_EXTERNS 65536",
        "to": "#define MAX_EXTERNS 70000",
        "make": ["build/release/compile.o"],
        "in_build": True,
        "caught": "an extern is named in an instruction in two bytes",
    },
    {
        # An index of the names a program declares that one of them is not in.
        # Everything is looked up through it, so a name it does not hold is a
        # name the program does not have — which is the whole program, from the
        # first declaration nothing else can see.
        "what": "a name declared and not put where names are looked up",
        "file": "src/types.c",
        "from": "    index_put(program, program->global_count - 1);\n",
        "to": "",
        "make": ["kest"],
        "program": "unfound.kest",
        "source": "fn one() -> i32 {\n    return 1\n}\n\n"
                  "fn main() -> i32 {\n    return one()\n}\n",
        "caught": "unknown name `one`",
    },
    {
        # An index that holds a place the list it indexes does not have. The
        # other way round — a name in the list and not in the index — is caught
        # by the first program that uses that name; this way round is a lookup
        # answering with somebody else's declaration, and nothing about a
        # program says which of its names that happened to.
        "what": "an index that names a place there is no name at",
        "file": "src/types.c",
        "from": "    program->by_name[slot] = at + 1;",
        "to": "    program->by_name[slot] = at + 2;",
        "make": ["debug"],
        "binary": "kest-debug",
        "program": "declared.kest",
        "source": "fn one() -> i32 {\n    return 1\n}\n\n"
                  "fn main() -> i32 {\n    return one()\n}\n",
        "caught": "the index names place",
    },
    {
        # An index rebuilt into a bigger one that puts a name back in the wrong
        # order. Everything under one name is on one run of slots, and which of
        # them a lookup answers with is which went in first: the first `abs` is
        # the one found, and a second declaration of a name is told which line
        # the first is on. Appending keeps that for nothing, so a rebuild is
        # the only place it can be lost, and what is lost is a message pointing
        # at the wrong line rather than a program that runs differently.
        "what": "an index rebuilt with the names in the other order",
        "file": "src/types.c",
        "from": """    for (uint32_t i = 0; i < program->global_count; i++) {
        index_put(program, i);
    }""",
        "to": """    for (uint32_t i = program->global_count; i-- > 0;) {
        index_put(program, i);
    }""",
        "make": ["debug"],
        "binary": "kest-debug",
        "program": "twice.kest",
        # The pair first and the rest after it, because what is put back in
        # again is what was there before the table filled up: a name declared
        # after the last rebuild is only ever appended.
        "source": ("fn same(a: i32) -> i32 {\n    return a\n}\n\n"
                   "fn same(a: f32) -> f32 {\n    return a\n}\n\n"
                   + "".join("fn f%d() -> i32 {\n    return %d\n}\n\n"
                             % (i, i) for i in range(40))
                   + "fn main() -> i32 {\n    return same(1) - 1\n}\n"),
        "caught": "is found where it was declared second",
    },
    {
        # A file that asks the sanitiser for itself rather than asking the one
        # place that answers. One compiler defines that name and another
        # answers a question about it, so this is a file whose checks are in
        # one build and not the other — and the build without them runs
        # everything and finds nothing.
        "what": "a file that spells the sanitiser's own name",
        "file": "src/types.c",
        "from": "#if KEST_CHECKED",
        "to": "#if defined(__SANITIZE_ADDRESS__)",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "the sanitiser's own name is spelt in",
    },
    {
        # An option the command line reads and `help` does not print. The
        # commands have been held to their two places for a long time and the
        # options were held to neither: `-h` and `--help` worked and nothing
        # said they were there.
        "what": "an option nothing tells a reader about",
        "file": "src/main.c",
        "from": '"  --reset           tick throws the heap away between events\\n"\n',
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "does something and `kest help` does not say so",
    },
    {
        # A version nobody can read. It is the one thing here a tool asks for
        # rather than a person, and what a tool does with nothing is carry on.
        "what": "a version that says nothing",
        "file": "src/main.c",
        "from": """        printf("kest %s%s\\n", kest_version(), KEST_CHECKED ? " checked" : "");""",
        "to": """        printf("%s", KEST_CHECKED ? "" : "");""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "which is not this being named and numbered",
    },
    {
        # A heap thrown away between events by nobody. The option was printed,
        # answered and run by nothing, so what it changes — the one thing here
        # that changes what a program is standing on rather than what is
        # printed about it — was a promise with no run behind it.
        "what": "a heap between events that nothing throws away",
        "file": "src/main.c",
        "from": """                if (!kest_heap_reset(runtime)) {
                    return;
                }
                out->thrown++;""",
        "to": """                out->thrown++;""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "thrown away between events is still there",
    },
    {
        # A fix that is recorded nowhere, so both forms of every diagnostic
        # lose it together. What holds the two forms is that they say the same
        # thing, and two forms that agree are two forms that lost the same
        # thing: this is the shape of wrong that comparing them cannot see.
        "what": "a fix no diagnostic carries in either form",
        "file": "src/diag.c",
        "from": """void kest_diags_suggest(KestDiags *diags, const char *format, ...) {
    if (diags->muted || diags->count == 0) {""",
        "to": """void kest_diags_suggest(KestDiags *diags, const char *format, ...) {
    if (diags != NULL || diags->muted || diags->count == 0) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "a diagnostic with everything in it did not say",
    },
    {
        # A note with nowhere to point at. A note is the second place a
        # diagnostic is about — the first declaration, the promise, the call
        # between them — and one with no line under it is prose about somewhere
        # the reader has to go and find.
        "what": "a note with nowhere to point at",
        "file": "src/diag.c",
        "from": """        for (uint8_t n = 0; n < diag->note_count; n++) {
            if (diag->notes[n].source == NULL) {
                continue;
            }
            render_frame(diag->notes[n].source, diag->notes[n].span,
                         diag->notes[n].label, gutter, out);
        }""",
        "to": """        for (uint8_t n = 0; n < diag->note_count; n++) {
            if (diag->notes[n].source != NULL) {
                continue;
            }
            render_frame(diag->notes[n].source, diag->notes[n].span,
                         diag->notes[n].label, gutter, out);
        }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "a diagnostic about three places pointed at",
    },
    {
        # A note pointing at the wrong line. The chain a broken promise prints
        # reads forwards — the promise, then the calls under it — and a note in
        # it that points at the promise instead of at the call says the right
        # words under the wrong line, which is the one kind of wrong a reader
        # cannot see: every note here looks like this one.
        "what": "a note that points where its own words are not",
        "file": "src/contract.c",
        "from": """            kest_diags_note(program->diags, &units->items[path.units[n]].source,
                            path.calls[n], "which calls `%s`",""",
        "to": """            kest_diags_note(program->diags, &units->items[path.units[n]].source,
                            function->decl->name, "which calls `%s`",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "a note points somewhere its own words are not",
    },
    {
        # A note read out of the file the diagnostic is about rather than out
        # of the one the note is in. A promise in one module broken in another
        # is one diagnostic about two files: the note keeps its line and loses
        # its file, so it points at whatever is on that line of the other one,
        # which is a real line of a real file and says nothing about being
        # wrong.
        "what": "a note that names its line and not its file",
        "file": "src/contract.c",
        "from": """            kest_diags_note(program->diags, &units->items[path.units[n]].source,
                            path.calls[n], "which calls `%s`",""",
        "to": """            kest_diags_note(program->diags, NULL,
                            path.calls[n], "which calls `%s`",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "a line of helper.kest without it",
    },
    {
        # A name typed at a command line put under the root module whatever it
        # was. A program is the file that was named and everything it imports,
        # so `shapes.doubled` is a function of it — and this made
        # `working.shapes.doubled`, said there was no such thing, and said it
        # under a name nobody could have typed.
        "what": "a qualified name put under the module it was typed at",
        "file": "src/build.c",
        "from": """    if (strchr(name, '.') != NULL) {
        return name;
    }""",
        "to": """    size_t written = strlen(alias);
    if (strncmp(name, alias, written) == 0 && name[written] == '.') {
        return name;
    }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "a function of an imported module answered",
    },
    {
        # The second pass over what was typed, which settles which of four
        # `min`s was meant by how the number is written rather than by what it
        # could fit. Without it every number fits every width of its family and
        # a command line that can reach a library of overloads can call none of
        # them.
        "what": "a number at a command line that settles nothing",
        "file": "src/main.c",
        "from": """                if (fits && pass == 1 &&""",
        "to": """                if (fits && pass == 2 &&""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "call math.min 3 7: answered",
    },
    {
        # A call whose answer is on the same stream as what the program said
        # while it ran. Both are lines of text on standard output, in the order
        # they happened, and nothing says which of them is the value: a shell
        # reading one gets the other above it.
        "what": "a call that answers where the program is writing",
        "file": "src/main.c",
        "from": """                KestHost *host = make_host(stderr);""",
        "to": """                KestHost *host = make_host(stdout);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "what a program said is on the answer's stream",
    },
    {
        # A frame's cost with the program's own writing in the middle of it.
        # What `tick` answers with is a table of numbers, and a program that
        # says something every event says it between the rows.
        "what": "a frame's cost written into by the program",
        "file": "src/main.c",
        "from": """            KestHost *host = make_host(json || ticking ? stderr : stdout);""",
        "to": """            KestHost *host = make_host(json ? stderr : stdout);""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "in the middle of what a frame cost",
    },
    {
        # A run that could not write what the program said and answered as
        # though it had. `Io.write` gives nothing back, so the program cannot
        # be told; the stream remembers, and nobody asked it. What that is from
        # outside is a script carrying on with an empty file.
        "what": "a run that wrote nothing and said it had worked",
        "file": "src/main.c",
        "from": """    if (program_wrote_to != NULL &&
        (fflush(program_wrote_to) == EOF || ferror(program_wrote_to))) {""",
        "to": """    if (false) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "whose writing went nowhere answered nought",
    },
    {
        # A read that could not happen, handed to the program as an empty
        # input. `Io.read` gives back text and has no way to say it failed, so
        # a stream that is not there and a stream with nothing in it are the
        # same piece of text and the same answer.
        "what": "a read that failed and was handed over as nothing",
        "file": "src/main.c",
        "from": """    if (ferror(stdin)) {
        program_could_not_read = true;
        held = 0;
    }""",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "that would not be read answered nought",
    },
    {
        # A host that calls itself something else. What a host calls itself is
        # the one thing a program cannot work out for itself, and the only way
        # to find out what this one says is to ask it — so the document that
        # says `kest` and the host that says it are two places, and this is
        # what keeps them one.
        "what": "a host that calls itself something the reference does not",
        "file": "src/main.c",
        "from": '    frame[0] = kest_text(runtime, "kest", 4);',
        "to": '    frame[0] = kest_text(runtime, "kestrel", 7);',
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "this host says it is",
    },
    {
        # A name the command line hands a program that no document mentions. A
        # host is a list of bindings, and what this one binds beyond what the
        # library asks for is between it and whoever writes the `extern` — so
        # it is written in the reference or a program can only find it by
        # reading the C.
        "what": "a name the command line provides and nothing says so",
        "file": "src/main.c",
        "from": '"Host.clock", host_clock',
        "to": '"Host.ticks", host_clock',
        "make": ["kest"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "and no document says so",
    },
    {
        # A promise about a host that nothing measures. It is the one promise
        # in this language somebody else keeps: a declaration says a host
        # function does not reach the heap, and the compiler lets a `no.alloc`
        # body call it on the strength of that. Nothing else would notice a
        # host that made a piece of text in it — including the host this
        # compiler ships with, which makes one in three of the names it binds.
        "what": "a promise about a host that nothing measures",
        "file": "src/vm.c",
        "from": """            if (promised && kest_heap_used(rt) != held) {""",
        "to": """            if (false && kest_heap_used(rt) != held) {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/world.kest"],
        "caught": "reaches the heap and was let promise",
    },
    {
        # A lend that copies what it was lent. What a lend costs is a header,
        # and a header is one size whatever it stands in front of — that is the
        # whole reason a host lends rather than hands over a copy, and a copy
        # made quietly is a frame budget that grows with somebody else's array.
        "what": "a lend that costs what it is lent",
        "file": "src/vm.c",
        "from": """    } else {
        array = kest_arena_alloc(runtime->heap, sizeof(Array), 16);
    }""",
        "to": """    } else {
        array = kest_arena_alloc(runtime->heap, sizeof(Array), 16);
        (void)kest_arena_alloc(runtime->heap, (size_t)length * stride, 16);
    }""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "and lending four cost",
    },
    {
        # A lend that could not be written down and said nothing. What a host
        # gets back is a value with nothing in it, which is also what it gets
        # for a name the program has no array of — one of those is about the
        # program and the other is about the heap the host itself gave, and
        # they were the same answer.
        "what": "a lend refused for want of room that says nothing",
        "file": "src/vm.c",
        "from": """    if (array == NULL) {
        no_room_to_lend(runtime);
        return value;
    }""",
        "to": """    if (array == NULL) {
        return value;
    }""",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "the heap it gave ran out was told",
    },
    {
        # The list of headers waiting to be used again, left pointing at a heap
        # that has been thrown away. Ending a lend puts its header on that list
        # so the next lend costs nothing, and the list is on the heap: a reset
        # takes the headers and has to take the list with them. One left behind
        # hands the next lend a header out of memory the machine gave back,
        # which is a write through a pointer into what was freed.
        "what": "a list of spare headers a reset left behind",
        "file": "src/vm.c",
        "from": """    // Every one of those was on it, and so was the list of what is lent.
    runtime->spare_lends = NULL;""",
        "to": """    // Every one of those was on it, and so was the list of what is lent.""",
        "make": ["embed-debug"],
        "host": "examples/embed-debug",
        # The arena poisons what it takes back, so a header handed out of
        # that list again is memory the sanitised build already knows is
        # nobody's.
        "caught": "use-after-poison",
    },
    {
        # A reset that hands the block back without emptying it. Everything
        # this arena gives out arrives as nought — a header whose unwritten
        # fields are noughts, a length nobody has set yet — and the one it
        # keeps is the one a reset hands out again, so what was written in it
        # before is what the next thing reads.
        "what": "a reset that hands back what was written before it",
        "file": "src/mem.c",
        "from": """    OPEN(first->data, first->used);
    memset(first->data, 0,
           first->used < first->capacity ? first->used : first->capacity);""",
        "to": """    OPEN(first->data, first->used);""",
        "make": ["embed-debug"],
        "host": "examples/embed-debug",
        "caught": "was not nought",
    },
    {
        # A rule a host has to keep, with nothing behind it. The machine
        # refuses what it can see; the rest is a list in the reference, and a
        # list of rules nobody has watched being broken is a paragraph. The
        # engine breaks each of them on purpose and says what happened, and
        # what holds the list is that a run of it says those lines.
        "what": "a host's own rule the engine stopped showing",
        "file": "examples/embed.c",
        "from": """    printf("and text kept across a heap being thrown away reads what the "
           "machine made next: `%s`\\n",
           first_word.text);""",
        "to": """    printf("and the text this host kept reads `%s`\\n", first_word.text);""",
        "make": ["kest", "embed"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "the engine says nothing about",
    },
    {
        # A host that lends a block it has given back. The machine holds an
        # address and a count and cannot know when the block went, which is
        # the rule a host keeps for itself — except in the build that checks
        # itself, which is told where every block a host has ends and refuses
        # the lend in the machine's own words.
        "what": "a host that lends what it has given back",
        "file": "examples/embed.c",
        "from": """    engine->frame[0] = kest_borrow(engine->runtime, letters, 4, "u8", sizeof(letters[0]));
    if (engine->frame[0].object == NULL) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    KestValue lent = engine->frame[0];""",
        "to": """    unsigned char *gone = malloc(4);
    memcpy(gone, letters, 4);
    hands_it_back(gone);
    engine->frame[0] = kest_borrow(engine->runtime, gone, 4, "u8", 1);
    if (engine->frame[0].object == NULL) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    KestValue lent = engine->frame[0];""",
        # The free is one function away, because a compiler that can see both
        # says so itself and this is about what the machine says.
        "also": ("examples/embed.c", "static bool lends_bytes(Engine *engine) {",
                 "static void hands_it_back(void *block) {\n"
                 "    free(block);\n"
                 "}\n"
                 "\n"
                 "static bool lends_bytes(Engine *engine) {"),
        "make": ["embed-debug"],
        "host": "examples/embed-debug",
        "caught": "does not own that many",
    },
    {
        # A lend at no address at all, given to the program as an array. The
        # machine cannot tell a bad address from a good one and this is the
        # one address it can: what a host with nothing to lend has is a count
        # of nought, and what it used to get instead was four bytes at nowhere
        # and a program that reads them.
        "what": "a lend at no address that is given anyway",
        "file": "src/vm.c",
        "from": """    if (data == NULL && length > 0) {""",
        "to": """    if (false) {""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "at no address was given",
    },
    {
        # A lend longer than the program can count. `len` gives back an `i32`,
        # so what a host lends above that is a run whose end the program cannot
        # see: every loop over it walks off memory that is really there into
        # memory that is not. It is the one thing about a count that either
        # build can weigh, which is why it is asked before the one only the
        # sanitised build can.
        "what": "a lend longer than `len` can count",
        "file": "src/vm.c",
        "from": """    if (length > MAX_COUNTED) {""",
        "to": """    if (false) {""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "a lend longer than a count was allowed",
    },
    {
        # An address a type may not be read at, taken anyway. Where an array
        # sits is the one thing about a lend that nothing in the program
        # decides, and a payload read across a word boundary is something the
        # C standard has no answer for — so a host with bytes copies them into
        # an array of the type, and the refusal is what tells it to.
        "what": "an address a type may not be read at",
        "file": "src/vm.c",
        "from": """    uintptr_t past = (uintptr_t)data % align;
    if (past != 0) {""",
        "to": """    uintptr_t past = (uintptr_t)data % align;
    if (false) {""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "a lend at a crooked address was allowed",
    },
    {
        # A byte read out of a host's memory as though it were signed. Bytes
        # out of a wire form are widened and shifted into place, so one above
        # 127 read as a negative number makes every record above it wrong —
        # and what the program answers with is a number either way, which is
        # what it was going to answer with.
        "what": "a byte read out of a lend as though it were signed",
        "file": "src/vm.c",
        "from": """        case KEST_L_U8: {
            uint8_t v;
            memcpy(&v, at, 1);
            out[i].integer = v;
            break;
        }""",
        "to": """        case KEST_L_U8: {
            int8_t v;
            memcpy(&v, at, 1);
            out[i].integer = v;
            break;
        }""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "a batch read out of bytes came to",
    },
    {
        # A byte written into a host's memory out of the wrong end of the
        # number it came from. A program answering in a wire form writes the
        # bytes where the host reads them, and a byte that is the next one
        # along is an answer the host reads as something else — with nothing
        # anywhere saying a word about it.
        "what": "a byte written into a lend out of the wrong end",
        "file": "src/vm.c",
        "from": """        case KEST_L_U8: {
            uint8_t v = (uint8_t)from[i].integer;
            memcpy(at, &v, 1);
            break;""",
        "to": """        case KEST_L_U8: {
            uint8_t v = (uint8_t)(from[i].integer >> 8);
            memcpy(at, &v, 1);
            break;""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "into this host's bytes",
    },
    {
        # A byte of text read as though it were signed. Text is its bytes and
        # `t[i]` is a `u8`, so a byte above 127 read as a negative number is a
        # word that says something else — and every word in this tree is ASCII
        # except the one written to have such a byte in it, which is why
        # nothing noticed until it was asked for.
        "what": "a byte of text read as though it were signed",
        "file": "src/vm.c",
        # A byte read out of what a call gave back. The same line is written
        # again for a byte read out of a name, so this says which by taking
        # the line above it as well.
        "from": """            }
            (top++)->integer = (unsigned char)text[index];""",
        "to": """            }
            (top++)->integer = text[index];""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "and said nothing",
    },
    {
        # And a byte read out of a name, which is the other instruction and
        # the one nothing had ever gone through. `\u0131` is two bytes and the
        # first of them is 196: read as though it were signed it is minus
        # sixty, and a walk over what a program typed goes on for ever.
        "what": "a byte read out of a name as though it were signed",
        "file": "src/vm.c",
        "from": """#endif
            (top++)->integer = (unsigned char)text[index];""",
        "to": """#endif
            (top++)->integer = text[index];""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "and said nothing",
    },
    {
        # A character counted by its bytes. UTF-8 says how wide a character is
        # in its first byte, and a library that reads that wrong walks into the
        # middle of one: `hız` is four bytes and three characters, and counting
        # it as four is a program told a word is longer than it is.
        "what": "a character counted as many as its bytes",
        "file": "lib/std/text.kest",
        "from": """    if first < u8(224) {
        return 2
    }""",
        "to": """    if first < u8(224) {
        return 1
    }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "and said nothing",
    },
    {
        # A cut that the promise does not count. `slice` copies the piece it
        # names, which is what makes a walk over characters cost a piece of
        # text each — and a `no.alloc` body that cuts and is let through is a
        # program told its loop is free when it is not.
        "what": "a cut the promise does not count",
        "file": "src/contract.c",
        "from": """                {"slice", "`slice` copies the piece it names"},""",
        "to": """                {"slice", NULL},""",
        "make": ["kest"],
        "program": "cutting.kest",
        "source": """import std.text

fn counted(word: text) -> i32 no.alloc {
    let n = 0
    for i in 0..text.chars(word) {
        if let one = text.charAt(word, i) {
            n += len(one)
        }
    }
    return n
}

fn main() -> i32 {
    return counted("kest") - 4
}
""",
        # Caught by the other half of the same promise: the tree walk let it
        # through and what was emitted says otherwise, which is the fault
        # `K0405` is for.
        "caught": "the promise was allowed and the code says otherwise",
    },
    {
        # A character asked for whole when only part of it is there. Text
        # arriving a piece at a time ends in the middle of one, and a library
        # that reads what the first byte says rather than what is there stops
        # the program at a line it cannot help — a half-read line is not a
        # mistake anybody made.
        "what": "a character read past the end of what was read",
        "file": "lib/std/text.kest",
        "from": """        let after = rest(piece, i)
        if after == "" {
            return i
        }""",
        "to": """        let after = rest(piece, i)
        if false {
            return i
        }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "a character cut off at the end of what was read",
    },
    {
        # A character that swallows what comes after it. What its first byte
        # says is three bytes wide is three bytes only if the two after it are
        # the middles of one — a byte that begins a character of its own ends
        # the one before it. Without that, one wrong byte in a line takes the
        # letter after it with it, and the count comes out short.
        "what": "a character that swallows the one after it",
        "file": "lib/std/text.kest",
        "from": """        if charBytes(after[0]) != 0 {
            return i
        }""",
        "to": """        if false {
            return i
        }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "a character cut off at the end of what was read",
    },
    {
        # A walk back that stops at the first byte behind it. The middle of a
        # character says so in every one of its bytes, which is what makes
        # walking back possible at all — and a walk that does not use it lands
        # inside a character and calls it a place, so a program moving one
        # left through a line ends up between the bytes of a letter.
        "what": "a walk back that lands inside a character",
        "file": "lib/std/text.kest",
        "from": """    while back > 0 && steps < 3 && charBytes(subject[back]) == 0 {""",
        "to": """    while false {""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "a character cut off at the end of what was read",
    },
    {
        # A number spelled in a text that the type it is read into cannot hold.
        # An `i32` handed more than it holds wraps without a word, so the text
        # comes back as a different number rather than as nothing — which is
        # the promise this project keeps in the other direction, that a number
        # written down reads back as the number it was written from.
        "what": "a number too big to hold read as something else",
        "file": "lib/std/text.kest",
        "from": """    if value > 2147483647 {
        return none
    }
    return i32(value)""",
        "to": """    return i32(value)""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "call text.number 2147483648: answered",
    },
    {
        # The same in the other kind of number. More than an `f32` holds
        # narrows to infinity, which is a value this language has and not one
        # any text spells: a field a line too long is read as a number bigger
        # than every number rather than as something that is not one.
        "what": "a number too big for an `f32` read as infinity",
        "file": "lib/std/text.kest",
        "from": """    if narrowed - narrowed != 0.0 {
        return none
    }""",
        "to": """    if false {
        return none
    }""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "call text.real 340282400000000000000000000000000000000: answered",
    },
    {
        # A function written for one width and not the other, in a module
        # written in both. Four of those were there for months and nothing
        # could see them: what holds every library function to being reached
        # holds the ones that are there, and a half nobody wrote is named by
        # nobody. This is the check that can see a gap rather than a leftover.
        "what": "a function written for one width and not the other",
        "file": "lib/std/math.kest",
        "from": """fn abs(value: i64) -> i64 no.alloc {
    return if value < 0 -> 0 - value else -> value
}""",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "and nothing takes (i64), in a module written in both",
    },
    {
        # A line broken after an operator a line may end after. The formatter
        # breaks a long chain after its operator, because a line ending in one
        # carries on — and `>` is the one that does not, since `ref<Npc>` ends
        # in one. Broken there, a comparison is two statements and the second
        # of them begins with a number; the formatter answered nought and
        # printed it.
        "what": "a line broken where a line may end",
        "file": "src/fmt.c",
        "from": """            if (kest_lexer_ends_statement(operators[i])) {""",
        "to": """            if (false && kest_lexer_ends_statement(operators[i])) {""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "refused lines longer than the one form allows",
    },
    {
        # A match arm whose value was put on a line of its own. That is two
        # lines where the author wrote one, so the arm under it looks a line
        # further down than it is and gains a blank line — and the next time
        # the file is formatted, another. A formatter that does not settle is
        # one nobody can leave running on save.
        "what": "an arm that gains a blank line every time it is formatted",
        "file": "src/fmt.c",
        "from": """                printer->previous_line = line_of(
                    printer, arm->value->span.offset + arm->value->span.length);""",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "refused lines longer than the one form allows",
    },
    {
        # A tree that does not say a function promised anything. What holds
        # the formatter to keeping the meaning is that the tree of what came
        # out matches the tree of what went in — so a tree that leaves a thing
        # out is a formatter free to drop that thing, over every file in this
        # tree, and every one of them still called faithful.
        "what": "a tree that leaves out what it is compared for",
        "file": "src/ast.c",
        "from": """        if (decl->function.no_alloc) {
            fputs(" no.alloc", out);
        }
        fputc('\\n', out);""",
        "to": """        fputc('\\n', out);""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "differing in the promise on a function have one tree",
    },
    {
        # A machine-readable list of what a file says that stops one short.
        # What holds the formatter to keeping every comment is that list,
        # compared before and against after — and a second reading of the same
        # file beside it, so that a list which quietly stopped saying
        # everything is a check that says so rather than a check that agrees
        # with itself.
        "what": "a list of what a file says that stops one short",
        "file": "src/main.c",
        "from": """    fputs(",\\"comments\\":[", out);
    for (uint32_t i = 0; i < count; i++) {""",
        "to": """    fputs(",\\"comments\\":[", out);
    for (uint32_t i = 0; i + 1 < count; i++) {""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "read a different comment from the compiler",
    },
    {
        # A comment written after code on a line, left where the line used to
        # end. What is written there was written about what is on that line,
        # so it belongs above it — and left behind it comes out above the next
        # thing instead, which is a comment about something nobody wrote it
        # about. Every word is still there and in order, so what says a
        # formatter keeps comments does not see it.
        "what": "a comment moved past what it was written about",
        "file": "src/fmt.c",
        "from": """    while (at < printer->source->length && printer->source->text[at] != '\\n') {""",
        "to": """    while (at < printer->source->length && false) {""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "a comment moved past what it was written about",
    },
    {
        # A comment written on the line a block ends on, left behind the brace.
        # What is written there was written about the block that is ending, and
        # left behind it comes out above whatever follows — the next
        # declaration, the next statement, or nothing at all. It was every
        # closing brace in the language, and what found it was putting a
        # comment in every place a file offers rather than in the places
        # somebody thought of.
        "what": "a comment on a closing brace left behind it",
        "file": "src/fmt.c",
        "from": """    flush_comments(printer, rest_of_line(printer, closing));""",
        "to": """    flush_comments(printer, closing);""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "came out above",
    },
    {
        # The file a comment is put in every place of, with one of the
        # language's words no longer in it. What that file uses of the
        # language is what decides which places there are, so a construct it
        # stops writing is a place nothing tries — and nothing would say so,
        # because the sweep would go on reporting every place it found.
        "what": "a file of every place that stopped using one of them",
        "file": "tools/check-fmt.sh",
        "from": """    defer push(steps, 0)
""",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "does not use `defer`",
    },
    {
        # A comment inside a hole in a string, taken. A hole is code and the
        # formatter writes it back from what it means, so the comment was
        # dropped and nothing said anything: no reading of the file sees a
        # comment inside a string, so what holds the formatter to keeping
        # every comment never knew there was one.
        "what": "a comment nothing can keep, taken anyway",
        "file": "src/lexer.c",
        "from": """            if (lexer->in_hole) {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0111",""",
        "to": """            if (false && lexer->in_hole) {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0111",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "a comment inside a hole said",
    },
    {
        # The same file with a whole kind of declaration gone. `flags` is not a
        # keyword — it declares a type where a declaration begins and is a name
        # everywhere else — so holding this file to the keywords does not reach
        # it, and what does is the list a run gives when a file holds something
        # else.
        "what": "a file of every place with a kind of declaration missing",
        "file": "tools/check-fmt.sh",
        "from": """flags State: u8 {
    Moving
    Hurt
}

""",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "does not use `flags`",
    },
    {
        # A comment kept with whatever was left at the end of it. Space nobody
        # can see is not something anybody wrote, so a form that keeps it is a
        # form there are two of — and every file in this tree would still have
        # been in it, because every file in this tree was written by hand and
        # has none.
        "what": "a form that keeps space nobody can see",
        "file": "src/fmt.c",
        "from": """        while (span.length > 0) {
            char last = printer->source->text[span.offset + span.length - 1];
            if (last != ' ' && last != '\\t') {
                break;
            }
            span.length--;
        }
""",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "roughed up, it does not come back",
    },
    {
        # An enum case written over two lines, and the case under it a line
        # further down than it is. A payload broken at its comma is two lines
        # where one was written, so a blank line nobody wrote goes between the
        # cases — and only when a file is written that way, which no file here
        # is until one is roughed up on purpose.
        "what": "a case that gains a blank line under it",
        "file": "src/fmt.c",
        "from": """            if (i > 0) {
                const KestVariant *before = decl->choice.cases[i - 1];
                KestSpan ended = before->payload_count > 0
                                     ? before->payload[before->payload_count -
                                                       1]->span
                                     : before->name;
                printer->previous_line =
                    line_of(printer, ended.offset + ended.length);
            }
""",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/embed.kest"],
        "caught": "roughed up, it does not come back",
    },
    {
        # A dotted name copied out of the file rather than written back. A `.`
        # carries on to the next line, so `vec.Vec2` may be written over two —
        # and a name is one thing however it was typed. Copying the span put
        # the line break back in, and the one form is then two.
        "what": "a name copied with what a line break left in it",
        "file": "src/fmt.c",
        "from": """    case KEST_TYPE_NAMED:
        // A dotted one is more than one token, and what a line break left
        // between the pieces is not part of it.
        print_name(printer, type->name);""",
        "to": """    case KEST_TYPE_NAMED:
        print_span(printer, type->name);""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/world.kest"],
        "caught": "roughed up, it does not come back",
    },
    {
        # A statement written over two lines, with what was said at the end of
        # the second one left behind. It came out above the statement after,
        # which is a comment about something the author did not write it
        # about — the mistake `rest_of_line` is for, slipped past by a
        # statement longer than a line.
        "what": "a statement that leaves what was said at the end of it",
        "file": "src/fmt.c",
        "from": """        if (prints_flat(stmt) && stmt->span.length > 0) {""",
        "to": """        if (false && prints_flat(stmt) && stmt->span.length > 0) {""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "arguments": ["examples/words.kest"],
        "caught": "belongs above `walk`, came out above `if`",
    },
    {
        # A block that stands on its own and says something the checker
        # refuses. It parses and it is in the one form, so everything the
        # documents were held to before this passes — and a reader who takes
        # it at its word writes a program that does not compile.
        "what": "a documented block that does not check",
        "file": "docs/language.md",
        "from": """fn isSpace(byte: u8) -> bool no.alloc {""",
        "to": """fn isSpace(byte: text) -> bool no.alloc {""",
        "make": ["kest", "embed"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "stands on its own and does not check",
    },
    {
        # A function compiled under a name that leaves out what it takes, so
        # two of one name are one. The checker tells them apart and the
        # compiler cannot, which is the two halves disagreeing about what a
        # program is — and a block of the documents that checks and cannot be
        # made is a block a reader finds out about after typing it.
        "what": "a documented block that checks and does not compile",
        "file": "src/types.c",
        "from": """    char *out = kest_arena_alloc(program->arena, room, 1);
    if (out == NULL) {
        return name;
    }""",
        "to": """    char *out = kest_arena_alloc(program->arena, room, 1);
    if (out == NULL || true) {
        return name;
    }""",
        "make": ["kest", "embed"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "this block checks and does not compile",
    },
    {
        # A program in the documents that says something other than what is
        # written under it. It parses, it is in the one form, it checks and it
        # compiles — everything the documents were held to before this — and
        # what a reader is shown it printing is not what it prints.
        "what": "a program that says something other than what is under it",
        "file": "docs/language.md",
        "from": """    io.print("hello")
    return 0""",
        "to": """    io.print("goodbye")
    return 0""",
        "make": ["kest", "embed"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "wrote 'goodbye",
    },
    {
        # A counter given the name a set further down the same check already
        # had. Every line of the check runs and the last one refuses with a
        # `TypeError` from Python, which says nothing about what the check was
        # for — and a reader looking at that is looking at the wrong file.
        "what": "a name in a check that stands for two things",
        "file": "tools/check-docs.sh",
        "from": """said_it = 0""",
        "to": """printed = 0""",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "and one name is one thing",
    },
    {
        # A name in the shell a check is written in that stands for a place and
        # for what a command answered. That is how the directory holding a
        # sweep's answers became the last thing a command said: every complaint
        # the sweep made went to a file nothing read, and four holes that had
        # been caught the day before were missed.
        "what": "a name in a check that is a place and an answer",
        "file": "tools/check.sh",
        "from": """gives_nothing="$scratch"/quiet-main.kest""",
        "to": """quiet="$scratch"/quiet-main.kest""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "`quiet` is a text at line",
    },
    {
        # And the same mistake with the two furthest apart: a name that is a
        # function and a file. `said "$said"` reads as one of them called on
        # the other and is one of them called on itself.
        "what": "a name in a check that is a function and a file",
        "file": "tools/check-fmt.sh",
        "from": """commented="$scratch"/fmt-commented.kest""",
        "to": """said="$scratch"/fmt-commented.kest""",
        "make": [],
        "tool": "tools/check-tables.sh",
        "caught": "`said` is a function and a place",
    },
    {
        # The same mistake in the other kind of Python a check carries: a
        # string handed to `python3 -c` rather than a heredoc. Two thirds of
        # the Python in `tools` is written that way, and a shell string cannot
        # hold the quote that would end it, so what is in one is written to
        # avoid a character — which is the writing a reader skims.
        "what": "a name that stands for two things in a quoted Python",
        "file": "tools/check-fmt.sh",
        "from": """breaks = {}""",
        "to": """broken = {}""",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "and one name is one thing",
    },
    {
        # A name that is a function in one place and something else in
        # another. Which of the two a line gets is whichever was written above
        # it, so the mistake works until somebody adds a line — and what it
        # says then is a `TypeError` from Python about a function not being a
        # container, in a file about documents.
        "what": "a name that is a function and a value",
        "file": "tools/check-docs.sh",
        "from": """names_written = set()""",
        "to": """written = set()""",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "is a function and a set, and one name is one thing",
    },
    {
        # A name that is what a run gave back in one place and a piece of text
        # in another. What a name is made of is told from the words where the
        # words say it, and `subprocess.run` is what these checks are mostly
        # written out of — a name meaning both is a name a reader has to hold
        # two answers for.
        "what": "a name that is a run and a piece of text",
        "file": "tools/check-docs.sh",
        "from": """        fence = lines[at][3:].strip()""",
        "to": """        said = lines[at][3:].strip()""",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "is a run and a str, and one name is one thing",
    },
    {
        # A name that is a list through another name and a number of its own.
        # What `last` is made of is written nowhere on the line it is assigned
        # — it is whatever `statements` is — so reading one line at a time
        # says nothing about it, and a second assignment of another kind goes
        # unnoticed.
        "what": "a name whose kind comes through another name",
        "file": "tools/check-docs.sh",
        "from": """    last = statements
    for one in body.splitlines():""",
        "to": """    last = statements
    last = 0
    for one in body.splitlines():""",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "`last` is a list and a int, and one name is one thing",
    },
    {
        # A walk over text that goes one byte past what it measured. The read
        # a walk does is the one read in this language that does not ask, and
        # what makes it right is the measurement before the first turn — so a
        # measurement that is wrong is a read of a byte the arena handed out
        # for something else. It is neither poisoned nor unmapped, so the
        # sanitisers say nothing; only the build that checks itself can tell.
        "what": "a walk that reads a byte past what it measured",
        "file": "src/compile.c",
        "from": """                emit(compiler, over_text ? KEST_OP_TEXT_LEN : KEST_OP_LEN,
                     stmt->span);""",
        "to": """                emit(compiler, over_text ? KEST_OP_TEXT_LEN : KEST_OP_LEN,
                     stmt->span);
                if (over_text) {
                    KestValue one = {0};
                    one.integer = 1;
                    stack_push(compiler, 1);
                    emit_constant(compiler, one, KEST_CONST_INT, stmt->span);
                    stack_pop(compiler, 1);
                    emit(compiler, KEST_OP_ADD_I, stmt->span);
                }""",
        "make": ["debug"],
        "binary": "kest-debug",
        "program": "walked.kest",
        "source": """module walked

fn main() -> i32 {
    let count = 0
    for byte in "abcdef" {
        if byte != u8(0) {
            count += 1
        }
    }
    return count - 6
}
""",
        "caught": "K0645]: a walk read byte",
    },
    {
        # A fault saying what it is in its own words. What a fault is — this
        # project got it wrong, not the program — was written out eight times
        # in five files, so a reader met the same news in five voices, and the
        # ninth would have arrived in a sixth. There is one door now, and this
        # is what says nobody has gone round it.
        "what": "a fault that says what it is in its own words",
        "file": "src/value.c",
        "from": """            kest_diags_fault(diags,
                             "an instruction is a different width from what "
                             "it says");""",
        "to": """            kest_diags_suggest(diags,
                               "an instruction is a different width from what "
                               "it says, which is a fault in the compiler");""",
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "says what a fault is in its own words",
    },
    {
        # A run of pieces where each one is longer than the last. What a
        # program asking for every character wants is a piece each; a walk that
        # keeps the rest of the text in every one of them is the same words
        # copied as many times as there are characters, and the heap says so
        # before anybody notices the wait.
        "what": "a piece per character that grows with the text",
        "file": "lib/std/text.kest",
        "from": """        push(out, slice(tail, 0, wide))""",
        "to": """        push(out, slice(subject, 0, len(subject) - len(tail) + wide))""",
        "make": ["kest"],
        "tool": "tools/check-costs.sh",
        "caught": "which is not twice for twice the work",
    },
    {
        # A cut that copies what it did not have to. Text ends at a nought, so
        # a piece that ends where the text ends is the one that was already
        # there and a place inside it is the whole of the answer — which is
        # what `rest` is. Copying it anyway is every walk that takes the rest
        # of a line paying for the line again.
        "what": "a cut that copies what was already ending",
        "file": "src/vm.c",
        "from": """            if (text[want] == '\\0') {
                (top++)->text = text + from;
                break;
            }""",
        "to": "",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "a cut that ends where the text ends cost",
    },
    {
        # A refusal that names a length nobody measured. A cut walks to the
        # place it was asked for rather than measuring the whole of the text,
        # so what is left after that place is unread — and the one thing that
        # needs it is the message, which is a run that is stopping and can pay
        # for the rest.
        "what": "a cut refused without saying how long the text was",
        "file": "src/vm.c",
        "from": """                size_t length = seen + strlen(text + seen);
                fail(vmp, frame, instruction, "K0604",
                     "%lld bytes from %lld is outside text of %zu bytes",""",
        "to": """                size_t length = (size_t)seen;
                fail(vmp, frame, instruction, "K0604",
                     "%lld bytes from %lld is outside text of %zu bytes",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "reaching outside the text said",
    },
    {
        # The same for a byte read at a place past the end. Both refusals now
        # quote the line after them as well, because the two are the same line
        # and a hole that breaks whichever comes first breaks the other one.
        "what": "a byte read past the end that says nothing about how long "
                "the text was",
        "file": "src/vm.c",
        "from": """                size_t length = seen + strlen(text + seen);
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside text of %zu bytes",""",
        "to": """                size_t length = (size_t)seen;
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside text of %zu bytes",""",
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
        "arguments": ["examples/words.kest"],
        "caught": "reaching outside the text said",
    },
    {
        # A header that is not given back when the lend it belonged to ends.
        # What a host pays for lending is then how many times it has lent
        # rather than the most it has lent at once, so a host lending and
        # ending a batch every frame pays for every frame it has ever run.
        "what": "a header a lend does not give back",
        "file": "src/vm.c",
        "from": """        one->bytes = (unsigned char *)(void *)runtime->spare_lends;
        runtime->spare_lends = one;""",
        "to": """        one->bytes = NULL;""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "frames of lending grew the heap by",
    },
    {
        # A lend that stays in the list of what is lent after it has ended.
        # The list is what a heap reset walks and what the next lend is written
        # into, and one that only ever grows is a place in it for every lend a
        # host has ever made.
        "what": "a lend that stays in the list after it ends",
        "file": "src/vm.c",
        "from": """        runtime->lent[at] = runtime->lent[--runtime->lent_count];""",
        "to": """        at++;""",
        "make": ["kest", "embed"],
        "host": "examples/embed",
        "caught": "frames of lending grew the heap by",
    },
    {
        # A promise in `help` that nothing walks. Every command and option in
        # it is held to being answered; the names it marks out are held to
        # being run, because a sentence a reader acts on is worth as much as
        # what is behind it.
        "what": "a name `help` marks out that nothing walks",
        "file": "src/main.c",
        "from": '            "KEST_LIB says where the standard library is. Without it the\\n"',
        "to": '            "KEST_LIB says where the standard library is, and `KEST_HOME`\\n"',
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "and nothing in `check-commands.sh` walks it",
    },
    {
        # A command the documents write and the command line does not answer
        # to. Two documents describe this one — `help`, which is held to what
        # `main` compares against, and the reference, which writes the same
        # commands in its own words — and a command renamed in one of them
        # leaves the two disagreeing with nothing to say which is the program.
        "what": "a command the documents write and nothing answers to",
        "file": "src/main.c",
        "from": 'strcmp(argv[1], "emit") == 0',
        "to": 'strcmp(argv[1], "dump") == 0',
        "make": ["kest"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "and the command line does not answer to it",
    },
    {
        # A library function renamed while the documents still call it. A block
        # in the reference is a reader's next line of code and it only had to
        # parse: a call to something that is not there parses like every other
        # call, and the reader finds out where they always do.
        "what": "a library the documents call and the library has not got",
        "file": "lib/std/text.kest",
        "from": "\nfn number(",
        "to": "\nfn numbered(",
        "make": ["kest"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "has no such function",
    },
    {
        # A `print` this language has not got, in a document that says in one
        # place that there is no such thing. It is what every block here said
        # for as long as there have been blocks, and what a reader does with it
        # is type it.
        "what": "a block calling a print this language has not got",
        "file": "docs/language.md",
        "from": "    io.print(\"hello\")",
        "to": "    print(\"hello\")",
        "make": ["kest"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "which this language has not got",
    },
    {
        # A block that is a whole program and would not compile. Parsing is
        # what a fragment can be held to; a program carries everything it uses,
        # so what it is held to is the compiler.
        "what": "a documented program that does not compile",
        "file": "docs/language.md",
        "from": """import std.io

fn main() -> i32 {
    io.print("hello")""",
        "to": """fn main() -> i32 {
    io.print("hello")""",
        "make": ["kest"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "is a program and does not compile",
    },
    {
        # A program fenced as though it were not one. A fence with nothing
        # after it is what a message or a signature is written in, and nothing
        # reads one — so this is a block that stops being parsed, stops being
        # compiled, and says nothing about having stopped.
        "what": "a program fenced as though it were not Kest",
        "file": "docs/language.md",
        "from": """```kest
import std.io

fn main() -> i32 {""",
        "to": """```
import std.io

fn main() -> i32 {""",
        "make": ["kest"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "reads as Kest and is fenced without it",
    },
    {
        # A file the reference sends a reader to that is not there. What holds
        # the prose in these documents is that the things it points at are
        # real: the check that holds the blocks, the host that shows a rule,
        # the file a rule is run in. A name that has moved leaves a paragraph
        # describing something that is not there, which reads like one that is
        # true.
        "what": "a file the documents name that is not there",
        "file": "docs/language.md",
        "from": "`tools/check-docs.sh` holds those",
        "to": "`tools/check-blocks.sh` holds those",
        "make": ["kest"],
        "tool": "tools/check-docs.sh",
        "arguments": ["docs/language.md", "docs/decisions.md"],
        "caught": "and there is no such file",
    },
]

failed = 0

# The tree's own objects come along with each copy, so that breaking one file
# rebuilds one file rather than sixteen, twenty-one times over. The `.d` files
# beside them are what makes that safe: the build wrote down what each object
# was made from, headers included, so an object older than any of those is
# made again. The tree is built first because objects behind the source they
# came from would make every copy neither one thing nor the other.
built = subprocess.run(["make", "-s", "-j4", "kest", "embed", "debug",
                        "embed-debug"],
                       capture_output=True, text=True)
if built.returncode != 0:
    print("the tree these are broken copies of does not build")
    sys.exit(1)

# Each hole is its own copy, its own build and its own run, and none of them
# reads anything another writes — so they are done at once rather than one
# after another. What is said about them is not: the answers are kept and
# printed in the order they are written above, because a list that reports
# itself in whatever order finished first is a list nobody can read twice.
def put_out_of_order(hole):
    """One hole, in a tree of its own. Answers what to say about it."""
    said = []
    work = tempfile.mkdtemp()
    try:
        # What nothing writes into is linked rather than copied: the same bytes
        # under another name, which costs a directory entry. What a build
        # writes into is copied, because a compiler opens its output and cuts
        # it short, and cutting a link short cuts the file this tree is made of
        # short with it.
        def bring(where, to):
            """The same bytes under another name where that is allowed."""
            try:
                os.link(where, to)
            except OSError:
                # Somewhere else on the machine, where a name cannot be a
                # second one for the same file. Then it is a copy, and what
                # this saves is nothing rather than everything.
                shutil.copy2(where, to)

        for what in ("src", "include", "lib", "tools", "docs"):
            shutil.copytree(what, os.path.join(work, what),
                            copy_function=bring)
        # The two hosts are making into this one, so they are made rather than
        # brought: making one where nothing is makes a file of its own.
        shutil.copytree("examples", os.path.join(work, "examples"),
                        copy_function=bring,
                        ignore=shutil.ignore_patterns("embed", "embed-debug"))
        for what in ("Makefile", "CLAUDE.md"):
            bring(what, os.path.join(work, what))
        # The times come with these: an archive that looks newer than the
        # objects in it is one nothing rebuilds.
        shutil.copy2("libkest.a", work)
        # And the objects, which are what a build writes. The sanitised ones
        # are nine megabytes and are only wanted by the holes that ask for a
        # sanitised build; the rest are made again from source when a hole is
        # about a file nothing else touches.
        wants = " ".join(hole.get("make", []))
        shutil.copytree("build/release", os.path.join(work, "build/release"))
        if "debug" in wants:
            shutil.copytree("build/debug", os.path.join(work, "build/debug"))

        # Written rather than cut short: what is under this name in the tree
        # this was linked from is the same file, and opening one to write is
        # opening the other.
        def instead(where, was, now):
            text = open(where).read()
            if was not in text:
                return False
            # What it was allowed to be goes with it. A new file is a new
            # file's rights, and a check put out of order and then run is a
            # check nothing may run: the first hole in a check that runs
            # itself refused with `Permission denied` and named a file nobody
            # had touched the rights of.
            mode = os.stat(where).st_mode
            os.remove(where)
            open(where, "w").write(text.replace(was, now, 1))
            os.chmod(where, mode)
            return True

        path = os.path.join(work, hole["file"])
        # What is read from the end is broken by what comes after it. A
        # worklog's last entry is whichever is last, so nothing written in the
        # one there today is there tomorrow and there is no line to quote; what
        # stays true is that another entry can be put after it. Written the
        # long way round for the same reason `instead` is: this file is a
        # second name for the one in the tree, and appending to it appends to
        # that.
        if "end" in hole:
            mode = os.stat(path).st_mode
            text = open(path).read()
            os.remove(path)
            open(path, "w").write(text + hole["end"])
            os.chmod(path, mode)
        elif not instead(path, hole["from"], hole["to"]):
            return ["%s: the code this expects to break has moved"
                    % hole["what"]], True

        # A break that takes two edits: a definition is not in a header and a
        # declaration is not in a file.
        if "also" in hole:
            second, was, now = hole["also"]
            if not instead(os.path.join(work, second), was, now):
                return ["%s: the code this expects to break has moved"
                        % hole["what"]], True

        if "program" in hole:
            program = os.path.join(work, hole["program"])
            open(program, "w").write(hole["source"])

        making = subprocess.run(["make", "-C", work, "-s"]
                               + hole.get("make", []),
                               capture_output=True, text=True)
        # Some of what this project holds itself to is held by the compiler:
        # a list with no `default` in it, a message whose words disagree with
        # the numbers put in them. What catches those is a build that stops,
        # so for those holes a tree that does not build is the catch and a
        # tree that does is the miss.
        if hole.get("in_build"):
            answered = making.stdout + making.stderr
            if making.returncode == 0:
                return ["MISSED: %s" % hole["what"],
                        "    the broken tree making"], True
            if hole["caught"] in answered:
                return ["caught: %s" % hole["what"]], False
            return ["MISSED: %s" % hole["what"],
                    "    the build stopped and did not say %s; it said %r"
                    % (hole["caught"], answered.strip()[-160:])], True
        if making.returncode != 0:
            said.append("%s: the broken tree does not build" % hole["what"])
            said.append("    " + making.stderr.strip().splitlines()[0])
            return said, True

        # Nothing on the standard input, the same as everything else that
        # runs a program here: a hole is a program that answers the same way
        # every time.
        #
        # And with a while to answer in. A hole breaks the tree on purpose, and
        # some of the ways a tree can be broken do not stop: a text comparison
        # written as a comparison of addresses makes a loop in the library that
        # walks for ever, and the run under it never comes back. Without this
        # the gate hangs with nothing said and nothing to say which of four
        # hundred holes it was in. What that is worth is a message; a hole that
        # needs longer than this is one nobody would wait for either.
        try:
            if "tool" in hole:
                ran = subprocess.run([os.path.join(work, hole["tool"])]
                                     + hole.get("arguments", []), cwd=work,
                                     capture_output=True, text=True,
                                     stdin=subprocess.DEVNULL,
                                     timeout=A_WHILE)
            elif "host" in hole:
                # The other host, which is the only thing here that lays its
                # own memory over what the compiler says a type is.
                ran = subprocess.run([os.path.join(work, hole["host"])],
                                     cwd=work, capture_output=True, text=True,
                                     stdin=subprocess.DEVNULL,
                                     timeout=A_WHILE)
            else:
                # Under the sanitisers when the hole is one only they can see.
                ran = subprocess.run(
                    [os.path.join(work, hole.get("binary", "kest")), "run",
                     os.path.join(work, hole["program"])],
                    capture_output=True, text=True,
                    stdin=subprocess.DEVNULL, timeout=A_WHILE)
        except subprocess.TimeoutExpired:
            return ["MISSED: %s" % hole["what"],
                    "    the broken tree never answered"], True
        answered = ran.stdout + ran.stderr
        if hole["caught"] in answered:
            # And it has to refuse as well as say so. What reads a check is a
            # shell reading a number: a check that says what is wrong and comes
            # back nought is a gate printing the complaint as though it were
            # what the check had to say for itself, in the same green as the
            # rest.
            if ran.returncode == 0:
                return ["MISSED: %s" % hole["what"],
                        "    said %s and came back nought" % hole["caught"]], True
            # And the first thing it says is what is wrong with the tree. A
            # reader with a failing gate reads the first few lines under the
            # name and nothing else, so a check that leads with a detail or
            # with what it did is a check whose answer is further down than
            # anybody looks.
            first = (answered.strip() or "\n").splitlines()[0]
            if first.startswith(" "):
                return ["MISSED: %s" % hole["what"],
                        "    said %r first, which is a detail" % first[:60]], True
            return ["caught: %s" % hole["what"]], False
        return ["MISSED: %s" % hole["what"],
                "    nothing said %s; it said %r"
                % (hole["caught"], answered.strip()[:120])], True
    finally:
        shutil.rmtree(work, ignore_errors=True)


# A worker a core. Starting the heavy ones — the holes that build under the
# sanitisers — before the rest was tried and changed nothing: what this waits
# for is the work itself and not the order it is begun in.
# What went wrong is said first and the list of what was caught after it. A
# hole that missed used to be the thirtieth line of thirty-three, which is
# past where anything reading this prints.
# What a hole says it breaks has to be one place. `instead` writes over the
# first of them, so a hole quoting a line the tree has twice breaks whichever
# was written first and reads as caught either way — while the other copy is
# held by nothing. Two were like that: the ceiling on how deep calls may nest
# is written once in front of a call by name and once in front of a call
# through a value, and the byte a piece of text holds is read once out of what
# a call gave back and once out of a name. See D439.
#
# Read here rather than where a hole is put out of order, because this is
# about the hole and not about the copy of the tree: what it says has to be
# true of this one.
for hole in BREAKS:
    for where in [hole["file"]] + ([hole["also"][0]] if "also" in hole else []):
        if not os.path.exists(where):
            print("%s: names `%s`, which is not there" % (hole["what"], where))
            failed = 1
    # A hole that writes after a file quotes nothing, so there is nothing here
    # to be written in two places.
    for where, was in ([(hole["file"], hole["from"])]
                       if "from" in hole else []) + (
                          [(hole["also"][0], hole["also"][1])]
                          if "also" in hole else []):
        if not os.path.exists(where):
            continue
        found = open(where).read().count(was)
        if found > 1:
            print("%s: quotes %u places in `%s`, and what it breaks is "
                  "whichever is written first" % (hole["what"], found, where))
            failed = 1

caught = []
with concurrent.futures.ThreadPoolExecutor(
        max_workers=os.cpu_count() or 1) as doing:
    for said, went_wrong in doing.map(put_out_of_order, BREAKS):
        if went_wrong:
            for line in said:
                print(line)
            failed = 1
        else:
            caught.extend(said)
for line in caught:
    print(line)

# Every check this project makes about its own work has a hole of its own. A
# sentence in `CLAUDE.md` says what each check holds and nothing can read a
# sentence; what can be read is whether the check has ever been seen catching
# anything, and a check with no hole never has.
tools_here = {os.path.basename(path) for path in glob.glob('tools/check-*.sh')}
tools_here.discard('check-backstops.sh')
broken = {os.path.basename(hole["tool"]) for hole in BREAKS if "tool" in hole}
for name in sorted(tools_here - broken):
    print("no hole is written for `%s`, so nothing has seen it catch anything"
          % name)
    failed = 1

if not failed:
    print("every backstop catches what it is for")
sys.exit(failed)
PY
