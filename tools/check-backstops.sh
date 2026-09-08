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
# write it, that a host keeps a promise made on its behalf, and that every
# function in the library is named by something that runs. Every one of them
# only fires when this project is wrong.
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
        # A command that prints nothing looks exactly like one that works.
        # The file it happens on is the file nothing in this tree is: one
        # that holds nothing at all.
        "what": "a command that answers a file with silence",
        "file": "src/types.c",
        "from": '        fputs("this file declares nothing\\n", out);\n',
        "to": '',
        "make": ["kest"],
        "tool": "tools/check-commands.sh",
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
        "caught": "comments changed",
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
                    !grow_store(rt->heap, store)) {
                    no_room(vmp, frame, instruction, rt);""",
        "to": """                if (store->used == store->capacity &&
                    !grow_store(rt->heap, store)) {
                    no_room(vmp, frame, instruction, rt);
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

for hole in BREAKS:
    work = tempfile.mkdtemp()
    try:
        for what in ("src", "include", "lib", "tools", "examples", "docs",
                     "Makefile", "CLAUDE.md", "build", "libkest.a"):
            if os.path.isdir(what):
                shutil.copytree(what, os.path.join(work, what))
            else:
                # The times come too: an archive that looks newer than the
                # objects in it is one nothing rebuilds.
                shutil.copy2(what, work)

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
