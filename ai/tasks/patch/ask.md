# A bag that is filling wrong

A bag of stackable things, written twice: once in Kest and once in Luau. The
task is the same in both, and so is what it is judged by.

## What to do

**This one is already written, and it is wrong.** Find what is wrong with it
and fix it. Do not change the shapes, the names, or what each function takes
and gives back, and do not rewrite what is right.

A bag holds stacks. Each stack is of one kind and holds at most `MOST` of it.
The bag holds at most `room` stacks.

- **`held(bag, kind)`** — how many of `kind` the bag holds altogether.
- **`put(bag, room, kind, many)`** — put `many` of `kind` in, and answer how
  many would not go. Part-used stacks of that kind are filled first, in the
  order they are in; then new stacks are started while the bag has room for
  another. **No stack ever holds more than `MOST`.** Nothing put in is nothing
  left over and nothing moved.

## What it is judged by

Tests you are not shown. They ask what the two sentences above say and nothing
else: what a bag holds, what comes back, how many stacks there are, and that
no stack holds more than a stack holds.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`.

Neither compiler nor analyser has anything to say about this code. That is the
point of the task: the mistake is one a type system does not catch, and what
is being measured is whether it is easier to find here or there.
