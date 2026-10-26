# Compiler

`compile` writes each checked function as IR, `lower` writes IR as bytecode, and `value` holds the instruction set both the verifier and the machine read. `emitc` is the other reader of the IR; see [C backend](c-backend.md).

## IR

[`src/ir.h`](../../src/ir.h) is the one statement of what a program means, so that more than one backend reads the same answer. The bytecode lowering and the C backend both read it, and so does any question about where a value came from, so none of them resolves names again or walks the checked tree for meaning.

A body is one concrete function: a generic has one body per set of types. It is a list of operations, each reading the values on top and leaving one; values are consumed innermost first, in the order a walk of the tree makes them, which is what lets a stack backend need no scratch slots. Three things it is not:

- not SSA: a name is a place, and a place is written to;
- not a second checker: everything in it was decided already, and nothing in it refuses a program;
- not a tree: control flow is a branch naming where it lands.

Every place a value lives is written as what reaches it (a local's slots, a field of a field, an element of an array, a place in host memory through an address) rather than an address already worked out, so a backend chooses how to reach it. Names are kept beside the slots they own, for the debugger and anything else asking a body about itself.

[`src/compile.c`](../../src/compile.c) writes the bodies. What the checker allowed and the compiler cannot write is reported as a fault in the compiler, never written as something that means something else. The compile-time limits (names in a function, nested loops, `break`s, `defer`s, `scratch` nesting, the 65535-byte jump distance, 65536 doors) are defined here in one place, each a diagnostic with the number in it.

### What compiling decides

- **Constants are folded where they are declared**, once, and every use reads the result; a constant costs one instruction where it is used. The folder also works out arithmetic on constants in bodies (`0 - 1`, `x * 4` with `x` a constant) and a hash of literal text. `emit --json` reports per function how many values were folded into its chunk.
- **Generic copies** are compiled from one body per set of types, and `emit --json` counts the copies and their bytes.
- **Functions that take blocks** are compiled into each call site, and the block into each place it is called.
- **Small range walks are written out.** A `for` over a range between two numbers written where it stands, of four turns or fewer, with a small body that does not assign to the count, is compiled as the body once per turn with the count as a constant in each. `for way in 0..4` over four neighbours costs no backward jumps and no fuel.
- **A walk's name is the count** when nothing in the body assigns to it, which saves a copy per turn; when something does, the walk keeps its own count and the write only lasts the turn.

## Lowering

[`src/lower.c`](../../src/lower.c) reads one body at a time and decides only which instructions, how wide a jump is, and which pairs of instructions are worth writing as one. It decides nothing about meaning.

**Fused instructions.** A stack machine pays a dispatch per instruction, so the commonest sequences are written as one: a load followed by a constant (`x + 1`, `i < n`), two loads of adjacent slots as one wide load (a struct built from locals), a comparison and the jump that reads it, arithmetic and the store that takes its answer, an index and the store or field read after it (one bounds check instead of an address round trip), a local or a constant as the right side of arithmetic. Each fusion is a peephole over the last instruction written, refused when anything points between the two.

**Carried bodies.** A small function is written into its caller at the call instead of called: its instructions with its slots moved above the caller's, its constants added to the caller's, the arguments stored where a call would have put them, and every `return` a jump to the end. Only bodies that cannot fault and spend no budget are carried (arithmetic that wraps, comparisons, constants, locals and forward jumps), so no error is ever reported from inside a body that has no frame of its own and fuel still bounds what it bounded. A frame step that calls two small helpers costs the same as one with the helpers written out by hand.

**Holding optimizations to meaning.** `KEST_PLAIN=1` turns off what lowering does (fusions and carried bodies) and `KEST_NOOPT=1` turns off what compiling does to a body (written-out walks and the other IR rewrites). The gate runs every example with each switch and requires the same output, and the fuzzer compares the two, which is how every transformation is held to keeping a program's meaning.

## The instruction set

[`src/value.c`](../../src/value.c) holds one table of every instruction: its name, its operands and widths, what it takes from and gives to the operand stack, and whether it reaches the heap or the host. The verifier, the disassembler (`kest emit`), the second promise proof and the machine all read it, and `check-tables.sh` holds the names to the opcode enum in `value.h`. An instruction added without saying whether it allocates stops the build, because the proof asks the table and a missing answer is an error.

There are 223 instructions. Operands are one or two bytes; jumps carry a two-byte forward distance, which is the 65535-byte limit. Each chunk keeps, beside every byte of code, four bytes saying where in the source it came from, which is what a runtime error's line and caret are read from.

## Measuring the compiler

Every command reports `cost` (bytes compiling asked for), `held` (bytes still held when done), `working` (memory the backends and verifier used and gave back), `askings` (how many times the arena was asked) and `work` (units of work, which `--work` bounds). Tokens and trees are given back where the last stage reading them ends, so a finished build holds about half of what compiling asked for. A program of 600 modules (165 KB of source) compiles in under 100 ms; most of the cost of a small program is the standard library it imports, since an import compiles the whole module.

Nothing is carried between builds: a reload compiles everything again. That was measured to be fast enough that a cache would not be worth a format.
