# Development

Setting up, building, running one check, and the conventions of the source.

## Toolchain

A C11 compiler and `make`; that is all the library and the command line need. The checks also use `python3`, `clang` (for the libFuzzer target and the WebAssembly build), `lld`, `wasi-libc` and clang's WebAssembly runtime library, and Node (to run the WebAssembly build). The raylib games need raylib 5.5 built from source and X11 and OpenGL headers.

On Windows the build is `tools\build.bat` from a Developer Command Prompt; the Makefile is for Linux and macOS.

## Builds

| Target | What it builds |
| --- | --- |
| `make` | `kest` and `libkest.a`, optimized |
| `make kest-debug` | the command line with the library's own checks and AddressSanitizer and UndefinedBehaviorSanitizer; it counts every instruction and is told where every host allocation ends |
| `make races` | objects built for ThreadSanitizer |
| `make kest.wasm` | the library and command line as WebAssembly (WASI); `node tools/wasi-run.mjs kest.wasm run examples/math.kest` runs it |
| `make least engine embed` | the example hosts; `embed-debug` and `engine-debug` are the sanitized ones |
| `make tetris RAYLIB=<raylib/src>` and `make colony RAYLIB=<raylib/src>` | the raylib games |
| `make playground` | the browser playground in `build/playground` |
| `make install PREFIX=...`, `make uninstall` | install and remove |
| `make release` | the release archive and its checksum, see [Release](release.md) |
| `make clean` | removes everything built |

Objects go in `build/release`, `build/debug`, `build/races` and `build/wasm`. Header dependencies are tracked, so changing a header rebuilds every object that includes it in every build.

## Running things

```sh
./kest run examples/math.kest           # one program
./kest check --json examples/math.kest  # what the compiler says, for a tool
sh tools/check-verifier.sh              # one check on its own
make fast                               # the loop: seconds
make check                              # everything: run before saying a change is done
```

Every check script runs on its own from the root of the tree after `make`. On the shared development server, run `make check`, fuzzing and the benchmarks through the workspace's heavy-job wrapper.

## Environment switches

| Variable | Effect |
| --- | --- |
| `KEST_LIB` | where the standard library is |
| `KEST_PLAIN=1` | no fused instructions and no carried bodies |
| `KEST_NOOPT=1` | no IR rewrites (written-out walks, numbers in joins) |
| `KEST_DEEP=1` | with `kest-debug`, report instruction counts and the machine's own questions |
| `KEST_WALK_EVERY=1` | collect at every allocation, to shake out a missing root |
| `KEST_REFUSE_ARENA`, `KEST_REFUSE_AT` | make an arena refuse at a chosen request, to walk out-of-memory paths |
| `KEST_IRSAY=1`, `KEST_CSAY=1` | print what the IR builder and the C backend decided per body |

## Source conventions

- C11 and libc, nothing else. `-Wall -Wextra -Wshadow -Wconversion -Werror` for the library; the hosts drop `-Wshadow`. `.clang-format` is the layout.
- A public function is `kest_` followed by snake case and is declared in `include/kest.h` or a module header; an internal function is plain snake case and `static`. `tools/check-dead.sh` holds every declared function to existing and being called from outside its own file, and every `kest_` name to being declared.
- A module includes only modules above it in `tools/modules.txt`.
- No global mutable state except the reference stamp counter.
- Every list that has to stay in step with another (instruction names, token names, keywords, builtins, commands and options, promise names, reach reasons) is held by `tools/check-tables.sh`. A `switch` over one of those enums has no `default`, so a new case is a compile error until it is handled.
- Every allocation can fail, and every failure ends in a diagnostic, never a crash. Every limit is a named constant with a diagnostic that says the number.
- A diagnostic code is never reused for another meaning. A new code needs a check that makes it happen.
- Comments say why, not what, and cite no record numbers.

## Adding things

- **A diagnostic:** pick the next code in the right family (see [Diagnostics](../user/diagnostics.md#codes)), give it a message that says what is wrong and a suggestion where the fix is known, and add the program that makes it happen to the check of its area (usually `tools/check-commands.sh`). `check-tables.sh` refuses a code no check asks for.
- **An instruction:** add it to the opcode enum in `value.h` and the table in `value.c` with its operands, stack effect and whether it reaches the heap or the host; the verifier, the promise proof, the disassembler and the C backend all read that table.
- **A builtin:** it must be known to the checker, the compiler and the promise proof's `REACHES` table, and `check-tables.sh` holds the three together.
- **A door of the C API:** declare it in `kest.h` with a comment saying when it refuses and what it writes into the report, call it from `examples/embed.c`, and bump `KEST_ABI_VERSION` if anything a host could already see changed.
- **A standard library function:** write it in Kest in `lib/std`, with the promises it can make, and in both widths where the module has both.
- **An example:** a program that checks itself and answers 0, under `examples/`, in the one form.
