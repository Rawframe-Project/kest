# 0002 — A walk over a struct's fields, written out while compiling

## What changes

`for name, value in fields(x) { ... }` walks the fields of a struct `x`, and is
written out while compiling, once a field: in each copy `name` is the field's
name as text and `value` is the field itself, with its own type. Writing
`value` writes that field of `x`.

```
fn describe(n: Npc) -> i32 {
    for name, value in fields(n) {
        io.print("{name} = {value}")
    }
    return 0
}

fn saved(out: [u8], n: Npc) {
    for value in fields(n) {
        put(out, value)
    }
}

fn loaded(from: Reader, n: Npc) -> Npc {
    for value in fields(n) {
        value = take(from, value)
    }
    return n
}
```

Each copy is checked and compiled on its own, with `value` the type of its
field, so a call inside it is chosen for that type: `put(out, value)` is the
`put` that takes an `i32` for an `i32` field and the one that takes text for a
text field, and a field of a type nothing takes is refused at the call in the
copy for that field. `break` leaves the walk and `continue` goes on to the
next field.

What `x` may be: a name the body holds that is a struct, or a field of one
that is a struct, because `value` is that field of it and writing it has to
write somewhere. `fields` of anything else is refused, and `fields` is a walk
and nothing else: it is not a value and has no type.

## Why

A save writes every field of every shape in a world and a load reads them back
in the same order, and today both are written out by hand, twice, for every
shape: a field added to a struct and not to its save is a world that loads
wrong, and nothing refuses it. What a debugger panel or an inspector shows is
the same list. K11 asked for reflection aimed at exactly that and nothing
more: no macros, no code written by code, nothing at run time.

## What it breaks, and for whom

Nothing. `fields` joins the language's own names the way `len` and `find` are
there: a file that declares a `fields` of its own that takes what is passed
gets its own, and the language's answers for the rest. So a program with a
function called `fields` means what it meant.

## The edition

Made in every edition, since nothing that compiles today changes meaning.

## What else was weighed

- **A list of fields at run time** -- names, offsets and kinds a program walks
  and reads through. It makes every field a value of no particular type, which
  is the one thing this language does not have, and it costs a table at run
  time for something the compiler already knows.
- **Macros.** K11 said no, and this is what was wanted from them.
- **A save and a load the compiler writes.** It fixes the format for every
  program, and a program's save is the program's to decide: which fields,
  which order, what version.

Taken as D1264.
