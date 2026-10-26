# C backend

[`src/emitc.c`](../../src/emitc.c) writes the IR as C, which the host's compiler turns into the release engine. It is the second reader of the one IR: `lower` writes what the machine dispatches, and this writes what those instructions would have done, so the host's compiler can keep operands and loops in registers. The machine compiles in milliseconds and pays a dispatch per operation; this costs a C compile and pays nothing between operations. Neither replaces the other: the machine is what iteration and reloading want, the C is what a frame budget wants. User-facing use is in [Release builds](../user/release-builds.md).

## What a generated file is

One translation unit per program. It is both a translation and a host of that program:

- every body the backend can write, as a C function over a frame of `KestValue` slots;
- a table binding those bodies to a machine, exported as `kest_natives_here(KestRuntime *)`;
- with no `KEST_NO_MAIN`, a `main` that builds the program from source (or from files carried inside it, for `kest build --release`), binds the natives and runs `main`.

A body that holds something the backend has no C for is named in the file with the reason and left to the machine; compiled bodies call it through a door of its own, so one such body costs only itself. Over the examples and the library every body is written. The one known gap is a constant run of numbers containing an infinity, whose bits cannot be written in a C initializer; two checks keep a body like that on purpose so the door from compiled code into the machine stays exercised.

## Rules the generated C keeps

**Where a slot lives.** A slot is a C local (which the compiler may keep in a register) only when nothing about it can hold a pointer the collector must see and its address is never handed out. Otherwise it lives on the machine's stack, because the collector walks only the machine's stack for roots. Keeping every slot in both places, written through, was tried and bought nothing: what the reads saved, the writes cost.

**Handles in locals.** A handle may sit in a C local only in a body that cannot allocate, read off the declarations of everything it calls. A body that can allocate could be mid-collection while the local holds the only reference.

**Every operand is a named place**, so where a value sits must be the same along every path into an operation. A body whose paths leave different amounts on the stack is refused rather than guessed about. The one exception is the unreachable edge after the last arm of an exhaustive `match`, which is written as a stop.

**Floating point.** A multiply and an add stay two roundings. The file sets `#pragma STDC FP_CONTRACT OFF` for clang and `#pragma GCC optimize ("fp-contract=off")` for GCC; MSVC does not contract by default. Clang told `-ffp-contract=fast` ignores the pragma, and is the one build that answers other bits from `deterministic` code.

**Moving values.** Moving a value between memory and slots is written as a walk of the type at compile time, which a C compiler turns into individual loads and stores and sees through a tag `switch`. The machine instead loops over a layout's pieces for every element, which was a third of one benchmark's time.

**Promises and budgets.** Compiled bodies keep `no.alloc` and `no.host`. They spend no fuel, but they check the cancel flag at the back of every `while`, so a runaway loop in a release still stops when the host asks, without costing a frame anything measurable. A machine started with `kest_start_untrusted` never enters a compiled body; `tools/check-c.sh` counts the instructions the machine itself ran to prove it.

## Held to the machine

The two engines must answer the same thing. `tools/check-c.sh` compiles every example and every program it writes itself both ways, with the host's compiler under its strictest warnings, and requires the same output and status; CI runs the same comparison on every platform, and `determinism.kest` folds the profile's rules into one number that both engines must give. `KEST_CSAY=1` makes the backend print what it decided about each body, and `KEST_NATIVES` set in a generated program's environment makes it say how much of the run was compiled code.

## A release binary

`kest build --release` emits the file, writes the program's files (every module read, the library's modules and the manifest) into it as data, compiles it with `CC` (or `cc`) against the `libkest.a` and `kest.h` beside the command line, and the binary builds from those files through `kest_build_from` without touching the disk. It carries readable source by design; there is no bytecode format to ship instead.
