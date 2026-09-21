# Doors somebody else wrote

Driving a set of functions that were handed to you, written twice: once in
Kest and once in Luau. The task is the same in both, and so is what it is
judged by.

## What to write

`Doors` holds two functions. `spawned(log, id)` is called once for each thing;
`moved(log, id, step)` is called for each step of each thing and answers
whether that thing may go on. Neither is yours; what is yours is when each is
called.

Fill in the one function the scaffold leaves undone. Do not change the shapes,
the names, or what it takes and gives back.

- **`marched(doors, log, many, steps)`** — **every thing is spawned before any
  thing moves.** Then each thing in turn takes its steps, numbered from one; a
  thing whose `moved` answers no stops there and takes no more. Answers how
  many things took all their steps. `log` is handed to the doors and is not
  yours to write into.

## What it is judged by

Tests you are not shown. The doors they hand you write down what they were
called with, so what the log holds afterwards **is the order you called them
in** — and that is most of what this task is about. There are checks for
nothing to march, for no steps to take, for a thing that is refused at once,
and for one that is never refused.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`.

Kest's function values carry what they promise: these two promise `no.alloc`,
`no.host` and `deterministic`, and a body that calls one under those promises
is held to them. What Luau does about that is Luau's business.
