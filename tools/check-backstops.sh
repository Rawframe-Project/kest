#!/bin/sh
# This project checks its own work in places nobody looks: that a `no.alloc`
# promise is kept by the code emitted for it and by the body a value call
# enters, that every chunk can be walked instruction by instruction, that no
# `return` gives back more than the declaration a host reads the width from,
# that a handle is what the instruction following it thinks it is, that the
# formatter leaves a file it cannot read alone, that a header declares what
# is there and nothing nothing calls, that a message the reference quotes is one
# a run of this compiler says, that no module includes one below it, and that
# every check this project makes is one it runs. Every one of them only fires
# when this project is wrong.
#
# A net nobody has seen catch anything is indistinguishable from no net. So
# each one is put out of order on purpose, in a copy of the tree, and has to
# be caught. The copy is why this cannot leave the repository broken.
set -u
exec python3 - "$@" <<'PY'
import os
import shutil
import subprocess
import sys
import tempfile

# Each of these is a hole this project has actually had, or the exact shape of
# one. Nothing here is a mutation for its own sake. A hole names what to break
# and either a program to run, which is the compiler catching itself, or a tool
# to run, which is a check catching the tree.
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
        # The one that would hide all the others: a check that is written,
        # named, and never reached for. Nothing else in `make check` says a
        # word about a check that does not run.
        "what": "a check that is written and never run",
        "file": "tools/check.sh",
        "from": 'run "header" tools/check-header.sh\n',
        "to": '',
        "make": ["kest"],
        "tool": "tools/check-tables.sh",
        "caught": "is not run by",
    },
    {
        "what": "a formatter that writes what it only half read",
        "file": "src/main.c",
        "from": """        bool read = loaded && diags.error_count == 0;""",
        "to": """        bool read = loaded;""",
        "make": ["kest"],
        "tool": "tools/check-fmt.sh",
        "caught": "wrote over a file that does not parse",
    },
    {
        "what": "a header promising a function nobody wrote",
        "file": "src/loader.h",
        "from": """// Reads and parses one file and follows nothing.""",
        "to": """bool kest_never(KestArena *arena);

// Reads and parses one file and follows nothing.""",
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "is declared and is not there",
    },
    {
        "what": "a function in a header that nothing outside its file calls",
        "file": "src/loader.c",
        "from": """bool kest_load_alone(KestArena *arena, KestDiags *diags, const char *path,""",
        "to": """void kest_alone_here(void) {
}

bool kest_load_alone(KestArena *arena, KestDiags *diags, const char *path,""",
        "also": ("src/loader.h", """// Reads and parses one file and follows nothing.""",
                 """void kest_alone_here(void);

// Reads and parses one file and follows nothing."""),
        "make": ["kest", "embed"],
        "tool": "tools/check-dead.sh",
        "caught": "nothing outside",
    },
]

failed = 0
for hole in BREAKS:
    work = tempfile.mkdtemp()
    try:
        for what in ("src", "include", "lib", "tools", "examples", "docs",
                     "Makefile", "CLAUDE.md"):
            if os.path.isdir(what):
                shutil.copytree(what, os.path.join(work, what))
            else:
                shutil.copy(what, work)

        path = os.path.join(work, hole["file"])
        text = open(path).read()
        if hole["from"] not in text:
            print("%s: the code this expects to break has moved" % hole["what"])
            failed = 1
            continue
        open(path, "w").write(text.replace(hole["from"], hole["to"], 1))

        # A break that takes two edits: a definition is not in a header and a
        # declaration is not in a file.
        if "also" in hole:
            second, was, now = hole["also"]
            beside = os.path.join(work, second)
            text = open(beside).read()
            if was not in text:
                print("%s: the code this expects to break has moved"
                      % hole["what"])
                failed = 1
                continue
            open(beside, "w").write(text.replace(was, now, 1))

        if "program" in hole:
            program = os.path.join(work, hole["program"])
            open(program, "w").write(hole["source"])

        built = subprocess.run(["make", "-C", work, "-s", "-j4"]
                               + hole.get("make", []),
                               capture_output=True, text=True)
        if built.returncode != 0:
            print("%s: the broken tree does not build" % hole["what"])
            print("    " + built.stderr.strip().splitlines()[0])
            failed = 1
            continue

        # Nothing on the standard input, the same as everything else that
        # runs a program here: a hole is a program that answers the same way
        # every time.
        if "tool" in hole:
            ran = subprocess.run([os.path.join(work, hole["tool"])]
                                 + hole.get("arguments", []), cwd=work,
                                 capture_output=True, text=True,
                                 stdin=subprocess.DEVNULL)
        else:
            ran = subprocess.run(
                [os.path.join(work, "kest"), "run",
                 os.path.join(work, hole["program"])],
                capture_output=True, text=True, stdin=subprocess.DEVNULL)
        said = ran.stdout + ran.stderr
        if hole["caught"] in said:
            print("caught: %s" % hole["what"])
        else:
            print("MISSED: %s" % hole["what"])
            print("    nothing said %s; it said %r"
                  % (hole["caught"], said.strip()[:120]))
            failed = 1
    finally:
        shutil.rmtree(work, ignore_errors=True)

if not failed:
    print("every backstop catches what it is for")
sys.exit(failed)
PY
