# Documentation

## Using Kest

- [Getting started](user/getting-started.md): building, the first program, running and checking it.
- [Language basics](user/language-basics.md): files, names, numbers, operators and control flow.
- [Data](user/data.md): structs, arrays, stores and references, enums, flags, optionals and vectors.
- [Text](user/text.md): strings, building text, walking it, and where Unicode stops.
- [Functions](user/functions.md): overloading, generics, function values, blocks and bodies that wait.
- [Promises](user/promises.md): `no.alloc`, `no.host` and `deterministic`, and how the compiler proves them.
- [Determinism](user/determinism.md): the profile, and what makes a program compute the same world everywhere.
- [Standard library](user/standard-library.md), and the [reference](https://rawframe-project.github.io/kest/docs/std.html) `kest doc` writes for every module.
- [Embedding](user/embedding.md): a host, the C API, doors and machines.
- [Host memory](user/host-memory.md): lending, worlds across frames, and reloading a running game.
- [Running untrusted code](user/untrusted-code.md): ceilings, budgets and what a host must keep for itself.
- [Release builds](user/release-builds.md): the C backend and shipping one binary.
- [Command line](user/command-line.md) and [Diagnostics](user/diagnostics.md).
- [Editions and versions](user/editions.md): what each version number promises.
- [Coming from Lua or Rust](user/coming-from-lua-or-rust.md).
- [Examples](user/examples.md): what each program under `examples/` shows.

## How it works

- [Overview](internals/overview.md): the pipeline and how the parts fit.
- [Glossary](internals/glossary.md).
- [Language design](internals/language-design.md): the rules the language is built on and what was rejected.
- [Front end](internals/front-end.md), [Checking](internals/checking.md), [Compiler](internals/compiler.md), [Verifier](internals/verifier.md), [Machine](internals/machine.md), [Memory](internals/memory.md), [C backend](internals/c-backend.md).
- [Host boundary](internals/host-boundary.md), [Sandbox](internals/sandbox.md), [Determinism](internals/determinism.md).
- [Tooling](internals/tooling.md): diagnostics, JSON, the language server, the debugger and `kest doc`.
- [Performance](internals/performance.md): where Kest stands, what dominates, and what was measured and rejected.

## Maintaining it

- [Development](operations/development.md): toolchain, builds, switches and source conventions.
- [Checks](operations/checks.md): the local tiers, what the gate holds, CI and fuzzing.
- [Debugging](operations/debugging.md): the checking build, switches and fuzz findings.
- [Benchmarks](operations/benchmarks.md): taking the numbers in `bench/` again.
- [Release](operations/release.md).
