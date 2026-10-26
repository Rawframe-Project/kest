# Release builds

Kest has two engines for one language. The bytecode machine compiles a program in milliseconds and reloads it while a game runs. The release engine writes the same program as C for the C compiler a game ships with, which runs it several times faster. A program answers the same on both; CI runs every example both ways and compares.

## A single binary

```sh
kest build --release game.kest
./game
```

`kest build --release` writes the program as C, compiles it with the compiler `CC` names (or `cc`) against the `libkest.a` and `kest.h` found beside the command line, and writes one binary named after the program without `.kest`. The binary carries every file the program was read from, its modules, the standard library's and the project manifest, and builds from those in memory, so it runs where there is no source and no library. Every word after the binary's name on its command line belongs to the program. A release that cannot be made says why (`K0663`).

The binary carries readable source. A game that must not ship readable Kest packs the binary the way it packs its other content; there is no bytecode format to ship instead, by design (see [Versions and editions](editions.md#what-is-not-promised)).

## Inside an engine

An engine with its own `main` and its own doors uses the C directly:

```sh
kest emit --c rules.kest > rules.c
cc -O2 -Iinclude -DKEST_NO_MAIN -c rules.c -o rules.o
```

`rules.c` is one translation unit. With `-DKEST_NO_MAIN` it exports one function, `bool kest_natives_here(KestRuntime *runtime)`, which binds every compiled body to a machine the engine started. The engine builds the same program from source as usual (`kest_build`, `kest_start`), calls `kest_natives_here` on the machine, and from then on calls into compiled bodies through the same `kest_call` and the same frames. A compiled body that calls a door goes through the host's binding as the machine would.

A body the C backend cannot write is named in the file with the reason and stays with the machine, called from the compiled bodies through a door of its own; one such body costs that body and nothing above it. Over the examples and the standard library every body is written. The one thing it does not write today is a constant run of numbers containing an infinity.

`examples/colony` does this: `--native 1` binds its rules as C, and without it the machine runs them from source, which is what a reload needs. The engine can switch between them at a reload.

## Keeping the two engines alike

**Floating point.** The machine rounds a multiply and an add separately. The generated file asks the C compiler to do the same (no floating-point contraction), which GCC and MSVC respect. A compiler that fuses them anyway answers different bits from `deterministic` code; clang told `-ffp-contract=fast` ignores what the file asks for, so do not build release Kest with that flag.

**Promises.** The release engine keeps `no.alloc` and `no.host`. It spends no fuel budget, but it does hear `kest_cancel` at the back of every `while`, so a runaway loop in a release still stops when asked.

**Untrusted code.** A release is for code its author trusts: nothing proves the C a host compiler made the way the verifier proves bytecode. A machine started with `kest_start_untrusted` never enters a compiled body, even one the host linked in. See [Running untrusted code](untrusted-code.md).
