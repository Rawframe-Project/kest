# The Kest Language

This describes what is decided, not what is implemented. `docs/worklog.md` says
what runs today. Anything here without an entry there is a target.

## Shape

```kest
module world.quests

import math

const GRAVITY: f32 = -9.81

struct Player {
    x: f32
    y: f32
    velocity: f32
    health: i32
}

fn update(p: Player, dt: f32) -> bool {
    p.velocity = p.velocity + GRAVITY * dt
    p.y = p.y + p.velocity * dt

    if p.y < 0.0 {
        p.y = 0.0
        p.velocity = 0.0
    }

    for e in enemies {
        if math.distance(p, e) < 1.0 {
            p.health = p.health - 10
            print("hit, health {p.health}")
        }
    }

    return p.health > 0
}
```

## One form

`kest fmt` prints a file the one way the language writes it. `-w` writes each
file it is given and names the ones it changed; `--check` names them without
writing and exits non-zero, which is the question "is this already right".
It writes through a file beside the target and renames over it, so a program
that stops half way leaves the file rather than half of it: four spaces a
level, one space around a binary operator, none inside a bracket, and a
bracket only where taking it away would change what binds to what. It prints
from the tree rather than from the characters, which is the only way to tell
`ref<Npc>` from `a < b`.

Comments are kept, at the indent of what they are written above. What is
inside a string, including the expressions in its holes, is left exactly as
written, and so is the spelling of a number.

A list that does not fit in eighty columns goes one item to a line, all of
them or none: half on one line and half on the next is the arrangement nobody
asked for.

```kest
let w = World(
    [
        Enemy(Vec2(0.0, 0.0), 30),
        Enemy(Vec2(1.0, 1.0), 40),
        Enemy(Vec2(2.0, 2.0), 5)
    ],
    0
)
```

A chain of operators too long for the line breaks after each operator, one
level in, or two when it is a condition, because a condition has a block
starting one level in right after it:

```kest
let total = alpha * 1000 +
    beta * 2000 +
    gamma * 3000

if alpha != 1 ||
        beta != 2 ||
        gamma != 3 {
    return 1
}
```

The operator ends the line rather than starting the next one, because that is
what the language allows: a line that ends in an operator continues and one
that ends in a value does not.

A long string cannot be broken.

## Modules

A file may say what it is called, and what it reads:

```kest
module game.world
import game.render
```

A module's name is where its file is: `module examples.game.npc` lives at
`examples/game/npc.kest`, and the file the command names settles where the
package directories start by having its own name taken off its path.
`import examples.game.npc` therefore reads the same file whoever writes it. Its
names live under the last part of what it calls itself, so a file that imports
it writes `render.draw` and `render.Sprite`, and the file itself may write
`draw` and `Sprite`. Where a name came from is written at every use of it. Two modules whose names
end the same way would put their names under the same one, and that is refused
rather than mixed.

Every command takes more than one file. `kest check *.kest` checks a project
as a project; checking only the entry point checks only what it reaches, and a
file nothing imports is never looked at.

## Rules

A newline ends a statement. There are no semicolons, and a `;` is a syntax
error. A statement continues onto the next line while it is incomplete: inside
brackets, or after a binary operator.

Conditions take no parentheses. `if x < 3 { }` is the only spelling; `if (x <
3) { }` is refused, because `(x < 3)` is a redundant grouping the formatter
would strip and the strict parser does not accept two spellings of one thing.

Blocks are braces, always, including single-statement bodies.

Keywords are English. Identifiers are UTF-8, so `let hız = 5` and
`fn oyuncuGüncelle()` are legal.

Comments are `//` to end of line. Nothing else.

A string may hold expressions in braces, and `\{` writes a brace:

```kest
print("{len(world)} left, and the escort reads \"{escortOf(world, guard)}\"")
```

There is no `+` on text. Building a string reaches the heap, so a function
promising `no.alloc` may hold a string and may not build one.

Text is its bytes. `len(t)` counts them and walks the string to do it, `t[i]`
reads one as a `u8`, and two pieces compare by them. There is no character
type: `"hız"` is four bytes, and a program that wants characters says what it
means by one.

`find(t, needle)` gives where it is, or nothing, and costs nothing.
`slice(t, from, count)` makes a new piece of text, which reaches the heap:

```kest
if let at = find(entry, "=") {
    let name = slice(entry, 0, at)
}
```

## Keywords

```
break  const   continue  else    extern  false   fn      for
if      import  in       let     module  return  struct  true
while
```

Reserved but not yet given meaning: `enum`, `match`, `type`, `defer`.

## Types

Signatures declare types. Bodies infer them.

```kest
fn scale(v: Vec3, k: f32) -> Vec3 {
    let x = v.x * k        // inferred f32
    return vec3(x, v.y * k, v.z * k)
}
```

Primitives: `i8 i16 i32 i64`, `u8 u16 u32 u64`, `f32 f64`, `bool`, `text`.

Naming a number type makes one, the same way naming a struct does:

```kest
let average = total / f32(len(items))
let index = i32(position.x)
```

Nothing converts on its own. An integer going into a narrower integer wraps,
which is what C does; a float going into an integer is truncated toward zero
and stops at the end of the range rather than being undefined, which is what
C does not.

