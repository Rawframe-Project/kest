# Taking from a pool, all of it or none of it

Handing slots out of a pool without reaching the heap, written twice: once in
Kest and once in Luau. The task is the same in both, and so is what it is
judged by.

## What to write

A `Pool` holds the slots that are free and writes down every slot it hands out.
`take` and `give` are already written and are not yours to change: `take`
answers the slot that went out or nothing where there are none left, and `give`
puts one back so that it is the next one out again.

Fill in the one function the scaffold leaves undone. Do not change the shapes,
the names, or what it takes and gives back.

- **`refill(pool, want, into)`** — put `want` slots into `into`, in the order
  `take` hands them over, and answer `want`. `into` is **emptied first**,
  whatever it was holding.
  - **All of them or none of them.** Where the pool runs out before `want` are
    had, **every slot this call took goes back** — newest first, so the pool
    ends holding exactly what it held and in the order it held it — `into` is
    left empty, and the answer is nought.
  - Asking for nought or fewer takes nothing, empties `into`, and answers
    nought.

## What it is judged by

Tests you are not shown. The pool writes down every slot it hands out, so what
it is holding afterwards **is whether what was taken came back**. There are
checks for part of the pool, for the whole of it, for one more than there is,
for a pool that gave everything back handing it out again, for nought and for
less than nought, and for `into` arriving with something already in it.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`.

This one carries a promise and is about it: **`refill` promises `no.alloc`, and
the compiler holds it to that.** A list of your own to remember what you took
in is something that can grow, and a body that makes one cannot keep the
promise — so what you took is remembered in the room you were handed. Luau has
nothing that says this, and the same answer there is a table nobody counts.
