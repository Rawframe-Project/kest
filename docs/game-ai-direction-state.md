# Where the game-first / AI-native work is

This is the operational state for the mission in
`/home/kest/mission/direction/`. It is read first by whatever resumes the work
and written before every invocation ends. `docs/decisions.md` holds the
reasoning; this holds the position.

    MISSION START SHA: e458ee2c5387b7181c08cbe5e530a0c75f6d3812
    CURRENT SHA:       (this commit) D1093-D1151
    PHASE:             B — the release engine, and it is whole: every one of
                       this tree's 2,088 bodies is written as C (D1119),
                       `bench/rules.kest` compiles entire, the gameplay
                       workload is inside D1092's trigger against `g++` and
                       ahead of Luau's native tier on four of the five
                       workloads, and the kernel is 2.8 times `g++` where it
                       was five. And the machine is measured rather than
                       guessed at: the persistent-world reference program
                       runs 2.7 times fewer instructions since it stopped
                       working out where a refusal would be reported before
                       every door call
    LAST CI:           green on 6bf6e02, all nine jobs -- linux, linux-arm64,
                       macos, windows, clang, threads, package, linux-full
                       and agree. What is after it is D1145 to D1147
    LAST FAST GATE:    green
    LAST FULL GATE:    `make check` green here with D1148's three refusals
                       in it, every one of the 900 holes caught
    REFERENCE MACHINE: the spare Linux box this repository is on --
                       12 cores, 62 GB, gcc, release build, warm page cache.
                       Every number below was taken on it.

## What the owner is asking for

Extremely fast to run, extremely readable, fit for large games, easy for a
newcomer, good to write with AI, catching AI mistakes early, and an extremely
fast check/compile/iteration loop. The binding question behind all of it:
*why would a serious game developer choose this over Luau or Daslang?*

The documents beside the mission propose mechanisms — a daemon, an incremental
session, hot patching, a second machine protocol, a backend tournament. None of
them is adopted until a measurement asks for it. Goals are fixed; mechanisms
are earned.

## Baseline, taken at e458ee2

Clean `kest check`, wall time, release build, warm cache:

| what | time |
| --- | --- |
| a four-line program | 1 ms |
| `examples/slice` entry | 3 ms |
| all of `lib/std` (9 modules) | 3 ms |
| 100 generated modules (~3.5k lines) | 16 ms |
| 1000 generated modules (~35k lines) | 609 ms |

The corpus is `/tmp/uni/scale` — 1000 modules of a struct and three functions
each over a shared base module, plus entries that import 100/200/400/800/1000
of them. Generated, but with real bodies: loops, field access, generic calls,
contracts.

The curve was quadratic: ten times the modules cost thirty-eight times the
time. Stage timing (`KEST_SPENT=1`) put it in naming and in checking bodies.

**Rerun at this HEAD** (D1130), read above the floor a process costs -- 6.5 ms
for `kest --version` on this box, where `/bin/true` is 5.7:

| what | then | here, above the floor |
| --- | --- | --- |
| a four-line program | 1 ms | inside the floor's own noise |
| `examples/slice` entry | 3 ms | 2.6 ms |
| all of `lib/std` | 3 ms | 3.6 ms |
| 100 generated modules | 16 ms at ~3.5k lines | 14.6 ms at 6,312 lines |
| 1000 generated modules | 609 ms at ~35k lines | 173.8 ms at 62,607 lines |

Three and a half times faster at a thousand modules on nearly twice the lines,
and the quadratic curve is gone and stayed gone through two engines, a ledger
frame and two thousand bodies of C backend.

## What has been done

**D1086 — three walks that were the program's own size.** `kest_find_type`
walked every type in the program for every type a body names;
`kest_symbol_at` walked every global for every declaration the contract prover
looked at; the listing walked every file for every name and every module line
for every declaration. All four are tables now. The clean-check curve is
linear:

| modules | before | after |
| --- | --- | --- |
| 100 | 16 ms | 10 ms |
| 400 | 78 ms | 35 ms |
| 800 | 285 ms | 78 ms |
| 1000 | 609 ms | 104 ms |

At 35k lines and 1000 modules a clean check is 104 ms. Extrapolated linearly
that is about 300 ms at 100k lines against the 250 ms the compiler direction
asks for, and about 3 s at 1M against 2 s — close enough that the remaining
distance is constant factors rather than algorithm, and near enough to measure
properly with a corpus of that size rather than by extrapolation.

## D1087 — a corpus with bodies in it, and two more of the same

`tools/make-project.py` writes a gameplay-shaped project at any size: leaf
systems with a struct, an enum, a state machine, a frame rule and contracts,
twenty to a group, groups under a top. On it:

| lines | at e458ee2 | after D1086 | after D1087 |
| --- | --- | --- | --- |
| 12,567 | 45 ms | 29 ms | 27 ms |
| 37,587 | 289 ms | 106 ms | 83 ms |
| 112,647 | 2,157 ms | 522 ms | **282 ms** |

