# 0001 — A body that waits, and resumes from its data

## What changes

A function may say that it resumes from a field of what it is handed:

```
fn step(c: Chore, dt: f32) -> Chore resumes c.at {
    while c.next < len(c.route) {
        c.pos = toward(c.pos, c.route[c.next], dt)
        wait Walking
        if c.pos == c.route[c.next] {
            c.next += 1
        }
    }
    c.left = 5.0
    while c.left > 0.0 {
        c.left -= dt
        wait Working
    }
    c.at = ChoreAt.Done
    return c
}
```

`c.at` is a field of an enum the program declares, here `enum ChoreAt { Start
Walking Working Done }`. `wait Walking` sets `c.at` to `ChoreAt.Walking` and
gives `c` back: the call is over. The next call finds `c.at` is `Walking` and
carries on from the line after that `wait`. A call that finds a case no `wait`
names -- `Start`, `Done` -- starts from the top.

The rules that keep that a function of data:

- `resumes p.f` names a parameter the function takes by value and gives back,
  and a field of it whose type is an enum. Each `wait` names a case of that
  enum that carries nothing, and no case is waited at twice, so a case is one
  place in the body.
- Nothing but the parameter lives across a `wait`. A `let`, a `for` and the
  names an `if let` or a `match` arm binds may not be in reach of one:
  whatever a body wants to have after a wait it keeps in a field. So a `wait`
  is not inside a `for`, a `defer` is not in a body that waits, and nor is a
  `scratch { }` around one.
- A `wait` is a statement and is written only in the body that resumes, not
  in a block handed to it.

## Why

A colonist walks a route and then works for a while; an enemy patrols, sees
something, chases, gives up. That is sequential code with pauses between
frames, and today it is written as a state machine by hand: an enum of where
it is, a `match` at the top of a step, and each arm a piece of the sequence
that says which arm comes next (D1183, the colony's `Job`). The order of the
sequence is nowhere on the page.

What D1183 refused was the ordinary answer, a coroutine: a frame kept in
flight, which a save has nothing to write for and a reload has nothing to
point at, because what it holds is the old code's instruction pointer. This
keeps the sequence and not the frame. Where a body is waiting is a case of an
enum, a named thing in the program's own data; what it has is the fields of a
struct. Both are saved the way any struct is, both are read by a debugger or a
host the way any struct is, and a reload under a waiting world goes on from
the same named place in the new code -- or, if the enum changed its shape, is
refused the way any shape change is. Nothing is on the heap and nothing new
is at the host boundary.

## What it breaks, and for whom

Nothing. `resumes` and `wait` are words, not keywords: `resumes` means this
only after a function's result, and `wait` only at the start of a statement
followed by a name, which is nothing a program can write today. A program
with a function called `wait` or a field called `resumes` means what it meant.

## The edition

Made in every edition, since nothing that compiles today changes meaning.

## What else was weighed

- **A coroutine**, a frame suspended at a `yield`. Refused by D1183 for the
  reason above: the state is an instruction pointer.
- **A generated state struct**, the compiler turning every local into a
  field. It saves the writing of fields, and it makes the state something
  nobody wrote: the names of hidden fields, what a `for` keeps, what shape a
  reload is matching against. Here what is saved is exactly what is written.
- **A generated enum of wait points.** Named by the compiler, it would be a
  type no program declared, and a program saving or matching one would be
  saving something it cannot name. Declaring it costs a line and makes the
  points the program's own.
- **Resuming on a `match` written by hand.** That is what is written today,
  and it is what this is for.

Taken as D1263.
