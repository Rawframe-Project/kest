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
        "from": """            } else {
                report(checker, stmt->each.sequence->span, "K0317",
                       "`for` walks an array, text, a store or a set of bits, "
                       "found `%s`",
                       type_name(checker, sequence));
            }""",
        "to": """            } else {
                element = sequence;
            }""",
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
        "from": '        fprintf(stderr, "kest: out of memory\\n");',
        "to": "",
        "also": ("src/mem.c",
                 "KestArena *kest_arena_new(void) {\n"
                 "    KestArena *arena = calloc(1, sizeof(KestArena));",
                 "KestArena *kest_arena_new(void) {\n"
                 "    KestArena *arena = NULL;"),
        "make": ["kest"],
        "program": "unread.kest",
        "source": "fn main() -> i32 {\n    return 0\n}\n",
        "caught": "out of memory",
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
        # A heap a host said was all there is, spent without a word. The
        # ceiling is a host's number and the only thing that reads it is the
        # allocator, so a program that walks past it is a frame budget that was
        # never a budget.
        "what": "a heap ceiling nothing is held to",
        "file": "src/mem.c",
        "from": '''    if (arena->ceiling != 0 && arena->handed + taking > arena->ceiling) {
        arena->refused = taking;
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
        "from": "            if (rt->frame_count == rt->call_depth) {",
        "to": "            if (false) {",
        "make": ["kest"],
        "tool": "tools/check-ceilings.sh",
        "caught": "was not told what the machine has",
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
        return NULL;
    }
    if (fresh) {""",
        "to": """    if (arena->ceiling != 0 && arena->handed + taking > arena->ceiling) {
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
        # The two hosts are built into this one, so they are made rather than
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
            os.remove(where)
            open(where, "w").write(text.replace(was, now, 1))
            return True

        path = os.path.join(work, hole["file"])
        if not instead(path, hole["from"], hole["to"]):
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

        built = subprocess.run(["make", "-C", work, "-s"]
                               + hole.get("make", []),
                               capture_output=True, text=True)
        # Some of what this project holds itself to is held by the compiler:
        # a list with no `default` in it, a message whose words disagree with
        # the numbers put in them. What catches those is a build that stops,
        # so for those holes a tree that does not build is the catch and a
        # tree that does is the miss.
        if hole.get("in_build"):
            answered = built.stdout + built.stderr
            if built.returncode == 0:
                return ["MISSED: %s" % hole["what"],
                        "    the broken tree built"], True
            if hole["caught"] in answered:
                return ["caught: %s" % hole["what"]], False
            return ["MISSED: %s" % hole["what"],
                    "    the build stopped and did not say %s; it said %r"
                    % (hole["caught"], answered.strip()[-160:])], True
        if built.returncode != 0:
            said.append("%s: the broken tree does not build" % hole["what"])
            said.append("    " + built.stderr.strip().splitlines()[0])
            return said, True

        # Nothing on the standard input, the same as everything else that
        # runs a program here: a hole is a program that answers the same way
        # every time.
        if "tool" in hole:
            ran = subprocess.run([os.path.join(work, hole["tool"])]
                                 + hole.get("arguments", []), cwd=work,
                                 capture_output=True, text=True,
                                 stdin=subprocess.DEVNULL)
        elif "host" in hole:
            # The other host, which is the only thing here that lays its own
            # memory over what the compiler says a type is.
            ran = subprocess.run([os.path.join(work, hole["host"])], cwd=work,
                                 capture_output=True, text=True,
                                 stdin=subprocess.DEVNULL)
        else:
            # Under the sanitisers when the hole is one only they can see.
            ran = subprocess.run(
                [os.path.join(work, hole.get("binary", "kest")), "run",
                 os.path.join(work, hole["program"])],
                capture_output=True, text=True, stdin=subprocess.DEVNULL)
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
