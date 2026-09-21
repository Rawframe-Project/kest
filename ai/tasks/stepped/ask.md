# A world that steps the same however the frames fall

A fixed-step update, written twice: once in Kest and once in Luau. The task is
the same in both, and so is what it is judged by.

## What to write

A world is a run of numbers, one for each thing in it, and a `Clock` beside it
holding the time handed in that has not been spent yet and how many whole steps
the world has taken.

Fill in the one function the scaffold leaves undone. Do not change the shapes,
the names, or what it takes and gives back.

- **`advance(values, clock, elapsed)`** — add `elapsed` milliseconds to what the
  clock is holding, then run **whole steps of twenty milliseconds** while there
  is time for one, taking twenty off for each. Answers the clock afterwards.
  - **No one call runs more than five steps.** Where five have run and there is
    still time held, that time is **dropped rather than kept**: a frame that
    arrived late must not make the next one later still.
  - **One step gives each thing its own value plus the one the thing after it
    around the ring held at the start of that step.** The last thing's
    neighbour is the first. Nothing in the world reads a value another thing
    has already been given this step.

## What it is judged by

Tests you are not shown. **The same elapsed time, split into frames any way at
all, has to give the same world and the same number of steps** — one call of a
hundred milliseconds, five of twenty, and five uneven ones adding to a hundred
are all checked against each other. There are checks for the time that is left
over being carried into the next call, for the five-step limit and what happens
to the time it could not spend, for an empty world, and for a world of one
thing, whose neighbour is itself.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`.

The step and the limit are written down in the scaffold; use them rather than
writing the numbers again. Kest's `deterministic` promise is on this function
and is held by the compiler: a body that reads a clock or a random source
cannot carry it. What that promise cannot see is the part this task is about —
that the answer does not depend on how the time arrived, or on the order the
things are walked in.
