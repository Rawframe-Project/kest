# The Kest book

A short way in. Each chapter is a program you can run, with what it writes
under it; the reference (`docs/language.md`) says everything about each thing
shown here, and `docs/decisions.md` says why. Every program in this book is
compiled and run by the gate, so what is written under it is what it writes.

## 1. A program

A file with a `main` is a program, and `main` answers with a status.

```kest
import std.io

fn main() -> i32 {
    io.print("hello from kest")
    return 0
}
```

```text
hello from kest
```

`kest run hello.kest` runs it. `kest new game` makes a project -- a directory
with a manifest, a program and a test -- and `kest run` inside it runs the
program the manifest names. Saying something is the host's to do, and
`std.io` is where a program asks for it; there is no `print` of the language's
own.

## 2. Names and values

A `let` names a value, and a name may be written to after. Numbers do not
convert on their own: an `i32` becomes an `f32` by naming the type. A hole in a
piece of text writes a value into it.

```kest
import std.io

fn main() -> i32 {
    let apples = 3
    let price: f32 = 1.5
    let total = f32(apples) * price
    let name = "Ann"
    io.print("{name} pays {total} for {apples} apples")
    let count = 0
    count += apples
    return count - 3
}
```

```text
Ann pays 4.5 for 3 apples
```

## 3. Functions, and calling one on a value

A function takes what it works on, and `x.f(a)` is another way of writing
`f(x, a)`, so a chain reads left to right. A struct is a value: `fill` hands
back a new pot and `pot` is what it was.

```kest
import std.io

struct Pot {
    level: i32
}

fn fill(p: Pot, by: i32) -> Pot {
    return Pot(p.level + by)
}

fn main() -> i32 {
    let pot = Pot(1)
    let full = pot.fill(2).fill(3)
    io.print("{pot.level} then {full.level}")
    return 0
}
```

```text
1 then 6
```

## 4. One of several, and maybe nothing

An enum is a thing that is one of several cases, and `match` answers every
case or is refused. Something that may be absent is an optional, `i32?`, and
`if let` takes what it holds.

```kest
import std.io

enum Event {
    Idle
    Hit(i32)
    Moved(f32, f32)
}

fn describe(e: Event) -> text {
    return match e {
        Idle -> "nothing"
        Hit(damage) -> "hit for {damage}"
        Moved(x, y) -> "moved to {x}, {y}"
    }
}

fn firstBig(xs: [i32]) -> i32? {
    for x in xs {
        if x > 10 {
            return x
        }
    }
    return none
}

fn main() -> i32 {
    io.print(describe(Event.Hit(3)))
    io.print(describe(Event.Moved(1.5, 2.0)))
    let xs = [4, 12, 30]
    if let big = firstBig(xs) {
        io.print("first big one is {big}")
    }
    return 0
}
```

```text
hit for 3
moved to 1.5, 2.0
first big one is 12
```

## 5. Things that live in a world

A `store` holds things and hands back a `ref` to each; a reference to one that
was removed is `none` when it is asked for, never another thing that took its
place.

```kest
import std.io

struct Npc {
    name: text
    health: i32
}

fn main() -> i32 {
    let world: store<Npc> = store()
    let ann = add(world, Npc("ann", 10))
    let bo = add(world, Npc("bo", 3))
    remove(world, bo)
    if let npc = get(world, ann) {
        io.print("{npc.name} has {npc.health}")
    }
    if get(world, bo) == none {
        io.print("bo is gone, and the reference says so")
    }
    return 0
}
```

```text
ann has 10
bo is gone, and the reference says so
```

## 6. Vectors, and a frame that answers the same everywhere

`vec2`, `vec3` and `vec4` are the language's, with `+`, `-`, `*` and `/` a
component at a time. A function that promises `deterministic` answers the same
bits on every machine, and one that promises `no.alloc` reaches no heap; the
compiler proves both before anything runs.

```kest
import std.io

struct Body {
    at: vec2
    speed: vec2
}

fn step(b: Body, dt: f32) -> Body no.alloc deterministic {
    let gravity = vec2(0.0, -9.81)
    return Body(b.at + b.speed * dt, b.speed + gravity * dt)
}

fn main() -> i32 {
    let b = Body(vec2(0.0, 10.0), vec2(2.0, 0.0))
    for frame in 0..60 {
        b = step(b, 1.0 / 60.0)
    }
    io.print("after a second: {b.at}")
    return 0
}
```

```text
after a second: vec2(1.9999994, 5.1767507)
```

## 7. Handing a body to a function

A function that takes a `block` is handed a body where it is called, and the
body reads and writes the names there. Nothing is captured and nothing is kept:
the function is written into the place it is called.

```kest
import std.io

fn each<T>(xs: [T], body: block(T)) {
    for x in xs {
        body(x)
    }
}

fn countWhere<T>(xs: [T], keep: block(T) -> bool) -> i32 {
    let n = 0
    for x in xs {
        if keep(x) {
            n += 1
        }
    }
    return n
}

fn main() -> i32 {
    let hp = [5, 0, 12, 7]
    let total = 0
    hp.each(|h| {
        total += h
    })
    let floor = 4
    io.print("{total} in all, {hp.countWhere(|h| h > floor)} above {floor}")
    return 0
}
```

```text
24 in all, 3 above 4
```

## 8. What the compiler refuses

A promise is checked rather than trusted. A body that promises `no.alloc` and
builds a piece of text -- `fn label(n: i32) -> text no.alloc` giving back
`"n is {n}"` -- is refused where it builds it, with the promise it broke:

```
error[K0401]: this allocates, and `label` promises `no.alloc`
 --> label.kest:2:12
  |
2 |     return "n is {n}"
  |            ^^^^^^^^^^ text with a hole in it is built, and what is built is on the heap
```

Every refusal has a code, and `kest check --json` says the same things for a
tool. Everything wrong with a file is said in one run rather than the first
thing. What a program may run into -- how deep, how many, how long -- is a
number a host sets, and reaching one is a refusal in words, never a crash.

## 9. Saying what a module is for

A comment on the lines above a declaration is about that declaration, and the
comments before the first one are about the file. `kest doc` writes both out
for somebody who is going to call the module rather than read it: every
declaration as it is written -- a function up to where its body starts -- with
its comment under it.

```kest
module shop.shelf

// A shop's shelves. Everything here is a value.

struct Shelf {
    count: i32
}

// One more on the shelf.
fn fill(s: Shelf) -> Shelf {
    return Shelf(s.count + 1)
}
```

`kest doc shop/shelf.kest` writes it as Markdown, and `--json` writes the same
for a tool. A blank line between a comment and a declaration says the comment
is not about it.

## 10. A host

A program runs inside a host: a game, an engine, the command line. The host
compiles it, binds the doors it declares with `extern fn`, starts a machine
and calls into it. What a host does is the C API in `include/kest.h`, and
`examples/least.c` is the smallest host there is. A host running code it did
not write starts the machine with `kest_start_untrusted`, which takes only the
doors the host opened to such code; `SECURITY.md` says what that promises.

## 11. Where next

`examples/` holds a program for each thing the language does, each one run by
the gate, and `lib/std` is the standard library, which `kest doc` reads the
same way it reads yours. The reference is `docs/language.md`, and
`ARCHITECTURE.md` says how the compiler and the machine are built.
