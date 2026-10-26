# Data

Structs, fixed runs, arrays, stores and references, enums, sets of flags, optionals and vectors: what each is, what it costs, and what copies.

## Contents

- [Values and handles](#values-and-handles)
- [Structs](#structs)
- [Fixed runs](#fixed-runs)
- [Arrays](#arrays)
- [Stores and references](#stores-and-references)
- [Enums and `match`](#enums-and-match)
- [Flags](#flags)
- [Optionals](#optionals)
- [Saying that something failed](#saying-that-something-failed)
- [Vectors](#vectors)
- [What compares, hashes and prints](#what-compares-hashes-and-prints)
- [Containers and the heap](#containers-and-the-heap)

## Values and handles

Every type is one of two kinds, and the difference decides what a function can change:

- **Values**: numbers, `bool`, text, structs, fixed runs `[T; N]`, vectors, enums, flags and optionals. Handing one to a function or to a `let` copies it.
- **Handles**: arrays `[T]`, stores `store<T>` and function values. Handing one over hands over the same thing, so writing through any name for it is seen through every other. A struct holding an array holds the handle, not a copy of the elements.

```kest
module walk

import std.io

struct Body {
    x: f32
    speed: f32
}

fn moved(b: Body, dt: f32) -> Body {
    let after = b
    after.x += b.speed * dt
    return after
}

fn main() -> i32 {
    let b = Body(0.0, 2.0)
    b = moved(b, 0.5)
    let xs = [1, 2, 3]
    let ys = xs
    ys[0] = 9
    io.print("{b.x} {xs[0]}")
    return 0
}
```

```text
1.0 9
```

The mistake this makes easiest: writing a field of a struct parameter writes the function's own copy, which the caller never sees. The compiler warns (`K0346`) when a function writes a field of a parameter and does not give that type back. The fix is to answer with the changed value and let the caller store it, as `moved` does.

## Structs

A struct is built by calling it with its fields in the order they are declared. There are no struct literals with names and no named arguments.

```kest
module npcs

import std.io

struct Npc {
    name: text
    health: i32
}

fn main() -> i32 {
    let ann = Npc("ann", 10)
    let hurt = ann
    hurt.health -= 3
    io.print("{ann.health} {hurt.health} {hurt}")
    return 0
}
```

```text
10 7 Npc("ann", 7)
```

A struct is laid out like the C struct of the same fields, so a host can lend an array of them and the program works on the host's memory in place.

**Fields only one module may name.** A field written `own` can only be named inside the module that declared the struct: read, written or given when the struct is built. A struct whose every field is `own` can only be built by its own module, which is how an opaque type is written. `std.table` keeps its arrays this way so nothing outside can move a key without its value.

```kest
module ledger

struct Ledger {
    own entries: [i32]
}

fn empty() -> Ledger {
    let fresh: [i32] = array()
    return Ledger(fresh)
}

fn record(l: Ledger, amount: i32) {
    push(l.entries, amount)
}
```

`own` is a word only in front of a field name, so a field may still be called `own`.

## Fixed runs

`[T; N]` is `N` values of `T` laid out in place, and it is a value: copying it copies every element. `N` is a number or a constant. An index written as a number is checked while compiling; one worked out while running is checked while running. `len` of a fixed run is the number in its type.

```kest
module quads

const CORNERS: i32 = 4

struct Quad {
    xs: [f32; CORNERS]
    tag: i32
}

fn width(q: Quad) -> f32 {
    return q.xs[1] - q.xs[0]
}
```

## Arrays

An array is a growable handle. `array()` is an empty one, typed by where it goes; `array(n, v)` is `n` copies of `v`; `[1, 2, 3]` is a literal. `push` adds to the end, `pop` takes the last off as an optional, `remove(xs, i)` takes out the one at `i` and shifts the rest down, and `clear` empties it while keeping its room.

```kest
module counts

import std.io

fn main() -> i32 {
    let xs: [i32] = array()
    push(xs, 3)
    push(xs, 4)
    push(xs, 5)
    let last = pop(xs)
    let first = remove(xs, 0)
    xs[0] += 10
    io.print("{len(xs)} {last} {first} {xs[0]}")
    return 0
}
```

```text
1 5 3 14
```

An element of an array is a place: `xs[i].health -= 1` writes the element where it is. Reading past the end stops the program. `room(xs, n)` makes room for `n` elements without changing the length, and `fit(xs, v)` is `push` that never grows: it answers `false` when there is no room. Those two are how a function that promises not to allocate fills a buffer, see [Promises](promises.md).

A `[u8]` holds bytes. `push(bytes, "some text")` appends a whole piece of text, and `text(bytes)` turns bytes back into text, refusing bytes that are not UTF-8.

## Stores and references

A `store<T>` owns values and hands out a `ref<T>` for each one added. A reference can go stale, because something else may remove what it named, so reading through one with `get` answers an optional. `set` writes a value back, `remove` takes it out, and a `for` over a store walks a reference at a time.

```kest
module world

import std.io

struct Npc {
    hp: i32
}

fn main() -> i32 {
    let npcs: store<Npc> = store()
    let a = add(npcs, Npc(10))
    let b = add(npcs, Npc(20))
    remove(npcs, a)
    if let one = get(npcs, b) {
        let hurt = one
        hurt.hp -= 5
        set(npcs, b, hurt)
    }
    let alive = 0
    for r in npcs {
        if let one = get(npcs, r) {
            alive += one.hp
        }
    }
    io.print("{len(npcs)} {get(npcs, a) == none} {alive}")
    return 0
}
```

```text
1 true 15
```

**Stale stays stale.** A reference is a place and a stamp, and every place a process hands out gets a stamp it never hands out again. A reference to something removed reads `none` even after its place is reused, and a reference from another store reads `none` in this one. Writing or removing through a stale reference does nothing and answers that it did nothing.

Removing during a walk of the store is safe; the walk does not go back. Adding during a walk lands wherever the store puts it, which the walk may or may not reach, so gather what to add and add it afterwards.

Two references compare equal when they are the same handout, and a reference hashes, so `table.Table<ref<Npc>, V>` hangs data off an entity without changing the entity. A reference has no text and means nothing in another machine: a saved world stores the program's own identifiers, not references.

An element of a store is not a place: read it with `get`, change the copy, write it back with `set`. Reading a whole element, changing several fields and writing it back is faster than resolving the element once per field.

Nothing counts references and nothing is told about a removal, so two values may name each other through references without either keeping the other alive.

## Enums and `match`

An enum is one of several cases, and a case may carry values, written by position.

```kest
module door

import std.io

enum Door {
    Shut
    Locked(i32)
    Open(f32)
}

fn describe(d: Door) -> text {
    return match d {
        Shut -> "shut"
        Locked(key) -> "locked with {key}"
        Open(width) -> "open {width} wide"
    }
}

fn main() -> i32 {
    io.print(describe(Door.Locked(7)))
    match Door.Open(1.5) {
        Open(width) {
            io.print("{width}")
        }
        else {
            io.print("not open")
        }
    }
    return 0
}
```

```text
locked with 7
1.5
```

- `match` chooses between the cases of an enum and nothing else. Numbers and text are chosen between with `if`.
- Every case is answered, or `else` answers the rest. A `match` that misses a case is an error that names each missing case.
- An arm is `Case -> value` or `Case { ... }`, and all arms of one `match` are the same kind. A `match` whose arms give values is itself a value.
- `match a, b { ... }` answers two enums at once, an arm per combination, with `else` in a position for any case there. Arms are tried in order, and an arm nothing can reach is an error.

An enum is laid out as a C tagged union: a four-byte tag, then the payload at its own alignment. A case added in the middle renumbers the cases after it, which a host or a saved file reading tags sees; see [Host memory](host-memory.md#saving-and-reloading).

## Flags

`flags` declares a set of named bits over an unsigned width. Which bit a name is depends on where it was written, so there are no powers of two to get wrong.

```kest
module states

import std.io

flags State: u8 {
    Moving
    Airborne
    Hurt
}

fn main() -> i32 {
    let state = State.Moving | State.Hurt
    if state & State.Hurt != State() {
        io.print("hurt")
    }
    let count = 0
    for flag in state {
        count += 1
    }
    io.print("{count} {u8(state)} {state}")
    return 0
}
```

```text
hurt
2 5 State.Moving | State.Hurt
```

`&`, `|`, `^` and `~` combine two of one set; `==` and `!=` compare. `State()` is the empty set, `u8(state)` gives the bits and `State(bits)` takes them back. A set is not a number, so arithmetic on one is an error, and a ninth flag on a `u8` is an error rather than a silent change of layout. `match` does not apply to a set.

## Optionals

`T?` holds a `T` or `none`. A value standing where an optional is wanted becomes one; that is the only conversion the language makes on its own. `if let` and `while let` take the value out under a name, and `== none` and `!= none` ask without taking it out. There is no operator that takes the value out without asking.

```kest
module maybe

import std.io

fn find(xs: [i32], wanted: i32) -> i32? {
    for i in 0..len(xs) {
        if xs[i] == wanted {
            return i
        }
    }
    return none
}

fn main() -> i32 {
    let xs = [4, 8, 15]
    if let at = find(xs, 8) {
        io.print("at {at}")
    }
    if find(xs, 16) == none {
        io.print("not there")
    }
    return 0
}
```

```text
at 1
not there
```

`if let` and `while let` bind a name and nothing else: there are no patterns. An optional of an optional is two questions that stay apart, and a value becomes an optional only once, so `1` does not fit where an `i32??` is wanted.

## Saying that something failed

There are no exceptions and no built-in result type. A function that can fail picks one of three shapes, by what the caller can do about it:

- **`T?`** when there is nothing to say beyond "there is none": `get`, `find`, `os.read`, `text.number`.
- **`bool`** when the caller only needs to know whether it worked: `fit`, `table.fit`.
- **An enum** when the caller handles different reasons differently. An enum may take types, `enum Answer<T> { Held(T) Trouble(text) }`, and `match` makes the caller answer every case.

None of them stops the program. A fault is reserved for what the machine cannot make sense of.

## Vectors

`vec2`, `vec3` and `vec4` are two to four `f32`s named `x`, `y`, `z` and `w`. They are the only types with operators the language did not already have: `+`, `-`, `*` and `/` between two vectors of one width a component at a time, `*` and `/` between a vector and an `f32` on either side, unary `-`, `==`, `!=` and the four assignment forms.

```kest
module falling

import std.io

struct Body {
    at: vec3
    speed: vec3
}

fn fall(b: Body, dt: f32) -> Body no.alloc no.host deterministic {
    let down = vec3(0.0, -9.81, 0.0)
    return Body(b.at + b.speed * dt, b.speed + down * dt)
}

fn main() -> i32 {
    let b = fall(Body(vec3(0.0, 10.0, 0.0), vec3(1.0, 0.0, 0.0)), 0.5)
    io.print("{b.at.x} {b.speed.y}")
    return 0
}
```

```text
0.5 -4.905
```

Each component is the single `f32` operation it would be written out as, so vectors give the same bits everywhere. A vector is laid out as a C struct of its floats; `vec3` is twelve bytes aligned to four. A struct the program declares never gets operators. `std.vec` builds `dot`, `length`, `distance`, `direction`, `lerp`, `cross` and `perpendicular` on top.

## What compares, hashes and prints

A value laid out flat compares with `==` when everything in it compares: numbers, `bool`, text, flags, vectors, a struct whose fields compare, a fixed run of something that compares, and an enum whose cases carry only things that compare. Arrays, stores and function values never compare, nor does anything holding one, because equal contents and the same handle are different questions. A reference compares as an identity.

`hash(x)` gives a `u64` for exactly the values that compare. It is the same number on every machine and in every version: FNV-1a over the bytes for text, and fixed arithmetic for everything else. So a struct can be a table key, and two structs with the same fields are one key. A float that is not a number is not equal to itself, so a table refuses it as a key. Nothing is salted; a program taking keys from someone it does not trust hashes them with something of its own first.

A hole in a string writes any value that compares, the way a program would write it: `Npc("ann", 7)`, `[1, 2, 3]`, `Door.Locked(7)`, `State.Moving | State.Hurt`, or `none`. A whole-number float keeps its point (`3.0`), the shortest spelling that reads back as the same number is used, and the three values without a spelling print as `inf`, `-inf` and `nan`. Arrays, stores, references and function values have no text.

## Containers and the heap

Arrays, stores and text live on the program's heap. What putting one more entity into each costs a frame, when nothing said how many were coming, is roughly one piece of text (16 bytes), one array element (about 50 bytes), one table pair (about 75) or one store element (about 115), because each grows by doubling. Told how many are coming, with `array(n, v)`, `store(n)`, `room` or `table.refill`, the frame pays nothing: the room is made once, before the frame.

A store or array whose elements are removed and added again forever reuses its room and does not grow. Memory nothing can reach is given back by the collector; [Host memory](host-memory.md#the-collector) says when.
