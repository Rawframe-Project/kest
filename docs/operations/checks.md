# Checks

What runs locally and in CI, what each part holds, and how to run one part on its own.

## Tiers

Every check is a ctest test of the build it runs against, labeled with its tier (`cmake/Tests.cmake`).

| Command | What it runs | Time on a desktop |
| --- | --- | --- |
| `cmake --workflow --preset dev-fast` | the build, then the `fast` label: every example run for its answer, the library resolving, the one form over every Kest file, a diagnostic in text and JSON, a fuel budget stopping a loop, the example host, the guide, the regression programs and the conformance trace | about a minute |
| `cmake --workflow --preset dev` | the build, then every test below | several minutes |
| `ctest --preset dev -R fuzzing` | the seeded fuzzer, eight seeds of 400 inputs over each boundary, under the sanitizers | under a minute |

`dev-fast` is the loop while writing a change; `dev` is what passing means. On the shared server both go through the workspace's heavy-job wrapper. Each check also runs on its own from the root of the tree (`sh tools/check-c.sh ...`), against `out/dev` or the build `KEST_BUILD` names.

## What the gate holds

The test `gate` (`tools/check.sh`) runs the examples, threads, reload and room steps over the build; every other line is a test of its own, run through `tools/ask.sh`. A check that sweeps many files runs eight at a time, each in a scratch directory it removes:

