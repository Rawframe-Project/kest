# Overview

Kest is a compiler, a bytecode machine and a C backend in one C11 library with no dependency beyond the C library. This page is the map: how a program goes through the tree, what each module is for, and the constraints that hold across all of them. Each part has its own page.

## What the product never compromises on

1. **Embeddable.** One header (`include/kest.h`), one static library, libc only, no global mutable state except one atomic counter, no threads or allocator the host did not hand over. Everything a host sees is declared and versioned (`KEST_ABI_VERSION`).
2. **Promises are proved, not trusted.** `no.alloc`, `no.host` and `deterministic` are proved over the source and again over the emitted instructions; the verifier proves every chunk before it runs.
3. **The same bits everywhere.** Arithmetic is the language's, not the C compiler's; the four CI platforms and both engines are held to identical output.
4. **Hostile input is answered in words.** Every input, source or host call, ends as a program or as a diagnostic with a code. A crash, a hang or a silent wrong answer is a bug.
5. **Diagnostics are a feature.** Every error a program can meet has a stable code, a place and a fix where one is known, in text and JSON, and is exercised by a check.

## A program's way through

```
source files
   |  loader: reads the named file and every import (project, lexer, parser, ast)
   v
trees, one per file
   |  types: registers every declaration, resolves names, makes generic copies
   |  check: holds every body to the declarations
   |  contract: proves no.alloc, no.host, deterministic over the trees
   v
checked trees
   |  compile: writes each concrete function as IR
   v
IR (ir.h)
   |-- lower: IR -> bytecode for the machine (value.h holds the instruction set)
   |      |  verify: proves every chunk, and the promises again over the bytecode
   |      v
   |    vm: runs it, on the heap in ground
   |
   '-- emitc: IR -> one C translation unit for the release engine
```

`build` is all of the compiling stages as one object, which is what a host holds; `kest` is the C API over it. `held` keeps a machine and its world across frames and reloads. `lsp`, `debug`, `dap`, `doc` and `hostile` are the same build answering an editor, a debugger, a reader and a host testing its doors; `wire` is the framing and JSON those protocols share. `main` is the command line, which is a host like any other.

## Modules and their order

`tools/modules.txt` lists the modules of `src/` in dependency order, and `tools/check-tables.sh` holds the tree to it: a module includes only modules above it. The order is the pipeline above, with the lower layers first:

- `kest` is first because every module may use the public types; `mem` (arenas) and `ground` (the heap a running program stands on) are the memory layers; `diag` is how anything is said.
- the front end: `lexer`, `ast`, `parser`, `project`, `loader`. See [Front end](front-end.md).
- the middle: `types`, `check`, `contract`. See [Checking and promises](checking.md).
- `value` (the instruction set) and `verify` sit before the backends because the instruction table is shared by the compiler, the verifier and the machine. See [Verifier](verifier.md).
- `fmt`, then `ir`, `lower`, `emitc` and `compile`. See [Compiler](compiler.md) and [C backend](c-backend.md).
- `vm`, then `build` and `held`. See [Machine](machine.md), [Memory](memory.md) and [Host boundary](host-boundary.md).
- the tools: `wire`, `lsp`, `debug`, `dap`, `hostile`, `doc`, `main`. See [Tooling](tooling.md).

## Two arenas and a heap

Everything compiling makes lives in arenas (`mem`): bump allocators freed all at once, under one ceiling a host can set. Tokens and trees live in arenas of their own that are given back when the last stage reading them finishes, so a finished build holds roughly its source, its types and its compiled module. A running program's values live on a separate heap (`ground`) that can give individual places back. See [Memory](memory.md).

## Two engines, one meaning

The IR is the one statement of what a program means. The bytecode machine and the C backend both read it, and nothing either backend decides changes a result. CI runs every example on both and compares output byte for byte; see [C backend](c-backend.md).

## Constraints that cross modules

- **Determinism.** Every operation whose answer could differ between platforms is either defined by the language (integer wrapping, float conversion, shifts) or behind a host door. Nothing a program can observe comes from an address, a clock or iteration order of a hash table the program did not fill. See [Determinism](determinism.md).
- **Counts, not durations.** Budgets (fuel), compile work and every limit are counts, so a ceiling stops at the same place on every machine.
- **Lists that must stay in step.** Instruction names and opcodes, token names and kinds, the keyword table, the builtins known to the checker, the compiler and the promise proof, the commands `main` answers and the ones `help` prints: `tools/check-tables.sh` holds each pair to the other.
- **No global state.** A build and its machines own everything they use. The one process-wide value is the reference stamp counter, an atomic that only goes up, so two machines never hand out the same reference.
- **Untrusted source.** The compiler and the machine are written for source somebody else wrote: every stage is bounded by the build's memory ceiling and work count, and every chunk is verified. See [Sandbox](sandbox.md).

## Where things are tested

Each example in `examples/` and each module in `lib/std` checks itself and answers 0. `tools/check.sh` runs them under both engines, the sanitizers and WebAssembly, runs every command over every file, asks for every diagnostic code, and drives the fuzzers. See [Checks](../operations/checks.md).
