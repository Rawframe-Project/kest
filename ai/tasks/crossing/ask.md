# Doors the engine gives you

Driving an engine through the API it hands your program, written twice: once
in Kest and once in Luau. The task is the same in both, and so is what it is
judged by.

## What to write

The engine gives you three doors and they are not yours to change:

- **`spawn(kind)`** — make one thing of a kind. Answers **the id it was
  given, or a number below nought where the engine would not make one.**
- **`despawn(id)`** — take one away. Answers **whether there was one there to
  take.**
- **`alive()`** — how many are alive now.

Fill in the one function the scaffold leaves undone. Do not change the shapes,
the names, or what it takes and gives back.

- **`settle(kind, mine, many, wanted)`** — bring the world to `wanted` alive
  things and answer how many ids are in `mine` afterwards.
  - `mine` is a run **the engine lent you**, holding `many` ids your program is
    responsible for and room for more. **It cannot grow.** Write into it and
    say how many are in it; never write past the end of it, and where there is
    no room left, what you are holding is all you can hold.
  - Too few: spawn `kind` and write each id after the ones already there.
  - Too many: take the **last** id in `mine` off the end and despawn it.
  - Ask `alive()` once. The doors answer what they did, so count from their
    answers rather than asking again.

## What it is judged by

Tests you are not shown. Every door writes down what it was called with, so
what that log holds afterwards **is the order you called them in and what you
asked for** — and that is most of what this task is about. There are checks
for a world that is already right, one that is short, one that is over, an
engine that will not make another, an id you are holding that the engine has
already lost, and a run with no room left in it.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`. It
  warns that nothing calls the three doors yet, which is true until you do.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`. The
  engine arrives as a table, which is how a Luau embedder hands a script the
  API it is allowed to call; in Kest it is `extern`, bound by name.

This is the task where **what a door answered matters more than that it was
called**. An engine that will not make another answers so, and asking it again
answers the same thing; an id it has already lost answers that there was
nothing to take. Both are one line of reading and both are wrong to assume.
