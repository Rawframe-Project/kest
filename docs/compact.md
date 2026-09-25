# Kest in one sitting

The whole language, short, for a model or anybody who has to write it today.
It says what; `docs/language.md` says why and is the one that decides. Every
program here is run by the gate, and what is written under it is what it
wrote.

## A file

A file is a module named by where it is: `module game.map` is
`game/map.kest`. A file with no `module` line is a program to run and cannot be
imported. What another module declares is always written through the last
part of its name -- `map.Tile`, `map.load(...)` -- including after `import`.
There is no `use`, no bare import and no wildcard.

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

`main` takes nothing and answers an `i32`, which is the exit status. There is
no `print` of the language's own: `io.print` from `std.io` prints one piece of
text and a line end, and `io.write` prints it without one.

`kest run f.kest` runs, `kest check f.kest` says everything wrong at once,
`kest fmt -w f.kest` writes the one form, `kest test` runs a project's tests,
`kest doc f.kest` writes what a module declares. `--json` makes any of them
say it for a tool.

## Lines, blocks, names

A newline ends a statement; there are no semicolons. A statement goes on while
a bracket is open or a line ends in a binary operator. Blocks are braces,
always, even for one statement. Conditions take no parentheses:
`if x < 3 { }`. Comments are `//` and nothing else. Names are UTF-8.

The keywords: `break const continue defer else enum extern false fn for if
import in let match module none return struct true while`. `flags`, `scratch`,
`own`, `compares`, `orders`, `block`, `resumes` and `wait` are words that
mean something only where they stand and are names everywhere else.

The one form is the formatter's: four spaces, a blank line between
declarations, `kest fmt -w` writes it and `kest fmt --check` says which files
are not in it.

## Types

| Type | What |
| --- | --- |
| `i8 i16 i32 i64 u8 u16 u32 u64` | whole numbers; a literal is `i32` unless it goes where another width is wanted |
| `f32 f64` | floats; a literal is `f32` unless it goes where an `f64` is wanted |
| `bool` | `true`, `false` |
| `text` | UTF-8 bytes; `len` counts bytes |
| `vec2 vec3 vec4` | two to four `f32`s, `x y z w` |
| `[T]` | an array: a growable handle |
| `[T; N]` | `N` of `T` in place, a value |
| `store<T>` | a handle that owns `T`s and hands out `ref<T>` |
| `ref<T>` | a reference into a store; can go stale |
| `T?` | a `T` or `none` |
| `fn(A, B) -> R no.alloc` | a function value, promises and all |
| `block(T) -> R` | a body handed to a function; only as a parameter |

Nothing converts on its own. Naming a type converts: `f32(n)`, `i32(x)`,
`u8(300)` is 44. Whole numbers wrap at their width. A float into a whole
number truncates toward nought and stops at the ends of the range. A literal
that does not fit where it is written is refused: `let x: u8 = 300`. `bits(x)`
is a float's bits as `u32`/`u64` and `float(b)` is the way back.

```kest
module sizes

import std.io

fn main() -> i32 {
    let n = 7
    let half = f32(n) / 2.0
    let small: u8 = u8(300)
    let wide: i64 = 5000000000
    io.print("{half} {small} {wide}")
    return 0
}
```

```text
3.5 44 5000000000
```

Operators, tightest first: `~ - !` on one thing; `* / %`; `+ -`; `<< >>`; `&`;
`^`; `|`; `< <= > >=`; `== !=`; `&&`; `||`. The bitwise ones bind tighter than
the comparisons, so `state & MOVING == MOVING` means what it says. `&`, `|`,
`^`, `~` are for whole numbers and sets of flags; `bool` has `&&`, `||`, `!`.
Whole-number division by nought stops the program; a float's gives an
infinity or `nan`. Assignment is `=`, `+=`, `-=`, `*=`, `/=` and nothing else.
There is no `++`, no ternary and no `+` on text.

## Declarations

