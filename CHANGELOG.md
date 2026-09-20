# What changed

Newest first, one section a version. This is for somebody who has a program or
a host written against an earlier one: what it says is what a reader has to do
about the change, not what was built — `docs/worklog.md` is that, and
`docs/decisions.md` is why.

What each number means and when it moves is in D983.

## 0.0.1 — 2026-09-19

**Kest is unstable again, on purpose.** The `1.0.0` below was published on
2026-09-18 and withdrawn a day later. The tag and the GitHub release are gone;
the history is not, and the section below is kept as the record of what was
claimed.

**What a reader with a program written against 1.0.0 has to do.** Nothing yet,
and that is because there was nobody: each release asset had been downloaded
once, by this project's own CI, and the repository had no forks. If you are the
exception, the tree at `53a0771858a4d0b7770d0c48a15fdce4325e9292` is still in
this history and still builds.

**What to expect from here.** Nothing is frozen while this is `0.0.x`:
semantics, syntax, the C ABI, the shape of the JSON, what `deterministic`
covers, and what a reference is made of may all change. Every break is a
decision in `docs/decisions.md` that says what it supersedes and why the old
thing was worse, and this file says what to do about it.

**Why it was withdrawn.** Two independent readings from outside the project
found foundational things still worth changing before Kest pretends to be a
stable language, and the first of them reproduced on the first try:

- **A reference could name somebody else's object.** A reference carried which
  world it came from, in sixteen bits of a count of the machines a process had
  made. A count of sixteen bits comes round: the 65,537th machine was told it
  was the first, and the first could still be standing. A world holding 7, 8, 9
  handed its first reference to a world holding 1000, 1001, 1002 that had been
  told it was the same world, and the second answered **1000**, with no
  refusal. There is no world in a reference now and nothing to wrap. See D1033.
- **A world ran out of identity in half a second.** The count that stamps
  places was one machine's and twenty-four bits wide, so a world holding
  exactly one thing — one in, one out — refused after 16,777,215 turns with a
  live set of one. `bench/agents.kest` had five minutes and forty seconds in it
  at sixty hertz. The count is the process's and forty bits now, which is
  65,536 times further out. See D1034.

**What changed for a program.** Nothing in the syntax and nothing in the
library. A `ref<T>` is still one slot and no shape in any program got wider; a
store costs thirteen bytes an entity more, because what a place is stamped with
went from four bytes to eight. The ceiling on places handed out is
`1099511627775` rather than `16777215`, and `K0630` still says so at the line
that asked.

**A module is the whole of what a file calls itself.** `import a.math` beside
`import b.math` used to be refused for the whole program, so `render.math` and
`physics.math` could not coexist even when no file wanted both. They can now.
What a file writes is still the last part — `math.twice` — and which module
that means is expanded through that file's own imports. **What a program has to
do:** nothing, unless it reads names out of a tool. A chunk is compiled under
the whole module, so `emit`, the profiler, the debugger and `check --json` say
`std.text.upper#text` where they said `text.upper#text`, and every declaration
in the JSON carries a `module` beside its name. A file reaching two modules
that end in one word is now what `K0328` refuses, pointing at that file's two
import lines. See D1039.

**A struct field may be the module's own.** Written `own name: type`, it can
only be named from inside the module that declared the shape. `std.table`'s
four fields are written that way, because two lines of ordinary Kest could
leave them disagreeing. **What a program has to do:** if it read a table's
`keys` or `values` directly, walk the pairs by their places instead —
`table.keyAt(t, at)` and `table.valueAt(t, at)` for `at` under
`table.count(t)`, which copy nothing and promise `no.alloc`. `table.keysOf` is
unchanged. Nothing else in the library keeps a field. `own` is a word and not a
keyword, so a field, a function or a name called `own` still works. See D1041.

**A fault does not run `defer`.** This is not a change, it is the reference
saying it where `defer` is introduced rather than leaving it to be found out.
`defer` runs on every way out the program itself takes — off the end, `return`,
`break`, `continue` — and a machine that divides by nought or reads past an
array stops there. **What a host has to do:** close what it paired across a
call when the call answers false, the same way it would for any other refusal.
See D1040.

**A generic says what it asks of a type.** `fn firstAt<T: compares>(...)` and
`fn ascending<T: orders>(...)`: two words, written after a colon in the list of
type names, and there is no third. The body is checked once where it is
written, against exactly what the declaration says -- until now a generic body
was not read at all until something copied it, so one that could not work for
any type checked clean. **What a program has to do:** put the word on any
generic of its own whose body compares or orders what it was given. The
compiler says which and where. A body that does arithmetic on a type name,
reads a field of one or indexes one is now refused where it is written; nothing
in this tree did any of those. `kest check` prints the type names in a
signature and `--json` carries a `typeParameters` list beside `parameters`.
`compares` and `orders` are words and not keywords. See D1043.

**Reading a field of an array element is one instruction.** `elem.at` replaces
`elem.addr` and the `load.at` after it, which is 16.9 % of the instructions of
a program written to touch elements in place and nothing at all to one written
to read an element into a local and write it back. **What a program has to
do:** nothing. The hoist `docs/state.md` named as open is measured and not
built, because the measurement says it is not where the gap is. See D1044.

**A host says when the machine walks on its own.** `kest_collect_after` sets
how many times what is held may be handed out before that is worth a walk. One
by default, which is what it always did and the shortest pause there is to
have; three takes the collector from 15.7 % of a call to 6.8 % for a pause half
again as long and twice the memory held at most. **What a host has to do:**
nothing. A pause is 0.68 milliseconds for every megabyte still reachable, which
is in the reference now, beside what the collector is and why there is no
incremental marking. See D1045.