The two walks were the contract prover finding a call's callee by walking every
function in the graph (three times over, once per promise), and asking whether a
dotted name is out of reach by walking every file. Both are tables now.

A clean check of a hundred thousand lines is 282 ms against the 250 ms the
compiler direction asks for, and the curve is linear.

## D1088 — a million lines

`compose` and `kest_instance_of` were two more lists walked to find what was
already in them. With tables:

| lines | at e458ee2 | after D1088 |
| --- | --- | --- |
| 12,567 | 45 ms | 27 ms |
| 37,587 | 289 ms | 83 ms |
| 112,647 | 2,157 ms | 288 ms |
| 1,000,857 | — | 4,910 ms, 456 MB |

At a million lines the split is 2.0 s reading and parsing 66 MB of source,
1.3 s naming, 1.1 s bodies, 0.4 s promises. No walk the size of the program
is left. What remains there is parallel reading and less copying, and neither
is where the product's daily loop is: that is a hundred thousand lines at
288 ms against a 250 ms target.

## D1090 — the runtime, measured against the incumbent

`bench/rules.lua` is a faithful Luau twin of the gameplay-shaped workload; both
print the same checksum. Four thousand actors, two hundred rounds, best of five
whole processes, 6 ms of process start on both:

| ran by | time |
| --- | --- |
| kest | 693 ms |
| luau -O2 | 430 ms |
| luau -O2 --codegen | 208 ms |

Kest runs 2.1× the machine instructions per actor-round (6,207 against 2,913)
at a *higher* IPC and with fewer branch misses. The ceiling is machine work per
gameplay operation: about a fifth of the interpreter is the dispatch sequence,
about a fifth is packing and unpacking values.

Daslang's interpreter is close to Kest on work alone once its 100 ms process
start is taken off; its `-jit` would not run here and its AOT path is not
measured yet.

**This is the mission's central finding so far: the runtime is the ceiling, and
"extremely fast runtime" is not true today against Luau's best realistic mode.**

## D1091, D1092 — a fault found by measuring, and what two engines are for

Writing the gameplay workload through the place instead of copying the struct
found a compiler fault (`K0505` on `who[at].cools[i] = n`), fixed in D1091 —
and the shape that avoids the copy turned out to be *slower*, so value structs
are being paid for here rather than paid.

The gameplay workload in four languages, same checksum, machine instructions
(the clock on this shared box moves ±50 per cent between afternoons; the
instruction count does not):

| | instructions | against C++ |
| --- | --- | --- |
| c++ -O2 (`bench/rules.cpp`) | 0.43 G | 1.0 |
| luau -O2 --codegen | 1.27 G | 3.0 |
| luau -O2 | 3.23 G | 7.5 |
| kest | 5.31 G | 12.4 |

The machine spends 34 machine instructions per bytecode operation, a fifth of
it dispatch, and half the operations it runs move values between slots and the
stack. Fewer operations and cheaper ones together land near Luau's
*interpreter*; nothing an interpreter does reaches its native code generation.

So D1092: **two engines.** The machine stays what a program is developed
against — it compiles a hundred thousand lines in 282 ms and carries the
debugger, the profiler and the contracts. Shipping gets native code generated
as C11 out of the same checked IR, through the host's own compiler, linked with
the same runtime. Both must answer the same, the way optimizer-on and
optimizer-off already do.

## D1093 — the C backend exists, and what the first of it is worth

`src/emitc.c` is handed each body the same way the lowering is, and the build
hands each one to both: one reading of the program, two backends. `kest emit
--c` writes one translation unit; a body it has no C for is named in the file
with the reason and is a body the machine runs.

Over this tree it writes **627 of 2,088 bodies**. What stops the rest, in
order: a crossing into the host, making an array, asking how long one is, an
equality over something wider than a number, and text.

Two scalar workloads, four million rounds each, machine instructions:

| workload | the machine | generated C | |
| --- | --- | --- | --- |
| an actor's rule: five states, four numbers, a call a round | 3.04 G | 0.187 G | 16× |
| a step: four floats, two comparisons, a call a round | 3.34 G | 0.144 G | 23× |

Cycles move further than instructions (1.22 G against 0.049 G on the first).
Both answer the same number under both engines. Two things are in those ratios
and only one is dispatch: the host's compiler also inlines the call, keeps the
frame in registers and strength-reduces the loop.

**These are scalar bodies and reach nothing the runtime owns.** A workload that
touches an array pays the same bounds check, the same generation check and the
same layout in both engines, so the next number is the one that matters and it
is not this one.

`tools/check-c.sh` holds the two engines to being one language: every program
in the tree written as C and compiled by the host's compiler, five programs the
check writes itself run both ways for the same answer, and how much was left
out read back and held to being neither nothing nor everything. Two backstop
holes have been seen catching it.

*(That paragraph is what the check was on the day this was written. It is
twenty-one programs of its own now, forty-one of the tree's, one run both ways
inside a single process, and twelve holes; and the rule about writing
everything went when there was C for a crossing. See D1108 and D1111.)*

## D1094 — the seam, and the first number from a real program

