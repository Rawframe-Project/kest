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

**Kest 0.1.0 → unreleased. ABI 1 → 3. JSON schema 1 → 2. Profile kest-det 1,
unchanged.**

### A program may have to change

- **The deterministic profile answers a different number.** It is
  `2470919380724047420`, where it was `3909859238992895122`. Nothing about how a
  program runs changed: the conformance corpus grew a ninth part covering a
  nought with a sign on it, how far a number goes before it is nought, and a
  thing that is not a number — three things a platform can be wrong about while
  agreeing about every arithmetic rule. The profile number itself is still
  `kest-det 1`, because what `deterministic` promises did not change; what
  changed is how much of it is checked. A host that wrote the old number down
  beside a replay writes the new one. See D995.

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
- **Seventeen doors were added** and none was taken away: `kest_abi_version`
  and `kest_profile` for what shape things are in, `kest_build_capability` for
  what a program may do, `kest_count`, `kest_counted` and `kest_counted_entry`
  for what a run did, and `kest_stopped`, `kest_stopped_in`, `kest_resume`,
  `kest_code_of`, `kest_came_from`, `kest_frames_deep`, `kest_frame_in`,
  `kest_frame_ip`, `kest_frame_wide`, `kest_frame_slot` and `kest_frame_name`
  for stopping a machine and asking it where it is. Nothing changed shape, so a
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
- `kest debug`, a source-level debugger whose breakpoints are written into the
  program and taken out again, so a machine nobody is debugging pays nothing
  (D991).
- `make release`, one archive with a checksum beside it, unpacked and run in CI
  (D989). The build is reproducible: two clean builds are the same bytes.
- Windows x86-64 and macOS arm64 beside Linux, all three held to writing the
  same bytes for every example (D970, D995).
- A VS Code command that opens `kest debug` on the file in front of you (D978).
- `kest new`, `kest build`, `kest test` and `kest doctor`, and a project
  manifest of `name value` lines (D982).
- `kest check --cost`, which says what the compiler proved about each body
  (D976).
- Windows x86-64, built and run in CI beside Linux, with the two held to saying
  the same thing byte for byte (D970).
- `bench/`, four workloads in this language and two others (D980).

## 0.1.0

The first version there was. Everything in `docs/worklog.md` up to it.