**An enum takes types.** `enum Answer<T> { Held(T) Trouble(text) }`, through
the same door a generic struct goes through. **What a program has to do:**
nothing. Nothing is added to the library — there is no `std.result` — and the
reference says which of `T?`, `bool` and an enum a fallible function should
answer with. See D1048.

**A project's `source` lines are where its modules are looked for.** They were
read and printed and nothing else, so multiple source roots — which is how a
dependency is written here — did not resolve. **What a program has to do:**
nothing, unless it has a project whose files sit outside every `source` line,
which was never resolvable across roots anyway. A module under two sources is
now `K0707` rather than whichever was looked in first. Which project a file is
in is where the file is, so a host embedding one file of a project resolves
what the command line resolves. See D1049.

**`std.sort` gains `byWith`.** `sort.byWith(npcs, player, nearer)` sorts with
something beside what it compares, which is what a closure would have captured
and what sorting by distance from a point needed. **What a program has to do:**
nothing; `sort.by` is unchanged. There are still no closures, and D1051 says
what the five callback shapes are instead.

**`sort.by` sorts with gaps, and `std.table` gains `compact`.** Plain insertion
was quadratic: four thousand numbers out of order cost two hundred and sixty
million instructions, and cost a million now. **What a program has to do:**
nothing, unless it relied on the sort being stable, which nothing said it was —
equal elements may come back in another order. A table still keeps room for
what it has held, and `by = table.compact(by)` is the cold path that gives it
back. See D1052.

**A reference hashes by the place it names.** It hashed the whole reference,
and the number above the place is one the whole process hands out — so a
function declared `deterministic` answered two different things on two machines
of one process. **What a program has to do:** nothing, unless it wrote a hash
of a reference down and compared it with one from another run, which was never
a number it could rely on. See D1054.

**Machines that share a process no longer share a write.** Every `add` to a
store wrote one word that every thread of the process writes; a machine claims
a thousand of them at a time now. **What a host has to do:** nothing. Sharding
a world across eight threads went from 2.5 times to 4.9, and a program that
never makes a second machine got 37 to 47 per cent faster at making places. See
D1053.

**What changed for a host.** The version. The other three numbers — the C ABI,
the JSON schema, and the deterministic profile — still read 4, 3 and 1, and
will be settled to clean `0.0.x` numbers once the architecture this reset is
correcting has stopped moving. See D1035.

## 1.0.0 — 2026-09-18 (withdrawn 2026-09-19)

The first version with a number that promises something. What 1.x promises is
in the reference under *What 1.x promises*, and the short of it is four
numbers: a program that checks under 1.x checks under every later 1.x, the C
ABI is frozen, the JSON a tool reads has its own number, and the deterministic
profile has a third. The bytecode is none of them — it is internal, it has no
version, and what ships is the source beside the runtime.

**Kest 0.1.0 → 1.0.0. ABI 1 → 4. JSON schema 1 → 3. Profile kest-det 1,
unchanged.**

What this is: a statically typed language for the simulation half of a game,
compiled by a C11 library with no dependency beyond libc and run on a bytecode
machine a host embeds. One resolved semantic representation feeding one stack
machine — the register backend was built, measured at 1.35x slower and removed.
Persistent memory is a heap the machine gives back a piece at a time; text is
UTF-8 with an O(1) byte length and a nought is a character; `scratch { }` is
lexical working memory the checker refuses to let anything escape; `store<T>`
hands out generation-checked `ref<T>`; the C ABI is typed handles with leases,
fuel and cancellation; `kest-det 1` is answered byte for byte on Linux
x86-64, Windows x86-64 and macOS arm64; a reload is transactional and leaves
the old world running if the new one will not have it. The tooling is a
formatter, a language server that is this compiler, a source debugger, a
profiler and a structural cost report, with a VS Code extension over them.
Packaged as an archive with a checksum, unpacked and run in CI.

What it is not: a sandbox for code that is trying to get out. The boundary is
for code the host wrote or trusts to be cooperative, under capabilities and
budgets the host sets.

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

- **`KEST_ABI_VERSION` is 4 and `kest_abi_version()` reads it back.** Compare
  the two at startup: they differ when the header and the library are from two
  versions of this project. `examples/engine.c` does it in its first six lines.
  See D974.
- **`kest_heap_used` goes down now.** What a program makes and then replaces is
  given back, so what it answers is what the program is holding rather than a
  running total. A host that read the difference between two of them to find
  what a call cost reads `kest_heap_taken`, which is every byte the program has
  ever been handed and only `kest_heap_reset` moves the other way.
  `kest_heap_most` is the most it ever held at once, which is the figure a host
  makes room for. This is the change ABI 4 is for. See D996.
- **A host that keeps a handle across a call says so.** What a program makes
  stands on memory the machine gives back when nothing can reach it, and what
  it walks to decide that is the program's own slots and the worlds they name —
  not the host's variables. A host either hands its handle back in with the
  call, which `examples/engine.c` does with the world it drives, or says
  `kest_keeps`. Without one of the two, a world a host holds and does not pass
  in is memory nothing names. `kest_lets_go` is the other end of it. See
  D996.
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

- **JSON schema 3.** Every function `kest check --json` writes carries a
  `proved` object: what the promises' proof found about that body, and which
  promise it keeps and does not make (D976). `kest tick --json` and
  `kest call --json` carry `taken` beside `heap` — what the run was handed
  against what it is holding at the end — and `tick` carries `allowed`, what
  the machine was given to put on the heap. `kest profile --json` carries
  `most`. A reader of schema 1 or 2 that was told the list was everything has
  fields it does not know. See D996.

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