- **examples** under the release build, the sanitized build, `KEST_PLAIN` and `KEST_NOOPT`, each answering 0 with the same output; every library module checking; a project built and tested.
- **formatting** (`check-fmt.sh`): every Kest file in the tree is in the one form; the formatter's output parses, means the same, is stable under a second pass, and leaves a file it cannot parse untouched.
- **commands** (`check-commands.sh`): every command over every file, each producing output of the right shape and valid JSON, and every diagnostic code a program can meet asked for by a program written here.
- **tables** (`check-tables.sh`): the lists that must stay in step (see [Development](development.md#source-conventions)), the module order in `tools/modules.txt`, and every diagnostic code being asked for by some check.
- **header** (`check-header.sh`): `kest.h` compiles on its own and a host calling every function links against the library alone.
- **declarations** (`check-dead.sh`): every declared function exists and is called from outside its file, and the example hosts reach every public door.
- **backend** (`check-c.sh`): every program compiled to C, built with the host compiler's strictest warnings, answers what the machine answers, and an untrusted machine never enters a compiled body.
- **regressions** (`check-regress.sh`): every program in `test/regress` (a defect that once escaped, each naming it) checks with nothing to say and answers 0 with the same output under the release build, the checking build, the heap walked at every allocation, `KEST_PLAIN`, `KEST_NOOPT` and compiled to C; every program in `test/refused` is refused under both builds with the code its first line names.
- **lends** (`check-lends.sh`), **verifier** (`check-verifier.sh`), **doors** (`check-doors.sh`, `kest hostile` at eight seeds against the engine example under the sanitizers), **ceilings** (`check-ceilings.sh`, every limit met with its number), **work** (`check-work.sh`, compile work is a count, the same on every run), **costs** (`check-costs.sh`, library functions that build text stay linear, containers cost what they should a frame, a `no.alloc` frame allocates nothing), **wasm** (`check-wasm.sh`).
- **guide** (`check_guide.py`): every Kest block in `docs/user` checks with nothing to say, every block followed by its output runs and prints exactly that, and every C host on those pages builds.
- **threads**: four machines started at once and two machines of one build run on two threads under ThreadSanitizer.
- **reload**: every kind of edit a reload must answer, driven through the held doors, ends with either the new program's world or the old one untouched.
- **fuzzing** (`check-fuzz.sh`): a short seeded campaign over every boundary, and the source boundary folding to the same answers with the fusions and the optimizer turned off.
- **conformance** (`conformance.sh`): every example's output and answer, byte for byte the same as `test/conformance.trace`, on every platform.
- **room**: every check gave back the scratch it took, and the tree is unchanged by running the gate.

Each check prints one summary line when it passes; anything else is a failure with what broke.

## CI

`.github/workflows/ci.yml` runs on every pull request, on every push to `main`, and every night at 00:07 UTC. A new push to a pull request cancels its running CI; runs on `main` are never cancelled.

| Job | Runs |
| --- | --- |
| `changes` | decides whether anything beyond documentation changed; the jobs below it skip when nothing did. `docs/user` counts as code, since `check_guide.py` runs its programs |
| `zizmor` | audits every workflow |
| `site` | builds the documentation site with `tools/build_site.py`, in the CI image |
| `fuzz` | the libFuzzer target through `tools/fuzz_target.sh`: a minute on a pull request, from the committed programs only; ten minutes on `main` and at night, from the kept corpus, which is then minimized and saved from `main` |
| `build` | one cell per CMake preset through the organization's `cpp.yml`, warnings as errors, each running its test preset: `linux-gcc` every test but the C backend, the doors, fuzzing and WebAssembly, with ThreadSanitizer; `linux-clang` the fast tests, the C backend, the doors, fuzzing and WebAssembly; `linux-arm64` the fast tests, the regression programs and the C backend, where every processor fuses a multiply and an add; `macos-clang` the fast tests; `windows-msvc` the conformance trace |
| `build-main` | on `main` and at night only: the seeded fuzzer under the checking build on arm64 (`linux-arm64-fuzz`), which takes ten minutes on that runner |
| `package` | the release archive from the `release` preset on Linux, macOS and Windows, unpacked elsewhere, run, and a host built against it |
| `games`, `tetris-windows` | the raylib hosts built and played under Xvfb, and the colony run both ways; the Windows zip built |
| `required` | passes when every job above passed or was skipped by `changes`; the ruleset requires this and the pull request title check |

Every workflow sets `permissions: {}` and grants each job what it needs, runs its steps in bash unless a step names another shell (`scorecard.yml` sets no default shell, which Scorecard's publishing rejects), checks out without keeping credentials, and pins every action by commit SHA; Dependabot moves the pins weekly.

Other workflows:

- `pr-title.yml` checks the pull request title against the scopes in `AGENTS.md`.
- `pr-gate.yml` closes a pull request from outside the organization unless it closes an issue labeled `accepted`.
- `codeql.yml` analyzes the C sources on every pull request, on `main` and weekly.
- `scorecard.yml` grades the repository's supply chain weekly for the README badge.
- `release.yml` keeps the release pull request and builds a release's files, see [Release](release.md).
- `pages.yml` publishes the playground and the documentation site from `main`.
- `determinism.yml` (run by hand, or when its files change) compares one simulation across languages and platforms.

## Fuzzing

`tools/fuzz.c` is deterministic from a seed: `out/dev/tools/fuzz-debug <seed> <count> <file> <target>` replays exactly the same inputs on any machine, so a finding is a seed and a count. Targets: `source` (bytes and source assembled from language pieces), `handles`, `lends`, `refs`, `text`, `migrate` (a world carried between program versions) and `chunks` (compiled bytecode with one operand changed, handed to the verifier and run where it is accepted). `tools/fuzz-cover.c` is a libFuzzer target over the source boundary that keeps inputs reaching new code (the `fuzz-cover` target of the `linux-clang` preset, then `out/linux-clang/tools/fuzz-cover corpus/ -max_total_time=60`). `tools/fuzz_target.sh` is how CI runs it: with a fresh seed it does not print, writing what it finds and its log under `out/`, and saying only which kind of finding it was and the sanitizer's report, never the input. `tools/oss-fuzz-build.sh` builds it the way OSS-Fuzz does. Every input runs with ceilings on compile memory, work, heap and fuel; a finding is anything other than a program or a diagnostic. A finding becomes a program in `test/regress` or `test/refused`.
