#!/bin/sh
# This project checks its own work in places nobody looks: that a `no.alloc`
# promise is kept by the code emitted for it, that every chunk can be walked
# instruction by instruction, that no `return` gives back more than the
# declaration a host reads the width from, and that a header declares what is
# there and nothing nothing calls. Every one of them only fires when this
# project is wrong.
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
        for what in ("src", "include", "lib", "tools", "examples", "Makefile"):
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

        if "tool" in hole:
            ran = subprocess.run([os.path.join(work, hole["tool"])], cwd=work,
                                 capture_output=True, text=True)
        else:
            ran = subprocess.run(
                [os.path.join(work, "kest"), "run",
                 os.path.join(work, hole["program"])],
                capture_output=True, text=True)
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
