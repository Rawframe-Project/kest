# Things that are taken out of a world while somebody is holding them

A frame of a game, written twice: once in Kest and once in Luau. The task is
the same in both, and so is what it is judged by.

## What to write

A world holds things. Each one has a name, how much it is worth, and whether
it is still standing. A round takes some of them out and then adds up what is
left.

Fill in the two functions the scaffold leaves undone. Do not change the
shapes, the names, or what each function takes and gives back.

- **`fell(world, hits)`** — every hit names one thing and how much it takes
  off. A thing whose worth reaches nought or less is taken out of the world.
  Answers how many were taken out.
- **`standing(world)`** — what everything still in the world is worth, added
  up. Nothing that was taken out counts, and reading one that was is not a
  thing this is allowed to do.

## What it is judged by

Tests you are not shown. They ask what the two sentences above say: that the
count is right, that the total is right, that a thing taken out is not read
again, and that the world is still walkable afterwards.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`.

A hit names a thing by the handle the world gave out. In Kest that is a
`ref<Thing>` and the world is a `store`; in Luau it is the index of a table.
What happens when one of those names something that is no longer there is
part of the task rather than beside it.
