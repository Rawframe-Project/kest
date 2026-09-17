# What changed

Newest first, one section a version. This is for somebody who has a program or
a host written against an earlier one: what it says is what a reader has to do
about the change, not what was built — `docs/worklog.md` is that, and
`docs/decisions.md` is why.

What each number means and when it moves is in D983.

## Unreleased

Everything below is since 0.1.0 and none of it has been released. The version
number has not moved because nothing has been tagged; the ABI and schema
numbers below have, because a host built against the header in 0.1.0 and linked
against this library would be reading memory that means something else.

**Kest 0.1.0 → unreleased. ABI 1 → 2. JSON schema 1 → 2. Profile kest-det 1,
unchanged.**

### A program may have to change

- **A nought is a character.** `"a\0b"` was refused and is now three bytes of
  text that `len` counts as three. Nothing that compiled before stops
  compiling; what changes is that a program which relied on `\0` being refused
  no longer is. See D971.
- **Text is UTF-8 where it arrives.** `text(bytes)` refuses a run of bytes that
  is not UTF-8, under `K0604`, where it used to refuse only a nought. A program
  making text out of bytes it did not choose has a refusal to handle that it did
  not have. The message and the code are the same; the sentence is not. See
  D971.
- **A `scratch { }` block will not grow what outlives it.** `push`, `room` and
  `add` on an array or a store the block did not make are refused under
  `K0507`. They were accepted before and the heap went back underneath them
  when the block ended, taking the container with it — so a program that did
  this was losing a world quietly. See D972.

### A host may have to change

- **`KEST_ABI_VERSION` is 2 and `kest_abi_version()` reads it back.** Compare
  the two at startup: they differ when the header and the library are from two
  versions of this project. `examples/engine.c` does it in its first six lines.
  See D974.
- **Three doors were added**: `kest_abi_version`, `kest_profile`,
  `kest_build_capability`, `kest_count`, `kest_counted` and
  `kest_counted_entry`. Nothing was taken away and nothing changed shape, so a
  host built against ABI 1 and recompiled against this header works unchanged.
- **`kest_text` refuses bytes that are not UTF-8**, under `K0611`, where it used
  to refuse a nought. A host handing over bytes it did not choose has a refusal
  to handle. See D971.

### A tool may have to change

- **JSON schema 2.** Every function `kest check --json` writes now carries a
  `proved` object: what the promises' proof found about that body, and which
  promise it keeps and does not make. A reader of schema 1 that was told the
  list was everything has a field it does not know. See D976.

### New

- `kest lsp`, a language server that is this compiler (D977), and a VS Code
  extension that is a grammar and a client (D978).
- `kest profile`, which says what a run did in counts and no durations (D979).
- `kest new`, `kest build`, `kest test` and `kest doctor`, and a project
  manifest of `name value` lines (D982).
- `kest check --cost`, which says what the compiler proved about each body
  (D976).
- Windows x86-64, built and run in CI beside Linux, with the two held to saying
  the same thing byte for byte (D970).
- `bench/`, four workloads in this language and two others (D980).

## 0.1.0

The first version there was. Everything in `docs/worklog.md` up to it.