A chunk may carry a C function now, and a call enters it instead of the
instructions: a frame is pushed for it, a refusal inside it is reported at the
same line with the same words, and the file this backend writes is a host of
the program it was written from — it builds that program, binds what it wrote,
and calls `main`. Which engine runs a body is a fact about the build.

`bench/control.kest` with its one writable body compiled, by the delta method
over twice the rounds:

| | an actor-round |
| --- | --- |
| the machine | 1,333 instructions |
| with `decide` compiled | 1,144 instructions |

**Fourteen per cent, for the fifth of that loop which is the call. The other
four fifths are array elements read and written in `main`.** That is the
measurement that says what to write next, and it says elements.

The check now runs thirty of this tree's own programs both ways, on their
answer and their words, and asks each one whether the compiled half was
entered at all. Its first sweep caught a miscompilation: text constants were
written into the C as addresses in the compiling process.

## D1095 — elements, and the first workload the release engine wins

Element reads and writes are two doors and a run of moves: the bounds and
handle checks are a call, and the layout walk is gone because which piece sits
at which byte is known while compiling. Only a body that cannot reach the heap
may hold a handle, which is read off the body and off what its callees
promised — the `no.alloc` promise earning something beyond being checked.

`bench/kernel.kest`, every row answering the same checksum, instruction counts,
whole process and then a body-step by the delta method:

| | whole process | a body-step | against g++ |
| --- | --- | --- | --- |
| `g++ -O2` | 0.052 G | 23 | 1.0 |
| **Kest, the C backend** | **0.270 G** | **119** | **5.2×** |
| `luau -O2 --codegen` | 0.331 G | 158 | 6.9× |
| `luau -O2` | 1.008 G | 491 | 21× |
| Kest, the machine | 1.087 G | 529 | 23× |

**The first row of the mission's own question answered: on this workload the
release engine is a quarter cheaper than Luau's best realistic mode**, where
the machine alone is level with Luau's interpreter. One workload, and the one
most favourable to a native backend.

## D1099 — the gameplay workload, measured

With the doors for what reaches the heap, eleven of `bench/rules.kest`'s
fourteen bodies compile. Whole processes, every row answering the same
checksum:

| | instructions | against g++ |
| --- | --- | --- |
| `g++ -O2` (`bench/rules.cpp`) | 0.429 G | 1.0 |
| **Kest, the release engine** | **1.244 G** | **2.9×** |
| `luau -O2 --codegen` | 1.294 G | 3.0× |
| `luau -O2` | 3.323 G | 7.7× |
| Kest, the machine | 6.254 G | 14.6× |

**D1092's re-evaluation trigger was three times `bench/rules.cpp`. This is
2.9.** The architecture holds, and on the workload written to be gameplay
rather than a kernel the release engine is level with Luau's best realistic
mode — where the machine alone is twice Luau's interpreter.

That is the mission's binding question answered on both workloads there are:
what a serious game developer gets here that Luau does not give them is the
same runtime ceiling with a check loop of 282 ms for a hundred thousand lines
and a language that refuses what it cannot prove.

## D1112 — the kernel's five times, read down to 2.8

