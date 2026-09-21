# Lines somebody else wrote

Reading records out of lines that may be anything at all, written twice: once
in Kest and once in Luau. The task is the same in both, and so is what it is
judged by.

## What to write

A record is written as one line, in this form and no other:

```text
name=<anything but a semicolon>;worth=<a whole number>
```

Fill in the two functions the scaffold leaves undone. Do not change the
shapes, the names, or what each function takes and gives back.

- **`read(line)`** — the record that line spells, or nothing. *Nothing* is the
  answer for every line that is not exactly the form above: a line missing
  either half, the two halves the wrong way round, a third half, a name with
  nothing in it, a worth that is not a whole number, and a worth too big for
  the one a record holds. A name may hold spaces.
- **`readAll(lines)`** — every line read, and what came of it: how many were
  records, how many were not, and what the records are worth altogether.

## What it is judged by

Tests you are not shown. They are mostly lines that are not records, because
that is what this task is about: a reader that is right about the good line
and guesses at the bad one is a reader that is wrong.

## What you are given

- Kest: `kest/start.kest`. Check it with `kest check kest/start.kest`.
- Luau: `luau/start.lua`. Check it with `luau-analyze luau/start.lua`.

The Kest scaffold answers `Record?`, which is nothing or a record and nothing
else: there is no record with a name nobody set. What the Luau side does with
`nil` is the Luau side's to decide.
