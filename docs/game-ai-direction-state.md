# Where the game-first / AI-native work is

This is the operational state for the mission in
`/home/kest/mission/direction/`. It is read first by whatever resumes the work
and written before every invocation ends. `docs/decisions.md` holds the
reasoning; this holds the position.

    MISSION START SHA: e458ee2c5387b7181c08cbe5e530a0c75f6d3812
    CURRENT SHA:       the commit this file arrived in
    PHASE:             A — baseline, and the clean-check curve
    LAST FAST GATE:    green
    LAST FULL GATE:    green at e458ee2; running again for this work
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

## Open, in priority order

1. Measure a million lines with the same generator, and decide whether the
   remaining distance to the targets is algorithm or constant factor.
2. Attribute what is left of a clean check at 100k: loading/lexing/parsing, the
   checker itself, and `kest check`'s own listing, which is output rather than
   verification. A check that only answers "is this valid" should not pay for a
   full declaration listing; decide what `check` prints by default.
3. Measure the edit loop the way an agent drives it: edit → check → diagnostic,
   including process start, on the 100k corpus. Only then decide whether
   persistence or incrementality is worth its correctness cost.
4. Game-shaped runtime profile: where the ceiling actually is (dispatch, value
   movement, allocation, collector, host crossing) on `examples/slice` and the
   engine, before touching the VM.
5. Comparators: Luau in its best realistic gameplay mode, Daslang interpreter
   and AOT named separately. Not before our own numbers are understood.
6. The AI mistake corpus and the silent-error interception measurement.

## Rejected so far

Nothing yet. No mechanism from the direction documents has been adopted or
ruled out; the first measurement said the clean algorithm was the problem, and
that is what was fixed.