```kest
module shapes

const MOST: i32 = 20
const STEP: f32 = 1.0 / 60.0

struct Npc {
    name: text
    health: i32
    at: vec2
}

enum Door {
    Shut
    Locked(i32)
    Open(f32)
}

flags State: u8 {
    Moving
    Airborne
    Hurt
}

fn heal(n: Npc, by: i32) -> Npc no.alloc {
    let after = n
    after.health += by
    return after
}
```

A `const` writes its type and is worked out while compiling. A `let` takes its
type from its value, may write one, and is always given one: `let a: i32` on
its own is refused. A function that gives nothing has no `->`. A struct is
built by calling it with its fields in order, `Door`'s cases through their
enum, and there is no `Npc { name: ... }` and no naming of arguments:

```kest
let ann = Npc("ann", 10, vec2(0.0, 0.0))
let door = Door.Locked(7)
let state = State.Moving | State.Hurt
let calm = State()
```

## Values and handles

A struct, a `[T; N]`, a vector and an enum are values: handed to a function or
given to a `let`, they are copies. Writing a field of a parameter writes the
copy, and the compiler warns (`K0346`); give back the changed one instead. An
array, a store and a function value are handles: writing through one is
writing the thing every name for it sees, and a struct holding an array holds
the handle.

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

An element of an array is a place: `xs[i].health -= 1` writes it. An element of
a store is not: it is read with `get` and written back with `set`.

## Control

```kest
module control

import std.io

fn main() -> i32 {
    let hp = 3
    let said = if hp > 0 -> "alive" else -> "down"
    let total = 0
    for i in 0..4 {
        if i == 2 {
            continue
        }
        total += i
    }
    let left = 10
    while left > 7 {
        left -= 1
    }
    let xs = [5, 6]
    for at, x in xs {
        total += at * x
    }
    io.print("{said} {total} {left}")
    return 0
}
```

```text
alive 10 7
```

An `if` gives a value when its arms are written with `->`, and then it needs an
`else`. `for` walks a range `a..b` (up to, not including, `b`), an array, a
`[T; N]`, text (a `u8` at a time), a store (a `ref` at a time) and a set of
flags; `for i, x in xs` gives the place as well. `while`, `while let`,
`break`, `continue`. A body that gives something back has to end in something
that gives it. `defer f(x)` runs the call when the block it is in ends, last
written first; it does not run when the program faults.

A statement that is only a value is refused -- `a == b` where `a = b` was
meant.

## Enums and `match`

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

`match` is over an enum's cases and nothing else -- not numbers, not text;
those are `if`. Every case is answered or `else` answers the rest. An arm is
`Case -> value` or `Case { ... }`, and all arms of one `match` are one kind.
`match a, b { Shut, Push -> ... }` answers two at once. Two enum values
compare when what they carry compares.

## Nothing is `none`

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

A value where a `T?` is wanted becomes one. `if let x = maybe { } else { }`
and `while let` open one; `== none` and `!= none` ask; there is no operator
that opens one without asking and no `nil` anywhere else.

## Arrays, stores, text

```kest
module containers

import std.io

struct Npc {
    hp: i32
}

fn main() -> i32 {
    let xs: [i32] = array()
    push(xs, 3)
    push(xs, 4)
    let last = pop(xs)
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
    io.print("{len(xs)} {last} {get(npcs, a) == none} {alive}")
    return 0
}
```

```text
1 4 true 15
```

The builtins, written without a module:

| Call | What |
| --- | --- |
| `array()`, `array(n, v)` | an empty array, or `n` of `v` |
| `push(xs, v)`, `pop(xs)` | one more on the end; the last off, as `T?` |
| `remove(xs, i)`, `clear(xs)` | take out the one at `i`, shifting; empty it |
| `fit(xs, v)` | `push` without growing: `false` when there is no room |
| `room(xs, n)` | room for `n` in an array or a store |
| `len(x)` | an array's, a store's or text's count |
| `store()`, `store(n)` | an empty store, with room for `n` |
| `add(s, v)`, `get(s, r)`, `set(s, r, v)`, `remove(s, r)` | put in, read as `T?`, write, take out |
| `find(t, needle)`, `find(t, needle, from)` | where in text, as `i32?` |
| `matches(t, at, needle)`, `rest(t, at)`, `slice(t, from, count)` | a test, the tail, a cut |
| `hash(x)`, `bits(x)`, `float(b)` | a `u64` for a value; a float's bits and back |

