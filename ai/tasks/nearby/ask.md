# Names that are nearly the one you want

A scoreboard read out of lines somebody else wrote, written twice: once in Kest
and once in Luau. The task is the same in both, and so is what it is judged by.

## What to write

Fill in the two functions the scaffold leaves undone. Do not change the shapes,
the names, or what they take and give back.

- **`tally(lines)`** — read a board out of the lines. A line is a name, a
  colon, and a whole number: `gil:12`. Spaces at either end of the name and of
  the number are not part of them. A line that has no colon, or more than one,
  or an empty name, or a score that is not a whole number, is **not on the
  board and is not an error** — it is passed over. A name that turns up more
  than once **adds**: `gil:12` and then `gil:5` is `gil` with seventeen.
- **`best(board)`** — the name with the greatest score, or nothing where the
  board is empty. Where two names have the same greatest score, it is **the one
  that comes first in order**.

## What it is judged by

Tests you are not shown. There are checks for a plain board, for a name that
turns up three times, for each of the ways a line is not a line, for the tie,
for scores below nought, and for a board with nothing on it.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`.

Most of the work is calls to the library, and **the names in it are close to one
another**. In Kest: `table.get` answers the value kept under a key, where
`table.find` answers *where* it is kept and `table.slotOf` answers where the
table looked; `table.set` puts a name on the board, where `table.fit` only
writes over a name already on it and answers whether it did; `text.number`
reads a whole number where `text.real` reads one with a fraction; and
`text.left` and `text.right` **pad a piece of text out to a width** rather than
cutting it down to one. In Luau, `table.find` walks the values of an array
rather than looking up a key, `string.find` answers where a piece begins and
ends rather than the piece, and `tonumber` reads a number with a fraction in it
as readily as a whole one.

A name that is not in the library at all is refused before anything runs, in
both languages. The ones that *are* there are what this task is about.
