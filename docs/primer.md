# Kest for somebody who knows Lua or Rust

One page of what is different here, written from what people and models new to
the language actually got wrong. `docs/language.md` is the whole of it and says
why; this says what, and every program on it runs.

## A file is a module, and a name keeps its module

A file says which module it is, and that is where it lives: `module game.map`
is `game/map.kest`. What a module declares is named through it everywhere else,
including after `import`. There is no `use` and no bare name.

```kest
module shop

import std.io
import std.math

fn main() -> i32 {
    io.print("{math.max(2, 7)}")
    return 0
}
```

```text
7
```

`print` is `io.print` from `std.io`, and it takes text. A hole makes text of
anything: `"{x}"`.

## A struct is built by position

There are no struct literals and no named arguments. A struct is called like a
function, with its fields in the order they are declared.

```kest
module point

import std.io

struct P {
    x: i32
    y: i32
}

fn main() -> i32 {
    let p = P(3, 4)
    io.print("{p.x},{p.y}")
    return 0
}
```

```text
3,4
```

`P { x: 3, y: 4 }` and `P(x: 3, y: 4)` are refused, and the refusal says this.

## A struct is a value; an array is a handle

A struct handed to a function is a copy. Writing a field of it writes the copy,
and the compiler says so (`K0346`). Answer with the changed one instead.

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
    io.print("{b.x}")
    return 0
}
```

```text
1.0
```

An array, a `store` and a table are handles: writing through one writes the
thing every name for it sees. A struct holding an array holds the handle.

## Arrays count from nought

`array()` is empty, `array(n, value)` is `n` of `value`, `push` grows one,
`len` counts, and `xs[0]` is the first. A read past the end stops the program
with where it was, rather than answering nothing.

```kest
module count

import std.io

fn main() -> i32 {
    let xs = array(3, 0)
    push(xs, 7)
    for i in 0..len(xs) {
        xs[i] += i
    }
    io.print("{len(xs)} {xs[0]} {xs[3]}")
    return 0
}
```

```text
4 0 10
```

## Numbers are sized and never convert on their own

A whole number is `i32` unless it is written as something else, `f32` and `f64`
are separate types, and nothing converts without being named: `f32(n)`,
`i32(x)`. Whole numbers wrap at their width, the way C's do.

```kest
module sizes

import std.io

fn main() -> i32 {
    let n = 7
    let half = f32(n) / 2.0
    let small: u8 = u8(300)
    io.print("{half} {small}")
    return 0
}
```

```text
3.5 44
```

## `if` gives a value with `->`, and there is no ternary

```kest
module pick

import std.io

fn main() -> i32 {
    let hp = 3
    let said = if hp > 0 -> "alive" else -> "down"
    io.print(said)
    return 0
}
```

```text
alive
```

## `match` is over the cases of an enum

An arm that gives a value is `Case -> value`. An arm that does something is a
block with no arrow: `Case { ... }`. Every case is answered, or `else` answers
the rest.

```kest
module door

import std.io

enum Door {
    Shut
    Locked(i32)
}

fn open(door: Door) -> text {
    return match door {
        Shut -> "shut"
        Locked(key) -> "locked with {key}"
    }
}

fn main() -> i32 {
    match Door.Locked(7) {
        Shut {
            io.print("nothing")
        }
        Locked(key) {
            io.print(open(Door.Locked(key)))
        }
    }
    return 0
}
```

```text
locked with 7
```

## Nothing is `none`, and is taken out with `if let`

There is no `nil` that goes anywhere. A value that may not be there is `T?`,
and `if let` names what it holds.

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

## A world is a `store`, and a `ref` into one can go stale

A `store<T>` hands out a `ref<T>` for what is added to it. A `ref` to something
that has been removed reads as `none` -- never as whatever took its place.

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
    if get(npcs, a) == none {
        io.print("a is gone")
    }
    if let one = get(npcs, b) {
        let hurt = one
        hurt.hp -= 5
        set(npcs, b, hurt)
    }
    for r in npcs {
        if let one = get(npcs, r) {
            io.print("{one.hp}")
        }
    }
    return 0
}
```

```text
a is gone
15
```

## Text

A hole holds code, so a string inside one needs no escape, and `${x}` is a `$`
followed by a hole. Text is joined by holes rather than by `+`, and text is its
bytes: `len` counts bytes.

```kest
module words

import std.io

fn main() -> i32 {
    let name = "ann"
    let greeting = "hi {name}!"
    io.print("{greeting} {len("é")}")
    return 0
}
```

```text
hi ann! 2
```

## Promises the compiler proves

A function may promise `no.alloc` (it reaches no heap), `no.host` (it calls
nothing of the host's) and `deterministic` (it answers the same on every
machine). The compiler proves each one or refuses at the line that breaks it.
A frame step is usually all three.

```kest
module step

fn cooled(left: [i32]) no.alloc no.host deterministic {
    for i in 0..len(left) {
        if left[i] > 0 {
            left[i] -= 1
        }
    }
}
```

Under `no.alloc`, `push` and `array()` are refused; `fit` writes into room an
array already has.

## A `const` says its type

```kest
module limits

const MOST: i32 = 20
const STEP: f32 = 1.0 / 60.0
```

## The commands

`kest check file.kest` checks a program and says everything wrong with it at
once; `kest run file.kest` runs one whose `main` answers an `i32`; `kest fmt -w`
writes the one form; `kest test` runs a project's tests; `kest lsp` is what an
editor starts. `--json` makes a command say what it says for a tool rather than
for a reader.