Text is joined with holes, `"{a}{b}"`, and not with `+`. A hole writes any
value that compares: a number, text, an enum, a struct, a set of flags, an
optional. `\{` is a brace, and a hole holds code, so a string inside one needs
no escape: `"{find(t, "x") != none}"`. `'a'` is a `u8`. `t[i]` is a byte. A
`[u8]` takes a whole piece of text with `push`, and `text(bytes)` makes text
back out of one.

## Generics, function values, blocks

```kest
module kinds

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

fn twice(n: i32) -> i32 no.alloc {
    return n * 2
}

fn apply(f: fn(i32) -> i32 no.alloc, n: i32) -> i32 no.alloc {
    return f(n)
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
    let xs = [3, 9, 4]
    let floor = 3
    io.print("{xs.largest(0)} {apply(twice, 5)} {xs.countWhere(|x| x > floor)}")
    return 0
}
```

```text
9 10 2
```

A type name may be asked to `compares` (`==`) or `orders` (`<`); nothing else.
A struct or an enum may take types: `struct Pair<A, B>`, `enum Answer<T>`, and
the types are written where the value goes, never at the call. There are no
closures: a function value names a function, and what it would have captured
is passed beside it. A block, `|x| value` or `|x| { ... }`, is handed to a
parameter of type `block(T) -> R` and reads and writes the names where it is
written; it cannot be kept, and `return` inside one is refused. `x.f(a)` is
`f(x, a)`, looked for in the file, then in the module the value's type comes
from, then among the builtins. Two functions may share a name when they take
different things.

## A body that waits

`fn step(c: Chore, dt: f32) -> Chore resumes c.at` resumes from `c.at`, a
field that is an enum the program declares. `wait Walking` sets `c.at` to
`Walking`, gives `c` back and ends the call; the next call carries on from the
line after it. A case no `wait` names starts from the top. Nothing but `c` is
kept across a `wait`, so no `let` or `for` name may be in reach of one: what
the body needs afterwards is a field of `c`. `examples/chores.kest` is one.

## Promises

