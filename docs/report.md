# What this direction did, and what it is worth

The final report section 35 of the mission asks for. Every number here is one
this tree takes and can take again; where the record is thin this says so
rather than filling it in.

## 1. Start SHA

`e458ee2c5387b7181c08cbe5e530a0c75f6d3812`, which is where the baseline in
`docs/game-ai-direction-state.md` was measured.

## 2. Final SHA

This commit. `LAST FULL GATE` in the state file names the last one the whole
gate was green at.

## 3. Which final-foundation defects survived the first audit

Forty-six, and they are the table at the top of `docs/state.md`. Each was
reproduced against this tree with the smallest program or host that asks the
question, and **none of them is open**: each is fixed and each names what holds
it. The shapes worth naming, because they are the ones a reader will meet
again:

- **a lent run read as the wrong element type** (F1) — eight `Row` of eight
  bytes read as eight `Wide` of sixteen, found by ASan;
- **`a[0] = grow(a)` losing the assignment** (F4) — the address is emitted
  before the call and the store goes to the abandoned buffer; silent, and only
  when the array is not the last block on the heap;
- **a reference from one world resolving in another** (F9), and then **world
  identity wrapping into a live one** (F11) — the 65,537th machine of a process
  was told it was the first while the first was still standing. Fixed by taking
  the world out of a reference altogether;
- **a generic body never read until something copied it** (F16) — a library
  could ship a generic that works for no type;
- **two lines of ordinary Kest breaking a table from outside** (F14).

## 4. Which cold-review probes still reproduced

The record this tree keeps is the table above and the second one under it,
"read from the source rather than run". That second list had four entries and
now has none that are open: **F10 was the last, and D1132 closed it by
measurement** — a refusal inside a body that promises `no.alloc` leaves the
program heap at 912 bytes with 912 ever asked for, and a build where the
refusal path takes sixteen bytes of that heap does not fail the assertion, it
dies, because taking heap runs the collector over a machine in the middle of
refusing.

This project does not keep a separate cold-review log, so that is the whole of
the honest answer.

## 5. What was fixed before strategy work

D1086 through D1091: three walks that were the program's own size, the
million-line measurement, and a fault found by measuring rather than by
reading. The clean-check curve went from quadratic to linear before any of the
direction's own work began.

## 6. Game-shaped runtime numbers

Whole processes, best of five, wall clock, on a machine somebody else was also
using (D1116):

| workload | kest | **kest, compiled** | `g++ -O2` | `luau -O2` | `luau --codegen` | daslang |
| --- | --- | --- | --- | --- | --- | --- |
| kernel | 127 ms | **27 ms** | 13 ms | 108 ms | 49 ms | 180 ms |
| control | 186 ms | **34 ms** | 13 ms | 127 ms | 57 ms | 168 ms |
| graph | 79 ms | **9 ms** | 7 ms | 19 ms | 15 ms | — |
| words | 48 ms | **33 ms** | 17 ms | 36 ms | 32 ms | — |
| rules | 909 ms | **122 ms** | 64 ms | 637 ms | 182 ms | — |

And a frame of a real program, both engines, over sixty calls of
`examples/slice` (D1124):

| | the machine | the release engine |
| --- | --- | --- |
| a call, p50 | 1.613 ms | **0.640 ms** |
| p95 | 1.949 ms | 0.849 ms |
| p99 | 3.438 ms | 0.893 ms |
| max | 4.089 ms | 2.796 ms |

## 7. Strongest competitor, and why

**Luau with `--codegen`.** It is the only comparator that is both a guest
language a game embeds and within sight on a gameplay workload: 182 ms on
`rules` against this language's 122 compiled, and 49 against 27 on the kernel.
Daslang's interpreter is behind both on every row it runs, and **its AOT is
named and not measured** — `daslang -exe` is a row in the harness and this
build has no LLVM behind it, so `-exe` and `-jit` both refuse. That is an
evidence gap and it is written down as one.

`g++ -O2` is in the table as the floor rather than as a competitor: it is not a
guest language and nothing here is trying to replace it.

## 8. Where Kest wins

- **Against Luau's native tier, on four of the five workloads**, compiled:
  `rules` 122 ms against 182, `kernel` 27 against 49, `control` 34 against 57,
  `graph` 9 against 15.
- **On what it refuses before running.** Contracts the compiler proves rather
  than conventions a reader keeps: `no.alloc`, `no.host` and `deterministic`
  are held at the line, and the paired suite counts one wrong answer refused
  before running in Kest and none in Luau.
- **On the edit loop.** `kest check` is 3.0 ms against `luau-analyze`'s 14.9 on
  a task file, and a keystroke in an editor is 0.3 ms on a 112,647-line project
  because latency follows what the file imports and not how big the project is.
