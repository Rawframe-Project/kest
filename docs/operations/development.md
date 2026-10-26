# Development

Setting up, building, running one check, and the conventions of the source.

## Toolchain

A C11 compiler and CMake 3.25 or newer; that is all the library and the command line need. The presets use Ninja on Linux and macOS and Visual Studio on Windows; `release` uses each platform's default generator, so a release runner needs nothing beyond CMake. The checks also use `python3`, and `clang`, `lld`, `wasi-libc`, clang's WebAssembly runtime library and Node for the WebAssembly build. The raylib games need raylib 5.5 built from source and X11 and OpenGL headers. The documentation site needs Python's `markdown` package. The CI image (`ghcr.io/rawframe-project/ci-cpp`) has every one of these.

## Builds

A preset configures a build into `out/<preset>`, and everything it makes lands there at a fixed path: `kest`, `kest-debug`, `libkest.a`, `examples/embed`, `tools/inward`, `bench/measure` and the rest, beside links to the standard library in `lib/std` and the header in `include` (copies on Windows). The checks find a build through `KEST_BUILD`, which is `out/dev` unless set.

| Preset | Builds |
| --- | --- |
| `dev` | this machine's compiler: the release build, the checking build and every host |
| `linux-gcc` | GCC 14, plus the ThreadSanitizer objects |
| `linux-clang` | Clang 20, plus the libFuzzer target and the WebAssembly playground |
| `linux-arm64` | GCC 14 on arm64, where every processor fuses a multiply and an add |
| `macos-clang` | Apple Clang |
| `windows-msvc` | MSVC through Visual Studio: the release build and the hosts |
| `release` | the release build with the sources, documentation and editor extension installed, for `cpack` |

| Target or option | Builds |
| --- | --- |
| `kest`, `libkest` | the command line and `libkest.a`, optimized |
| `kest-debug` (`KEST_CHECKED`, on except with MSVC) | the command line with the library's own checks and AddressSanitizer and UndefinedBehaviorSanitizer; it counts every instruction and is told where every host allocation ends. `embed-debug`, `engine-debug` and `fuzz-debug` are built the same way |
| `least`, `embed`, `engine` | the example hosts |
| `inward`, `measure`, `frame`, `fuzz` | the instruments and the seeded fuzzer |
| `KEST_RACES` | the library's objects for ThreadSanitizer, in `build/races` |
| `KEST_FUZZ` (Clang) | `tools/fuzz-cover`, the libFuzzer target |
| `KEST_WASM` | `kest.wasm` and the browser playground in `playground/` (`node tools/wasi-run.mjs out/<preset>/kest.wasm run examples/math.kest` runs it) |
| `KEST_RAYLIB=<raylib/src>` | `tetris` and `colony`, the raylib games |
| `time` | runs the four timing programs and prints one number each |
| `cmake --install`, `cpack` | install, and the release archive (see [Release](release.md)) |
| `python3 tools/build_site.py` | the documentation site in `_site/docs`, with the standard library reference, `llms.txt` and `llms-full.txt`; `pages.yml` publishes it beside the playground |

Header dependencies are tracked, so changing a header rebuilds every object that includes it in every build.

## Running things

```sh
cmake --preset dev && cmake --build --preset dev   # configure once, build after every change
out/dev/kest run examples/math.kest                # one program
out/dev/kest check --json examples/math.kest       # what the compiler says, for a tool
sh tools/check-verifier.sh                         # one check on its own
cmake --workflow --preset dev-fast                 # the loop: build and the fast tests
cmake --workflow --preset dev                      # everything: run before saying a change is done
ctest --preset dev -R regressions                  # one test of the gate
```

Every check script runs on its own from the root of the tree after a build, reading the build from `KEST_BUILD` (`out/dev` by default). On the shared development server, run the `dev` workflow, fuzzing and the benchmarks through the workspace's heavy-job wrapper.

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
- **A fixed defect:** a program that shows it, with a comment naming the defect, in `test/regress` (it checks itself and answers 0) or `test/refused` (its first line is `// Refused with K0000.` with the code); `tools/check-regress.sh` runs them.
