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

Whole processes, best of fifteen by processor time, at `80a8d331` on
2026-09-23 with `bench/compare.sh` (D1180), on the machine this was written on
at a load of two to three. The runs of each row are spread through the sitting
rather than taken together, because taken together one neighbour's half second
fell on every run of a row: the same binary was 54 ms on `kernel` in one
sitting and 74 in the next (D1195). A run taken at a load of forty once put
Luau's interpreter a third slower on `rules` than a quiet machine does (D1190).
`bench/results.tsv` has the instructions each run retired beside it, which is
the number that does not move with the load.

| workload | kest | **kest, compiled** | `g++ -O2` | `luau -O2` | `luau --codegen` | daslang | daslang `-exe` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| kernel | 53.4 ms | **7.6 ms** | 5.6 ms | 71.7 ms | 29.5 ms | 107.3 ms | 18.0 ms |
| control | 80.1 ms | **11.0 ms** | 5.0 ms | 82.2 ms | 34.2 ms | 117.9 ms | 18.8 ms |
| graph | 9.2 ms | **3.7 ms** | 1.6 ms | 11.4 ms | 7.9 ms | 57.9 ms | 12.5 ms |
| words | 18.0 ms | **14.0 ms** | 8.7 ms | 23.6 ms | 22.4 ms | 74.2 ms | 36.5 ms |
| rules | 271.1 ms | **54.2 ms** | 38.2 ms | 289.1 ms | 147.6 ms | 341.5 ms | 40.5 ms |

daslang's `-jit` is not a row: a process of it is 8 to 10 seconds with its
compiled-code cache off, which is what every row here is run with, and 285 ms
on `kernel` with every cache it has warm -- its LLVM is loaded and set up on
every run, so its JIT is a thing a long-running process pays once and not a
per-run number (D1161). The first measurement, from before D1154, was `rules`
909 ms on the machine and 122 compiled, `kernel` 127 and 27; what took the
machine from there is D1154 to D1193: each instruction handing over to the
next through a table of labels was a tenth of every workload's cycles
(D1181), and a scalar read where the instruction reading it is rather than
through one switch for the whole machine is what put it ahead on `rules`
(D1193).

And a frame of a real program, both engines, over sixty calls of
`examples/slice` (D1124):

| | the machine | the release engine |
| --- | --- | --- |
| a call, p50 | 1.613 ms | **0.640 ms** |
| p95 | 1.949 ms | 0.849 ms |
| p99 | 3.438 ms | 0.893 ms |
| max | 4.089 ms | 2.796 ms |

## 7. Strongest competitor, and why

**Two, one a tier.** Against the machine it is **Luau's interpreter**, and the
machine is ahead of it on all five -- `kernel` 53.4 ms against 71.7,
`control` 80.1 against 82.2, `graph` 9.2 against 11.4, `words` 18.0 against
23.6, `rules` 271.1 against 289.1. It retires fewer instructions than Luau's
on all five, 15% fewer on `rules`. `control` and `rules` are 3% and 6%, which
is close enough that a busier machine has turned them round, and did: a run at
a60730d6 had the machine 4% behind on `rules` (D1190). Daslang's interpreter is
behind the machine on all five.

Against the release engine it is **daslang's AOT**, which is its compiler
writing a native binary the way this one writes C: 40.5 ms on `rules` against
54.2, the one row where a guest language's shipping mode beats this one's, by
1.3 times, where it was 2.6 (D1161, D1179, D1186 to D1189). On the other four
the release engine is ahead of it -- 7.6 against 18.0, 11.0 against 18.8, 3.7
against 12.5, 14.0 against 36.5 -- and ahead of Luau's native tier on all
five.

`g++ -O2` is in the table as the floor rather than as a competitor: it is not a
guest language and nothing here is trying to replace it.

## 8. Where Kest wins

- **Against Luau's native tier, on all five workloads**, compiled: `rules`
  54.2 ms against 147.6, `kernel` 7.6 against 29.5, `control` 11.0 against
  34.2, `graph` 3.7 against 7.9, `words` 14.0 against 22.4.
- **Against daslang's AOT on four of five**: `kernel` by more than two times,
  `control` by 1.7, `graph` by three and a third, and `words` by two and a
  half.
- **The machine against Luau's interpreter on all five**, by 3% on `control`,
  6% on `rules` and a fifth to a quarter on the other three, and against
  daslang's interpreter on all five.
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

- **Daslang's AOT on `rules`, by 1.3×**: 40.5 ms against 54.2. The benchmark
  reads each actor out of its array and writes it back, because a struct is a
  value here, where the daslang version changes it where it stands; the copy
  is twelve slots each way on every call.
- **`g++ -O2` is ahead on all five**: by 1.4× on `kernel` and `rules`, 1.6× on
  `words`, and 2.2 to 2.3× on `control` and `graph`.
- **The machine's lead over Luau's interpreter on `control` and `rules` is
  thin**: 3% and 6%, where it retires 23% and 15% fewer instructions, so Luau
  does more of each cycle's work; a run at a60730d6 had it 4% behind on
  `rules` (D1190).
