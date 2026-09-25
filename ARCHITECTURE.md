# How Kest is built

A page on each part of the tree, in the order a program goes through them. The
rule that holds them together is that a module includes only what is above it
in `CLAUDE.md`'s pipeline, and `check-tables.sh` holds that. Why each part is
the way it is lives in `docs/decisions.md`; the numbers here are where to start
reading.

## A program's way through

A host -- the command line, an engine -- hands `build` a path or the text of
some files. The loader reads the named file and every file it imports; the
lexer and the parser make a tree of each. `types` registers every declaration,
`check` holds every body to them, and `contract` proves what each promises.
`compile` writes what the checked tree means as IR, once; `lower` writes that as
the machine's instructions and `emitc` as C for a release. `verify` proves the
instructions before a machine may run them, and `vm` runs them.

## The parts

**`mem`** is the arena every stage allocates from: one ceiling for a build's
bytes and for every arena taken under it (D843, D1247). **`diag`** is how
anything is said -- a code, a span, a sentence, a suggestion -- in text or
JSON, and the count of work a build may do (D1248).

**`lexer`**, **`ast`** and **`parser`** make the tree. There is one form for a
program, which **`fmt`** writes back from the tree (D398).

**`project`** reads `kest.project`; **`loader`** follows imports from it and
from the files.

**`types`** is what a type is and every name a program declares: structs,
enums, the language's vectors (D1250), copies of generics (D778), and the
retired names that say what replaced them (D1252). **`check`** holds each body
to its declarations and is where most of what a program is told comes from;
`x.f(a)` (D1256) and blocks (D1257) are settled here. **`contract`** proves
`no.alloc`, `no.host` and `deterministic` by walking what each body calls.

**`compile`** writes IR (**`ir`**): values, places and operations, with a
function that takes a block written into each place it is called (D1257).
**`lower`** makes bytecode of it, fusing what the machine does in one step;
**`emitc`** makes C of the same IR for the release engine (D1093).

**`value`** is the instruction set, one table saying what each instruction
takes and gives (D1237, D1239). **`verify`** proves every chunk before it runs:
every operand names something the program has, every jump lands on an
instruction, the stack is one depth on every path, and every slot is read as
what it holds (D1237-D1245). **`vm`** runs what was proved; **`ground`** is the
heap under it, handing places out and taking them back (D996).

**`build`** is the stages as one thing, which is what a host holds;
**`kest`** is the C API in `include/kest.h` (D1046).
**`held`** keeps a machine across frames and reloads a program under a running
world (D1151).

**`wire`**, **`lsp`**, **`debug`** and **`dap`** are the same build answering
an editor and a debugger. **`hostile`** writes a file's doors called with
anything a program could hand them, for a host to ask its own (D1254).
**`doc`** writes what a file declares with the comment above each, for
somebody who will call it (D1258). **`main`** is the command line.

## What holds it

`tools/check.sh` is the gate: every example run both ways, every refusal a
program can meet, every ceiling, every table held to the code it describes,
the verifier refusing what it must, the fuzzers, and `check-backstops.sh`
putting each check out of order to see it catch something. `make fast`, `make
most` and `make check` are the three tiers; CI runs the whole gate on every
push.
