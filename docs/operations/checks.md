# Checks

What runs locally and in CI, what each part holds, and how to run one part on its own.

## Tiers

| Command | What it runs | Time on a desktop |
| --- | --- | --- |
| `make fast` | the build, every example run for its answer, the library resolving, the one form over every Kest file, a diagnostic in text and JSON, a fuel budget stopping a loop, the example host | about a minute |
| `make check` | everything below | several minutes |
| `make fuzz` | the seeded fuzzer, eight seeds of 400 inputs over each boundary, under the sanitizers | about a minute |

`make fast` is the loop while writing a change; `make check` is what passing means. On the shared server both go through the workspace's heavy-job wrapper.

## What `make check` holds

`tools/check.sh` builds the release, the sanitized build and the example hosts, then runs these, eight files at a time where a check sweeps many files, each in a scratch directory it removes:

- **examples** under the release build, the sanitized build, `KEST_PLAIN` and `KEST_NOOPT`, each answering 0 with the same output; every library module checking; a project built and tested.
- **formatting** (`check-fmt.sh`): every Kest file in the tree is in the one form; the formatter's output parses, means the same, is stable under a second pass, and leaves a file it cannot parse untouched.
- **commands** (`check-commands.sh`): every command over every file, each producing output of the right shape and valid JSON, and every diagnostic code a program can meet asked for by a program written here.
- **tables** (`check-tables.sh`): the lists that must stay in step (see [Development](development.md#source-conventions)), the module order in `tools/modules.txt`, and every diagnostic code being asked for by some check.
- **header** (`check-header.sh`): `kest.h` compiles on its own and a host calling every function links against the library alone.
- **declarations** (`check-dead.sh`): every declared function exists and is called from outside its file, and the example hosts reach every public door.
- **backend** (`check-c.sh`): every program compiled to C, built with the host compiler's strictest warnings, answers what the machine answers, and an untrusted machine never enters a compiled body.
- **lends** (`check-lends.sh`), **verifier** (`check-verifier.sh`), **doors** (`check-doors.sh`, `kest hostile` at eight seeds against the engine example under the sanitizers), **ceilings** (`check-ceilings.sh`, every limit met with its number), **work** (`check-work.sh`, compile work is a count, the same on every run), **costs** (`check-costs.sh`, library functions that build text stay linear, containers cost what they should a frame, a `no.alloc` frame allocates nothing), **wasm** (`check-wasm.sh`).
- **guide** (`check_guide.py`): every Kest block in `docs/user` checks with nothing to say, every block followed by its output runs and prints exactly that, and every C host on those pages builds.
- **threads**: four machines started at once and two machines of one build run on two threads under ThreadSanitizer.
- **reload**: every kind of edit a reload must answer, driven through the held doors, ends with either the new program's world or the old one untouched.
- **fuzzing**: a short seeded campaign and, where clang is present, the libFuzzer target.
- **room**: every check gave back the scratch it took, and the tree is unchanged by running the gate.

Each check prints one summary line when it passes; anything else is a failure with what broke.

## CI

`.github/workflows/ci.yml` runs on every pull request and on `main`:

| Job | Runs |
| --- | --- |
| `linux` | build, `make fast`, `make fuzz`, the conformance trace, and what the workload families cost |
| `linux-arm64` | the same on arm64 hardware, plus the C backend over every example |
| `macos` | build, `make fast`, the conformance trace, and the archive unpacked and used |
| `windows` | MSVC build, the conformance trace, the zip unpacked and a host built against it |
| `clang` | the build and `make fast` under clang |
| `threads` | four machines of one build at once under ThreadSanitizer |
| `package` | `make release`, the Linux archive unpacked and a host built against it |
| `check` | `make check` |
| `tetris`, `tetris-windows` | the raylib hosts built and played under Xvfb, and the colony run both ways; the Windows zip built |
| `agree` | the four conformance traces are identical |
| `required` | passes when every job above passed; the ruleset requires this and the pull request title check |

`pr-title.yml` checks the pull request title against the scopes in `AGENTS.md`. `determinism.yml` (run by hand, or when its files change) compares one simulation across languages and platforms; `pages.yml` publishes the playground.

## Fuzzing

`tools/fuzz.c` is deterministic from a seed: `tools/fuzz-debug <seed> <count> <file> <target>` replays exactly the same inputs on any machine, so a finding is a seed and a count. Targets: `source` (bytes and source assembled from language pieces), `handles`, `lends`, `refs`, `text`, `migrate` (a world carried between program versions) and `chunks` (compiled bytecode with one operand changed, handed to the verifier and run where it is accepted). `tools/fuzz-cover.c` is a libFuzzer target over the source boundary that keeps inputs reaching new code (`make tools/fuzz-cover`, then `tools/fuzz-cover corpus/ -max_total_time=60`). `tools/oss-fuzz-build.sh` builds it the way OSS-Fuzz does. Every input runs with ceilings on compile memory, work, heap and fuel; a finding is anything other than a program or a diagnostic. A finding becomes a program in the check of its area.
