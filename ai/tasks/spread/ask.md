# A frame that does three things at once

Moving work out of one function into three, written twice: once in Kest and
once in Luau. The task is the same in both, and so is what it is judged by.

## What to do

`tick` is one loop doing three things: moving everything, bouncing whatever has
gone outside the board, and counting cooldowns down. **Move each of them into
the function that is named for it, and leave `tick` calling the three in that
order.** Do not change the shapes, the names, or what each function takes and
gives back, and do not change what a frame does.

- **`moved(all)`** — everything moves by its own speed.
- **`bounced(all, wide)`** — anything outside the board comes back in, turns
  round, and takes a point for it. Answers how many bounces there were.
- **`cooled(all)`** — every cooldown counts down by one, and never below
  nought.
- **`tick(all, wide)`** — the three of them, in that order, answering how many
  bounces there were.

## What it is judged by

Tests you are not shown. They call each of the three on its own with worlds
made for it, and then call `tick` on a world where the order matters — a thing
that would not have bounced where it started bounces where moving put it. A
`tick` that still does the work itself passes nothing.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`.

This is the refactoring kind. Nothing about it is hard; what is measured is
whether a frame comes out of it meaning exactly what it meant, which is what a
refactor is and what a model doing one at speed gets wrong.