`no.alloc` (reaches no heap), `no.host` (calls nothing of the host's) and
`deterministic` (answers the same bits on every machine) are written after a
function's result and proved by the compiler, or refused at the line that
breaks them with the path down to it. What reaches the heap: a growing array
literal, text with a hole, `push`, `add`, `array()`, `store()`, `room`,
`slice`. `fit`, `pop`, `remove`, `clear`, `get`, `set`, `find`, `rest` do not.
`scratch { }` is a block whose heap is given back when it ends; nothing made in
it may be kept.

## The host

A program reaches the outside only through what a host binds:
`extern fn Clock.now() -> u64 no.alloc` declares one, and `Clock` is the
capability a host grants by binding it. There is no file, clock, network or
environment of the language's own; `std.io` and `std.os` are declared that
way, and `kest run` binds them.

A host is C, and includes `include/kest.h` and nothing else: `kest_build`
compiles a file, `kest_host_new` and `kest_host_bind` bind what it asks for,
`kest_start` makes a machine, `kest_entry` finds a function and `kest_call`
calls it, answering false with the reason said when the program was refused
or faulted. A world a host keeps lives between calls; the program's heap is
its own. `examples/least.c` is the smallest host there is and
`examples/engine.c` is the shape a game has. Code nobody trusts is started
with `kest_start_untrusted`, and `SECURITY.md` says how far that is a sandbox
today.

## Tables, sorting, numbers that look random

```kest
module stock

import std.io
import std.random
import std.sort
import std.table

struct Item {
    name: text
    price: i32
}

fn cheaper(a: Item, b: Item) -> bool no.alloc no.host {
    return a.price < b.price
}

fn main() -> i32 {
    let counts: table.Table<text, i32> = table.empty()
    table.set(counts, "apple", 3)
    counts.set("pear", 1)
    counts.set("apple", counts.orElse("apple", 0) + 2)
    if let pears = table.get(counts, "pear") {
        io.print("pears {pears}")
    }
    io.print("apples {counts.orElse("apple", 0)}, {counts.count()} kinds, plum {counts.has("plum")}")
    let items = [Item("rope", 5), Item("lamp", 2), Item("map", 9)]
    sort.by(items, cheaper)
    let prices = [4, 1, 3]
    sort.by(prices, sort.descending)
    io.print("{items[0].name} {prices[0]}")
    let source = random.from(42)
    source = random.next(source)
    let roll = random.below(source, 6)
    io.print("rolled {roll}")
    return 0
}
```

```text
pears 1
apples 5, 2 kinds, plum false
lamp 4
rolled 4
```

`table.Table<K, V>` is a hash table in `std.table`: `table.empty()`, `set`,
`get` (a `V?`), `orElse(t, k, fallback)`, `has`, `remove`, `count`, and its
pairs walked with `keyAt`/`valueAt` up to `count`. A key is anything that
compares, a struct included. `sort.by(xs, before)` sorts in place by a
function that promises `no.alloc no.host`; `sort.ascending` and
`sort.descending` are the usual orders for anything that `orders`, and
`sort.byWith(xs, context, before)` passes something along. A random source is
a value: `random.from(seed)`, and `source = random.next(source)` before each
number, so the same seed gives the same run on every machine.

## Text

```kest
module words

import std.io
import std.text

fn main() -> i32 {
    let line = "  ann,bo,cy  "
    let names = text.split(text.trim(line), ",")
    io.print("{len(names)} {names[1]} {text.join(names, " and ")}")
    if let n = text.number("42") {
        io.print("{n + 1} {text.upper("hi")} {text.contains(line, "bo")}")
    }
    let at = find(line, "bo")
    let tail = rest(line, 7)
    io.print("{at} [{tail}] {slice(line, 2, 3)} {text.chars("hız")}")
    let out: [u8] = array()
    push(out, "a")
    push(out, 'b')
    io.print(text(out))
    return 0
}
```

```text
3 bo ann and bo and cy
43 HI true
6 [o,cy  ] ann 3
ab
```

`find`, `rest`, `matches` and `slice` are builtins; the rest of what text does
is `std.text`: `split`, `join`, `trim`, `number` and `real` (reading, as
`T?`), `fixed` (a float to so many places), `upper`, `lower`, `contains`,
`starts`, `ends`, `repeat`, and `chars`, `charAt`, `charWidth` for UTF-8
characters. `rest` and `find` copy nothing; `slice`, `split`, `join` and a hole
make new text on the heap. A piece of text is compared with `==` and `<` by
its bytes, and `t != ""` asks whether anything is left.

## A frame: places, `defer`, `scratch`

```kest
module frame

import std.io

struct Npc {
    name: text
    hp: i32
}

fn close(what: text) {
    io.print("closed {what}")
}

fn tick(world: [Npc]) -> i32 no.alloc {
    let alive = 0
    for i in 0..len(world) {
        if world[i].hp > 0 {
            world[i].hp -= 1
            alive += 1
        }
    }
    return alive
}

fn main() -> i32 {
    let world = [Npc("ann", 2), Npc("bo", 0)]
    {
        defer close("the first")
        defer close("the second")
        io.print("inside")
    }

    for round in 0..3 {
        scratch {
            let line = "round {round}: {tick(world)} alive"
            io.print(line)
        }
    }
    return 0
}
```

```text
inside
closed the second
closed the first
round 0: 1 alive
round 1: 1 alive
round 2: 0 alive
```

`world[i].hp -= 1` writes the element where it is. `tick` promises
`no.alloc` and keeps it: it reads and writes and never grows anything. A hole
reaches the heap, so building the line is in a `scratch { }` block, which
gives back everything made inside it each time round.

## Tests and projects

A test is a program that checks itself and answers with which check failed:
nought is a pass.

```kest
module adding

fn add3(a: i32) -> i32 {
    return a + 3
}

fn main() -> i32 {
    if add3(1) != 4 {
        return 1
    }
    if add3(-3) != 0 {
        return 2
    }
    return 0
}
```

```text
```

`kest test adding.kest` runs it and says `passed`. `kest new game` makes a
project: `kest.project` (lines of `name value`: `entry`, `source`, `tests`),
a `main.kest` under `src` and a test under `tests`, and inside it `kest build`, `kest
run` and `kest test` need no file named.

## The standard library

Every module is written in Kest under `lib/std`, and `kest doc` of one says
what each function takes. Each name below is called through its module, or on a
value with `x.f(a)`.

- `std.io` -- print, write
- `std.math` -- min, max, abs, clamp, isNumber, sqrt, floor, ceil, round,
  sign, lerp, sin, cos, tan, asin, acos, atan, atan2, pow
- `std.vec` -- dot, length, lengthSquared, distance, distanceSquared,
  direction, lerp, perpendicular, cross
- `std.text` -- number, real, fixed, left, right, contains, starts, ends,
  trim, split, join, repeat, upper, lower, bytes, append, fitting,
  fittingNumber, isSpace, chars, charsOf, charAt, charBytes, charWidth,
  charBack
- `std.table` -- empty, count, get, orElse, has, set, fit, remove, find,
  place, refill, compact, keysOf, keyAt, valueAt, slotOf
- `std.sort` -- by, byWith, ascending, descending
- `std.random` -- from, next, number, below, between, fraction, chance, one,
  shuffle
- `std.hash` -- join
- `std.bytes` -- putU8, putBool, putI32, putU32, putI64, putU64, putF32,
  putF64, putText, putBytes, putI32s, putF32s, reader, readU8, readBool,
  readI32, readU32, readI64, readU64, readF32, readF64, readText, readBytes,
  readI32s, readF32s, left, short, take, word
- `std.os` -- read, write, exists, argCount, arg, args, now, millisBetween
- `std.fdlibm` -- sin, cos, pow, atan, atan2, sinKernel, cosKernel, reduced,
  magnitude, negated, highWord, lowWord, lowCleared, withHighWord; what
  `std.math` computes its sines and powers with

## What a newcomer gets wrong

These are what models new to the language wrote, and what the compiler says
back.

```
error[K0306]: unknown name `max`
  |                ^^^ did you mean `math.max`?
```

A name from another module is written with its module, after `import`.

```
error[K0201]: expected end of line, found `{`
  |               ^ a struct is built by position, in the order its fields are declared: `P(...)`
error[K0201]: expected `)`, found `:`
  |                ^ nothing is passed by name: `P(...)` takes what it takes in the order it is declared
```

```
error[K0310]: `value` expects `text`, found `i32`
  |              ^ a hole makes text of it: `"{...}"`
```

`io.print` takes text, and it needs `import std.io`.

An escaped quote inside a hole, `"{f(\"x\")}"`, is `K0102`, which says *a hole
holds code, so a string inside one needs no escape*: it is `"{f("x")}"`.

And three the compiler cannot refuse: a field of a parameter written and not
given back (a warning, `K0346`); `remove(xs, i)` inside `for i in 0..len(xs)`,
which reads past the end and stops the program -- walk a store, or gather what
to remove and remove it after; and a `match` over a number or text, which is
`if`.

Every refusal has a code: `K01` reading, `K02` parsing, `K03` types and
bodies, `K04` promises, `K05` what cannot be compiled, `K06` what failed while
running, `K07` what cannot be read. The message says what to write instead
where it can.