- **On reading, where the work is about things that may not be there.** Over
  eleven paired tasks the answers are 2,463 words of Kest against 2,716 of
  Luau; `saved` is 60 per hundred and `frail` 55.

## 9. Where Kest loses

- **`g++ -O2` is ahead on all five**, by 1.4× on `graph` and about 2× on
  `kernel`, `control` and `rules`. The gap is smallest where the work is
  allocator-bound and largest where it is arithmetic.
- **`words` is level with Luau** rather than ahead: 33 ms against 32. It is
  allocator-bound, and that is where this language has the least advantage.
- **On reading, where the work is straightforward shuffling of data known to be
  there**, Kest is a tenth to a quarter longer, because the types are written
  down: `nearby` 125 per hundred, `stale` 111.
- **Daslang's AOT is unmeasured**, so "ahead of daslang" is true only of its
  interpreter.

## 10. Dominant runtime ceiling

For the machine, it was **dispatch and the instruction bodies at 54 per cent**
on a gameplay workload, with value movement at 22 and the collector at the
rest (D1117). Profiling also found 16.5 per cent going on **working out where a
refusal would be reported before every door call** — a cost paid on every call
for a message almost no call ever needs. Taking it off the hot path took
`bench/agents.kest` from 18.31 G instructions to 6.84 G.

For the release engine the ceiling is the C compiler's, which is why the answer
to the ceiling was a second engine rather than a faster interpreter.

## 11. Whether the VM or backend changed, and why

**Both, and on evidence.** The machine stayed the machine for developing —
breakpoints, the profiler, the contracts, a reload in under a millisecond — and
a second backend was written that emits C11 from the same resolved IR (D1092,
D1093). It writes **2,088 of this tree's 2,088 bodies** (D1119). The reason is
in the table above: the machine is 909 ms on `rules` and the C it writes is
122, and no amount of dispatch work closes seven times.

The two are held to being one language rather than two: 41 programs answer the
same thing and write the same words compiled either way, and one runs both ways
inside one process.

## 12. GC p99 and max on an integrated world

On `examples/slice` over sixty calls, making 125,466 allocations (D1124):

- **the collector's longest pause is 0.18 ms by the machine and 0.23 ms
  compiled** — one and a half per cent of a sixteen-millisecond budget;
- the heap does the same thing under both engines **to the byte**: 125,466
  allocations asking 6,487,998 bytes and given 8,303,328, 31 walks giving back
  8,052,528, 350 plots made and 326 handed back. Two engines that allocated
  differently would be two languages.

## 13. Compiler clean scaling

| lines | at D1088 | at this HEAD |
| --- | --- | --- |
| 6,312 | — | 17 ms |
| 62,607 | — | 141 ms |
| 112,647 | 288 ms | **286 ms** |
| 625,557 | — | 2,256 ms |
| 1,000,857 | 4,910 ms | **4,644 ms** |

Linear, and it stayed linear through two engines, a ledger frame and two
thousand bodies of C backend (D1130).

## 14. Contract-analysis scaling, before and after

The contract prover found a call's callee by walking every global, which made
it the program's own size for every declaration it looked at. With tables
(D1086):

| modules | before | after |
| --- | --- | --- |
| 100 | 16 ms | 10 ms |
| 400 | 78 ms | 35 ms |
| 800 | 285 ms | 78 ms |
| 1000 | 609 ms | 104 ms |

Six times at a thousand modules, and the curve is linear rather than quadratic.

## 15. Normal edit and check latency

Driving `kest lsp` over a pipe on the generated project of 112,647 lines
(D1131):

| what is being edited | a keystroke |
| --- | --- |
| a leaf system, importing one module | **0.3 ms** |
| a group importing twenty systems | 3.0 ms |
| the top, importing all ninety groups | 475 ms |

**Latency follows what the file imports, not how big the project is.** At
62,607 lines the same three are 0.2, 3.0 and 221 ms — only the top moved.

## 16. Whether incrementality was necessary

**No**, and that is the measurement rather than a preference. For the file
somebody is actually editing a resident compiler saves nothing measurable:
0.3 ms is already below the tick of anything an editor does with it.

## 17. If yes, the invalidation model

Not applicable.

## 18. If no, why not

Because the one case it would help is the one file in a project that imports
everything, where it would buy most of half a second — and that file is edited
when a system is added rather than while code is written. A symbol graph held
across keystrokes would have to be invalidated correctly for every other case,
and the numbers above do not pay for that trade. Section 12 of the compiler
direction says to consider one *when measurement justifies it*; this is the
measurement, and it does not.

## 19. Reload, edit to visible result

