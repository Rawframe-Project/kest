# Functions

Overloading, calling a function on a value, generics, function values, blocks, bodies that wait between frames, and walking a struct's fields.

## Contents

- [Declaring a function](#declaring-a-function)
- [One name, several functions](#one-name-several-functions)
- [`x.f(a)`](#xfa)
- [Generics](#generics)
- [Function values](#function-values)
- [Blocks](#blocks)
- [A body that waits](#a-body-that-waits)
- [Walking a struct's fields](#walking-a-structs-fields)

## Declaring a function

```kest
module heal

struct Npc {
    name: text
    health: i32
}

fn healed(n: Npc, by: i32) -> Npc no.alloc {
    let after = n
    after.health += by
    return after
}

fn shout(n: Npc) {
}
```

A function that gives nothing has no `->`; there is no `void` to write. Parameters and the result are always typed, and bodies infer the rest. [Promises](promises.md) such as `no.alloc` go after the result. Parameters are values (a struct parameter is a copy) and handles (an array parameter is the caller's array); see [Data](data.md#values-and-handles).

## One name, several functions

Two functions may share a name when they take different things. The call decides by what is passed, and since nothing converts on its own exactly one matches or none does. A literal fits any width of its family, and when that leaves more than one a whole number prefers `i32` and a float prefers `f32`, the same rule that types a bare `let`.

```kest
module sizes

import std.io

fn describe(n: i32) -> text {
    return "a number"
}

fn describe(t: text) -> text {
    return "text of {len(t)} bytes"
}

fn main() -> i32 {
    io.print("{describe(3)}, {describe("abc")}")
    return 0
}
```

```text
a number, text of 3 bytes
```

When nothing matches, the error lists every function of the name with what it takes, nearest first. `main` may not be overloaded. A file may add its own `len`, `get`, `set`, `find`, `add` or `remove`; the builtin still answers for the types the file's own does not take.

## `x.f(a)`

`x.f(a)` is another way of writing `f(x, a)`, so a chain reads left to right. The function is looked for among the file's own, then in the module that declared the value's type (`std.text` for text, `std.vec` for vectors), then among the builtins, and is taken when its first parameter can be `x`. There are no methods beyond that: nothing is declared inside a type.

```kest
module pots

import std.io

struct Pot {
    level: i32
}

fn fill(p: Pot, by: i32) -> Pot {
    return Pot(p.level + by)
}

fn main() -> i32 {
    let full = Pot(1).fill(2).fill(3)
    let xs = [4, 5]
    io.print("{full.level} {xs.len()}")
    return 0
}
```

```text
6 2
```

A field that holds a function is called as that field. The module a type came from has to be imported for its functions to be found.

## Generics

A function may take types as well as values, and a copy is compiled for each set of types it is called with. The types come from what is passed and where the answer goes; they are never written at the call.

A type parameter may be asked for exactly two things: `compares` (`==` and `!=`, and therefore `hash`) and `orders` (`<`, `<=`, `>`, `>=`). Nothing else, so arithmetic or a field on a type parameter is an error. The body is checked once, against what the declaration asks for, and a call with a type that lacks it is refused at the call.

```kest
module largest

import std.io

fn largest<T: orders>(xs: [T], start: T) -> T no.alloc {
    let best = start
    for x in xs {
        if x > best {
            best = x
        }
    }
    return best
}

struct Pair<A, B> {
    first: A
    second: B
}

fn main() -> i32 {
    let p = Pair(1, "a")
    io.print("{largest([3, 9, 4], 0)} {largest(["b", "c"], "a")} {p.second}")
    return 0
}
```

```text
9 c a
```

Structs and enums take types the same way: `Pair(1, "a")` is a `Pair<i32, text>`. A shape is written with its types wherever a type goes (`Pair<i32, text>`), and `Pair` on its own is not a type. Writing types at a call, as in `store<Node>()`, is an error that says to write `store()` and put the type on the binding.

A struct has `==` from its fields but no order, because a struct with three fields has at least three orders. What sorts is told the order with a function value; see `sort.by` in [The standard library](standard-library.md).

Nothing is boxed: a copy over `[vec2]` knows a `vec2` is two `f32`s. The cost is one compiled body per set of types.

## Function values

A function's name is a value of its function type, and the promises are part of the type: `fn(i32) -> i32 no.alloc`. A value that promises more may go where one that promises less is wanted, never the other way.

```kest
module apply

import std.io

fn twice(n: i32) -> i32 no.alloc {
    return n * 2
}

fn apply(f: fn(i32) -> i32 no.alloc, n: i32) -> i32 no.alloc {
    return f(n)
}

fn main() -> i32 {
    let rules: [fn(i32) -> i32 no.alloc] = array()
    push(rules, twice)
    io.print("{apply(twice, 5)} {rules[0](4)}")
    return 0
}
```

```text
10 8
```

**There are no closures.** A function value names a function, and what a closure would have captured is passed beside it: a predicate that needs a threshold takes the threshold as a parameter. To run something later, store a struct holding the function and its data. A generic function is called, not handed around, and two function values do not compare.

`let f = twice` binds the name to that one function, and assigning another to it is an error. To hold any function of a shape, write the shape on the `let`.

## Blocks

A block is a body written where a function is called and handed to a parameter of type `block(T) -> R` (or `block(T)` for one that gives nothing). `|x| value` gives a value, and `|x| { ... }` does something and gives nothing. A block reads and writes the names where it is written.

```kest
module counting

import std.io

fn countWhere<T>(xs: [T], keep: block(T) -> bool) -> i32 {
    let n = 0
    for x in xs {
        if keep(x) {
            n += 1
        }
    }
    return n
}

fn each<T>(xs: [T], body: block(T)) {
    for x in xs {
        body(x)
    }
}

fn main() -> i32 {
    let floor = 3
    let total = 0
    let xs = [3, 9, 4]
    let over = xs.countWhere(|x| x > floor)
    xs.each(|x| {
        total += x
    })
    io.print("{over} {total}")
    return 0
}
```

```text
2 16
```

A block is not a closure: the function that takes it is compiled into each place it is called, so nothing is captured and nothing goes on the heap, and a `no.alloc` function may use one. A block may only be handed to a parameter and called or handed on; it cannot be stored, returned or given to a host. `return` inside a block is an error, and `break` only leaves a loop inside the block.

## A body that waits

Work a game does over many frames (walk a route, then work, then rest) is a sequence with a pause at the end of each frame. A function can be written that way: it names a field it resumes from, and `wait` ends the call.

```kest
module patrol

import std.io

enum GuardAt {
    Start
    Walking
    Looking
    Done
}

struct Guard {
    post: i32
    posts: i32
    looks: i32
    at: GuardAt
}

fn watch(g: Guard) -> Guard resumes g.at {
    while g.post < g.posts {
        g.post += 1
        wait Walking
        g.looks = 2
        while g.looks > 0 {
            g.looks -= 1
            wait Looking
        }
    }
    g.at = GuardAt.Done
    return g
}

fn main() -> i32 {
    let g = Guard(0, 2, 0, GuardAt.Start)
    while g.at != GuardAt.Done {
        g = watch(g)
        io.print("{g.at} at post {g.post}")
    }
    return 0
}
```

```text
GuardAt.Walking at post 1
GuardAt.Looking at post 1
GuardAt.Looking at post 1
GuardAt.Walking at post 2
GuardAt.Looking at post 2
GuardAt.Looking at post 2
GuardAt.Done at post 2
```

`resumes g.at` names a parameter the function takes and gives back, and a field of it whose type is an enum the program declares. `wait Walking` sets that field to `Walking`, gives the parameter back and ends the call; the next call carries on after that `wait`. A case no `wait` names starts the body from the top.

Only the parameter is kept between calls, which is what makes the state plain data that saves, loads, compares and survives a reload like any struct. So no `let`, `for`, `if let` or `match` name may be in reach of a `wait`; whatever the body needs afterwards goes in a field. A `wait` cannot be inside a `for`, a body that waits has no `defer` and no open `scratch` block at a `wait`, each `wait` names a case that carries nothing, no case is waited at twice, and a block handed to another function cannot wait. `examples/chores.kest` is a longer example.

## Walking a struct's fields

`for name, value in fields(x)` is written out while compiling, once per field of the struct `x`, in declaration order. `name` is the field's name as text and `value` is the field itself with its own type; writing `value` writes that field of `x`. Each copy is checked on its own, so a call inside picks the overload for that field's type. `for value in fields(x)` leaves out the names.

```kest
module inspect

import std.io

struct Where {
    x: f32
    y: f32
}

struct Npc {
    name: text
    hunger: i32
    at: Where
}

fn main() -> i32 {
    let n = Npc("ann", 3, Where(1.5, 2.0))
    for name, value in fields(n) {
        io.print("{name}: {value}")
    }
    for part in fields(n.at) {
        part = part * 2.0
    }
    io.print("{n.at}")
    return 0
}
```

```text
name: ann
hunger: 3
at: Where(1.5, 2.0)
Where(3.0, 4.0)
```

It exists for two jobs, a save and load written once for every shape and what an inspector shows, and nothing more: nothing is kept at run time and no code writes code. `x` has to be a local or a field of one. `examples/records.kest` writes a struct to bytes with one overloaded `put` per field type and reads it back.
