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

## Modules

A file may say what it is called, and what it reads:

```kest
module game.world
import game.render
```

`import game.render` reads `game/render.kest` beside the importing file. Its
names live under the last part of what it calls itself, so a file that imports
it writes `render.draw` and `render.Sprite`, and the file itself may write
`draw` and `Sprite`. Where a name came from is written at every use of it.

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

The same run with `--errors=json` emits the identical set as JSON, for tooling
and for models repairing their own output.