`f32` and `f64` are different types and different instructions. `f32`
arithmetic rounds to `f32`, because the engine on the other side of the
boundary does, and an answer that differs from that one is the wrong answer.
Nothing converts between them on its own.

A host boundary is always declared and never inferred:

```kest
extern fn Clock.now() -> u64 no.alloc
```

## Values and references

A `struct` is a value. It lives where its frame does. A temporary is moved
rather than copied. A value passed to a function that neither keeps it nor
writes through it is lent, and costs nothing.

`ref<T>` is a handle into managed or host storage. It can go stale, because
something else may delete the target, so reading through it is a lookup that
can fail rather than a dereference. The failure cannot be ignored.

This split is why `Vec3` returned from a helper costs nothing: see D006.

## References

`store<T>` owns values and hands out `ref<T>`. `add` puts one in, `remove`
takes it out, and `get` reads through a reference and returns `T?`, because
what it named may be gone:

```kest
let world: store<Npc> = store()
let guard = add(world, Npc("guard", none))
set(world, guard, Npc("guard", smith))

if let npc = get(world, guard) {
    print(npc.name)
}
```

Nothing is notified of a removal and nothing counts references, so two values
may point at each other and neither has to be told. `get`, `set` and `remove`
allocate nothing; `add` can grow the store and does.

`array(n, v)` makes an array of `n` of `v`, and `push` puts one more on the
end. What it holds comes from what it is filled with, so nothing is written
down twice:

```kest
let samples = array(0, Sample(0, 0.0))
push(samples, Sample(1, 0.5))
```

Both reach the heap. An array the host lent cannot grow, because growing moves
the elements and the block is not Kest's to move; that is a failure with a
message rather than a write past the end of what was lent.

`for i, x in a` asks for the position as well. The name is a copy of the
walk's own count, so assigning to it changes nothing and the compiler says so.

`for` walks a store and gives a reference, because a reference is what
removing and writing take. Removing while walking is allowed: the slot goes
dead behind the cursor and the walk does not go back to it.

```kest
for r in world {
    if let npc = get(world, r) {
        if npc.health <= 0 {
            remove(world, r)
        }
    }
}
```

## When there might be nothing

`T?` holds a `T` or nothing. A value standing where one is wanted becomes one,
which is the only conversion the language does:

```kest
fn find(items: [Item], id: i32) -> Item? {
    for item in items {
        if item.id == id {
            return item
        }
    }
    return none
}
```

`if let` is the only way to open it, and the name exists only inside the arm
where the value did:

```kest
if let item = find(stock, 7) {
    print("in stock")
} else {
    print("not carried")
}
```

There is no operator that opens one without asking, because the whole point of
the type is that the question was asked.

## Cost contracts

`no.alloc` on a function is a promise the compiler proves or refuses.

```kest
fn stepBody(p: Player, dt: f32) -> Player no.alloc {
    return integrate(p, dt)
}
```

The promise is written at entry points. Callees defined in the same unit are
judged by their bodies, transitively; only boundaries need a written promise.
A refusal names the path down to the body that allocates, not the function
that made the promise:

```
error[K0401]: this allocates, and `stepFrame` promises `no.alloc`
 --> frame.kest:5:17
  |
5 |     let trail = [n, n, n]
  |                 ^^^^^^^^^ reached through `second` -> `third` -> `leaf`
```

Building an array is the only thing in the language that reaches the heap.
Structs, optionals and calls do not. A foreign function is judged by what it
declares, because its body is not here to be read.

## The host boundary

The default shape is one crossing carrying a borrowed view of contiguous host
storage. Per-value crossing stays expressible and is visible where it is
written, because it costs between four and ten times as much.

Inward and outward are separate specifications. The event path is bulk-first:
the host hands Kest a batch of events to walk, rather than calling Kest once
per event.

## Running

`kest run` calls `main`. A `main` that returns `i32` supplies the process exit
status, and one that returns nothing exits zero.

```kest
fn main() -> i32 {
    print("hello")
    return 0
}
```

A failure at runtime is reported in the same shape as a failure at compile
time, with the same codes, the same source location and the same `--errors=json`
output. Nothing about repairing a program needs to know which of the two it is
reading.

## Diagnostics

Compilation reports every error it can find, not the first. Each has a stable
code, a span, and a suggestion where one is knowable.

```
error[K0104]: unknown function `printf`
  --> player.kest:14:9
   |
14 |         printf("hit")
   |         ^^^^^^ did you mean `print`?
```

A diagnostic about more than one place says both:

```
error[K0401]: this allocates, and `stepFrame` promises `no.alloc`
  --> chain.kest:5:17
   |
 5 |     let trail = [n, n, n]
   |                 ^^^^^^^^^
  --> chain.kest:17:4
   |
17 | fn stepFrame(n: i32) -> i32 no.alloc {
   |    ^^^^^^^^^ `stepFrame` promises it here
  --> chain.kest:18:12
   |
18 |     return second(n)
   |            ^^^^^^^^^ which calls `second`
```

The same run with `--errors=json` emits the identical set as JSON, notes and
all, for tooling and for models repairing their own output.
