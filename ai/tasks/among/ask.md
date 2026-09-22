# One body, any set

Three small functions that have to work for whatever they are handed, written
twice: once in Kest and once in Luau. The task is the same in both, and so is
what it is judged by.

## What to write

Fill in the three the scaffold leaves undone. Do not change the shapes, the
names, or what they take and give back. Each takes a set of some type and a
function that answers yes or no about one of them.

- **`firstThat(xs, keeps)`** — **the first** of `xs` that `keeps` answers yes
  to, or **nothing** where there is none. Nothing is what there is: a value of
  the set picked to stand for "none" is one the caller cannot tell from an
  answer.
- **`howMany(xs, keeps)`** — how many it answers yes to.
- **`allThat(xs, keeps, into)`** — every one it answers yes to, written into
  `into` **in the order they are in `xs`**, and how many were written. `into`
  is a run somebody else owns and **may have room for fewer than there are**:
  write what fits and answer what you wrote.

## What it is judged by

Tests you are not shown. Every one of the three is asked of **two sets that
share nothing** — whole numbers and pieces of text — because one body for any
set is the whole of this, and a body written for one of them passes half. There
are checks for none matching, for an empty set, for a run with room for fewer
than there are, and for one with no room at all.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`.

In Kest a function value carries what it promises: `keeps` is written
`fn(T) -> bool no.alloc no.host`, and a body that calls one under those
promises is held to them — which is why these three can promise them too.
Luau's `into` comes with the number of places it has, because a Lua table does
not carry one.
