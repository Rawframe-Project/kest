# A world written down and read back

Saving and loading, written twice: once in Kest and once in Luau. The task is
the same in both, and so is what it is judged by.

## What to write

A save is one piece of text: the version `v1`, then one record for each thing,
`|` between them. A record is `name,worth,alive`, where `alive` is `1` or `0`.
A world with nothing in it is the text `v1`.

```text
v1|oak,10,1|ash,-4,0|elm,0,1
```

Fill in the two functions the scaffold leaves undone. Do not change the
shapes, the names, or what each function takes and gives back.

- **`written(all)`** — the world, in that form. Names in these worlds hold no
  commas and no bars.
- **`read(save)`** — the world that text spells, or nothing. *Nothing* is the
  answer for a save of another version, a text that is no save at all, a
  record with a field missing or a field too many, a worth that is not a whole
  number, an `alive` that is neither `1` nor `0`, and a name with nothing in
  it.

The two have to be each other's undoing, from both ends: reading what was
written gives back the world, and writing what was read gives back the text.

## What it is judged by

Tests you are not shown. They round-trip a world both ways, and then hand
`read` eight kinds of text that are not saves.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`.

This is the fourth kind the mission lists and the second where most of what is
asked is what *does not* happen. A loader that is right about the save it
wrote and guesses at the save somebody else wrote is a loader that corrupts a
world.
