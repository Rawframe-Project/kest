# Kest

Kest is a statically typed language for the gameplay half of a game: a compiler, a bytecode machine and a C backend in one C11 library with nothing beneath it but libc. A native engine embeds it through `include/kest.h`; `kest` is the command line, one host of the library among several. [docs/internals/overview.md](docs/internals/overview.md) is the map of the pipeline, and [docs/README.md](docs/README.md) indexes the rest.

It never compromises on:

1. **Every input ends in an answer or a diagnostic.** Source, bytecode, handles and lends from a host: none of them crashes, hangs or reads memory it should not, in any build. Every allocation can fail, and failing is a diagnostic.
2. **Promises are proved, not trusted.** `no.alloc`, `no.host` and `deterministic` hold over every body a function reaches, in both engines.
3. **The same bits everywhere.** A deterministic program prints the same bytes on Linux x86-64 and arm64, macOS arm64 and Windows x86-64, interpreted and compiled to C.
4. **Fast inside a frame.** Cost is visible: a frame step is counted in instructions, and a change that adds to the count of frame code needs a reason.
5. **Libc and nothing else.** No dependency, no global mutable state, no allocator or thread the host did not hand over.

## Commands

```sh
make                        # kest and libkest.a, optimized
make fast                   # the loop while writing a change (about a minute)
make check                  # the whole gate: what "it passes" means (several minutes)
make fuzz                   # the seeded fuzzer, short, sanitized
sh tools/check-verifier.sh  # any one check, from the root after make
python3 tools/check_guide.py   # every example in docs/user checked and run
make kest-debug && ./kest-debug run file.kest   # the checking, sanitized build
```

Run `make check`, `make fuzz`, long fuzzing and every benchmark through the workspace's heavy-job wrapper on the shared machine (`/home/rawframe-project/tools/heavy.sh make check`). The sweeps already run eight files at a time; do not raise that. [docs/operations/checks.md](docs/operations/checks.md) says what each check holds and what each CI job runs.

## Ways to hurt yourself

- **Killing by pattern.** Never `pkill -f`, `pgrep | kill` or `kill` a PID found by matching a name or path: other sessions build, fuzz and run hosts named `kest`, `fuzz` and `engine` on the same machine. Kill only a PID you started.
- **Endless fuzzing.** `tools/fuzz-cover` is libFuzzer and runs until it finds something. Always pass `-max_total_time`, and stop what you started. Never print a seed's input or upload a corpus or crash from CI.
- **Running the games headless and forgetting them.** `tetris` and `colony` under Xvfb take `--frames`; always pass it.
- **Files a run writes.** `examples/settlement.kest` writes `kest-settlement-day.txt` in the current directory. Every check works in a scratch directory under `TMPDIR` and gives it back; the gate's `room` and `tree` steps fail when anything is left behind or a built file is committed.
- **Two copies held to each other.** `editors/vscode/LICENSE` is the same bytes as `LICENSE`. `bench/hosts/tetromino.lua` is a third party's file kept unchanged under its own license.
- **Benchmark results.** `bench/*.tsv` and `bench/chart-*.svg` are regenerated only when a release changes them, on a quiet machine; never commit a sitting taken on a busy one.

## Invariants

- **Modules.** One module is a `.c` and `.h` pair in `src/`. `tools/modules.txt` is the dependency order: a module includes only modules above it. A new module goes there.
- **Lists in step.** Token names, instruction names, keywords, builtins, promise names, commands and options, reach reasons, the gate's steps: `tools/check-tables.sh` holds each to the others it must match. A `switch` over one of these enums has no `default`, so a new case is a compile error until every switch handles it; a table indexed by an enum has a `_Static_assert` on its count.
- **Instructions.** An instruction is one row in `src/value.c` (operands, stack effect, whether it reaches the heap or the host). The verifier, the promise proof, the disassembler and the C backend read that row; never special-case an opcode elsewhere.
- **Diagnostics.** Every diagnostic has a stable code (`K01xx` lexer, `K02xx` parser, `K03xx` types and bodies, `K04xx` promises, `K05xx` what the compiler cannot emit, `K06xx` running and the command line, `K07xx` reading). A code is never reused for another meaning, and every code is asked for by a check that makes it happen; `check-tables.sh` refuses one nothing asks for. A compiler fault is said only through `kest_diags_fault`. Never stop at the first error.
- **Limits.** Every limit is a named constant with a diagnostic that says the number; `tools/check-ceilings.sh` reaches each one.
- **Versions.** In `include/kest.h`: `KEST_ABI_VERSION` moves when anything a compiled host could see changes; `KEST_JSON_SCHEMA` when a JSON field changes meaning, is removed, or is added to a list a reader was told was complete; `KEST_PROFILE_VERSION` when anything a deterministic program observes changes; `KEST_EDITION` when the language changes in a way an existing program notices. Each move is named in the changelog. The library version is changed only by the release process.
- **Determinism.** Arithmetic, NaN spelling, hashing and iteration order are the language's, implemented once and shared by the folder, the machine and the C backend. Mathematics is Kest in `lib/std/fdlibm.kest`; only `sqrt`, `floor` and `ceil` are host doors. Nothing a program reads comes from an address, a clock or the environment.
- **No global mutable state** except the process-wide reference stamp counter. Every module hangs off a context the caller owns.
- **Public API.** A host includes `include/kest.h` and nothing else; it compiles as strict C11 on its own (`tools/check-header.sh`). Every door says when it refuses and what it writes into the report, is called from `examples/embed.c`, and `tools/check-dead.sh` holds every declared function to existing and being called from outside its file.
- **Workflows.** Every action is pinned by commit SHA with its version in a comment, and each workflow and job declares the least `permissions` it needs. The `required` job in `ci.yml` needs every other job; a new job is added to its `needs`.