- **On reading, where the work is straightforward shuffling of data known to be
  there**, Kest is a tenth to a quarter longer, because the types are written
  down: `nearby` 125 per hundred, `stale` 111.

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
in the first measurement: the machine was 909 ms on `rules` and the C it
wrote 122, and no amount of dispatch work closes seven times. After the
dispatch work of D1154 to D1181 it is 300 against 56, and the reason stands.

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

**Run blind, seventy-two times** (D1148): twelve tasks, both languages, three
attempts, each an Opus model in a room holding the task, the scaffold and --
for Kest -- the reference, and judged afterwards by the tests it never saw.
**Kest 36 of 36, Luau 30 of 36**, and all six Luau misses are two tasks whose
hidden tests held a rule `ask.md` never said; on the ten tasks asked fairly it
is **30 of 30 in both**. The suite does not tell the two languages apart on
correctness, because a strong model gets every fair task right in either.

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

Run blind (D1148), no Kest answer failed a hidden test. The six Luau answers
that did were each believed right by the run that wrote it, with the analyser
clean -- which is what a silent escape looks like -- and every one of them is a
rule the task never stated, so they count against the suite rather than for
this language.

The small number is honest and deliberate: eleven of the twelve mistakes
written here are the kind no type system can be credited for, because a suite
whose mistakes are all spelling is a suite that measures nothing. `pooled` is
the one the compiler answers — the same wrong answer, a scratch list of what
was taken, is a refusal in Kest and a table nobody counts in Luau.

## 24. Kest changes caused by AI evidence

Four, the first two from writing the tasks and the last two from running a
model against them and against a game:

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
- **Three refusals that say what to write** (D1148). Run blind, the mistakes
  models made again and again in Kest were a struct built by field name, a call
  by name, and a number handed where text is wanted; each was told only which
  token or type it had, and is told the fix now.
- **`Case -> {` said as what it is** (D1147), written by a model writing the
  colony: one diagnostic an arm with the arm's own fix, where it was one about
  `if` and then a line about an arrow for every arm after it.

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

Second to it: **`words` was level with Luau rather than ahead**, and it was the
allocator-bound row. It is ahead now, 14.6 ms against 24.0 compiled and 18.7
against 25.2 on the machine, since a whole number stopped going through
`snprintf` and the heap stopped finding a place a bit at a time (D1173); what
the allocator costs is still where this language has the least evidence about
why.

## 29. Largest remaining product weakness

**The AI claim is not shown.** The suite has been run blind with a model
(D1148) and it cannot tell the languages apart: both are 30 of 30 on the tasks
asked fairly. What does differ is cost, and against this language: 2.7
compiler runs a task against 2.2, about half as many tokens again, and every
Kest run searching a seven-thousand-line reference its Luau counterpart did not
need. The one time a model's mistake was caught before it ran is the colony
(D1146), not the suite. Settling the claim needs tasks a strong model gets
wrong in the incumbent.

Second: **every runtime number is a whole process on a shared machine**. Task
clock rather than wall clock, best of five, and a load of up to forty-seven
beside it on the day the table was taken: the ratios hold across runs and the
milliseconds are this box's. Daslang's AOT, which this section used to name
as unmeasured, is measured now (D1161).

Third: **readability is measured in tokens**, which is a proxy. The numbers are
honest and the spread is informative, but what would settle it is people.

## 30. Why should a serious game developer now consider Kest instead of the strongest relevant incumbent?

Against Luau with `--codegen`, which is the strongest relevant incumbent, the
factual answer is three things and no more:

**It is faster on gameplay-shaped work, with the numbers beside it.** 54.2 ms
against 147.6 on the rules workload, 7.6 against 29.5 on the kernel, 11.0 against
34.2 on control, 3.7 against 7.9 on the graph, 14.0 against 22.4 on the
allocator-bound one; and its interpreter is ahead of Luau's interpreter on all
five, by 3% at the least. Daslang's AOT is the one thing in the table ahead of
it on the rules, by 1.3 times, and behind it on the other four. A
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

**Where it is weak, it is weak in the open.** It is 1.4 to 2.3 times `g++`
and it is not trying to be C++; its interpreter's lead over Luau's on the
rules workload is 6% and has been a 4% deficit on a busier machine; a model
run blind against its AI suite does no better in it than in Luau and spends
more getting there; and daslang's AOT is 1.3 times faster on the rules. All
four are written down here rather than left for somebody to find.

**Is 30 still weak?** Partly, and in one place: the AI claim. The runtime
claim, the contract claim and the loop claim each hold by something that runs
and can be run again. The claim that this language is better to write *with a
model* is the one the owner asked for first, and run blind (D1148) it is not
shown: a strong model gets every fair task right in both languages, and costs
more to get there in this one.

**So the highest-value next technical direction is a harder suite** -- tasks a
strong model gets wrong in the incumbent often enough for a difference to be
seen, which is where a refusal before running is worth something -- and a
short page for a reader who knows Lua or Rust, because the reference is most of
what a Kest run spends. `ai/blind.sh` runs the next suite the way it ran this
one.

The second **has now been done** (D1141), and what it found is above in 28:
on the workload with the largest gap, 190.4 instructions a decision against
`g++`'s 48.7, of which the ledger frame is 26.0, the guard on every element
access 49.1 and inlining 13.4. **More than half the distance to C is the
safety, and it is the safety this language is for.** The one piece a compiler
can prove away without losing anything is the element guard where the index
comes from a walk over the same run.
