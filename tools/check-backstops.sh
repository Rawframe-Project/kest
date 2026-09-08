#!/bin/sh
# The compiler holds itself to two things it cannot be trusted about: that a
# `no.alloc` promise is kept by the code that was emitted for it, and that
# every chunk can be walked instruction by instruction. Both are refusals
# nobody sees, because they only fire when the compiler is wrong.
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

# Each of these is a hole this compiler has actually had, or the exact shape
# of one. Nothing here is a mutation for its own sake.
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
]

failed = 0
for hole in BREAKS:
    work = tempfile.mkdtemp()
    try:
        for what in ("src", "include", "lib", "Makefile"):
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

        program = os.path.join(work, hole["program"])
        open(program, "w").write(hole["source"])

        built = subprocess.run(["make", "-C", work, "-s", "-j4"],
                               capture_output=True, text=True)
        if built.returncode != 0:
            print("%s: the broken compiler does not build" % hole["what"])
            print("    " + built.stderr.strip().splitlines()[0])
            failed = 1
            continue

        ran = subprocess.run([os.path.join(work, "kest"), "run", program],
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
    print("both backstops catch what they are for")
sys.exit(failed)
PY