What a run of elements is in memory is in the public header now (`KestRun`,
held to the machine's own spelling by `_Static_assert`), so reading one is
four lines of C rather than a call to a door. What the call cost was never the
call: it was everything the host's compiler could not do across it.

`bench/kernel.kest`, per body-step, delta method over two million of them:

| | a body-step | against `g++` |
| --- | --- | --- |
| `g++ -O2` | 23.1 | 1.0 |
| **Kest, the release engine** | **65.5** | **2.8×** |
| Kest, before this | 105.1 | 4.5× |
| `luau -O2 --codegen` | 157.9 | 6.8× |
| Kest, the machine | 526.7 | 22.8× |

On the workload the comparison was worst on, the release engine runs two and a
half times fewer instructions than Luau's native tier.

## D1108, D1109 — the crossing into the host, and the backend is whole

The last family, and the biggest: forty-five of the sixty-two bodies left were
a call out to the host. The release engine writes **2,071 of this tree's 2,088
bodies**, and `bench/rules.kest` — the gameplay reference program — compiles
whole, printing included.

| | instructions | cycles |
| --- | --- | --- |
| `g++ -O2` (`bench/rules.cpp`) | 0.429 G | — |
| **Kest, the release engine** | **1.246 G** | 0.704 G |
| Kest, the machine | 6.311 G | 3.882 G |

The ratio against `g++` has not moved since D1099. What moved is that there is
nothing left of that program for the machine to run.

With one of a fixed run written too, and the arithmetic a generated file's own
host now offers, the backend writes **2,081 of 2,088** and 41 of this tree's
programs run both ways rather than 30. *(D1119 wrote the last seven: it is all
2,088 now, and 45 run both ways.)*

The seven questions a crossing asks are asked in one place now, which also
turned up a fault in the machine that could not be seen before: the check
holding a crossing to what a host was measured for read where the run began
off `running_top`, which a compiled body raises to the top of its own frame.
It reads the first frame of the run instead.

## D1105, D1107 — one language, two ways of running it

A body the backend cannot write is handed to the machine through a call of its
own, and a call through a function value is written too, so one refusal at the
bottom of a program no longer takes the chain above it. `settle()` is gone;
over this tree the backend writes **1,692 of 2,088 bodies**, and every
workload in `bench` now has its `main` compiled.

`bench/rules.kest` is unchanged at 1.239 G instructions against the machine's
6.232 G — what this buys is breadth, not speed. What it proves is the seam: a
program that recurses compiled → machine → compiled until it runs out of
frames refuses at the same depth, with the same words and the same call chain,
under both engines. That is what D1092's two-engine decision rests on.

**What is left is one door.** 45 of the 62 bodies this backend does not write
are the crossing into the host.

## D1104 — text whole, and where it says the ceiling is

Reading into text, making it, and the seven things a run of elements does that
a program building text needs. After them `std.text` compiles whole,
`bench/words.kest` goes from 2 of its 28 bodies written to 25, and the backend
writes **1,509 of 2,088 bodies** over this tree.

Two text workloads, delta method, whole processes:

| | the machine | the release engine | |
| --- | --- | --- | --- |
| scanning text, no allocation | 25.2 M | 8.8 M | 2.9× |
| building two thousand pieces a round | 357.3 M | 255.0 M | 1.4× |

**The second is the finding.** Building text is bound by the allocator and by
`memcpy`. Compiling the bodies around them takes off what dispatch cost and
leaves the rest standing, so a workload that spends its time in the runtime
gets runtime numbers whichever engine drives it. That is the same thing D1095
found about arrays, met again where it costs more — and it is what the
game-shaped runtime profile below is for.

## D1102, D1103 — a world of entities, and text put in order

Stores and the references into them are written as C, and so are the six ways
two pieces of text can be compared. A world of two thousand things each naming
the next, walked twenty rounds: 103.1 M instructions by the machine against
23.5 M compiled. Six hundred names compared in two loops: 259 instructions a
comparison against 95, which is 2.7× — the comparison itself is one door both
engines call, so what compiling took off there is the loop around it and not
every door pays 4×.

A reference to a place that has been handed back reads as nothing under both
engines, because the compiled half asks the same `resolve_ref` the machine
does. That is the property a persistent world most needs and the one a second
backend could most easily lose.

Over this tree the backend writes **928 of 2,088 bodies**.

One thing both decisions paid for about checks: a door and an instruction that
are one answer are one answer to a hole as well, so breaking the door breaks
both engines and the differential sees nothing. Both holes are in the
backend's own half instead.

## Where section 34 stands

Thirty-one criteria. **Thirty are met by something that runs**, and the last of
them -- supported CI green on the exact final HEAD -- went green on `6a1be89`,
all nine jobs, and again on `3b2607c`.

Committing past a green commit makes that criterion false again until CI runs
on the new HEAD, which is what CI is for and what it is being asked.

**The last is now measured.** The AI section asks that "task
completion/time/repair/silent escapes" be measured, and the suite has been run
blind with a model, seventy-two times (D1148). Kest 36 of 36 and Luau 30 of 36,
the six being two tasks whose hidden tests held a rule the task did not say;
30 of 30 each on the ten asked fairly. The measurement exists and what it says
is that this suite does not tell the languages apart on correctness, and that
Kest costs more to get there. The repeated Kest-specific failure classes it
turned up are addressed where practical: three refusals say what to write now.
The docs say what the evidence says and no more.

So every criterion is met by something that runs but one: supported CI green
on the exact final HEAD, which a commit cannot meet by itself. It is marked in
the commit after CI answers for this one. `docs/report.md` says the same thing
in its own words, including that the claim the owner asked for first was
measured and not shown.

## The report

`docs/report.md` answers the thirty questions section 35 asks (D1140). What it
names as still weak: **no model has been run against the AI suite**, so the one
claim the suite exists to settle has an instrument and no result; daslang's AOT
is named and not measured; and readability is measured in tokens, which is a
proxy. The highest-value next direction it names is running models against the
suite, and the second is reading the generated C against what `g++` writes for
the same workload.

## Open, in priority order

1. *(done, D1119)* **The doors.** The backend writes **every one of this
   tree's 2,088 bodies** (D1105 to D1109, D1112, D1119). The one thing left
   that it will not write is a run of numbers with an infinity in it, whose
   bits cannot go in an initialiser; `check-dead.sh` and two of
   `check-c.sh`'s own programs keep a body like that on purpose, so that the
   door a compiled body hands work to the machine through has a user and the
   holes that watch it have something to catch. Shipping one of these files is
   written and held: a generated file exports `kest_natives_here` for a
   game's own host, and the check runs one program twice in one process,
   compiled and not, through a door that calls back in (D1111). The
   **generated file's own host** offers writing, the arithmetic, a clock, the
   arguments and the files now (D1109, D1116), so 45 of this tree's programs
   run both ways where 30 did; the four that still cannot want the command
   line's own toy engine, which is not a generated file's business.
2. *(done, D1112)* **The five times on the kernel, read down to 2.8.** What
   a run of elements is in memory is in the public header now, so a read is
   four lines of C rather than a call: the host's compiler hoists the length
   out of the loop, keeps the block in a register, and knows nothing moved the
   array. `bench/kernel.kest` is 65.5 instructions a body-step where it was
   105.1, against `g++`'s 23.1 and Luau's native tier at 157.9. What is left
   against `g++` is a move per piece — a packed `f32` is four bytes and a slot
   is eight — and that is a different decision.
3. *(closed by D1102)* A world of entities: stores and the references into
   them are written, `bench/agents.kest`'s working bodies are all compiled,
   and a world of two thousand things walked twenty rounds runs 4.4 times
   fewer instructions. A reference to a place that has been handed back still
   reads as nothing, which is the property a game most needs from this.
4. *(done, D1116)* **The table, with both engines in it.** Whole processes,
   best of five, wall clock, on a machine somebody else was also using:

   | workload | kest | **kest, compiled** | `g++ -O2` | `luau -O2` | `luau --codegen` | daslang |
   | --- | --- | --- | --- | --- | --- | --- |
   | kernel | 127 ms | **27 ms** | 13 ms | 108 ms | 49 ms | 180 ms |
   | control | 186 ms | **34 ms** | 13 ms | 127 ms | 57 ms | 168 ms |
   | graph | 79 ms | **9 ms** | 7 ms | 19 ms | 15 ms | — |
   | words | 48 ms | **33 ms** | 17 ms | 36 ms | 32 ms | — |
   | rules | 909 ms | **122 ms** | 64 ms | 637 ms | 182 ms | — |

   Ahead of Luau's native tier on four of the five and level on `words`,
   which is allocator-bound. **Daslang's AOT is named and not measured**:
   `daslang -exe` is a row in the harness, and this build of daslang has no
   LLVM behind it, so `-exe` and `-jit` both refuse. What it would take is
   building daScript with LLVM, which is not this tree's to build. Every
   daslang row runs without its module cache (D1118), which is what the rows
   beside it do and what keeps its directory out of this tree.
5. *(done, D1137)* **The AI suite against the list that binds it.** Eleven of
   the fourteen in the mechanisms document -- which is a menu and says so --
   and **all nine the mission itself lists in section 20**. The last of the
   nine was *use host API*, and it was missing because the runner had no host:
   a task about the doors a host gives cannot be judged by a program on this
   side of them. `ai/run.sh` builds `kest/host.c` against `libkest.a` where a
   task has one, and it answers `checks N` like everything else. `crossing` is
   that task -- three doors whose *answers* are the whole of it, a run the
   engine lends that cannot grow (`fit` into one is K0608), and a wrong answer
   that reads a refusal as an id, caught at the same check in both languages.
   Running models against it is the owner's, at the end.
6. *(done, D1139)* **Generic API use.** `among` is three small functions that
   have to work for whatever they are handed, each asked of two sets that
   share nothing. Three things it is about and no type catches any of them:
   nothing is what there is rather than a value of the set standing for it,
   the first is the first rather than the last, and a run somebody else owns
   may be too small. Both languages fail the wrong answer at `checks 1`.
   Twelve of the menu's fourteen now; the two left are a hot-update-compatible
   change and a task about obeying `no.host`.
7. **The two families left, which are the menu's rather than the mission's.**
   Twelve of the **fourteen** the mechanisms document lists are written
   (D1101, D1106, D1110, D1113, D1114, D1121, D1125, D1137, D1139), each with
   held-out tests, a wrong answer beside it and a gate that holds all three --
   and two of the twelve are narrower than the family they sit under:
   `spread` refactors within one module rather than across modules, and
   `saved` is save and load rather than a change to a save format. Five of
   them are the kinds a type system cannot be credited for: a bug to find in
   code that compiles and analyses clean in both languages, input that is
   mostly wrong, a save somebody else wrote, a refactor that has to mean
   exactly what it meant, and doors whose answers must be read. The two left
   are **a hot-update-compatible change**, which wants the reload protocol
   `examples/engine` drives, and **a task about obeying `no.host`**, which
   every task here carries and none is about. `ai/README.md` holds the table.
   Running models against it is the owner's, at the end.
8. *(done, D1117)* **Game-shaped runtime profile**, on `bench/rules.kest`.
   The machine: dispatch and the instruction bodies 54%, working out where a
   refusal would be reported 16.5%, value movement 22%, the collector and the
   heap 0.4%. The release engine: the compiled bodies 73%, the ledger frame
   pushed and popped per call 14%, the runtime 5%, startup the rest. **The
   collector is not the ceiling**, which is what D1005 said and this measures.
   The 16.5% was a defect and is gone: `bench/agents.kest` went from 18.31 G
   instructions to 6.84 G, and from 11.40 G cycles to 3.80 G. What the profile names next is
   `kest_native_room` — see item 7.
9. *(done, D1122)* **The ledger frame.** What a call keeps is in the public
   header now, held to the machine's own shape by `_Static_assert`, and a
   body that makes calls reads it once and writes each call out.
   `bench/control.kest` 242.1 M instructions down to 194.2 M (20%),
   `bench/rules.kest` 1,048.1 M down to 904.2 M (14%), `bench/agents.kest`
   3,013.2 M down to 2,705.8 M (10%), `bench/kernel.kest` unmoved. The
   ceiling was measured first — a build with no ledger at all runs `rules` in
   0.755 G — so half the 28% is the ledger itself, and the ledger is what a
   refusal deep in a compiled program is reported from.
10. *(done, D1123)* **What a frame costs, and what its worst one costs.**
   Every tail figure here was the machine's until `bench/frame` was linked
   with what the C backend wrote for its own program. Twenty thousand bodies
   a frame, five hundred frames, one checksum across every row:

   | | p50 | p95 | p99 | max | mad |
   | --- | --- | --- | --- | --- | --- |
   | a lent frame, the machine | 1812 µs | 2921 | 5065 | 8168 | 123 |
   | **a lent frame, the release engine** | **306 µs** | **331** | **339** | **403** | **3.6** |
   | a crossing a body, the machine | 1892 µs | 2195 | 4895 | 13322 | 57 |
   | a crossing a body, the release engine | 1179 µs | 1239 | 1465 | 4552 | 5.7 |
   | the same arithmetic in C | 63 µs | 67 | 71 | 97 | 1.2 |

   **The worst frame the release engine had is below the median frame the
   machine had.** At sixty frames a second, twenty thousand bodies cost it
   2.4% of the budget at its worst and the machine 49%. Against hand-written
   C it is 4.9× at the middle and 4.8× at the ninety-ninth. Over five hundred
   frames: one allocation, no walks — **a frame that promised `no.alloc`
   cannot be interrupted by the collector**, and the instrument says so rather
   than the source. The C row is the control and the instrument says whether
   its own tails are worth reading, because on a shared machine pure C showed
   a ninety-ninth of 786 µs against a middle of 62.
11. *(done, D1124)* **What a serious integrated gameplay slice gets.**
   `bench/tails.sh` writes the C for whatever program it is given, builds
   `bench/measure` around it, and runs both halves. `examples/slice` -- a
   colony with references at each other, rules promising `no.alloc`, churn
   every round, and a save written as text and read back -- over sixty calls:

   | | the machine | the release engine |
   | --- | --- | --- |
   | a call, p50 | 1.613 ms | **0.640 ms** |
   | p95 | 1.949 ms | 0.849 ms |
   | p99 | 3.438 ms | 0.893 ms |
   | max | 4.089 ms | 2.796 ms |
   | dispersion | 0.171 ms | 0.092 ms |
   | compiling | 3.640 ms | 5.030 ms |

   **Two and a half times, where a frame of arithmetic was nearly six.** An
   integrated program spends its time in the runtime, and compiling the bodies
   around that leaves the rest standing — the same thing D1104 found in text
   and D1112 in the kernel. The heap does the same thing under both engines to
   the byte (125,466 allocations, 8,303,328 given, 31 walks, 350 plots made
   and 326 handed back), which is a measurement and a proof at once. **The
   collector's longest pause on it is 0.18 ms by the machine and 0.23 ms
   compiled** — one and a half per cent of a sixty-hertz budget on a program
   making a hundred and twenty-five thousand allocations in sixty calls.
12. *(done, D1127)* **The development loop, timed.** A reload costs **0.48 ms,
   of which 0.44 is building the program**; starting a machine, putting the
   world back into it, asking the seven doors and swapping are 0.05 ms between
   them. The reload path is a compile, so what it costs on a real project is
   what a compile costs on it -- and a compile has not moved across the whole
   backend era: **112,647 lines checked in 286 ms** against D1088's 288, and a
   million in **4,644 ms** against 4,910. A refusal costs 0.4 to 0.8 ms and
   stops at one of three places: building it (five of twelve edits), the shape
   of the world (four), or the doors the host calls (two).
   Timing it found a defect: the host asked for its doors *after* it had
   published the candidate, so a signature that moved left it running the new
   program while saying the world was the one it was. The doors are asked of
   the candidate now, and the gate holds every refused edit to ending where a
   run with no edit in it ends.
13. *(done, D1128)* **The newcomer workflow, run rather than described.** The
   README says an archive is installed by unpacking it and putting `bin` on
   the path. It was not: a shell that finds `kest` on `PATH` hands over the
   bare name, the two probes for the library resolved against whatever
   directory the caller was standing in, and the first command after
   `kest new` failed with `cannot read /usr/local/lib/kest/std/io.kest`. The
   same binary named with a path worked, so the one route the README
   recommends was the one that did not. `kest_library_path` walks `PATH`
   itself now. All six commands the README lists run from an unpacked archive
   with nothing installed and nothing in the environment, and `kest doctor`
   ends with `nothing here is wrong`. The gate lays out a directory like an
   unpacked archive and stands somewhere else; CI's package job runs it that
   way too, rather than only with `KEST_LIB` exported.
14. *(done, D1129)* **The rest of the documented workflow.** `make install
   PREFIX=...` puts twelve files there and a program importing `std.io` runs
   from it both named with a path and found on `PATH`; `make uninstall`
   removes every one of them. And the README's sentence about the editor --
   that `kest lsp` is this compiler, so what an editor says about a file and
   what `kest check` says cannot differ -- is held rather than asserted: four
   mistakes of four kinds, compared code by code, line by line, column by
   column and word by word against `kest check --json`.
15. *(done, D1131)* **Real edit latency, in an editor, on a large project.**
   Driving `kest lsp` over a pipe on 112,647 lines: **a keystroke in a leaf
   system is 0.3 ms**, in a group importing twenty systems 3.0 ms, and in the
   one file that imports all ninety groups 475 ms. Edit latency follows what
   the file imports and not how big the project is -- at 62,607 lines the same
   three are 0.2, 3.0 and 221 ms, and only the top moved. **So there is no
   resident compiler, by measurement rather than preference**: for the file
   somebody is editing it would save nothing measurable, and for the one file
   that imports everything it would buy half a second at the price of
   invalidating a symbol graph correctly everywhere else.
   And the editor stays right when the disk moves under it: rename a function
   in a dependency without touching the buffer and both sides say `K0353` at
   the same line and column. The gate holds that now.
   **Every one of these runs opened one file**, which is where item 23 came
   from: what was measured was measured right, and the scenario was never
   widened.
16. *(done, D1132, D1133)* **What a refusal costs and what it leaves.** F10,
   open since the foundation, is measured rather than read from the source: a
   refusal inside a body that promises `no.alloc` raises K0604 at the line
   that asked -- reading the file again and drawing a caret -- and leaves the
   program heap at 912 bytes with 912 ever asked for. A build where the
   refusal path takes sixteen bytes of that heap does not fail the assertion;
   it dies, because taking heap runs the collector over a machine in the
   middle of refusing.
   And the failure-and-state policy is explicit now that it is measured.
   **A fault is not a rollback and does not spoil the machine**: a body that
   stamps every row and then writes a second field past the end leaves the
   first pass whole, part of the second there, and answers the next call on
   the same half-written world. The reference says so beside the `defer` rule,
   with the three answers a host has, and `examples/embed` runs all three
   halves of it.
17. *(done, D1134)* **A file a run left in the tree.** `kest-colony-day.txt`,
   thirteen bytes, in the root of the repository: `examples/colony.kest`
   writes it beside whatever runs the program and `std.os` has no way to take
   a file away, so nothing tidied up. The tree check asked what a compiler
   made, whether a name has a space in it, and whether something `make clean`
   takes away got committed -- and a text file a program wrote is none of the
   three. `make clean` takes it now, and the fourth walk is there: the tree
   before the gate ran against the tree afterwards, with whatever `make clean`
   names allowed.
18. *(done, D1135)* **The Kest-specific mistake nothing answered.** Writing
   the three task files for D1125 turned up what a model gets wrong here, and
   the compiler answered all but one at the line: `pop` gives `T?` and not `T`
   (K0310), `none` is a keyword and not a name (K0201), `table.find` against
   `table.get` (both `i32?`, which is what the `nearby` task is about), and
   the one form (`kest fmt --check`). The one nothing answered was
   `let xs: [i32] = array()` followed by `fit(xs, 1)`: `array()` makes an
   array with no room, `fit` writes where there is room, so it writes nothing
   and says nothing -- the shape a body under `no.alloc` falls into, because
   `push` is refused there and `fit` is what is left. **K0347** says so now,
   at the `let`, as a warning for the reason K0346 is one. Quiet where `room`
   or `push` is called on the name or where it is handed to another function
   at all, and no false positives over every `.kest` file in the tree.
19. *(done, D1136)* **The tier between a tenth of a second and half an hour.**
   `make fast` is a quarter of a second and `make check` is half an hour on
   this box, and nothing sat in between -- so a task file, a paragraph or a
   one-line fix each cost a full gate. Timed one at a time, the eleven tools
   beside the sweep come to **about forty seconds of wall clock together**
   (ceilings 39s, costs 22, dead 20, tables 12, fmt 8, c 7, commands 4, docs
   2, the rest under 2); the sweep that puts every check out of order was
   killed at thirteen minutes without finishing, 899 holes each a tree of its
   own with a build in it. **`make most` is everything but that sweep: 356
   seconds.** It is the one check here about the *other checks* rather than
   about this language, so what it answers changes when a check changes and
   not when the compiler does. Its row says it was left out and its last line
   says `everything but the holes passes`, so no run of one reads as a run of
   the other and `LAST FULL GATE` goes on meaning `make check`. The other half
   of the slowness is the box: `/bin/true` costs 5.7 ms here and the load
   average sits at 31 to 37 on twelve cores, most of it other tenants.
20. *(done, D1138)* **What the same work costs a reader.** Of the seven things
   asked for, *very readable* had nothing behind it. The suite is the one
   place where identical gameplay work is written twice by the same hand:
   `ai/reading.sh` counts **2,463 words of Kest against 2,716 of Luau** for
   the answers, and **1,240 against 1,213** for the scaffolds -- what has to
   be read before a word of the answer can be written. Where the work is about
   things that may not be there, Kest is a little over half the words
   (`saved` 60 per hundred, `frail` 55); where it is shuffling data known to
   be there it is a tenth to a quarter longer (`nearby` 125, `stale` 111),
   because the types are written down. Not a claim that fewer is better --
   what it is for is that the difference is small and known. Tokens are a
   proxy for reading and not the thing itself; people would settle it, and
   that is the owner's to run.
21. *(done, D1142)* **A comment that measurement made false.** `src/emitc.c`
   said the element guard is four lines the host's compiler sees through --
   that it hoists the length out of a loop and keeps the block in a register.
   It does not, and the reason is the shape of a frame rather than gcc: a
   gameplay loop reads an element, **calls a body**, and writes one back, and
   a call may allocate, so nothing about the run survives it as far as the C
   compiler can tell. Split, the guard is **25.1 for the two tests asking what
   the handle is and 14.6 for the bounds** -- and the larger part is the
   invariant one, because this heap is non-moving. What would have to be
   proved to hoist it is that the slot is not written between the uses, which
   is a question inside one body.
22. Comparators, kept in step as the engines move. The harness names the mode
   of every row (D1090), which is what stops an interpreter's number being
   read as a compiler's.
23. *(done, D1143)* **The file an editor is asking about.** `kest lsp` kept
   one document, under a comment saying an editor has one file in front of the
   person using it. Driven through the protocol -- open A, open B, change A --
   the `K0306` typed into A was published **against B's name** while A was told
   it was clean, and hover, definition, rename and formatting all read the same
   one text. Beneath it, the loader's overlay held one path, so a file that
   imports one the person has edited and not saved was checked against the
   saved copy. The server keeps every open file now and chooses the one a
   message names out of its uri before it dispatches; the overlay is a set;
   there is still one build, with the file it is of beside it, so a request
   about another rebuilds at the price of a keystroke in it. Held by the check
   that was already there, with a third leg, so the sweep keeps the holes it
   has. **What this says about the discipline**: the 899 holes ask whether the
   code is wrong and not whether the scenario was narrow, and a thing measured
   carefully and never widened is a place where nothing has been seen.
24. *(done, D1144)* **A net that missed on one machine.** The whole gate
   refused here and not on CI with `MISSED: a ceiling crossed in a scratch
   arena and not carried back`. The check held a file to `K0658` under
   ceilings of 1000, 2000 and 4000 bytes; the hole shows only inside a
   1956-byte window whose start moves with the length of the file's path, and
   the gate nests its rooms, so under a plain `/tmp` the window is 2020 to
   3976 -- between the rungs. The ceiling is walked a quarter of a kilobyte at
   a time now. Every replication by hand had been one room deep.
25. *(done, D1146)* **A game written in it.** `/home/kest/colony` is a colony
   game whose rules are six hundred lines of Kest and whose host is raylib.
   Two thousand people on a 512-by-512 map are 0.74 ms a frame in the rules
   as C and 4.7 ms on the machine; a reload that keeps the world is 4.7 ms
   small and 50 ms for 512 people on 196,608 cells; a broken edit is refused
   in a millisecond with the diagnostic in the game's corner. Reading the
   ground where it lies rather than having the program copy it was 288 µs
   against 10. It found D1145 and D1147, and a 26% number for inlining.
26. *(done, D1145)* **A library named without a slash.** `kest_build` ran the
   library and a module's path together; `KEST_LIB` never did.
27. *(done, D1147)* **`Case -> {` in a `match`.** Said as what it is, one
   diagnostic an arm with the arm's own fix, where it was one about `if` and
   then a line about an arrow for every arm after it.
28. *(done, D1148)* **The AI suite run blind.** Seventy-two runs of an Opus
   model, each in a room holding only what it may see. Kest 36 of 36, Luau 30
   of 36, and the six are the suite's: two tasks whose tests held an unstated
   rule, stated now. No difference on correctness; Kest costs more runs and
   more tokens, mostly the reference. Three repeated Kest mistakes are told
   what to write now. The AI claim is not shown by this suite.
29. *(done, D1151)* **A world a host keeps, as doors.** Eight doors in the
   running family do what every host keeping a world wrote by hand, reload
   through the program's own `save` and `restore` included.

## Closed by measurement

- **The edit loop** (D1100): 7 ms for a real small project, 366 ms for 112k
  lines, 4.4 s for a million, from cold. A mistake costs no more than no
  mistake, one file on its own is milliseconds at any scale, and what `check`
  prints costs nothing measurable — which also closes the item that said the
  declaration listing cost as much as checking at scale. No daemon, no
  persistent session, no incremental state; what would earn one is written
  down instead.

## Rejected so far

Nothing yet. No mechanism from the direction documents has been adopted or
ruled out; the first measurement said the clean algorithm was the problem, and
that is what was fixed.