## C conventions

C11, `-Wall -Wextra -Wshadow -Wconversion -Werror` for the library; `.clang-format` is the layout.

| Thing | Style | Example |
| --- | --- | --- |
| Public function | `kest_` and snake case, declared in a header | `kest_build`, `kest_lexer_next` |
| Internal function | `static`, plain snake case | `scan_string` |
| Type | `Kest` and PascalCase | `KestToken`, `KestRuntime` |
| Enum constant, macro | `KEST_` and screaming snake case | `KEST_TOK_IDENT`, `KEST_MOST_PLACES` |
| Struct field | snake case | `line_start` |
| File | lower case, no underscores | `lexer.c` |

- Braces always. Declare a variable at its first use. `size_t` for sizes and counts; fixed-width types when the width is part of a format.
- A function that can fail returns a status or a tagged result, never a sentinel a caller can forget.
- No `assert` in place of a diagnostic: an assert is for a compiler bug, a diagnostic for a program's mistake.
- Compile-time memory lives in an arena and is freed in one call; no per-node `free`.
- Comments say why, never what. Public API is documented above its declaration in the header. No banners, no commented-out code, no `TODO` without a reason.

## Kest conventions

- A file's `module` line matches its path; `lib/std/<name>.kest` is `std.<name>`.
- Every Kest file in the tree is in the one form: run `./kest fmt -w` on what you touched. `make fast` refuses anything else.
- Every Kest file compiles without warnings.
- A keyword is added only when a program using the word as a name would be ambiguous; everything else is a word that means something only where it stands.
- A standard library function is written in Kest, with every promise it can keep, and in both widths where its module has both.

## Testing

Follow the [repository standard's Tests section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#tests); in this repository:

- An example under `examples/` checks its own answers and returns 0, or the number of the check that failed. `make check` runs every example under the release build, the sanitized build, the heap walked at every allocation, `KEST_PLAIN=1` and `KEST_NOOPT=1`, and compiled to C, and holds every run to the same output.
- A defect gets a program that shows it: an example, a case in the check of its area, or a program a check writes on the spot. Do not add checks that break the tree on purpose to see another check catch it.
- A new diagnostic needs the program that makes it happen, in the check of its area (usually `tools/check-commands.sh`).
- A fuzz finding is a seed, a count and a target; reduce the input and add it as a program to the check of its area.
- Every `kest` block in `docs/user` checks with nothing to say; a block followed by a `text` block runs and prints exactly that; every C host on those pages builds. `tools/check_guide.py` enforces it.
- An optimization shows in an instruction count (`KEST_DEEP=1` under `kest-debug`) or a benchmark, and changes no output under any switch.

## Documentation

Follow the [repository standard's Documentation section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#documentation). A change in what a program or a host may write or rely on updates its page of `docs/user/` in the same pull request, and a change in how a part works updates its page of `docs/internals/`. Nothing is recorded anywhere else: no decision log, no worklog, no numbered decisions in comments.

## Pull requests

Titles are conventional commits. Scopes: `lexer`, `parser`, `checker`, `promises`, `compiler`, `verifier`, `vm`, `heap`, `backend`, `api`, `reload`, `cli`, `fmt`, `lsp`, `debugger`, `doc`, `std`, `examples`, `playground`, `editors`, `bench`, `fuzz`, `build`, `tools`. Example: `fix(verifier): refuse a jump into the middle of an instruction`.

A change any existing program or host can notice says so in the body and names the version number it moves.