**0.48 ms**, of which **0.44 is building the program** (D1127). Starting a
machine, putting the world back into it, asking the seven doors and swapping
are 0.05 ms between them. So the reload path is a compile, and what a reload
costs on a real project is what a compile costs on it — the table in 13.

A refusal costs 0.4 to 0.8 ms and stops at one of three places: building it,
the shape of the world, or the doors the host calls.

## 20. Whether hot patching was needed

**No.** A reload that rebuilds the program and hands the world across is under
a millisecond on the program it was measured on, and the cost scales as the
compile does. Patching a running machine's code would buy the difference
between 0.48 ms and something smaller, at the price of a second code path
through every shape change — the eleven edits the gate drives are exactly the
cases that path would have to get right.

Timing it did find a real defect: the host asked for its doors **after** it had
published the candidate, so a signature that moved left it running the new
program while saying the world was the one it was. Fixed, and the gate now
holds every refused edit to ending where a run with no edit ends.

## 21. Paired AI task completion

**Twelve tasks, both languages, twenty-four pairs.** For every one of them the
gate holds three things: the answer written here keeps every hidden test, the
scaffold nobody filled in does not, and the plausible wrong answer is caught.
All nine families the mission lists in section 20 are written; twelve of the
fourteen in the mechanisms document, which is a menu and says so.

**No model has been run against it.** That is the owner's, at the end, and this
report does not guess at what a model would score.

## 22. Repair iteration results

What can be measured without a model is what a mistake costs to be told about,
and `ai/cost.sh` measures it (D1126):

- **being told by a compiler: 1.9 ms** — less than checking the answer that is
  right, because a refusal stops where it happens;
- **being told by a hidden test: 24 to 38 ms** — thirteen to twenty times as
  long, before anybody has read a word of what came back;
- and **the refusal names the line the repair is on**: `pooled`'s complaint
  points at the `array()` on line 32 of the wrong answer, which is one of the
  lines that differ from the right one, read off a diff rather than asserted.

## 23. Silent escaped defects

**None.** Of the twelve wrong answers, **one is refused before running in Kest
and none in Luau**; the rest are caught by a hidden test, and nothing escapes —
the gate would not pass if it did.

The small number is honest and deliberate: eleven of the twelve mistakes
written here are the kind no type system can be credited for, because a suite
whose mistakes are all spelling is a suite that measures nothing. `pooled` is
the one the compiler answers — the same wrong answer, a scratch list of what
was taken, is a refusal in Kest and a table nobody counts in Luau.

## 24. Kest changes caused by AI evidence

Two, and both came from writing the tasks rather than from thinking about them:

- **K0347** (D1135). Writing three task files turned up the Kest-specific
  mistakes a model makes here, and the compiler answered all but one at the
  line. The one nothing answered was `let xs: [i32] = array()` followed by
  `fit(xs, 1)`: `array()` makes an array with no room, `fit` writes where there
  is room, so those lines write nothing and say nothing — the shape a body
  under `no.alloc` falls into, because `push` is refused there and `fit` is
  what is left. It is a warning at the `let` now, quiet where `room` or `push`
  is called on the name or where it is handed to another function at all.
- **A runner with a host** (D1137). A task about the doors a host gives cannot
  be judged by a program on this side of them, so `ai/run.sh` builds a task's
  own `host.c` against `libkest.a` where there is one.

## 25. Whether a dedicated AI protocol was needed

**No.** What a model needs here is what a person needs: a refusal at the line
with the words to fix it, a check that answers in milliseconds, and a
diagnostic in JSON for anything reading it mechanically — all of which existed
for other reasons. The one thing the evidence asked for was a *diagnostic*
(K0347), not a channel.

The editor path is the same compiler, held to it: what an editor is told about
a file and what `kest check` says about it agree code by code, line by line,
column by column and word by word, including after a dependency changes on disk
under an open buffer (D1129, D1131).

## 26. Complexity added

- **A second backend**: `src/emitc.c`, writing C11 from the resolved IR for all
  2,088 bodies. Paid for by seven times on `rules`.
- **Shims and a ledger frame** (D1105, D1122) so that a written body and a
  compiled one call each other through one answer per operation.
- **A third gate tier** (D1136), because the distance between a quarter of a
  second and half an hour was making every change cost a full gate.
- **Three fields on a local** for K0347, which cost 128 bytes of checking
  `lib/std/text.kest` — measured, quoted in the reference, and held by a run.

## 27. Complexity deliberately avoided

- **A resident compiler**, because 0.3 ms a keystroke does not pay for a symbol
  graph that has to be invalidated correctly (16–18).
- **Hot patching a running machine**, because a reload is 0.48 ms and the
  eleven edits the gate drives are exactly what a second path would have to get
  right (20).
