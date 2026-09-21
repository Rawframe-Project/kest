# The tasks a model is given

A small paired suite: the same gameplay work, written twice — once in Kest and
once in Luau — with tests whoever does the task is not shown.

What it is for is the one claim about this language that cannot be measured by
running a program: that code written with a model is easier to get right here.
That has to come out of task outcomes rather than out of feature names, which
is what the mission this was written for says in so many words.

## What a task is

```text
ai/tasks/<name>/
  ask.md                the task, the same for both languages
  kest/start.kest       the scaffold, with the shapes and the signatures
  kest/done.kest        the answer written here
  kest/astray.kest      a plausible wrong answer
  kest/checks.kest      the tests, which the answer never sees
  luau/start.lua        the same four in the other language
  luau/done.lua
  luau/astray.lua
  luau/checks.lua
```

The tests live beside the task rather than beside the answer: what a task is
judged by is not something the answer can read. Each check answers with its
own number, so a failure says which sentence of `ask.md` was not kept.

## Running one

```text
ai/run.sh <task> <kest|luau> <the answer>
```

It writes the number of the first check the answer did not keep, nought for
all of them, or `refused` and what the compiler or the runtime said. A
refusal is not a failing check: it is the language catching the mistake before
a test did, which is exactly what this suite is here to count.

Luau is found rather than built: `KEST_LUAU` says where it is.

## What it costs to be told

`ai/cost.sh` reads that off the suite with no model near it: the loop somebody
edits in, which of the two caught each wrong answer, how long being told took,
and whether a compiler's complaint names a line the repair is on.

```text
KEST_LUAU=/where/luau/is RUNS=15 sh ai/cost.sh
```

It measures and does not check, for the reason none of `bench` does. See D1126.

## What holds it

`tools/check-ai.sh`, which `make check` runs. For every task and every
language it asks three things: the answer written here keeps every test, the
scaffold nobody filled in does not, and the plausible wrong answer is caught.
A suite nobody has seen fail is indistinguishable from no suite.

## What is here

| task | what it is about |
| --- | --- |
| `cooldown` | a gameplay feature, under a promise to reach no heap |
| `stale` | a handle to something the world has taken out |
| `patch` | a bug to find in code that compiles and analyses clean |
| `frail` | input that is mostly wrong, and what a reader does about it |
| `saved` | a world written down and read back, and the saves that are not one |
| `spread` | one function that does three things, moved into three |
| `called` | doors somebody else wrote, and the order you call them in |
| `stepped` | a fixed-step update, and the same time split into different frames |
| `nearby` | library names that are nearly the one you want |
| `pooled` | taking from a pool under a promise to reach no heap |

**Ten of the fourteen task families the mission lists**, and two of the ten
are narrower than the family they sit under. The mission's list, and what is here
against it:

| family | here |
| --- | --- |
| implement feature | `cooldown` |
| repair bug | `patch` |
| refactor across modules | `spread`, which is within one module |
| obey `no.alloc` | `pooled`, and `cooldown` carries it |
| obey `no.host` | every task carries it; no task is about it |
| deterministic update | `stepped` |
| ref-safe store logic | `stale` |
| host API use | — |
| save/load change | `saved`, which is save and load rather than a change |
| hot-update-compatible change | — |
| generic API use | — |
| error handling | `frail` |
| callback/context use | `called` |
| near-miss API names | `nearby` |

`pooled` is the one the compiler answers rather than a test: the wrong answer
written for it keeps a scratch list of what it took, which is something that can
grow, and `no.alloc` is refused before the program runs. The same wrong answer
in Luau is a table nobody counts, so what catches it there is a hidden test on
what the pool was left holding. That is the difference this suite exists to
measure, and `tools/check-ai.sh` counts it: of the wrong answers here, **one is
refused before running in Kest and none is in Luau**, and the other nine on each
side are caught by a test. A mistake a compiler refuses costs a turn, one only a
test catches costs a run, and one nothing catches escapes -- nothing here
escapes, because the gate would not pass if it did. See D1125.

`patch` is the one that is written rather than left undone: the scaffold is
working-looking code with a defect in it, and the answer is the fix. `frail`
and `saved` have more tests about what does not happen than about what does.
`spread`'s tests call each piece on its own, so a frame still doing the work
itself passes nothing. `called`'s doors write down what they were called with,
so the log is the order the calls were made in. `stepped`'s checks run the same
hundred milliseconds as one frame, as five even ones and as five uneven ones and
hold the three worlds against each other, which is the one thing the
`deterministic` promise cannot see. `nearby`'s wrong answer calls `table.find`
where it meant `table.get` -- both answer `i32?` on this board, so neither the
compiler nor the analyser has anything to say, and what tells them apart is a
name that turns up twice. What is here is written so that adding one is four
files and a row in the table above.
