# Abilities that cool down

A frame of a game, written twice: once in Kest and once in Luau. The task is
the same in both, and so is what it is judged by.

## What to write

An actor carries abilities. Each one has a name, how many ticks it takes to
cool down, and how many are left before it can be used again. The actor has
energy, which using an ability spends.

Fill in the three functions the scaffold leaves undone. Do not change the
shapes, the names, or what each function takes and gives back.

- **`tick(who, by)`** — every ability counts down by `by`. A cooldown never
  goes below nought, and `by` may be nought or more. The actor comes back
  changed; nothing else about it moves.
- **`ready(who, which, costs)`** — whether the ability at `which` can be used
  now: it is cooled down, and the actor has at least `costs` energy. An index
  that is not one of the abilities is not ready.
- **`spend(who, which, costs)`** — if it is ready, take `costs` off the energy
  and set that ability's cooldown back to full. If it is not ready, nothing
  changes. The actor comes back either way.

## What it is judged by

Tests you are not shown. They ask what the three sentences above say and
nothing else: what happens at nought, what happens past the end, what happens
when the answer is no, and that nothing else about the actor moved.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`.

The Kest scaffold promises `no.alloc` on all three. That promise is part of
the task: a body that reaches the heap is refused before it runs.
