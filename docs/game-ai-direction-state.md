# Where the game-first / AI-native work is

This is the operational state for the mission in
`/home/kest/mission/direction/`. It is read first by whatever resumes the work
and written before every invocation ends. `docs/decisions.md` holds the
reasoning; this holds the position.

    MISSION START SHA: e458ee2c5387b7181c08cbe5e530a0c75f6d3812
    CURRENT SHA:       (this commit) D1093-D1101
    PHASE:             B — the release engine: level with Luau's native tier
                       on the gameplay workload, and inside D1092's trigger
    LAST FAST GATE:    green
    LAST FULL GATE:    green at 96285c5
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

## Open, in priority order

1. **The rest of the doors**, in the order the tree asks for them: a crossing
   into the host (44 bodies), what reads and makes text (33 and 21), a store
   and what walks one (34 and 20), a call through a function value (17). None
   is in the way of the numbers below; what they buy is breadth — how much of
   a whole game compiles rather than how fast the part that does runs.
2. **The five times on the kernel, read down.** A call per element, a bounds
   check the C compiler cannot hoist because it is behind that call, and a
   `memcpy` a piece where four doubles could be one. All three go away by
   putting the array's header in a header, which makes the shape of a handle
   part of what a generated file is compiled against and so part of what the
   abi version carries.
3. The other half of a game: a world of tens of thousands of entities with
   references into it, measured the same way, because the rules workload is
   small arrays and a cold allocation path.
4. Daslang's AOT path, measured and named as AOT, so the comparison is against
   what its documentation points at rather than against its interpreter.
5. **More of the AI suite.** Two tasks of the nine kinds the mission lists
   are written (D1101) with held-out tests, a wrong answer beside each and a
   gate that holds all three. What is not written: repairing a bug,
   refactoring across modules, a host API, save and load, failure handling.
   Running models against it is the owner's, at the end.
6. Game-shaped runtime profile: where the ceiling actually is (dispatch, value
   movement, allocation, collector, host crossing) on `examples/slice` and the
   engine, now that the release engine changes which of them matter.
7. Comparators, kept in step as the engines move: Luau in its best realistic
   gameplay mode, Daslang's interpreter and its AOT named separately.

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