- **An AI-specific protocol** (25).
- **Incremental LSP edits**: the client sends the whole document, because
  applying an edit twice is the one way a server can be wrong about what a file
  says.
- **A JIT.** The release engine is C11 with no runtime code generation, which
  is what makes it a deployment claim on platforms that forbid one.

## 28. Largest remaining technical weakness

**`g++ -O2` is still about twice as fast on three of the five workloads, and
the release engine's floor is the C compiler's.** The machine's own ceiling was
found and moved — dispatch and instruction bodies at 54 per cent, and a 16.5
per cent cost on every door call taken off — but the compiled engine's
remaining distance to C is not a dispatch problem and will not yield to the
same kind of work.

**That distance has now been read** (D1141), on the workload with the largest
gap, in instructions rather than on the clock and with startup and compiling
taken off both sides:

| | instructions a decision | of the gap |
| --- | --- | --- |
| as the backend writes it | **190.4** | |
| the ledger frame round every call | −26.0 | 18% |
| the guard on every element access | −49.1 | 35% |
| the body not inlined at `-O2` | −13.4 | 9% |
| what is left | **101.9** | |
| `g++ -O2` | **48.7** | |

**More than half of it is the safety, and it is the safety this language is
for**: a host walking the stack of a running program, and an index past the end
refusing in words rather than reading past the end. The `i32` narrowing after
every arithmetic op is free — taking all nine out of that program made it
slower. The remaining 53 instructions are the slot machine itself, eight bytes
where C++ uses four.

So the weakness is real and it is now quantified rather than guessed, and the
one piece a compiler can prove away without losing anything is the element
guard where the index comes from a walk over the same run — the commonest shape
in gameplay code, and the thing to do next.

Second to it: **`words` is level with Luau rather than ahead**, and it is the
allocator-bound row. The collector is measured and its pause is small, but
allocation throughput is where this language has the least advantage and the
least evidence about why.

## 29. Largest remaining product weakness

**No model has been run against the AI suite.** It is built, it is held to
being worth being judged by, and what a mistake costs is measured — but the one
claim the suite exists to settle, that code written with a model is easier to
get right here, has no model behind it yet. Everything in section 21 to 23 is
the instrument rather than the result.

Second: **`daslang`'s AOT is named and not measured**, so every claim against
daslang is a claim against its interpreter. Closing it needs a daScript built
with LLVM, which is not this tree's to build.

Third: **readability is measured in tokens**, which is a proxy. The numbers are
honest and the spread is informative, but what would settle it is people.

## 30. Why should a serious game developer now consider Kest instead of the strongest relevant incumbent?

Against Luau with `--codegen`, which is the strongest relevant incumbent, the
factual answer is three things and no more:

**It is faster on gameplay-shaped work, with the numbers beside it.** 122 ms
against 182 on the rules workload, 27 against 49 on the kernel, 34 against 57
on control, 9 against 15 on the graph, level on the allocator-bound one. A
frame of a real program is 0.640 ms in the middle with a 0.893 ms p99, and the
collector's longest pause on a program making 125,466 allocations is 0.23 ms.

**It refuses things before they run that the incumbent cannot.** Whether a body
reaches the heap, whether it calls the host, whether it answers the same on
every machine — proved by the compiler at the line, not kept by convention.
That is what a codebase many hands write into is worth having, and it is the
one difference no amount of tuning on the other side closes.

**The loop is fast and it stays fast as the project grows.** A keystroke is
0.3 ms on a hundred and twelve thousand lines, a clean check of that project is
286 ms, a reload is under a millisecond, and a shipped build is C11 with no
runtime code generation.

**Where it is weak, it is weak in the open.** It is about twice `g++` and it is
not trying to be C++; it is level with Luau where the work is allocation; no
model has been run against its AI suite; and daslang's AOT is unmeasured. All
four are written down here rather than left for somebody to find.

**Is 30 still weak?** Partly, and in one place: the AI claim. The runtime
claim, the contract claim and the loop claim each hold by something that runs
and can be run again. The claim that this language is better to write *with a
model* is the one the owner asked for first and the one with an instrument but
no result.

**So the highest-value next technical direction is to run models against the
suite** — the same eleven paired tasks, several models, several attempts,
counting completion, repair turns and what escapes on each side. The suite, the
hidden tests, the harness and the cost measurements are all built for exactly
that and none of it has been spent.

The second **has now been done** (D1141), and what it found is above in 28:
on the workload with the largest gap, 190.4 instructions a decision against
`g++`'s 48.7, of which the ledger frame is 26.0, the guard on every element
access 49.1 and inlining 13.4. **More than half the distance to C is the
safety, and it is the safety this language is for.** The one piece a compiler
can prove away without losing anything is the element guard where the index
comes from a walk over the same run.
